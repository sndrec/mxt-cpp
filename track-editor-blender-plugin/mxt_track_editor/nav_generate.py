"""CPU navigation graph generation and bake persistence."""

import bpy
import time
import numpy as np
import bmesh
from bpy.props import (
    FloatProperty,
    FloatVectorProperty,
    EnumProperty,
    PointerProperty,
    StringProperty,
    BoolProperty,
    IntProperty,
)
from bpy.types import (
    PropertyGroup,
    Operator,
    Panel,
)
import mathutils
import gpu
from gpu_extras.batch import batch_for_shader
from mathutils import Vector, Quaternion, Matrix
import math
from bpy.app.handlers import persistent
from bpy.props import CollectionProperty
from contextlib import contextmanager

from .foundation import (
    MXT_NAV_EDGE_FLAGS,
    MXT_NAV_FORMAT_VERSION,
    MXT_NAV_NODE_FLAGS,
    MXT_NAV_TERRAIN_BITS,
    _bake_curve_matrix_direct,
    _mxt_profile_scope,
    _update_trigger_helper,
)

from .shapes import (
    _sample_curve_matrix_numpy,
)

from .curve_matrix import (
    _calculate_vertex_positions_numpy,
)

from .mesh import (
    _generate_checkpoints_for_segment,
)

from .nav_data import (
    _mxt_nav_apply_dash_chain_multipliers,
    _mxt_nav_collect_mesh_collision_triangles,
    _mxt_nav_dash_approach_scores,
    _mxt_nav_edge_clearance_tx,
    _mxt_nav_embed_bounds,
    _mxt_nav_json_vec3,
    _mxt_nav_mesh_blocker_clearance,
    _mxt_nav_normalized,
    _mxt_nav_route_alternatives,
    _mxt_nav_route_outgoing,
    _mxt_nav_sample_is_hole,
    _mxt_nav_sample_mesh_triangle,
    _mxt_nav_terrain_and_flags,
    _mxt_nav_terrain_lane_metrics,
)

from .nav_routes import (
    _mxt_nav_choice_points,
    _mxt_nav_embed_ty_samples,
    _mxt_nav_open_values_for_rows,
    _mxt_nav_reachable_trace,
    _mxt_nav_runtime_routes,
    _mxt_nav_segment_tx_samples,
    _mxt_nav_segment_ty_samples,
    _mxt_nav_unique_sorted_tx,
)

def _mxt_nav_row_laterally_cyclic(props, open_value):
    shape_type = getattr(props, "road_shape_type", "FLAT")
    if shape_type in ('CYLINDER', 'PIPE', 'ROUNDED_SQUARE'):
        return True
    if shape_type in ('CYLINDER_OPEN', 'PIPE_OPEN', 'ROUNDED_SQUARE_OPEN'):
        return abs(float(open_value)) <= 1.0e-5
    return False


def _mxt_nav_generate(context, filepath, seg_order, seg_index, include_routes=True):
    ts = context.scene.mxt_track_settings
    lateral_count = max(3, int(getattr(ts, "cpu_nav_lateral_samples", 9)))
    row_spacing_meters = max(1.0, float(getattr(ts, "cpu_nav_row_spacing_meters", 30.0)))
    transition_distance = max(0.0, float(getattr(ts, "cpu_nav_transition_distance", 32.0)))
    branch_distance = max(0.0, float(getattr(ts, "cpu_nav_branch_distance", 20.0)))
    mesh_spacing_meters = max(2.0, float(getattr(ts, "cpu_nav_mesh_sample_spacing_meters", 35.0)))

    base_tx_1d = np.linspace(-0.92, 0.92, lateral_count, dtype=np.float64)

    def segment_lateral_base_samples(props):
        override = int(getattr(props, "cpu_nav_lateral_samples", 0))
        if override >= 3:
            return np.linspace(-0.92, 0.92, override, dtype=np.float64)
        return base_tx_1d

    def segment_row_spacing(props):
        override = float(getattr(props, "cpu_nav_row_spacing_meters", 0.0))
        return max(1.0, override) if override > 0.0 else row_spacing_meters

    def segment_transition_distance(seg):
        props = seg.mxt_road_overall_props
        override = float(getattr(props, "cpu_nav_transition_distance", -1.0))
        return max(0.0, override) if override >= 0.0 else transition_distance

    def segment_branch_distance(seg):
        props = seg.mxt_road_overall_props
        override = float(getattr(props, "cpu_nav_branch_distance", -1.0))
        return max(0.0, override) if override >= 0.0 else branch_distance

    nodes = []
    edges = []
    edge_keys = set()
    segment_records = []
    segment_grids = {}
    skipped_segments = []
    triggers_by_segment = {seg: [] for seg in seg_order}
    for trig in ts.trigger_objects:
        if trig.segment in triggers_by_segment:
            triggers_by_segment[trig.segment].append(trig)
    dash_targets_by_segment = {seg: [] for seg in seg_order}
    all_dash_targets = []
    for seg, triggers in triggers_by_segment.items():
        for trig_idx, trig in enumerate(triggers):
            if trig.obj_type != 'DASHPLATE' or not trig.helper:
                continue
            center = trig.helper.matrix_world.translation
            forward_v = trig.helper.matrix_world.to_3x3() @ Vector((0.0, 0.0, 1.0))
            if forward_v.length <= 1.0e-6:
                forward_v = Vector((0.0, 0.0, 1.0))
            else:
                forward_v.normalize()
            target = {
                "id": f"{seg_index[seg]}:{trig_idx}:{trig.label}",
                "segment": int(seg_index[seg]),
                "segment_name": seg.name,
                "tx": float(trig.tx),
                "ty": float(trig.ty),
                "position": [float(center.x), float(center.y), float(center.z)],
                "forward": [float(forward_v.x), float(forward_v.y), float(forward_v.z)],
                "dash_chain_multiplier": 1.0,
                "label": trig.label,
            }
            dash_targets_by_segment[seg].append(target)
            all_dash_targets.append(target)
    _mxt_nav_apply_dash_chain_multipliers(all_dash_targets)

    def add_edge(from_id, to_id, cost, flags):
        if from_id is None or to_id is None or from_id == to_id:
            return False
        key = (int(from_id), int(to_id), int(flags))
        if key in edge_keys:
            return False
        edge_keys.add(key)
        edges.append({
            "from": int(from_id),
            "to": int(to_id),
            "cost": round(max(0.001, float(cost)), 6),
            "flags": int(flags),
        })
        return True

    for seg in seg_order:
        props = seg.mxt_road_overall_props
        helper = props.curve_matrix_helper_empty
        if not (helper and helper.animation_data and helper.animation_data.action):
            skipped_segments.append(seg.name)
            continue
        cps = getattr(props, "checkpoints", [])
        cp_count = max(1, len(cps))
        segment_triggers = triggers_by_segment.get(seg, [])
        segment_dash_targets = dash_targets_by_segment.get(seg, [])
        segment_row_spacing_meters = segment_row_spacing(props)
        segment_base_tx_1d = segment_lateral_base_samples(props)
        ty_1d = _mxt_nav_segment_ty_samples(helper, props, segment_triggers, segment_row_spacing_meters)
        tx_1d = _mxt_nav_segment_tx_samples(segment_base_tx_1d, props, segment_triggers, ty_1d)
        row_count = len(ty_1d)
        segment_lateral_count = len(tx_1d)
        tx_grid, ty_grid = np.meshgrid(tx_1d, ty_1d)
        centerline_pos, centerline_basis, centerline_scl = _sample_curve_matrix_numpy(helper, ty_1d)
        positions_local = _calculate_vertex_positions_numpy(props, centerline_pos, centerline_basis, centerline_scl, tx_grid, ty_grid)
        open_values = _mxt_nav_open_values_for_rows(props, ty_1d)
        row_cyclic = [
            _mxt_nav_row_laterally_cyclic(props, open_values[row])
            for row in range(row_count)
        ]

        row_nodes = [[None for _ in range(segment_lateral_count)] for _ in range(row_count)]
        for row in range(row_count):
            for col in range(segment_lateral_count):
                tx = float(tx_grid[row, col])
                ty = float(ty_grid[row, col])
                if row_cyclic[row] and col == segment_lateral_count - 1 and abs(tx - 1.0) <= 1.0e-6:
                    row_nodes[row][col] = row_nodes[row][0]
                    continue
                if _mxt_nav_sample_is_hole(props, tx, ty):
                    continue
                pos_local = positions_local[row, col]
                pos_world_v = seg.matrix_world @ Vector((float(pos_local[0]), float(pos_local[1]), float(pos_local[2])))
                pos_world = np.array((pos_world_v.x, pos_world_v.y, pos_world_v.z), dtype=np.float64)
                if row == 0:
                    fwd = positions_local[min(row + 1, row_count - 1), col] - positions_local[row, col]
                elif row == row_count - 1:
                    fwd = positions_local[row, col] - positions_local[row - 1, col]
                else:
                    fwd = positions_local[row + 1, col] - positions_local[row - 1, col]
                if row_cyclic[row]:
                    left_col = (col - 1) % segment_lateral_count
                    right_col = (col + 1) % segment_lateral_count
                    if left_col == segment_lateral_count - 1 and row_nodes[row][left_col] == row_nodes[row][0]:
                        left_col = max(0, segment_lateral_count - 2)
                    if right_col == segment_lateral_count - 1 and row_nodes[row][right_col] == row_nodes[row][0]:
                        right_col = 0
                else:
                    left_col = max(0, col - 1)
                    right_col = min(segment_lateral_count - 1, col + 1)
                lateral = positions_local[row, right_col] - positions_local[row, left_col]
                fwd_n = _mxt_nav_normalized(fwd, (0.0, 0.0, 1.0))
                normal_n = _mxt_nav_normalized(np.cross(fwd_n, lateral), (0.0, 1.0, 0.0))
                terrain, flags = _mxt_nav_terrain_and_flags(props, tx, ty, pos_world, segment_triggers)
                terrain_lane = _mxt_nav_terrain_lane_metrics(props, tx, ty)
                dash_scores = _mxt_nav_dash_approach_scores(pos_world, fwd_n, segment_dash_targets)
                if len(getattr(props, "next_segments", [])) > 1 or len(getattr(props, "prev_segments", [])) > 1:
                    flags |= MXT_NAV_NODE_FLAGS["branch"]
                node_id = len(nodes)
                checkpoint = min(cp_count - 1, int(math.floor(ty * cp_count)))
                node = {
                    "id": int(node_id),
                    "segment": int(seg_index[seg]),
                    "segment_name": seg.name,
                    "checkpoint": int(checkpoint),
                    "tx": round(tx, 6),
                    "ty": round(ty, 6),
                    "position": _mxt_nav_json_vec3(pos_world),
                    "normal": _mxt_nav_json_vec3(normal_n),
                    "forward": _mxt_nav_json_vec3(fwd_n),
                    "terrain": int(terrain),
                    "edge_clearance": round(_mxt_nav_edge_clearance_tx(props, tx, ty), 6),
                    "terrain_edge_clearance": round(float(terrain_lane["terrain_edge_clearance"]), 6),
                    "terrain_center_offset": round(float(terrain_lane["terrain_center_offset"]), 6),
                    "terrain_span_width": round(float(terrain_lane["terrain_span_width"]), 6),
                    "dash_approach_scores": {target_id: round(float(score), 6) for target_id, score in dash_scores.items()},
                    "dash_approach_score": round(max(dash_scores.values()) if dash_scores else 0.0, 6),
                    "dash_value_multiplier": 1.0,
                    "downward_curve_risk": 0.0,
                    "author_weight": 1.0,
                    "flags": int(flags),
                }
                nodes.append(node)
                row_nodes[row][col] = node_id

        row_centers_local = []
        for row in range(row_count):
            valid_cols = [col for col in range(segment_lateral_count) if row_nodes[row][col] is not None]
            if not valid_cols:
                row_centers_local.append(None)
                continue
            if row_cyclic[row]:
                unique_cols = []
                seen_node_ids = set()
                for col in valid_cols:
                    node_id = row_nodes[row][col]
                    if node_id in seen_node_ids:
                        continue
                    seen_node_ids.add(node_id)
                    unique_cols.append(col)
                row_centers_local.append(np.mean(positions_local[row, unique_cols], axis=0))
            else:
                left = positions_local[row, valid_cols[0]]
                right = positions_local[row, valid_cols[-1]]
                row_centers_local.append((left + right) * 0.5)
        for row in range(1, row_count - 1):
            prev_center = row_centers_local[row - 1]
            center = row_centers_local[row]
            next_center = row_centers_local[row + 1]
            if prev_center is None or center is None or next_center is None:
                continue
            incoming = _mxt_nav_normalized(center - prev_center, (0.0, 0.0, 1.0))
            outgoing = _mxt_nav_normalized(next_center - center, (0.0, 0.0, 1.0))
            turn_vec = outgoing - incoming
            turn_len = float(np.linalg.norm(turn_vec))
            if turn_len <= 1.0e-6:
                continue
            turn_dir = turn_vec / turn_len
            turn_strength = min(1.0, turn_len / 0.28)
            for col in range(segment_lateral_count):
                node_id = row_nodes[row][col]
                if node_id is None:
                    continue
                normal = _mxt_nav_normalized(np.asarray(nodes[node_id]["normal"], dtype=np.float64), (0.0, 1.0, 0.0))
                risk = max(0.0, -float(np.dot(normal, turn_dir))) * turn_strength
                nodes[node_id]["downward_curve_risk"] = round(float(risk), 6)

        for row in range(row_count):
            for col in range(segment_lateral_count - 1):
                a = row_nodes[row][col]
                b = row_nodes[row][col + 1]
                if a is not None and b is not None:
                    pa = np.asarray(nodes[a]["position"], dtype=np.float64)
                    pb = np.asarray(nodes[b]["position"], dtype=np.float64)
                    cost = float(np.linalg.norm(pb - pa))
                    add_edge(a, b, cost, MXT_NAV_EDGE_FLAGS["lateral"])
                    add_edge(b, a, cost, MXT_NAV_EDGE_FLAGS["lateral"])
            if row_cyclic[row]:
                a = row_nodes[row][segment_lateral_count - 1]
                b = row_nodes[row][0]
                if a is not None and b is not None and a != b:
                    pa = np.asarray(nodes[a]["position"], dtype=np.float64)
                    pb = np.asarray(nodes[b]["position"], dtype=np.float64)
                    cost = float(np.linalg.norm(pb - pa))
                    add_edge(a, b, cost, MXT_NAV_EDGE_FLAGS["lateral"])
                    add_edge(b, a, cost, MXT_NAV_EDGE_FLAGS["lateral"])

        for row in range(row_count - 1):
            for col in range(segment_lateral_count):
                a = row_nodes[row][col]
                if a is None:
                    continue
                for next_col in (col - 1, col, col + 1):
                    if row_cyclic[row + 1]:
                        next_col = next_col % segment_lateral_count
                    elif next_col < 0 or next_col >= segment_lateral_count:
                        continue
                    b = row_nodes[row + 1][next_col]
                    if b is None:
                        continue
                    pa = np.asarray(nodes[a]["position"], dtype=np.float64)
                    pb = np.asarray(nodes[b]["position"], dtype=np.float64)
                    flags = MXT_NAV_EDGE_FLAGS["normal"]
                    if int(nodes[b]["terrain"]) & MXT_NAV_TERRAIN_BITS["jump"]:
                        flags |= MXT_NAV_EDGE_FLAGS["jump"]
                    add_edge(a, b, float(np.linalg.norm(pb - pa)), flags)

        max_lookahead_rows = min(3, max(0, row_count - 1))

        def lookahead_edge_clear(row0, col0, row1, col1):
            if row1 <= row0 + 1:
                return True
            tx0 = float(tx_1d[col0])
            tx1 = float(tx_1d[col1])
            if row_cyclic[row0] or row_cyclic[row1]:
                delta = tx1 - tx0
                if delta > 1.0:
                    tx1 -= 2.0
                elif delta < -1.0:
                    tx1 += 2.0
            ty0 = float(ty_1d[row0])
            ty1 = float(ty_1d[row1])
            for mid_row in range(row0 + 1, row1):
                denom = max(1.0e-6, ty1 - ty0)
                alpha = (float(ty_1d[mid_row]) - ty0) / denom
                tx_mid = tx0 + (tx1 - tx0) * alpha
                if tx_mid > 1.0:
                    tx_mid -= 2.0
                elif tx_mid < -1.0:
                    tx_mid += 2.0
                if _mxt_nav_sample_is_hole(props, tx_mid, float(ty_1d[mid_row])):
                    return False
            return True

        for row in range(row_count - 2):
            for col in range(segment_lateral_count):
                a = row_nodes[row][col]
                if a is None:
                    continue
                pa = np.asarray(nodes[a]["position"], dtype=np.float64)
                for row_skip in range(2, max_lookahead_rows + 1):
                    next_row = row + row_skip
                    if next_row >= row_count:
                        break
                    max_col_delta = min(segment_lateral_count - 1, row_skip)
                    if row_cyclic[next_row]:
                        next_cols = [
                            (col + delta) % segment_lateral_count
                            for delta in range(-max_col_delta, max_col_delta + 1)
                        ]
                    else:
                        next_cols = range(max(0, col - max_col_delta), min(segment_lateral_count, col + max_col_delta + 1))
                    for next_col in next_cols:
                        b = row_nodes[next_row][next_col]
                        if b is None:
                            continue
                        if not lookahead_edge_clear(row, col, next_row, next_col):
                            continue
                        pb = np.asarray(nodes[b]["position"], dtype=np.float64)
                        flags = MXT_NAV_EDGE_FLAGS["normal"] | MXT_NAV_EDGE_FLAGS["lookahead"]
                        if int(nodes[b]["terrain"]) & MXT_NAV_TERRAIN_BITS["jump"]:
                            flags |= MXT_NAV_EDGE_FLAGS["jump"]
                        add_edge(a, b, float(np.linalg.norm(pb - pa)), flags)

        def surface_point(tx, ty):
            tx_point = np.array([[float(tx)]], dtype=np.float64)
            ty_point = np.array([[float(ty)]], dtype=np.float64)
            cl_pos_p, cl_basis_p, cl_scl_p = _sample_curve_matrix_numpy(helper, np.array([float(ty)], dtype=np.float64))
            pos = _calculate_vertex_positions_numpy(props, cl_pos_p, cl_basis_p, cl_scl_p, tx_point, ty_point)[0, 0]
            return pos

        def append_trigger_node(trig, dash_target):
            tx = max(-1.0, min(1.0, float(trig.tx)))
            ty = max(0.0, min(1.0, float(trig.ty)))
            if _mxt_nav_sample_is_hole(props, tx, ty):
                return None
            pos_local = surface_point(tx, ty)
            pos_world_v = seg.matrix_world @ Vector((float(pos_local[0]), float(pos_local[1]), float(pos_local[2])))
            pos_world = np.array((pos_world_v.x, pos_world_v.y, pos_world_v.z), dtype=np.float64)
            eps = 0.002
            pos_fwd = surface_point(tx, min(1.0, ty + eps))
            if ty + eps > 1.0:
                pos_fwd = pos_local + (pos_local - surface_point(tx, max(0.0, ty - eps)))
            pos_side = surface_point(min(1.0, tx + eps), ty)
            if tx + eps > 1.0:
                pos_side = pos_local + (pos_local - surface_point(max(-1.0, tx - eps), ty))
            fwd_n = _mxt_nav_normalized(pos_fwd - pos_local, (0.0, 0.0, 1.0))
            lateral_n = _mxt_nav_normalized(pos_side - pos_local, (1.0, 0.0, 0.0))
            normal_n = _mxt_nav_normalized(np.cross(fwd_n, lateral_n), (0.0, 1.0, 0.0))
            terrain, flags = _mxt_nav_terrain_and_flags(props, tx, ty, pos_world, [trig])
            flags |= MXT_NAV_NODE_FLAGS["preferred"]
            terrain_lane = _mxt_nav_terrain_lane_metrics(props, tx, ty)
            dash_value_multiplier = max(1.0, float(dash_target.get("dash_chain_multiplier", 1.0))) if dash_target else 1.0
            dash_scores = {dash_target["id"]: 125.0 * dash_value_multiplier} if dash_target else {}
            node_id = len(nodes)
            checkpoint = min(cp_count - 1, int(math.floor(ty * cp_count)))
            nodes.append({
                "id": int(node_id),
                "segment": int(seg_index[seg]),
                "segment_name": seg.name,
                "checkpoint": int(checkpoint),
                "tx": round(tx, 6),
                "ty": round(ty, 6),
                "position": _mxt_nav_json_vec3(pos_world),
                "normal": _mxt_nav_json_vec3(normal_n),
                "forward": _mxt_nav_json_vec3(fwd_n),
                "terrain": int(terrain),
                "edge_clearance": round(_mxt_nav_edge_clearance_tx(props, tx, ty), 6),
                "terrain_edge_clearance": round(float(terrain_lane["terrain_edge_clearance"]), 6),
                "terrain_center_offset": round(float(terrain_lane["terrain_center_offset"]), 6),
                "terrain_span_width": round(float(terrain_lane["terrain_span_width"]), 6),
                "dash_approach_scores": {target_id: round(float(score), 6) for target_id, score in dash_scores.items()},
                "dash_approach_score": round(max(dash_scores.values()) if dash_scores else 0.0, 6),
                "dash_target": dash_target["id"] if dash_target else "",
                "dash_value_multiplier": round(float(dash_value_multiplier), 6),
                "downward_curve_risk": 0.0,
                "author_weight": 1.0,
                "flags": int(flags),
                "source": f"trigger:{trig.label}",
            })
            return node_id

        def append_embed_node(embed, tx, ty):
            if embed.embed_type == 'HOLE':
                return None
            if _mxt_nav_sample_is_hole(props, tx, ty):
                return None
            pos_local = surface_point(tx, ty)
            pos_world_v = seg.matrix_world @ Vector((float(pos_local[0]), float(pos_local[1]), float(pos_local[2])))
            pos_world = np.array((pos_world_v.x, pos_world_v.y, pos_world_v.z), dtype=np.float64)
            eps = 0.002
            pos_fwd = surface_point(tx, min(1.0, ty + eps))
            if ty + eps > 1.0:
                pos_fwd = pos_local + (pos_local - surface_point(tx, max(0.0, ty - eps)))
            pos_side = surface_point(min(1.0, tx + eps), ty)
            if tx + eps > 1.0:
                pos_side = pos_local + (pos_local - surface_point(max(-1.0, tx - eps), ty))
            fwd_n = _mxt_nav_normalized(pos_fwd - pos_local, (0.0, 0.0, 1.0))
            lateral_n = _mxt_nav_normalized(pos_side - pos_local, (1.0, 0.0, 0.0))
            normal_n = _mxt_nav_normalized(np.cross(fwd_n, lateral_n), (0.0, 1.0, 0.0))
            terrain, flags = _mxt_nav_terrain_and_flags(props, tx, ty, pos_world, [])
            terrain_lane = _mxt_nav_terrain_lane_metrics(props, tx, ty)
            dash_scores = _mxt_nav_dash_approach_scores(pos_world, fwd_n, segment_dash_targets)
            node_id = len(nodes)
            checkpoint = min(cp_count - 1, int(math.floor(ty * cp_count)))
            nodes.append({
                "id": int(node_id),
                "segment": int(seg_index[seg]),
                "segment_name": seg.name,
                "checkpoint": int(checkpoint),
                "tx": round(float(tx), 6),
                "ty": round(float(ty), 6),
                "position": _mxt_nav_json_vec3(pos_world),
                "normal": _mxt_nav_json_vec3(normal_n),
                "forward": _mxt_nav_json_vec3(fwd_n),
                "terrain": int(terrain),
                "edge_clearance": round(_mxt_nav_edge_clearance_tx(props, tx, ty), 6),
                "terrain_edge_clearance": round(float(terrain_lane["terrain_edge_clearance"]), 6),
                "terrain_center_offset": round(float(terrain_lane["terrain_center_offset"]), 6),
                "terrain_span_width": round(float(terrain_lane["terrain_span_width"]), 6),
                "dash_approach_scores": {target_id: round(float(score), 6) for target_id, score in dash_scores.items()},
                "dash_approach_score": round(max(dash_scores.values()) if dash_scores else 0.0, 6),
                "dash_value_multiplier": 1.0,
                "downward_curve_risk": 0.0,
                "author_weight": 1.0,
                "flags": int(flags),
                "source": f"embed:{embed.label}",
            })
            return node_id

        def connect_feature_node(feature_id):
            feature_node = nodes[feature_id]
            feature_pos = np.asarray(feature_node["position"], dtype=np.float64)
            feature_forward = _mxt_nav_normalized(np.asarray(feature_node["forward"], dtype=np.float64), (0.0, 0.0, 1.0))
            feature_ty = float(feature_node["ty"])
            nearest_row = int(np.argmin(np.abs(ty_1d - feature_ty)))
            first_row = max(0, nearest_row - 8)
            last_row = min(row_count - 1, nearest_row + 4)
            for row in range(first_row, last_row + 1):
                row_delta = abs(row - nearest_row)
                edge_flags = MXT_NAV_EDGE_FLAGS["normal"]
                if row_delta > 1:
                    edge_flags |= MXT_NAV_EDGE_FLAGS["lookahead"]
                if int(feature_node["terrain"]) & MXT_NAV_TERRAIN_BITS["jump"]:
                    edge_flags |= MXT_NAV_EDGE_FLAGS["jump"]
                for other_id in row_nodes[row]:
                    if other_id is None:
                        continue
                    other_node = nodes[other_id]
                    other_pos = np.asarray(other_node["position"], dtype=np.float64)
                    dist = float(np.linalg.norm(feature_pos - other_pos))
                    if dist > segment_row_spacing_meters * 3.5:
                        continue
                    other_forward = _mxt_nav_normalized(np.asarray(other_node["forward"], dtype=np.float64), (0.0, 0.0, 1.0))
                    other_to_feature = feature_pos - other_pos
                    feature_to_other = other_pos - feature_pos
                    if float(np.dot(other_to_feature, other_forward)) >= -1.0:
                        add_edge(other_id, feature_id, dist, edge_flags)
                    if float(np.dot(feature_to_other, feature_forward)) >= -1.0:
                        add_edge(feature_id, other_id, dist, edge_flags)

        def connect_embed_node(feature_id):
            feature_node = nodes[feature_id]
            feature_pos = np.asarray(feature_node["position"], dtype=np.float64)
            feature_forward = _mxt_nav_normalized(np.asarray(feature_node["forward"], dtype=np.float64), (0.0, 0.0, 1.0))
            feature_ty = float(feature_node["ty"])
            nearest_row = int(np.searchsorted(ty_1d, feature_ty))
            nearest_row = max(0, min(row_count - 1, nearest_row))
            if nearest_row > 0 and abs(float(ty_1d[nearest_row - 1]) - feature_ty) < abs(float(ty_1d[nearest_row]) - feature_ty):
                nearest_row -= 1
            row_indices = [nearest_row]
            if nearest_row > 0:
                row_indices.append(nearest_row - 1)
            if nearest_row + 1 < row_count:
                row_indices.append(nearest_row + 1)
            max_secondary_dist = max(18.0, segment_row_spacing_meters * 1.8)
            for row in row_indices:
                row_candidates = []
                for other_id in row_nodes[row]:
                    if other_id is None:
                        continue
                    other_node = nodes[other_id]
                    other_pos = np.asarray(other_node["position"], dtype=np.float64)
                    dist = float(np.linalg.norm(feature_pos - other_pos))
                    row_candidates.append((dist, other_id, other_node, other_pos))
                row_candidates.sort(key=lambda item: item[0])
                for rank, (dist, other_id, other_node, other_pos) in enumerate(row_candidates[:3]):
                    if row != nearest_row and dist > max_secondary_dist:
                        continue
                    if row == nearest_row and rank > 0 and dist > max_secondary_dist:
                        continue
                    edge_flags = MXT_NAV_EDGE_FLAGS["normal"]
                    if int(feature_node["terrain"]) & MXT_NAV_TERRAIN_BITS["jump"]:
                        edge_flags |= MXT_NAV_EDGE_FLAGS["jump"]
                    other_forward = _mxt_nav_normalized(np.asarray(other_node["forward"], dtype=np.float64), (0.0, 0.0, 1.0))
                    other_to_feature = feature_pos - other_pos
                    feature_to_other = other_pos - feature_pos
                    if float(np.dot(other_to_feature, other_forward)) >= -1.0:
                        add_edge(other_id, feature_id, dist, edge_flags)
                    if float(np.dot(feature_to_other, feature_forward)) >= -1.0:
                        add_edge(feature_id, other_id, dist, edge_flags)

        trigger_node_count = 0
        embed_node_count = 0
        for embed in getattr(props, "embeds", []):
            if embed.embed_type == 'HOLE':
                continue
            for ty in _mxt_nav_embed_ty_samples(embed, ty_1d, include_epsilon=False):
                bounds = _mxt_nav_embed_bounds(embed, float(ty))
                if not bounds:
                    continue
                left = max(-1.0, min(1.0, float(bounds[0])))
                right = max(-1.0, min(1.0, float(bounds[1])))
                center = max(-1.0, min(1.0, (left + right) * 0.5))
                for tx in _mxt_nav_unique_sorted_tx((left, center, right)):
                    embed_node = append_embed_node(embed, tx, float(ty))
                    if embed_node is not None:
                        embed_node_count += 1
                        connect_embed_node(embed_node)
        for trig_idx, trig in enumerate(segment_triggers):
            dash_target = None
            if trig.obj_type == 'DASHPLATE':
                for target in segment_dash_targets:
                    if target["id"].startswith(f"{seg_index[seg]}:{trig_idx}:"):
                        dash_target = target
                        break
            trigger_node = append_trigger_node(trig, dash_target)
            if trigger_node is not None:
                trigger_node_count += 1
                connect_feature_node(trigger_node)

        segment_grids[seg] = {
            "rows": row_nodes,
            "tx": tx_1d,
            "ty": ty_1d,
            "row_count": row_count,
            "lateral_count": segment_lateral_count,
            "trigger_node_count": trigger_node_count,
            "embed_node_count": embed_node_count,
        }
        segment_records.append({
            "id": int(seg_index[seg]),
            "name": seg.name,
            "node_start": int(min((n for row in row_nodes for n in row if n is not None), default=0)),
            "node_count": int(sum(1 for row in row_nodes for n in row if n is not None)),
            "rows": int(row_count),
            "lateral_samples": int(segment_lateral_count),
            "row_spacing_meters": float(segment_row_spacing_meters),
            "trigger_nodes": int(trigger_node_count),
            "embed_nodes": int(embed_node_count),
        })

    analytic_node_count = len(nodes)
    mesh_node_ids = []
    mesh_blocker_edges = []
    mesh_collision_triangles = _mxt_nav_collect_mesh_collision_triangles(context, seg_index)
    blocker_tris = [tri for tri in mesh_collision_triangles if tri["blocker"]]
    for tri in blocker_tris:
        p0, p1, p2 = tri["positions"]
        mesh_blocker_edges.extend([
            Vector(tuple(p0)), Vector(tuple(p1)),
            Vector(tuple(p1)), Vector(tuple(p2)),
            Vector(tuple(p2)), Vector(tuple(p0)),
        ])

    analytic_positions = np.asarray(
        [nodes[i]["position"] for i in range(analytic_node_count)],
        dtype=np.float64
    ) if analytic_node_count > 0 else np.zeros((0, 3), dtype=np.float64)

    if blocker_tris:
        for i in range(analytic_node_count):
            pos = np.asarray(nodes[i]["position"], dtype=np.float64)
            mesh_clearance = _mxt_nav_mesh_blocker_clearance(pos, blocker_tris, mesh_spacing_meters * 1.5)
            nodes[i]["edge_clearance"] = round(min(float(nodes[i].get("edge_clearance", 1.0)), mesh_clearance), 6)

    def nearest_analytic_node_id(point, preferred_segment):
        if analytic_node_count <= 0:
            return None
        if preferred_segment >= 0:
            preferred_ids = [
                i for i in range(analytic_node_count)
                if int(nodes[i].get("segment", -1)) == int(preferred_segment)
            ]
            if preferred_ids:
                pref_positions = np.asarray([nodes[i]["position"] for i in preferred_ids], dtype=np.float64)
                idx = int(np.argmin(np.linalg.norm(pref_positions - point, axis=1)))
                return preferred_ids[idx]
        idx = int(np.argmin(np.linalg.norm(analytic_positions - point, axis=1)))
        return idx

    for tri in mesh_collision_triangles:
        if not tri["drivable"]:
            continue
        for pos, normal in _mxt_nav_sample_mesh_triangle(tri, mesh_spacing_meters):
            nearest_id = nearest_analytic_node_id(pos, tri.get("source_segment", -1))
            nearest = nodes[nearest_id] if nearest_id is not None else None
            terrain = int(tri["terrain"])
            flags = MXT_NAV_NODE_FLAGS["mesh"]
            if terrain & MXT_NAV_TERRAIN_BITS["dash"]:
                flags |= MXT_NAV_NODE_FLAGS["dash"] | MXT_NAV_NODE_FLAGS["preferred"]
            if terrain & MXT_NAV_TERRAIN_BITS["recharge"]:
                flags |= MXT_NAV_NODE_FLAGS["recharge"] | MXT_NAV_NODE_FLAGS["preferred"]
            if terrain & MXT_NAV_TERRAIN_BITS["jump"]:
                flags |= MXT_NAV_NODE_FLAGS["jump"]
            if terrain & MXT_NAV_TERRAIN_BITS["dirt"]:
                flags |= MXT_NAV_NODE_FLAGS["dirt"] | MXT_NAV_NODE_FLAGS["avoid"]
            if terrain & MXT_NAV_TERRAIN_BITS["ice"]:
                flags |= MXT_NAV_NODE_FLAGS["ice"] | MXT_NAV_NODE_FLAGS["avoid"]
            if terrain & MXT_NAV_TERRAIN_BITS["lava"]:
                flags |= MXT_NAV_NODE_FLAGS["lava"] | MXT_NAV_NODE_FLAGS["avoid"]
            edge_clearance = _mxt_nav_mesh_blocker_clearance(pos, blocker_tris, mesh_spacing_meters * 1.5)
            node_id = len(nodes)
            nodes.append({
                "id": int(node_id),
                "segment": int(nearest.get("segment", 0) if nearest else 0),
                "segment_name": nearest.get("segment_name", "<mesh>") if nearest else "<mesh>",
                "checkpoint": int(nearest.get("checkpoint", 0) if nearest else 0),
                "tx": round(float(nearest.get("tx", 0.0) if nearest else 0.0), 6),
                "ty": round(float(nearest.get("ty", 0.0) if nearest else 0.0), 6),
                "position": _mxt_nav_json_vec3(pos),
                "normal": _mxt_nav_json_vec3(normal),
                "forward": nearest.get("forward", [0.0, 0.0, 1.0]) if nearest else [0.0, 0.0, 1.0],
                "terrain": terrain,
                "edge_clearance": round(float(edge_clearance), 6),
                "terrain_edge_clearance": round(float(edge_clearance), 6),
                "terrain_center_offset": 0.0,
                "terrain_span_width": 2.0,
                "dash_approach_scores": {},
                "dash_approach_score": 0.0,
                "dash_value_multiplier": 1.0,
                "downward_curve_risk": 0.0,
                "author_weight": 1.0,
                "flags": int(flags),
                "source": tri["source"],
            })
            mesh_node_ids.append(node_id)

    if mesh_node_ids:
        connect_distance = max(row_spacing_meters, mesh_spacing_meters) * 1.45
        cell_size = max(1.0, connect_distance)
        spatial = {}
        for node in nodes:
            p = node["position"]
            key = (
                int(math.floor(float(p[0]) / cell_size)),
                int(math.floor(float(p[1]) / cell_size)),
                int(math.floor(float(p[2]) / cell_size)),
            )
            spatial.setdefault(key, []).append(int(node["id"]))

        def neighbour_cells(key):
            for dx in (-1, 0, 1):
                for dy in (-1, 0, 1):
                    for dz in (-1, 0, 1):
                        yield key[0] + dx, key[1] + dy, key[2] + dz

        for mesh_id in mesh_node_ids:
            p = nodes[mesh_id]["position"]
            key = (
                int(math.floor(float(p[0]) / cell_size)),
                int(math.floor(float(p[1]) / cell_size)),
                int(math.floor(float(p[2]) / cell_size)),
            )
            pa = np.asarray(p, dtype=np.float64)
            for cell in neighbour_cells(key):
                for other_id in spatial.get(cell, []):
                    if other_id == mesh_id:
                        continue
                    pb = np.asarray(nodes[other_id]["position"], dtype=np.float64)
                    dist = float(np.linalg.norm(pb - pa))
                    if dist > connect_distance:
                        continue
                    flags = MXT_NAV_EDGE_FLAGS["normal"]
                    if int(nodes[other_id]["terrain"]) & MXT_NAV_TERRAIN_BITS["jump"]:
                        flags |= MXT_NAV_EDGE_FLAGS["jump"]
                    add_edge(mesh_id, other_id, dist, flags)
                    add_edge(other_id, mesh_id, dist, flags)

    transition_edges = 0
    glide_drop_edges = 0

    def connect_boundary_rows(from_seg, from_row, to_seg, to_row, max_distance, flags):
        nonlocal transition_edges
        from_grid = segment_grids.get(from_seg)
        to_grid = segment_grids.get(to_seg)
        if not from_grid or not to_grid:
            return
        from_nodes = from_grid["rows"][from_row]
        to_nodes = to_grid["rows"][to_row]
        for a in from_nodes:
            if a is None:
                continue
            pa = np.asarray(nodes[a]["position"], dtype=np.float64)
            for b in to_nodes:
                if b is None:
                    continue
                pb = np.asarray(nodes[b]["position"], dtype=np.float64)
                delta = pb - pa
                dist = float(np.linalg.norm(delta))
                if dist <= max_distance:
                    b_node = nodes[b]
                    edge_flags = flags
                    if int(b_node["terrain"]) & MXT_NAV_TERRAIN_BITS["jump"]:
                        edge_flags |= MXT_NAV_EDGE_FLAGS["jump"]
                    if add_edge(a, b, dist * 1.1, edge_flags):
                        transition_edges += 1

    def segment_has_prev(seg, prev_seg):
        props = seg.mxt_road_overall_props
        for ref in props.prev_segments:
            if ref.segment == prev_seg:
                return True
        return False

    def connect_glide_drop_rows(from_seg, to_seg, flags):
        nonlocal glide_drop_edges
        from_props = from_seg.mxt_road_overall_props
        to_props = to_seg.mxt_road_overall_props
        if not getattr(from_props, "cpu_nav_allow_glide_drops", True):
            return
        if not getattr(to_props, "cpu_nav_allow_glide_drops", True):
            return
        from_grid = segment_grids.get(from_seg)
        to_grid = segment_grids.get(to_seg)
        if not from_grid or not to_grid:
            return
        from_nodes = from_grid["rows"][-1]
        to_nodes = to_grid["rows"][0]
        for a in from_nodes:
            if a is None:
                continue
            pa = np.asarray(nodes[a]["position"], dtype=np.float64)
            for b in to_nodes:
                if b is None:
                    continue
                pb = np.asarray(nodes[b]["position"], dtype=np.float64)
                y_drop = float(pa[1] - pb[1])
                if y_drop <= 0.0:
                    continue
                dist = float(np.linalg.norm(pb - pa))
                edge_flags = flags | MXT_NAV_EDGE_FLAGS["jump"] | MXT_NAV_EDGE_FLAGS["glide_drop"]
                if add_edge(a, b, dist * 1.1, edge_flags):
                    glide_drop_edges += 1

    authored_pairs = set()
    for seg in seg_order:
        props = seg.mxt_road_overall_props
        next_refs = [ref.segment for ref in props.next_segments if ref.segment in segment_grids]
        for nxt in next_refs:
            authored_pairs.add((seg, nxt))
            flags = MXT_NAV_EDGE_FLAGS["transition"]
            if len(next_refs) > 1 or len(getattr(nxt.mxt_road_overall_props, "prev_segments", [])) > 1:
                flags |= MXT_NAV_EDGE_FLAGS["branch"]
            connect_boundary_rows(seg, -1, nxt, 0, segment_transition_distance(seg), flags)
            if segment_has_prev(nxt, seg):
                connect_glide_drop_rows(seg, nxt, flags)

    branch_edges = 0
    if branch_distance > 0.0 or any(segment_branch_distance(seg) > 0.0 for seg in segment_grids.keys()):
        before = len(edges)
        segs_with_grids = list(segment_grids.keys())
        for seg_a in segs_with_grids:
            for seg_b in segs_with_grids:
                if seg_a == seg_b or (seg_a, seg_b) in authored_pairs:
                    continue
                physical_branch_distance = segment_branch_distance(seg_a)
                if physical_branch_distance > 0.0:
                    connect_boundary_rows(seg_a, -1, seg_b, 0, physical_branch_distance, MXT_NAV_EDGE_FLAGS["transition"] | MXT_NAV_EDGE_FLAGS["branch"])
        branch_edges = max(0, len(edges) - before)

    nav_segments = [seg for seg in seg_order if seg in segment_grids]
    first_seg = nav_segments[0] if nav_segments else None
    last_seg = nav_segments[-1] if nav_segments else None
    start_node = None
    start_nodes = []
    finish_nodes = []
    if first_seg in segment_grids:
        first_row = segment_grids[first_seg]["rows"][0]
        candidates = [n for n in first_row if n is not None]
        start_nodes = candidates
        if candidates:
            start_node = min(candidates, key=lambda n: abs(float(nodes[n]["tx"])))
    if last_seg in segment_grids:
        finish_nodes = [n for n in segment_grids[last_seg]["rows"][-1] if n is not None]

    route_kind_map = {
        "default": "default",
        "safe": "safe",
        "aggressive": "aggressive",
        "boost_dash": "dash",
        "dash": "dash",
        "recharge": "recharge",
    }
    nodes_by_id, route_outgoing = _mxt_nav_route_outgoing(nodes, edges)
    route_alternatives = {}
    routes = {}
    runtime_routes = {}
    runtime_route_alternatives = {}
    choice_points = []
    diagnostic_routes = {}
    if include_routes:
        alternatives_by_kind = {}
        for name, kind in route_kind_map.items():
            if kind not in alternatives_by_kind:
                seed_paths = route_alternatives.get("default", []) if kind == "safe" else None
                alternatives_by_kind[kind] = _mxt_nav_route_alternatives(
                    nodes, edges, start_node, finish_nodes, kind, 4, nodes_by_id, route_outgoing,
                    seed_paths, start_nodes, 100.0
                )
            route_alternatives[name] = alternatives_by_kind[kind]
        routes = {
            name: alternatives[0] if alternatives else []
            for name, alternatives in route_alternatives.items()
        }
        choice_points = _mxt_nav_choice_points(nodes, edges, route_alternatives, nodes_by_id, route_outgoing)
        runtime_route_spacing_meters = 14.0
        runtime_route_relax_iterations = 4
        runtime_routes, runtime_route_alternatives = _mxt_nav_runtime_routes(
            nodes_by_id, routes, route_alternatives, route_kind_map, choice_points,
            runtime_route_spacing_meters, runtime_route_relax_iterations
        )
        diagnostic_routes = {
            "reachable": _mxt_nav_reachable_trace(nodes, edges, start_node, "default", nodes_by_id, route_outgoing),
        }
    else:
        runtime_route_spacing_meters = 14.0
        runtime_route_relax_iterations = 4
    route_lengths = {name: len(path) for name, path in routes.items()}
    route_alternative_counts = {name: len(paths) for name, paths in route_alternatives.items()}
    runtime_route_lengths = {name: len(points) for name, points in runtime_routes.items()}
    diagnostic_route_lengths = {name: len(path) for name, path in diagnostic_routes.items()}

    connected_nodes = set()
    for edge in edges:
        connected_nodes.add(int(edge["from"]))
        connected_nodes.add(int(edge["to"]))
    disconnected = len(nodes) - len(connected_nodes)

    nav = {
        "format": "mxt_nav",
        "version": MXT_NAV_FORMAT_VERSION,
        "source_track": filepath,
        "settings": {
            "lateral_samples": int(lateral_count),
            "row_spacing_meters": float(row_spacing_meters),
            "transition_distance": float(transition_distance),
            "branch_distance": float(branch_distance),
            "mesh_sample_spacing_meters": float(mesh_spacing_meters),
            "runtime_route_spacing_meters": float(runtime_route_spacing_meters),
            "runtime_route_relax_iterations": int(runtime_route_relax_iterations),
        },
        "terrain_bits": MXT_NAV_TERRAIN_BITS,
        "node_flags": MXT_NAV_NODE_FLAGS,
        "edge_flags": MXT_NAV_EDGE_FLAGS,
        "segments": segment_records,
        "nodes": nodes,
        "edges": edges,
        "routes": routes,
        "route_alternatives": route_alternatives,
        "runtime_routes": runtime_routes,
        "runtime_route_alternatives": runtime_route_alternatives,
        "choice_points": choice_points,
        "dash_targets": all_dash_targets,
        "diagnostic_routes": diagnostic_routes,
        "diagnostics": {
            "node_count": int(len(nodes)),
            "edge_count": int(len(edges)),
            "transition_edges": int(transition_edges),
            "branch_edges": int(branch_edges),
            "glide_drop_edges": int(glide_drop_edges),
            "mesh_nav_nodes": int(len(mesh_node_ids)),
            "mesh_collision_triangles": int(len(mesh_collision_triangles)),
            "mesh_blocker_triangles": int(len(blocker_tris)),
            "dash_target_count": int(len(all_dash_targets)),
            "disconnected_nodes": int(disconnected),
            "start_node": -1 if start_node is None else int(start_node),
            "finish_node_count": int(len(finish_nodes)),
            "route_lengths": route_lengths,
            "route_alternative_counts": route_alternative_counts,
            "runtime_route_lengths": runtime_route_lengths,
            "choice_point_count": int(len(choice_points)),
            "diagnostic_route_lengths": diagnostic_route_lengths,
            "skipped_segments": skipped_segments,
        },
    }

    global _MXT_NAV_DRAW_CACHE
    draw_edges = []
    edge_entries = []
    for edge in edges:
        from_node = nodes[int(edge["from"])]
        to_node = nodes[int(edge["to"])]
        a = from_node["position"]
        b = to_node["position"]
        edge_positions = [Vector((a[0], a[1], a[2])), Vector((b[0], b[1], b[2]))]
        draw_edges.extend(edge_positions)
        edge_entries.append({
            "from_segment": from_node.get("segment_name", ""),
            "to_segment": to_node.get("segment_name", ""),
            "positions": edge_positions,
        })
    route_positions = {}
    for route_name, route_points in runtime_routes.items():
        if route_name == "dash":
            continue
        route_positions[route_name] = [
            Vector(tuple(point["position"]))
            for point in route_points
        ]
    for route_name, route_nodes in routes.items():
        if route_name == "dash" or route_name in route_positions:
            continue
        route_positions[route_name] = [
            Vector(tuple(nodes[int(node_id)]["position"]))
            for node_id in route_nodes
            if 0 <= int(node_id) < len(nodes)
        ]
    route_alternative_positions = []
    for route_name, alternatives in runtime_route_alternatives.items():
        if route_name == "dash":
            continue
        for alt_index, route_points in enumerate(alternatives):
            if alt_index == 0:
                continue
            positions = [
                Vector(tuple(point["position"]))
                for point in route_points
            ]
            if len(positions) >= 2:
                route_alternative_positions.append({
                    "route": route_name,
                    "index": int(alt_index),
                    "positions": positions,
                })
    if not route_alternative_positions:
        for route_name, alternatives in route_alternatives.items():
            if route_name == "dash":
                continue
            for alt_index, route_nodes in enumerate(alternatives):
                if alt_index == 0:
                    continue
                positions = [
                    Vector(tuple(nodes[int(node_id)]["position"]))
                    for node_id in route_nodes
                    if 0 <= int(node_id) < len(nodes)
                ]
                if len(positions) >= 2:
                    route_alternative_positions.append({
                        "route": route_name,
                        "index": int(alt_index),
                        "positions": positions,
                    })
    if not any(len(points) >= 2 for points in route_positions.values()):
        for route_name, route_nodes in diagnostic_routes.items():
            route_positions[route_name] = [
                Vector(tuple(nodes[int(node_id)]["position"]))
                for node_id in route_nodes
                if 0 <= int(node_id) < len(nodes)
            ]
    _MXT_NAV_DRAW_CACHE = {
        "node_positions": [Vector(tuple(node["position"])) for node in nodes],
        "node_entries": [
            {
                "segment": node.get("segment_name", ""),
                "position": Vector(tuple(node["position"])),
            }
            for node in nodes
        ],
        "edge_positions": draw_edges,
        "edge_entries": edge_entries,
        "route_positions": route_positions,
        "route_alternative_positions": route_alternative_positions,
        "mesh_node_positions": [Vector(tuple(nodes[node_id]["position"])) for node_id in mesh_node_ids],
        "mesh_blocker_edges": mesh_blocker_edges,
    }

    return nav


def _mxt_track_reachable_segment_order(first):
    segs = []
    visited = set()
    queue = [first]
    while queue:
        seg = queue.pop(0)
        if not seg or seg in visited:
            continue
        visited.add(seg)
        segs.append(seg)
        props = seg.mxt_road_overall_props
        for ref in props.next_segments:
            if ref.segment and ref.segment not in visited:
                queue.append(ref.segment)

    indeg = {s: 0 for s in visited}
    for seg in visited:
        props = seg.mxt_road_overall_props
        for ref in props.next_segments:
            nxt = ref.segment
            if nxt and nxt in indeg and nxt != first:
                indeg[nxt] += 1

    seg_order = []
    q = [s for s in segs if indeg[s] == 0]
    if first in q:
        q.remove(first)
        q.insert(0, first)

    seen = set()
    while q:
        seg = q.pop(0)
        if seg in seen:
            continue
        seen.add(seg)
        seg_order.append(seg)
        props = seg.mxt_road_overall_props
        for ref in props.next_segments:
            nxt = ref.segment
            if nxt and nxt in indeg and nxt != first:
                indeg[nxt] -= 1
                if indeg[nxt] == 0:
                    q.append(nxt)

    for seg in segs:
        if seg not in seen:
            seg_order.append(seg)

    return seg_order, visited


def _mxt_bake_cpu_nav_preview(context):
    ts = context.scene.mxt_track_settings
    first = ts.first_segment
    if not first:
        raise RuntimeError("First segment not set")
    seg_order, visited = _mxt_track_reachable_segment_order(first)
    with _mxt_profile_scope("nav_rebake_curvematrix_checkpoints"):
        for seg in seg_order:
            _bake_curve_matrix_direct(seg)
            _generate_checkpoints_for_segment(seg)
    for trig in ts.trigger_objects:
        if trig.segment in visited:
            _update_trigger_helper(trig)
    context.view_layer.update()
    seg_index = {s: i for i, s in enumerate(seg_order)}
    return _mxt_nav_generate(context, "", seg_order, seg_index)


def _mxt_preview_cpu_nav_graph(context):
    ts = context.scene.mxt_track_settings
    first = ts.first_segment
    if not first:
        raise RuntimeError("First segment not set")
    seg_order, visited = _mxt_track_reachable_segment_order(first)
    with _mxt_profile_scope("nav_graph_preview_curvematrix_checkpoints"):
        for seg in seg_order:
            _bake_curve_matrix_direct(seg)
            _generate_checkpoints_for_segment(seg)
    for trig in ts.trigger_objects:
        if trig.segment in visited:
            _update_trigger_helper(trig)
    context.view_layer.update()
    seg_index = {s: i for i, s in enumerate(seg_order)}
    return _mxt_nav_generate(context, "", seg_order, seg_index, include_routes=False)


def _mxt_store_cpu_nav_bake(context, nav):
    import base64
    import json
    import zlib

    raw = json.dumps(nav, separators=(",", ":")).encode("utf-8")
    packed = base64.b64encode(zlib.compress(raw, 6)).decode("ascii")
    context.scene["mxt_cpu_nav_bake_zlib_b64"] = packed
    context.scene["mxt_cpu_nav_bake_encoding"] = "json+zlib+base64"
    context.scene["mxt_cpu_nav_bake_format_version"] = int(MXT_NAV_FORMAT_VERSION)
    context.scene["mxt_cpu_nav_bake_byte_count"] = int(len(raw))
    context.scene["mxt_cpu_nav_bake_packed_char_count"] = int(len(packed))
    diag = nav.get("diagnostics", {})
    context.scene["mxt_cpu_nav_bake_node_count"] = int(diag.get("node_count", 0))
    context.scene["mxt_cpu_nav_bake_edge_count"] = int(diag.get("edge_count", 0))
    context.scene["mxt_cpu_nav_bake_choice_count"] = int(diag.get("choice_point_count", 0))


def _mxt_load_cpu_nav_bake(context):
    import base64
    import json
    import zlib

    packed = context.scene.get("mxt_cpu_nav_bake_zlib_b64", "")
    if not packed:
        return None
    encoding = context.scene.get("mxt_cpu_nav_bake_encoding", "")
    if encoding != "json+zlib+base64":
        return None
    raw = zlib.decompress(base64.b64decode(packed.encode("ascii")))
    nav = json.loads(raw.decode("utf-8"))
    if nav.get("format") != "mxt_nav":
        return None
    return nav
