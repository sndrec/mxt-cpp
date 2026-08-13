"""CPU navigation reachability and runtime route construction."""

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
    MXT_NAV_NODE_FLAGS,
    MXT_NAV_TERRAIN_BITS,
)

from .shapes import (
    _sample_curve_matrix_numpy,
)

from .mesh import (
    MXTRoad_OT_GenerateMesh,
)

from .nav_data import (
    _mxt_nav_cost_for_node,
    _mxt_nav_json_vec3,
    _mxt_nav_normalized,
    _mxt_nav_route_edge_distance_cost,
    _mxt_nav_route_outgoing,
)

def _mxt_nav_reachable_trace(nodes, edges, start_node_id, route_kind, nodes_by_id=None, route_outgoing=None):
    if start_node_id is None:
        return []
    import heapq
    by_id = nodes_by_id or {int(n["id"]): n for n in nodes}
    outgoing = route_outgoing
    if outgoing is None:
        _by_id, outgoing = _mxt_nav_route_outgoing(nodes, edges)

    dist = {int(start_node_id): 0.0}
    prev = {}
    heap = [(0.0, int(start_node_id))]
    best_node = int(start_node_id)
    best_key = (-1.0, -1.0, -1.0)
    while heap:
        cur_dist, node_id = heapq.heappop(heap)
        if cur_dist != dist.get(node_id):
            continue
        node = by_id.get(node_id)
        if node is not None:
            key = (float(node.get("segment", 0)), float(node.get("ty", 0.0)), cur_dist)
            if key > best_key:
                best_key = key
                best_node = node_id
        for edge in outgoing.get(node_id, []):
            to_id = int(edge["to"])
            to_node = by_id.get(to_id)
            if to_node is None:
                continue
            extra = _mxt_nav_cost_for_node(to_node, route_kind)
            from_node = by_id.get(int(edge["from"]))
            edge_cost = _mxt_nav_route_edge_distance_cost(edge, from_node, to_node, route_kind) if from_node else float(edge["cost"])
            cost = max(0.001, edge_cost + extra)
            next_dist = cur_dist + cost
            if next_dist < dist.get(to_id, float("inf")):
                dist[to_id] = next_dist
                prev[to_id] = node_id
                heapq.heappush(heap, (next_dist, to_id))

    if best_node == start_node_id:
        return [int(start_node_id)]
    path = [best_node]
    while path[-1] != start_node_id:
        parent = prev.get(path[-1])
        if parent is None:
            return [int(start_node_id)]
        path.append(parent)
    path.reverse()
    return path


def _mxt_nav_option_preview(nodes, edges, first_edge, route_kind, max_steps=20,
                            nodes_by_id=None, route_outgoing=None):
    cost_route_kind = "dash" if route_kind == "boost_dash" else route_kind
    by_id = nodes_by_id or {int(n["id"]): n for n in nodes}
    outgoing = route_outgoing
    if outgoing is None:
        _by_id, outgoing = _mxt_nav_route_outgoing(nodes, edges)

    path = [int(first_edge["from"]), int(first_edge["to"])]
    cur = int(first_edge["to"])
    terrain_counts = {
        "dash": 0,
        "recharge": 0,
        "jump": 0,
        "dirt": 0,
        "ice": 0,
        "lava": 0,
        "mine": 0,
    }
    min_edge_clearance = float(by_id.get(cur, {}).get("edge_clearance", 1.0))
    first_from = by_id.get(int(first_edge["from"]))
    first_to = by_id.get(int(first_edge["to"]))
    first_cost = _mxt_nav_route_edge_distance_cost(first_edge, first_from, first_to, cost_route_kind) if first_from and first_to else float(first_edge["cost"])
    score = -float(first_cost)
    visited = {path[0]}

    for _ in range(max_steps):
        node = by_id.get(cur)
        if node is None:
            break
        terrain = int(node.get("terrain", 0))
        flags = int(node.get("flags", 0))
        if terrain & MXT_NAV_TERRAIN_BITS["dash"]:
            terrain_counts["dash"] += 1
            score += 220.0 if route_kind in ("dash", "boost_dash") else 35.0
        if terrain & MXT_NAV_TERRAIN_BITS["recharge"]:
            terrain_counts["recharge"] += 1
            score += 85.0 if route_kind == "recharge" else 35.0
        if terrain & MXT_NAV_TERRAIN_BITS["jump"]:
            terrain_counts["jump"] += 1
            score += 15.0 if route_kind in ("dash", "boost_dash") else -10.0
        if terrain & MXT_NAV_TERRAIN_BITS["dirt"]:
            terrain_counts["dirt"] += 1
            score -= 55.0 if route_kind != "dash" else 12.0
        if terrain & MXT_NAV_TERRAIN_BITS["ice"]:
            terrain_counts["ice"] += 1
            score -= 30.0
        if terrain & MXT_NAV_TERRAIN_BITS["lava"]:
            terrain_counts["lava"] += 1
            score -= 200.0
        if flags & MXT_NAV_NODE_FLAGS["mine"]:
            terrain_counts["mine"] += 1
            score -= 180.0

        edge_clearance = max(0.0, float(node.get("edge_clearance", 1.0)))
        min_edge_clearance = min(min_edge_clearance, edge_clearance)
        if edge_clearance < 0.28:
            score -= ((0.28 - edge_clearance) / 0.28) * 45.0

        candidates = [
            edge for edge in outgoing.get(cur, [])
            if (int(edge["flags"]) & MXT_NAV_EDGE_FLAGS["lateral"]) == 0 and int(edge["to"]) not in visited
        ]
        if not candidates:
            break
        candidates.sort(key=lambda edge: (
            _mxt_nav_route_edge_distance_cost(edge, by_id.get(int(edge["from"])), by_id.get(int(edge["to"])), cost_route_kind) +
            _mxt_nav_cost_for_node(by_id.get(int(edge["to"]), {}), cost_route_kind)
        ))
        next_edge = candidates[0]
        cur = int(next_edge["to"])
        visited.add(cur)
        path.append(cur)
        score -= _mxt_nav_route_edge_distance_cost(next_edge, by_id.get(int(next_edge["from"])), by_id.get(int(next_edge["to"])), cost_route_kind)

    return {
        "to": int(first_edge["to"]),
        "edge_flags": int(first_edge["flags"]),
        "cost": round(float(first_edge["cost"]), 6),
        "score": round(float(score), 6),
        "lookahead": terrain_counts,
        "min_edge_clearance": round(float(min_edge_clearance), 6),
        "preview": path,
    }


def _mxt_nav_choice_points(nodes, edges, route_alternatives, nodes_by_id=None, route_outgoing=None):
    by_id = nodes_by_id or {int(n["id"]): n for n in nodes}
    outgoing = route_outgoing
    if outgoing is None:
        _by_id, outgoing = _mxt_nav_route_outgoing(nodes, edges)

    choice_by_node = {}

    def add_choice(node_id, kind, route_name, to_ids):
        if len(to_ids) < 2:
            return
        node_id = int(node_id)
        entry = choice_by_node.get(node_id)
        if entry is None:
            entry = {
                "node": node_id,
                "kind": kind,
                "routes": [],
                "options": {},
            }
            choice_by_node[node_id] = entry
        if route_name and route_name not in entry["routes"]:
            entry["routes"].append(route_name)
        if kind == "branch":
            entry["kind"] = "branch"
        for to_id in sorted(set(int(v) for v in to_ids)):
            for edge in outgoing.get(node_id, []):
                if int(edge["to"]) == to_id:
                    entry["options"][to_id] = _mxt_nav_option_preview(
                        nodes, edges, edge, route_name or "default", nodes_by_id=by_id, route_outgoing=outgoing
                    )
                    break

    for route_name, alternatives in route_alternatives.items():
        if len(alternatives) < 2:
            continue
        max_len = max(len(path) for path in alternatives)
        for idx in range(max_len - 1):
            by_node = {}
            for path in alternatives:
                if idx + 1 >= len(path):
                    continue
                by_node.setdefault(int(path[idx]), set()).add(int(path[idx + 1]))
            for node_id, to_ids in by_node.items():
                add_choice(node_id, "route_divergence", route_name, to_ids)

    for node in nodes:
        node_id = int(node["id"])
        branch_edges = [
            edge for edge in outgoing.get(node_id, [])
            if (int(edge["flags"]) & (MXT_NAV_EDGE_FLAGS["branch"] | MXT_NAV_EDGE_FLAGS["glide_drop"])) != 0
        ]
        if len(branch_edges) >= 2:
            add_choice(node_id, "branch", "default", [int(edge["to"]) for edge in branch_edges])

    choices = []
    for idx, entry in enumerate(choice_by_node.values()):
        options = list(entry["options"].values())
        if len(options) < 2:
            continue
        choices.append({
            "id": idx,
            "node": int(entry["node"]),
            "kind": entry["kind"],
            "routes": sorted(entry["routes"]),
            "options": sorted(options, key=lambda option: option["to"]),
        })
    return choices


def _mxt_nav_runtime_route_anchor_nodes(nodes_by_id, path, route_kind, choice_node_ids=None):
    anchors = set()
    choice_node_ids = choice_node_ids or set()
    for idx, node_id in enumerate(path):
        node = nodes_by_id.get(int(node_id))
        if node is None:
            continue
        terrain = int(node.get("terrain", 0))
        flags = int(node.get("flags", 0))
        source = str(node.get("source", ""))
        is_feature = source.startswith("trigger:") or source.startswith("embed:") or source.startswith("mesh:")
        if route_kind in ("dash", "boost_dash") and is_feature and (terrain & MXT_NAV_TERRAIN_BITS["dash"]):
            anchors.add(idx)
        if is_feature and (terrain & MXT_NAV_TERRAIN_BITS["jump"] or flags & MXT_NAV_NODE_FLAGS["jump"]):
            anchors.add(idx)
    return anchors


def _mxt_nav_runtime_route_samples(nodes_by_id, path, route_kind, choice_node_ids=None,
                                   spacing_meters=10.0, iterations=6):
    if len(path) < 2:
        return []
    route_nodes = [nodes_by_id.get(int(node_id)) for node_id in path]
    if any(node is None for node in route_nodes):
        return []
    positions = [np.asarray(node["position"], dtype=np.float64) for node in route_nodes]
    collapsed_nodes = []
    collapsed_positions = []
    for node, pos in zip(route_nodes, positions):
        if collapsed_positions and float(np.linalg.norm(pos - collapsed_positions[-1])) < 0.05:
            continue
        collapsed_nodes.append(node)
        collapsed_positions.append(pos)
    route_nodes = collapsed_nodes
    positions = collapsed_positions
    if len(positions) < 2:
        return []
    cumulative = [0.0]
    for i in range(1, len(positions)):
        cumulative.append(cumulative[-1] + float(np.linalg.norm(positions[i] - positions[i - 1])))
    total = cumulative[-1]
    if total <= 1.0e-5:
        return []

    spacing_meters = max(3.0, float(spacing_meters))
    collapsed_path = [int(node["id"]) for node in route_nodes]
    source_anchors = _mxt_nav_runtime_route_anchor_nodes(nodes_by_id, collapsed_path, route_kind, choice_node_ids)
    closed_loop = float(np.linalg.norm(positions[-1] - positions[0])) <= max(1.0, spacing_meters * 1.5)
    targets = [(0.0, False), (total, False)]
    dist = spacing_meters
    while dist < total:
        targets.append((dist, False))
        dist += spacing_meters
    for anchor_index in source_anchors:
        if 0 <= anchor_index < len(cumulative):
            targets.append((float(cumulative[anchor_index]), True))
    targets.sort(key=lambda item: item[0])

    merged = []
    for dist, anchor in targets:
        dist = max(0.0, min(total, float(dist)))
        if merged and abs(dist - merged[-1][0]) < 0.75:
            merged[-1] = (dist if anchor else merged[-1][0], merged[-1][1] or anchor)
        else:
            merged.append((dist, anchor))

    def sample_at(distance):
        if distance <= 0.0:
            return positions[0].copy(), 0
        if distance >= total:
            return positions[-1].copy(), len(positions) - 1
        hi = 1
        while hi < len(cumulative) and cumulative[hi] < distance:
            hi += 1
        lo = max(0, hi - 1)
        span = max(1.0e-6, cumulative[hi] - cumulative[lo])
        t = (distance - cumulative[lo]) / span
        pos = positions[lo] * (1.0 - t) + positions[hi] * t
        nearest = lo if t < 0.5 else hi
        return pos, nearest

    sample_positions = []
    sample_nearest_indices = []
    locked = []
    for dist, anchor in merged:
        pos, nearest_index = sample_at(dist)
        sample_positions.append(pos)
        sample_nearest_indices.append(nearest_index)
        locked.append(bool(anchor))

    if len(sample_positions) > 3:
        for _ in range(max(0, int(iterations))):
            next_positions = [p.copy() for p in sample_positions]
            start_i = 0 if closed_loop else 1
            end_i = len(sample_positions) if closed_loop else len(sample_positions) - 1
            for i in range(start_i, end_i):
                if locked[i]:
                    continue
                prev_i = (i - 1) % len(sample_positions)
                next_i = (i + 1) % len(sample_positions)
                if not closed_loop and (prev_i < 0 or next_i >= len(sample_positions)):
                    continue
                midpoint = (sample_positions[prev_i] + sample_positions[next_i]) * 0.5
                candidate = sample_positions[i] + (midpoint - sample_positions[i]) * 0.42
                next_positions[i] = candidate
            sample_positions = next_positions

    result = []
    for i, pos in enumerate(sample_positions):
        nearest_index = min(max(0, int(sample_nearest_indices[i])), len(route_nodes) - 1)
        node = route_nodes[nearest_index]
        if 0 < i < len(sample_positions) - 1:
            forward = _mxt_nav_normalized(sample_positions[i + 1] - sample_positions[i - 1], node.get("forward", [0.0, 0.0, 1.0]))
        elif i + 1 < len(sample_positions):
            forward = _mxt_nav_normalized(sample_positions[i + 1] - sample_positions[i], node.get("forward", [0.0, 0.0, 1.0]))
        else:
            forward = _mxt_nav_normalized(sample_positions[i] - sample_positions[i - 1], node.get("forward", [0.0, 0.0, 1.0]))
        result.append({
            "node": int(node["id"]),
            "segment": int(node.get("segment", 0)),
            "checkpoint": int(node.get("checkpoint", 0)),
            "position": _mxt_nav_json_vec3(pos),
            "normal": node.get("normal", [0.0, 1.0, 0.0]),
            "forward": _mxt_nav_json_vec3(forward),
            "terrain": int(node.get("terrain", 0)),
            "edge_clearance": round(float(node.get("edge_clearance", 1.0)), 6),
            "downward_curve_risk": round(float(node.get("downward_curve_risk", 0.0)), 6),
            "anchor": bool(locked[i]),
        })
    return result


def _mxt_nav_runtime_routes(nodes_by_id, routes, route_alternatives, route_kind_map, choice_points,
                            spacing_meters=14.0, iterations=4):
    choice_node_ids = {int(choice.get("node", -1)) for choice in choice_points}
    relaxed_cache = {}
    runtime_routes = {}
    runtime_alternatives = {}
    for route_name, alternatives in route_alternatives.items():
        kind = route_kind_map.get(route_name, route_name)
        relaxed_alts = []
        for path in alternatives:
            path_key = tuple(int(node_id) for node_id in path)
            cache_key = (kind, path_key)
            relaxed = relaxed_cache.get(cache_key)
            if relaxed is None:
                relaxed = _mxt_nav_runtime_route_samples(
                    nodes_by_id, path, kind, choice_node_ids, spacing_meters, iterations
                )
                relaxed_cache[cache_key] = relaxed
            if relaxed:
                relaxed_alts.append(relaxed)
        runtime_alternatives[route_name] = relaxed_alts
        primary = runtime_alternatives[route_name][0] if runtime_alternatives[route_name] else []
        if not primary and routes.get(route_name):
            primary = _mxt_nav_runtime_route_samples(
                nodes_by_id, routes[route_name], kind, choice_node_ids, spacing_meters, iterations
            )
        runtime_routes[route_name] = primary
    return runtime_routes, runtime_alternatives


def _mxt_nav_ty_samples_by_spacing(helper, spacing_meters):
    spacing_meters = max(1.0, float(spacing_meters))
    probe_t = np.linspace(0.0, 1.0, 129, dtype=np.float64)
    centerline_pos, _centerline_basis, _centerline_scl = _sample_curve_matrix_numpy(helper, probe_t)
    distances = np.zeros(len(probe_t), dtype=np.float64)
    for i in range(1, len(probe_t)):
        distances[i] = distances[i - 1] + float(np.linalg.norm(centerline_pos[i] - centerline_pos[i - 1]))

    total = float(distances[-1])
    if total <= 1.0e-6:
        return np.array([0.0, 1.0], dtype=np.float64)

    target_count = int(math.floor(total / spacing_meters))
    targets = [0.0]
    for i in range(1, target_count + 1):
        d = float(i) * spacing_meters
        if d < total - 1.0e-6:
            targets.append(d)
    targets.append(total)

    ty_values = []
    sample_idx = 0
    for target in targets:
        while sample_idx + 1 < len(distances) and distances[sample_idx + 1] < target:
            sample_idx += 1
        if target <= 0.0:
            ty_values.append(0.0)
            continue
        if target >= total:
            ty_values.append(1.0)
            continue
        d0 = distances[sample_idx]
        d1 = distances[min(sample_idx + 1, len(distances) - 1)]
        t0 = probe_t[sample_idx]
        t1 = probe_t[min(sample_idx + 1, len(probe_t) - 1)]
        alpha = 0.0 if d1 <= d0 else (target - d0) / (d1 - d0)
        ty_values.append(float(t0 + (t1 - t0) * alpha))

    return np.array(MXTRoad_OT_GenerateMesh._unique_sorted_ty(ty_values), dtype=np.float64)


def _mxt_nav_unique_sorted_tx(values):
    values = sorted(max(-1.0, min(1.0, float(v))) for v in values)
    out = []
    for v in values:
        if not out or abs(v - out[-1]) > 1.0e-7:
            out.append(v)
        else:
            out[-1] = v
    return out if out else [0.0]


def _mxt_nav_segment_ty_samples(helper, props, triggers, spacing_meters):
    samples = list(_mxt_nav_ty_samples_by_spacing(helper, spacing_meters))
    for embed in getattr(props, "embeds", []):
        if embed.embed_type != 'HOLE':
            continue
        start_t = max(0.0, min(1.0, float(embed.start_t)))
        end_t = max(0.0, min(1.0, float(embed.end_t)))
        if end_t < start_t:
            start_t, end_t = end_t, start_t
        samples.append(start_t)
        samples.append(end_t)
        if embed.helper and embed.helper.animation_data and embed.helper.animation_data.action:
            act = embed.helper.animation_data.action
            for fcu in (act.fcurves.find("location", index=1), act.fcurves.find("location", index=2)):
                if not fcu:
                    continue
                for kfp in fcu.keyframe_points:
                    t = max(start_t, min(end_t, float(kfp.co.x) / 100.0))
                    samples.append(t)
                    eps = 0.0005
                    if start_t < t < end_t:
                        samples.append(max(start_t, t - eps))
                        samples.append(min(end_t, t + eps))
    return np.array(MXTRoad_OT_GenerateMesh._unique_sorted_ty(samples), dtype=np.float64)


def _mxt_nav_embed_ty_samples(embed, base_ty_1d, include_epsilon=False):
    start_t = max(0.0, min(1.0, float(embed.start_t)))
    end_t = max(0.0, min(1.0, float(embed.end_t)))
    if end_t < start_t:
        start_t, end_t = end_t, start_t
    samples = [start_t, end_t]
    for ty in base_ty_1d:
        ty = float(ty)
        if start_t <= ty <= end_t:
            samples.append(ty)
    if embed.helper and embed.helper.animation_data and embed.helper.animation_data.action:
        act = embed.helper.animation_data.action
        for fcu in (act.fcurves.find("location", index=1), act.fcurves.find("location", index=2)):
            if not fcu:
                continue
            for kfp in fcu.keyframe_points:
                t = max(start_t, min(end_t, float(kfp.co.x) / 100.0))
                samples.append(t)
                if include_epsilon and start_t < t < end_t:
                    eps = 0.0005
                    samples.append(max(start_t, t - eps))
                    samples.append(min(end_t, t + eps))
    return np.array(MXTRoad_OT_GenerateMesh._unique_sorted_ty(samples), dtype=np.float64)


def _mxt_nav_segment_tx_samples(base_tx_1d, props, triggers, ty_1d):
    shape_type = getattr(props, "road_shape_type", "FLAT")
    count = max(3, len(base_tx_1d))
    if shape_type in ('CYLINDER', 'PIPE', 'ROUNDED_SQUARE'):
        return np.linspace(-1.0, 1.0, count, endpoint=False, dtype=np.float64)
    if shape_type in ('CYLINDER_OPEN', 'PIPE_OPEN', 'ROUNDED_SQUARE_OPEN'):
        return np.linspace(-1.0, 1.0, count, endpoint=True, dtype=np.float64)
    return np.array(_mxt_nav_unique_sorted_tx(float(v) for v in base_tx_1d), dtype=np.float64)


def _mxt_nav_open_values_for_rows(props, ty_1d):
    if getattr(props, "road_shape_type", "FLAT") not in ('CYLINDER_OPEN', 'PIPE_OPEN', 'ROUNDED_SQUARE_OPEN'):
        return np.ones(len(ty_1d), dtype=np.float64)
    values = np.ones(len(ty_1d), dtype=np.float64)
    helper = getattr(props, "openness_helper", None)
    if helper and helper.animation_data and helper.animation_data.action:
        fcu = helper.animation_data.action.fcurves.find("location", index=0)
        if fcu:
            values = np.array([fcu.evaluate(float(ty) * 100.0) for ty in ty_1d], dtype=np.float64)
    return values
