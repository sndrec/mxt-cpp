"""CPU navigation costs, terrain queries, and route alternatives."""

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
    MXT_NAV_MESH_BLOCKER_SURFACES,
    MXT_NAV_MESH_DRIVABLE_SURFACES,
    MXT_NAV_MESH_SURFACE_TERRAIN,
    MXT_NAV_NODE_FLAGS,
    MXT_NAV_TERRAIN_BITS,
)

def _mxt_nav_json_vec3(v):
    return [round(float(v[0]), 6), round(float(v[1]), 6), round(float(v[2]), 6)]


def _mxt_nav_normalized(v, fallback):
    length = float(np.linalg.norm(v))
    if length > 1.0e-9:
        return v / length
    return np.asarray(fallback, dtype=np.float64)


def _mxt_nav_embed_bounds(embed, ty):
    if not (embed.helper and embed.helper.animation_data and embed.helper.animation_data.action):
        return None
    start_t = max(0.0, min(1.0, float(embed.start_t)))
    end_t = max(0.0, min(1.0, float(embed.end_t)))
    if end_t < start_t:
        start_t, end_t = end_t, start_t
    ty = float(ty)
    if ty < start_t or ty > end_t:
        return None
    act = embed.helper.animation_data.action
    f_left = act.fcurves.find("location", index=1)
    f_right = act.fcurves.find("location", index=2)
    if not (f_left and f_right):
        return None
    frame = ty * 100.0
    left = float(f_left.evaluate(frame))
    right = float(f_right.evaluate(frame))
    if right < left:
        left, right = right, left
    if right < -1.0 or left > 1.0:
        return None
    return max(-1.0, min(1.0, left)), max(-1.0, min(1.0, right))


def _mxt_nav_sample_is_hole(props, tx, ty):
    for embed in getattr(props, "embeds", []):
        if embed.embed_type != 'HOLE':
            continue
        bounds = _mxt_nav_embed_bounds(embed, ty)
        if bounds and float(tx) >= bounds[0] and float(tx) <= bounds[1]:
            return True
    return False


def _mxt_nav_edge_clearance_tx(props, tx, ty):
    clearance = min(float(tx) + 1.0, 1.0 - float(tx))
    for embed in getattr(props, "embeds", []):
        bounds = _mxt_nav_embed_bounds(embed, ty)
        if not bounds:
            continue
        if embed.embed_type == 'HOLE':
            if tx < bounds[0]:
                clearance = min(clearance, bounds[0] - float(tx))
            elif tx > bounds[1]:
                clearance = min(clearance, float(tx) - bounds[1])
            else:
                return 0.0
    return max(0.0, float(clearance))


def _mxt_nav_terrain_lane_metrics(props, tx, ty):
    best = None
    tx = float(tx)
    for embed in getattr(props, "embeds", []):
        if embed.embed_type == 'HOLE':
            continue
        bounds = _mxt_nav_embed_bounds(embed, ty)
        if not bounds or tx < bounds[0] or tx > bounds[1]:
            continue
        left, right = bounds
        width = max(0.0, right - left)
        if width <= 1.0e-6:
            continue
        center = (left + right) * 0.5
        half_width = width * 0.5
        edge_clearance = min(tx - left, right - tx)
        center_offset = min(1.0, abs(tx - center) / max(1.0e-6, half_width))
        metric = {
            "terrain": embed.embed_type,
            "terrain_edge_clearance": max(0.0, float(edge_clearance)),
            "terrain_center_offset": float(center_offset),
            "terrain_span_width": float(width),
        }
        if best is None or metric["terrain_edge_clearance"] < best["terrain_edge_clearance"]:
            best = metric
    if best is None:
        return {
            "terrain": "",
            "terrain_edge_clearance": 1.0,
            "terrain_center_offset": 0.0,
            "terrain_span_width": 2.0,
        }
    return best


def _mxt_nav_trigger_contains(trig, world_pos):
    if not trig.helper:
        return False
    ext_map = {
        'DASHPLATE': Vector((6.0, 4.0, 12.0)),
        'JUMPPLATE': Vector((12.0, 4.0, 4.0)),
        'MINE': Vector((2.0, 3.0, 2.0)),
    }
    try:
        local = trig.helper.matrix_world.inverted() @ Vector((float(world_pos[0]), float(world_pos[1]), float(world_pos[2])))
    except Exception:
        return False
    ext = ext_map.get(trig.obj_type, Vector((1.0, 1.0, 1.0)))
    return abs(local.x) <= ext.x and abs(local.y) <= ext.y and abs(local.z) <= ext.z


def _mxt_nav_base_material_name(name):
    if len(name) > 4 and name[-4] == '.' and name[-3:].isdigit():
        return name[:-4]
    return name


def _mxt_nav_mesh_surface_for_preview_material(mesh, polygon_index):
    preview_material_map = {
        'track_surface': 'TRACK',
        'track_rail': 'RAIL',
        'embed_ice': 'ICE',
        'embed_recharge': 'RECHARGE',
        'embed_dirt': 'DIRT',
        'embed_lava': 'LAVA',
        'embed_hole': 'HOLE',
    }
    poly = mesh.polygons[polygon_index]
    if poly.material_index >= len(mesh.materials):
        return None
    mat_name = _mxt_nav_base_material_name(mesh.materials[poly.material_index].name)
    return preview_material_map.get(mat_name)


def _mxt_nav_collect_mesh_collision_triangles(context, seg_index):
    depsgraph = context.evaluated_depsgraph_get()
    triangles = []

    def append_object(obj, surface_for_polygon, source_segment=-1):
        eval_obj = obj.evaluated_get(depsgraph)
        mesh = eval_obj.to_mesh()
        try:
            mesh.calc_loop_triangles()
            normal_matrix = obj.matrix_world.to_3x3().inverted().transposed()
            for loop_tri in mesh.loop_triangles:
                surface = surface_for_polygon(mesh, loop_tri.polygon_index)
                if surface not in MXT_NAV_MESH_SURFACE_TERRAIN:
                    continue
                loop_indices = list(loop_tri.loops)
                if len(loop_indices) != 3:
                    continue
                positions = []
                normals = []
                for loop_index in loop_indices:
                    loop = mesh.loops[loop_index]
                    vert = mesh.vertices[loop.vertex_index]
                    p = obj.matrix_world @ vert.co
                    n = (normal_matrix @ loop.normal).normalized()
                    positions.append(np.array((p.x, p.y, p.z), dtype=np.float64))
                    normals.append(np.array((n.x, n.y, n.z), dtype=np.float64))
                face_normal = np.cross(positions[1] - positions[0], positions[2] - positions[0])
                face_len = float(np.linalg.norm(face_normal))
                if face_len <= 1.0e-9:
                    continue
                face_normal /= face_len
                triangles.append({
                    "positions": positions,
                    "normals": normals,
                    "face_normal": face_normal,
                    "surface": surface,
                    "terrain": int(MXT_NAV_MESH_SURFACE_TERRAIN[surface]),
                    "drivable": surface in MXT_NAV_MESH_DRIVABLE_SURFACES,
                    "blocker": surface in MXT_NAV_MESH_BLOCKER_SURFACES,
                    "source": obj.name,
                    "source_segment": int(source_segment),
                })
        finally:
            eval_obj.to_mesh_clear()

    for obj in bpy.data.objects:
        props = getattr(obj, "mxt_mesh_collision_props", None)
        if obj.type != 'MESH' or not props or not props.is_mxt_collision_mesh:
            continue
        surface_type = props.surface_type
        append_object(obj, lambda _mesh, _poly_index, surface_type=surface_type: surface_type)

    for seg, segment_index in seg_index.items():
        props = seg.mxt_road_overall_props
        if not getattr(props, "export_preview_mesh_collision", False):
            continue
        if getattr(props, "disable_preview_mesh_generation", False):
            continue
        mesh_name = f"{seg.name}_PreviewMesh"
        preview_obj = next((c for c in seg.children if c.name == mesh_name), None)
        if preview_obj and preview_obj.type == 'MESH':
            append_object(preview_obj, _mxt_nav_mesh_surface_for_preview_material, int(segment_index))

    return triangles


def _mxt_nav_closest_point_on_triangle(p, a, b, c):
    ab = b - a
    ac = c - a
    ap = p - a
    d1 = float(np.dot(ab, ap))
    d2 = float(np.dot(ac, ap))
    if d1 <= 0.0 and d2 <= 0.0:
        return a
    bp = p - b
    d3 = float(np.dot(ab, bp))
    d4 = float(np.dot(ac, bp))
    if d3 >= 0.0 and d4 <= d3:
        return b
    vc = d1 * d4 - d3 * d2
    if vc <= 0.0 and d1 >= 0.0 and d3 <= 0.0:
        v = d1 / (d1 - d3)
        return a + ab * v
    cp = p - c
    d5 = float(np.dot(ab, cp))
    d6 = float(np.dot(ac, cp))
    if d6 >= 0.0 and d5 <= d6:
        return c
    vb = d5 * d2 - d1 * d6
    if vb <= 0.0 and d2 >= 0.0 and d6 <= 0.0:
        w = d2 / (d2 - d6)
        return a + ac * w
    va = d3 * d6 - d5 * d4
    if va <= 0.0 and (d4 - d3) >= 0.0 and (d5 - d6) >= 0.0:
        w = (d4 - d3) / ((d4 - d3) + (d5 - d6))
        return b + (c - b) * w
    denom = 1.0 / (va + vb + vc)
    v = vb * denom
    w = vc * denom
    return a + ab * v + ac * w


def _mxt_nav_mesh_blocker_clearance(point, blocker_tris, comfort_distance):
    if not blocker_tris:
        return 1.0
    best = float("inf")
    for tri in blocker_tris:
        a, b, c = tri["positions"]
        closest = _mxt_nav_closest_point_on_triangle(point, a, b, c)
        best = min(best, float(np.linalg.norm(point - closest)))
    if not math.isfinite(best):
        return 1.0
    return max(0.0, min(1.0, best / max(1.0, float(comfort_distance))))


def _mxt_nav_sample_mesh_triangle(tri, spacing):
    a, b, c = tri["positions"]
    max_edge = max(float(np.linalg.norm(b - a)), float(np.linalg.norm(c - b)), float(np.linalg.norm(a - c)))
    steps = max(1, min(8, int(math.ceil(max_edge / max(1.0, float(spacing))))))
    samples = []
    for i in range(steps):
        for j in range(steps - i):
            u = (float(i) + 1.0 / 3.0) / float(steps)
            v = (float(j) + 1.0 / 3.0) / float(steps)
            if u + v >= 1.0:
                u = (float(i) + 1.0 / 3.0) / float(steps + 1)
                v = (float(j) + 1.0 / 3.0) / float(steps + 1)
            w = max(0.0, 1.0 - u - v)
            pos = a * w + b * u + c * v
            normal = _mxt_nav_normalized(tri["normals"][0] * w + tri["normals"][1] * u + tri["normals"][2] * v, tri["face_normal"])
            samples.append((pos, normal))
    if not samples:
        samples.append(((a + b + c) / 3.0, tri["face_normal"]))
    return samples


def _mxt_nav_terrain_and_flags(props, tx, ty, world_pos, triggers):
    terrain = MXT_NAV_TERRAIN_BITS["normal"]
    flags = 0
    for embed in getattr(props, "embeds", []):
        bounds = _mxt_nav_embed_bounds(embed, ty)
        if not bounds or float(tx) < bounds[0] or float(tx) > bounds[1]:
            continue
        if embed.embed_type == 'RECHARGE':
            terrain |= MXT_NAV_TERRAIN_BITS["recharge"]
            flags |= MXT_NAV_NODE_FLAGS["recharge"] | MXT_NAV_NODE_FLAGS["preferred"]
        elif embed.embed_type == 'DIRT':
            terrain |= MXT_NAV_TERRAIN_BITS["dirt"]
            flags |= MXT_NAV_NODE_FLAGS["dirt"] | MXT_NAV_NODE_FLAGS["avoid"]
        elif embed.embed_type == 'ICE':
            terrain |= MXT_NAV_TERRAIN_BITS["ice"]
            flags |= MXT_NAV_NODE_FLAGS["ice"] | MXT_NAV_NODE_FLAGS["avoid"]
        elif embed.embed_type == 'LAVA':
            terrain |= MXT_NAV_TERRAIN_BITS["lava"]
            flags |= MXT_NAV_NODE_FLAGS["lava"] | MXT_NAV_NODE_FLAGS["avoid"]

    for trig in triggers:
        if not _mxt_nav_trigger_contains(trig, world_pos):
            continue
        if trig.obj_type == 'DASHPLATE':
            terrain |= MXT_NAV_TERRAIN_BITS["dash"]
            flags |= MXT_NAV_NODE_FLAGS["dash"] | MXT_NAV_NODE_FLAGS["preferred"]
        elif trig.obj_type == 'JUMPPLATE':
            terrain |= MXT_NAV_TERRAIN_BITS["jump"]
            flags |= MXT_NAV_NODE_FLAGS["jump"]
        elif trig.obj_type == 'MINE':
            flags |= MXT_NAV_NODE_FLAGS["mine"] | MXT_NAV_NODE_FLAGS["avoid"]

    return int(terrain), int(flags)


def _mxt_nav_dash_approach_scores(world_pos, forward, dash_targets):
    p = np.asarray(world_pos, dtype=np.float64)
    fwd = _mxt_nav_normalized(np.asarray(forward, dtype=np.float64), (0.0, 0.0, 1.0))
    scores = {}
    for target in dash_targets:
        delta = np.asarray(target["position"], dtype=np.float64) - p
        forward_dist = float(np.dot(delta, fwd))
        if forward_dist < -1.0 or forward_dist > 90.0:
            continue
        lateral = delta - fwd * forward_dist
        lateral_dist = float(np.linalg.norm(lateral))
        alignment = max(0.0, 1.0 - lateral_dist / 28.0)
        lookahead = max(0.0, 1.0 - forward_dist / 90.0)
        score = alignment * alignment * (0.35 + 0.65 * lookahead) * 32.0
        score *= float(target.get("dash_chain_multiplier", 1.0))
        if score > 0.0:
            scores[target["id"]] = score
    return scores


def _mxt_nav_apply_dash_chain_multipliers(dash_targets):
    if not dash_targets:
        return
    ordered = sorted(dash_targets, key=lambda item: (int(item["segment"]), float(item["ty"])))
    for target in ordered:
        target["dash_chain_multiplier"] = 1.0
    max_forward_distance = 320.0
    max_lateral_distance = 90.0
    for i, target in enumerate(ordered):
        pos = np.asarray(target["position"], dtype=np.float64)
        forward = _mxt_nav_normalized(np.asarray(target.get("forward", (0.0, 0.0, 1.0)), dtype=np.float64), (0.0, 0.0, 1.0))
        for next_target in ordered[i + 1:i + 4]:
            next_pos = np.asarray(next_target["position"], dtype=np.float64)
            delta = next_pos - pos
            forward_distance = float(np.dot(delta, forward))
            if forward_distance <= 1.0 or forward_distance > max_forward_distance:
                continue
            lateral = delta - forward * forward_distance
            lateral_distance = float(np.linalg.norm(lateral))
            if lateral_distance > max_lateral_distance:
                continue
            closeness = max(0.0, 1.0 - forward_distance / max_forward_distance)
            alignment = max(0.0, 1.0 - lateral_distance / max_lateral_distance)
            bonus = 0.35 * closeness * alignment
            if bonus <= 0.0:
                continue
            target["dash_chain_multiplier"] = min(1.75, float(target["dash_chain_multiplier"]) + bonus)
            next_target["dash_chain_multiplier"] = min(1.75, float(next_target["dash_chain_multiplier"]) + bonus * 0.65)
    for target in ordered:
        target["dash_chain_multiplier"] = round(float(target["dash_chain_multiplier"]), 6)


def _mxt_nav_cost_for_node(node, route_kind):
    terrain = int(node.get("terrain", 0))
    flags = int(node.get("flags", 0))
    penalty = 0.0
    edge_clearance = max(0.0, float(node.get("edge_clearance", 1.0)))
    edge_comfort = 0.28
    if edge_clearance < edge_comfort:
        edge_t = (edge_comfort - edge_clearance) / edge_comfort
        edge_penalty = edge_t * edge_t * 70.0
        if route_kind == "safe":
            edge_penalty *= 1.65
        elif route_kind == "aggressive":
            edge_penalty *= 0.55
        elif route_kind == "dash":
            edge_penalty *= 0.75
        penalty += edge_penalty
    terrain_center_offset = max(0.0, min(1.0, float(node.get("terrain_center_offset", 0.0))))
    terrain_edge_clearance = max(0.0, float(node.get("terrain_edge_clearance", 1.0)))
    if terrain & MXT_NAV_TERRAIN_BITS["recharge"]:
        penalty += terrain_center_offset * terrain_center_offset * (95.0 if route_kind == "recharge" else 24.0 if route_kind == "aggressive" else 42.0)
        if terrain_edge_clearance < 0.12:
            terrain_edge_t = (0.12 - terrain_edge_clearance) / 0.12
            penalty += terrain_edge_t * terrain_edge_t * (75.0 if route_kind == "recharge" else 14.0 if route_kind == "aggressive" else 30.0)
    if terrain & MXT_NAV_TERRAIN_BITS["lava"]:
        penalty += 150.0 if route_kind == "aggressive" else 600.0
    if flags & MXT_NAV_NODE_FLAGS["mine"]:
        penalty += 120.0 if route_kind == "aggressive" else 450.0
    if terrain & MXT_NAV_TERRAIN_BITS["dirt"]:
        penalty += 0.0 if route_kind == "aggressive" else 180.0 if route_kind != "dash" else 35.0
    if terrain & MXT_NAV_TERRAIN_BITS["ice"]:
        penalty += 0.0 if route_kind == "aggressive" else 90.0
    if route_kind == "safe":
        penalty += float(node.get("downward_curve_risk", 0.0)) * 260.0
        if terrain & MXT_NAV_TERRAIN_BITS["dash"]:
            penalty += 80.0
        if terrain & MXT_NAV_TERRAIN_BITS["jump"]:
            penalty += 130.0
    elif route_kind == "default":
        penalty += float(node.get("downward_curve_risk", 0.0)) * 80.0
    elif route_kind == "aggressive":
        penalty += float(node.get("downward_curve_risk", 0.0)) * 30.0
    elif route_kind == "dash":
        if terrain & MXT_NAV_TERRAIN_BITS["dash"]:
            penalty -= 470.0 * max(1.0, float(node.get("dash_value_multiplier", 1.0)))
        if terrain & MXT_NAV_TERRAIN_BITS["jump"]:
            penalty -= 55.0
        dash_scores = node.get("dash_approach_scores", None)
        if isinstance(dash_scores, dict):
            penalty -= max((float(score) for score in dash_scores.values()), default=0.0)
        else:
            penalty -= float(node.get("dash_approach_score", 0.0))
    elif route_kind == "recharge":
        if terrain & MXT_NAV_TERRAIN_BITS["recharge"]:
            penalty -= 60.0
    return penalty


def _mxt_nav_edge_allows_route(nodes_by_id, edge):
    from_node = nodes_by_id.get(int(edge["from"]))
    to_node = nodes_by_id.get(int(edge["to"]))
    if from_node is None or to_node is None:
        return False

    from_seg = int(from_node.get("segment", 0))
    to_seg = int(to_node.get("segment", 0))
    flags = int(edge.get("flags", 0))

    if (flags & MXT_NAV_EDGE_FLAGS["lateral"]) != 0:
        return False
    if to_seg > from_seg:
        return True
    if to_seg == from_seg:
        from_pos = np.asarray(from_node.get("position", (0.0, 0.0, 0.0)), dtype=np.float64)
        to_pos = np.asarray(to_node.get("position", (0.0, 0.0, 0.0)), dtype=np.float64)
        forward = _mxt_nav_normalized(np.asarray(from_node.get("forward", (0.0, 0.0, 1.0)), dtype=np.float64), (0.0, 0.0, 1.0))
        return float(np.dot(to_pos - from_pos, forward)) >= -1.0
    return False


def _mxt_nav_route_edge_distance_cost(edge, from_node, to_node, route_kind):
    distance = float(edge["cost"])
    flags = int(edge.get("flags", 0))
    if (flags & (MXT_NAV_EDGE_FLAGS["transition"] | MXT_NAV_EDGE_FLAGS["branch"] | MXT_NAV_EDGE_FLAGS["glide_drop"])) != 0:
        return distance

    from_seg = int(from_node.get("segment", 0))
    to_seg = int(to_node.get("segment", 0))
    if from_seg != to_seg:
        return distance

    from_pos = np.asarray(from_node.get("position", (0.0, 0.0, 0.0)), dtype=np.float64)
    to_pos = np.asarray(to_node.get("position", (0.0, 0.0, 0.0)), dtype=np.float64)
    forward = _mxt_nav_normalized(np.asarray(from_node.get("forward", (0.0, 0.0, 1.0)), dtype=np.float64), (0.0, 0.0, 1.0))
    delta = to_pos - from_pos
    forward_world = max(1.0, float(np.dot(delta, forward)))
    lateral_vec = delta - forward * float(np.dot(delta, forward))
    lateral_world = float(np.linalg.norm(lateral_vec))
    lateral_rate = lateral_world / forward_world
    lateral_rate_scale = 0.10 if route_kind == "dash" else 0.30 if route_kind == "aggressive" else 0.18
    lateral_rate_penalty = lateral_rate * lateral_rate * distance * lateral_rate_scale
    if (flags & MXT_NAV_EDGE_FLAGS["lookahead"]) != 0:
        if lateral_rate < 0.55:
            lateral_rate_penalty *= 0.55
        elif lateral_rate > 1.35:
            lateral_rate_penalty *= 1.45
    return distance + lateral_rate_penalty


def _mxt_nav_route_outgoing(nodes, edges):
    nodes_by_id = {int(n["id"]): n for n in nodes}
    outgoing = {}
    for edge in edges:
        if not _mxt_nav_edge_allows_route(nodes_by_id, edge):
            continue
        outgoing.setdefault(int(edge["from"]), []).append(edge)
    return nodes_by_id, outgoing


def _mxt_nav_edge_cache_key(edge):
    return int(edge["from"]), int(edge["to"]), int(edge.get("flags", 0))


def _mxt_nav_route_cost_cache(nodes, edges, route_kind, nodes_by_id=None, route_outgoing=None):
    by_id = nodes_by_id or {int(n["id"]): n for n in nodes}
    outgoing = route_outgoing
    if outgoing is None:
        _by_id, outgoing = _mxt_nav_route_outgoing(nodes, edges)
    node_costs = {int(node["id"]): _mxt_nav_cost_for_node(node, route_kind) for node in nodes}
    edge_costs = {}
    for edge_list in outgoing.values():
        for edge in edge_list:
            from_node = by_id.get(int(edge["from"]))
            to_node = by_id.get(int(edge["to"]))
            edge_costs[_mxt_nav_edge_cache_key(edge)] = (
                _mxt_nav_route_edge_distance_cost(edge, from_node, to_node, route_kind)
                if from_node is not None and to_node is not None else float(edge["cost"])
            )
    return node_costs, edge_costs


def _mxt_nav_shortest_route(nodes, edges, start_node_id, finish_node_ids, route_kind,
                            extra_node_penalties=None, extra_edge_penalties=None,
                            nodes_by_id=None, route_outgoing=None, route_node_costs=None,
                            route_edge_costs=None):
    if start_node_id is None or not finish_node_ids:
        return []
    import heapq
    finish_set = set(int(n) for n in finish_node_ids)
    by_id = nodes_by_id or {int(n["id"]): n for n in nodes}
    extra_node_penalties = extra_node_penalties or {}
    extra_edge_penalties = extra_edge_penalties or {}
    outgoing = route_outgoing
    if outgoing is None:
        _by_id, outgoing = _mxt_nav_route_outgoing(nodes, edges)

    dist = {int(start_node_id): 0.0}
    prev = {}
    heap = [(0.0, int(start_node_id))]
    best_finish = None
    while heap:
        cur_dist, node_id = heapq.heappop(heap)
        if cur_dist != dist.get(node_id):
            continue
        if node_id in finish_set:
            best_finish = node_id
            break
        for edge in outgoing.get(node_id, []):
            to_id = int(edge["to"])
            to_node = by_id.get(to_id)
            if to_node is None:
                continue
            extra = float(route_node_costs.get(to_id, 0.0)) if route_node_costs is not None else _mxt_nav_cost_for_node(to_node, route_kind)
            edge_key = (int(edge["from"]), int(edge["to"]))
            extra += float(extra_node_penalties.get(to_id, 0.0))
            extra += float(extra_edge_penalties.get(edge_key, 0.0))
            if route_edge_costs is not None:
                edge_cost = float(route_edge_costs.get(_mxt_nav_edge_cache_key(edge), edge["cost"]))
            else:
                from_node = by_id.get(int(edge["from"]))
                edge_cost = _mxt_nav_route_edge_distance_cost(edge, from_node, to_node, route_kind) if from_node else float(edge["cost"])
            cost = max(0.001, edge_cost + extra)
            next_dist = cur_dist + cost
            if next_dist < dist.get(to_id, float("inf")):
                dist[to_id] = next_dist
                prev[to_id] = node_id
                heapq.heappush(heap, (next_dist, to_id))

    if best_finish is None:
        return []
    path = [best_finish]
    while path[-1] != start_node_id:
        parent = prev.get(path[-1])
        if parent is None:
            return []
        path.append(parent)
    path.reverse()
    return path


def _mxt_nav_path_length(nodes_by_id, path):
    length = 0.0
    for i in range(len(path) - 1):
        a = nodes_by_id.get(int(path[i]))
        b = nodes_by_id.get(int(path[i + 1]))
        if a is None or b is None:
            continue
        pa = np.asarray(a["position"], dtype=np.float64)
        pb = np.asarray(b["position"], dtype=np.float64)
        length += float(np.linalg.norm(pb - pa))
    return length


def _mxt_nav_prefix_anchor_index(nodes_by_id, path, prefix_meters):
    if len(path) < 3:
        return -1
    length = 0.0
    for i in range(1, len(path)):
        a = nodes_by_id.get(int(path[i - 1]))
        b = nodes_by_id.get(int(path[i]))
        if a is None or b is None:
            continue
        pa = np.asarray(a["position"], dtype=np.float64)
        pb = np.asarray(b["position"], dtype=np.float64)
        length += float(np.linalg.norm(pb - pa))
        if length >= prefix_meters:
            return i
    return min(len(path) - 2, max(1, len(path) // 4))


def _mxt_nav_rebuild_wrap_prefix(nodes, edges, path, route_kind, extra_node_penalties,
                                 extra_edge_penalties, nodes_by_id, route_outgoing,
                                 start_node_ids, prefix_meters,
                                 route_node_costs=None, route_edge_costs=None):
    if len(path) < 4 or not start_node_ids:
        return path
    anchor_index = _mxt_nav_prefix_anchor_index(nodes_by_id, path, prefix_meters)
    if anchor_index <= 1:
        return path
    finish_id = int(path[-1])
    anchor_id = int(path[anchor_index])
    start_set = set(int(node_id) for node_id in start_node_ids)
    wrap_candidates = []
    for edge in edges:
        if int(edge["from"]) == finish_id and int(edge["to"]) in start_set:
            wrap_candidates.append((int(edge["to"]), float(edge["cost"])))
    if not wrap_candidates:
        return path
    wrap_candidates.sort(key=lambda item: item[1])
    candidate_id, _wrap_cost = wrap_candidates[0]
    best_prefix = _mxt_nav_shortest_route(
        nodes,
        edges,
        candidate_id,
        [anchor_id],
        route_kind,
        extra_node_penalties,
        extra_edge_penalties,
        nodes_by_id,
        route_outgoing,
        route_node_costs,
        route_edge_costs,
    )
    if len(best_prefix) < 2 or int(best_prefix[-1]) != anchor_id:
        return path
    if not best_prefix:
        return path
    return best_prefix + path[anchor_index + 1:]


def _mxt_nav_route_alternatives(nodes, edges, start_node_id, finish_node_ids, route_kind, max_routes=4,
                                nodes_by_id=None, route_outgoing=None, seed_paths=None,
                                start_node_ids=None, wrap_prefix_meters=0.0):
    alternatives = []
    seen_paths = set()
    node_penalties = {}
    edge_penalties = {}
    by_id = nodes_by_id or {int(n["id"]): n for n in nodes}
    lane_targets = [0.28, 0.42, 0.58, 0.72] if route_kind in ("default", "safe") else []
    lane_fraction_by_node = {}
    lane_spread_weight_by_node = {}
    if lane_targets:
        row_groups = {}
        for node in nodes:
            key = (int(node.get("segment", 0)), round(float(node.get("ty", 0.0)), 5))
            row_groups.setdefault(key, []).append(node)
        row_centers_by_segment = {}
        for row_nodes in row_groups.values():
            if len(row_nodes) < 2:
                for node in row_nodes:
                    lane_fraction_by_node[int(node["id"])] = 0.5
                continue
            row_nodes.sort(key=lambda node: float(node.get("tx", 0.0)))
            cumulative = [0.0]
            total = 0.0
            for i in range(1, len(row_nodes)):
                pa = np.asarray(row_nodes[i - 1]["position"], dtype=np.float64)
                pb = np.asarray(row_nodes[i]["position"], dtype=np.float64)
                total += float(np.linalg.norm(pb - pa))
                cumulative.append(total)
            if total <= 1.0e-6:
                for node in row_nodes:
                    lane_fraction_by_node[int(node["id"])] = 0.5
                continue
            for node, dist in zip(row_nodes, cumulative):
                lane_fraction_by_node[int(node["id"])] = max(0.0, min(1.0, dist / total))
            segment_id = int(row_nodes[0].get("segment", 0))
            ty = round(float(row_nodes[0].get("ty", 0.0)), 5)
            left = np.asarray(row_nodes[0]["position"], dtype=np.float64)
            right = np.asarray(row_nodes[-1]["position"], dtype=np.float64)
            row_centers_by_segment.setdefault(segment_id, []).append((ty, (left + right) * 0.5, [int(node["id"]) for node in row_nodes]))
        for segment_rows in row_centers_by_segment.values():
            segment_rows.sort(key=lambda item: item[0])
            turn_amounts = [0.0 for _ in segment_rows]
            for row_index, (_ty, _center, node_ids) in enumerate(segment_rows):
                if 0 < row_index < len(segment_rows) - 1:
                    prev_center = segment_rows[row_index - 1][1]
                    center = segment_rows[row_index][1]
                    next_center = segment_rows[row_index + 1][1]
                    incoming = _mxt_nav_normalized(center - prev_center, (0.0, 0.0, 1.0))
                    outgoing = _mxt_nav_normalized(next_center - center, (0.0, 0.0, 1.0))
                    turn_amounts[row_index] = max(turn_amounts[row_index], math.acos(max(-1.0, min(1.0, float(np.dot(incoming, outgoing))))))
                if 1 < row_index < len(segment_rows) - 2:
                    prev_center = segment_rows[row_index - 2][1]
                    center = segment_rows[row_index][1]
                    next_center = segment_rows[row_index + 2][1]
                    incoming = _mxt_nav_normalized(center - prev_center, (0.0, 0.0, 1.0))
                    outgoing = _mxt_nav_normalized(next_center - center, (0.0, 0.0, 1.0))
                    turn_amounts[row_index] = max(turn_amounts[row_index], math.acos(max(-1.0, min(1.0, float(np.dot(incoming, outgoing))))))
            for row_index, (_ty, _center, node_ids) in enumerate(segment_rows):
                turn_angle = max(turn_amounts[max(0, row_index - 2):min(len(turn_amounts), row_index + 3)] or [0.0])
                spread_weight = 1.0
                if turn_angle > 0.025:
                    turn_t = min(1.0, (turn_angle - 0.025) / 0.11)
                    spread_weight = (1.0 - turn_t) ** 3
                for node_id in node_ids:
                    lane_spread_weight_by_node[node_id] = spread_weight

    def lane_target_penalties(route_index):
        if route_index >= len(lane_targets):
            return {}
        target = lane_targets[route_index]
        strength = 245.0 if route_kind == "safe" else 210.0
        penalties = {}
        for node_id, lane_fraction in lane_fraction_by_node.items():
            delta = abs(lane_fraction - target)
            penalties[node_id] = delta * delta * strength * lane_spread_weight_by_node.get(node_id, 1.0)
        return penalties

    def lane_change_edge_penalties(route_index):
        if route_index >= len(lane_targets):
            return {}
        penalties = {}
        strength = 950.0 if route_kind == "safe" else 820.0
        outgoing = route_outgoing or {}
        for from_id, edge_list in outgoing.items():
            from_fraction = lane_fraction_by_node.get(int(from_id))
            if from_fraction is None:
                continue
            for edge in edge_list:
                to_id = int(edge["to"])
                to_fraction = lane_fraction_by_node.get(to_id)
                if to_fraction is None:
                    continue
                lane_delta = abs(to_fraction - from_fraction)
                spread_weight = min(lane_spread_weight_by_node.get(int(from_id), 1.0), lane_spread_weight_by_node.get(to_id, 1.0))
                penalties[(int(from_id), to_id)] = lane_delta * lane_delta * strength * spread_weight
        return penalties

    corridor_penalty_radius = 28.0 if route_kind in ("default", "safe") else 0.0
    corridor_penalty_strength = 30.0 if route_kind == "safe" else 24.0
    corridor_spatial = None
    if corridor_penalty_radius > 0.0:
        cell_size = corridor_penalty_radius
        corridor_spatial = {}
        for node in nodes:
            p = node["position"]
            key = (
                int(math.floor(float(p[0]) / cell_size)),
                int(math.floor(float(p[1]) / cell_size)),
                int(math.floor(float(p[2]) / cell_size)),
            )
            corridor_spatial.setdefault(key, []).append(int(node["id"]))

        def add_corridor_penalties(path, scale=1.0):
            radius_sq = corridor_penalty_radius * corridor_penalty_radius
            for path_node_id in path[1:-1]:
                path_node = by_id.get(int(path_node_id))
                if path_node is None:
                    continue
                p = np.asarray(path_node["position"], dtype=np.float64)
                key = (
                    int(math.floor(float(p[0]) / cell_size)),
                    int(math.floor(float(p[1]) / cell_size)),
                    int(math.floor(float(p[2]) / cell_size)),
                )
                for dx in (-1, 0, 1):
                    for dy in (-1, 0, 1):
                        for dz in (-1, 0, 1):
                            for node_id in corridor_spatial.get((key[0] + dx, key[1] + dy, key[2] + dz), []):
                                if node_id == start_node_id or node_id in finish_node_ids:
                                    continue
                                node = by_id.get(node_id)
                                if node is None:
                                    continue
                                delta = np.asarray(node["position"], dtype=np.float64) - p
                                dist_sq = float(np.dot(delta, delta))
                                if dist_sq > radius_sq:
                                    continue
                                t = 1.0 - math.sqrt(dist_sq) / corridor_penalty_radius
                                node_penalties[node_id] = node_penalties.get(node_id, 0.0) + corridor_penalty_strength * scale * t * t
    else:
        def add_corridor_penalties(_path, scale=1.0):
            return

    for seed_path in seed_paths or []:
        add_corridor_penalties(seed_path, 0.35)

    route_node_costs, route_edge_costs = _mxt_nav_route_cost_cache(
        nodes, edges, route_kind, by_id, route_outgoing
    )

    for route_index in range(max_routes):
        route_node_penalties = node_penalties
        lane_penalties = lane_target_penalties(route_index)
        if lane_penalties:
            route_node_penalties = dict(node_penalties)
            for node_id, penalty in lane_penalties.items():
                route_node_penalties[node_id] = route_node_penalties.get(node_id, 0.0) + penalty
        route_edge_penalties = edge_penalties
        lane_edge_penalties = lane_change_edge_penalties(route_index)
        if lane_edge_penalties:
            route_edge_penalties = dict(edge_penalties)
            for edge_key, penalty in lane_edge_penalties.items():
                route_edge_penalties[edge_key] = route_edge_penalties.get(edge_key, 0.0) + penalty
        path = _mxt_nav_shortest_route(
            nodes,
            edges,
            start_node_id,
            finish_node_ids,
            route_kind,
            route_node_penalties,
            route_edge_penalties,
            nodes_by_id,
            route_outgoing,
            route_node_costs,
            route_edge_costs,
        )
        if wrap_prefix_meters > 0.0:
            path = _mxt_nav_rebuild_wrap_prefix(
                nodes,
                edges,
                path,
                route_kind,
                route_node_penalties,
                route_edge_penalties,
                by_id,
                route_outgoing,
                start_node_ids,
                wrap_prefix_meters,
                route_node_costs,
                route_edge_costs,
            )
        if len(path) < 2:
            break
        path_key = tuple(int(node_id) for node_id in path)
        if path_key in seen_paths:
            break
        seen_paths.add(path_key)
        alternatives.append(path)

        for node_id in path[1:-1]:
            node_penalties[int(node_id)] = node_penalties.get(int(node_id), 0.0) + 90.0
        for i in range(len(path) - 1):
            edge_key = (int(path[i]), int(path[i + 1]))
            edge_penalties[edge_key] = edge_penalties.get(edge_key, 0.0) + 35.0
        add_corridor_penalties(path, 0.35)

    return alternatives
