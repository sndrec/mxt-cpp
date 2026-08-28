#!/usr/bin/env python3
"""Create an MXT track-editor .blend and .mxt_track from GX sampler JSON.

Run through Blender:
  blender --background --python tools/track/fzgx/gx_samples_to_mxt_blend.py -- samples.json out.blend --export out.mxt_track
"""

from __future__ import annotations

import argparse
import copy
import importlib.util
import json
import math
from pathlib import Path

import bpy
from mathutils import Matrix, Quaternion, Vector


DEFAULT_PLUGIN = Path(__file__).resolve().parents[3] / "track-editor-blender-plugin" / "mxt_track_editor"


def parse_args() -> argparse.Namespace:
    import sys

    args = sys.argv
    if "--" in args:
        args = args[args.index("--") + 1 :]
    else:
        args = []
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("samples_json", type=Path)
    parser.add_argument("blend_out", type=Path)
    parser.add_argument("--export", type=Path, default=None, help="Optional .mxt_track output path")
    parser.add_argument("--plugin", type=Path, default=DEFAULT_PLUGIN)
    parser.add_argument("--track-name", default=None)
    parser.add_argument("--max-samples-per-segment", type=int, default=0)
    parser.add_argument("--checkpoint-stride", type=int, default=8)
    return parser.parse_args(args)


def load_plugin(path: Path):
    package_path = path.resolve()
    init_path = package_path / "__init__.py"
    spec = importlib.util.spec_from_file_location(
        "mxt_track_editor",
        str(init_path),
        submodule_search_locations=[str(package_path)],
    )
    if spec is None or spec.loader is None:
        raise RuntimeError(f"could not load plugin package from {package_path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    module.register()
    return module


def clear_scene() -> None:
    for obj in list(bpy.data.objects):
        bpy.data.objects.remove(obj, do_unlink=True)
    for collection in (bpy.data.meshes, bpy.data.curves, bpy.data.actions, bpy.data.materials):
        for item in list(collection):
            if item.users == 0:
                collection.remove(item)


def ensure_material(name: str, color: tuple[float, float, float, float]) -> None:
    mat = bpy.data.materials.get(name)
    if mat is None:
        mat = bpy.data.materials.new(name)
    mat.diffuse_color = color


def ensure_preview_materials() -> None:
    ensure_material("track_surface", (0.42, 0.42, 0.44, 1.0))
    ensure_material("track_rail", (0.78, 0.78, 0.82, 1.0))
    ensure_material("embed_border", (0.03, 0.03, 0.035, 1.0))
    ensure_material("embed_ice", (0.35, 0.75, 1.0, 1.0))
    ensure_material("embed_recharge", (0.05, 0.95, 0.35, 1.0))
    ensure_material("embed_dirt", (0.58, 0.36, 0.16, 1.0))
    ensure_material("embed_lava", (1.0, 0.16, 0.04, 1.0))
    ensure_material("embed_hole", (0.0, 0.0, 0.0, 1.0))
    ensure_material("gxmesh_track", (0.35, 0.75, 0.42, 0.36))
    ensure_material("gxmesh_wall", (0.95, 0.88, 0.35, 0.58))
    ensure_material("gxmesh_dash", (0.05, 0.45, 1.0, 0.62))
    ensure_material("gxmesh_jump", (1.0, 0.9, 0.05, 0.62))
    ensure_material("gxmesh_ice", (0.35, 0.85, 1.0, 0.52))
    ensure_material("gxmesh_dirt", (0.62, 0.35, 0.12, 0.58))
    ensure_material("gxmesh_damage", (1.0, 0.12, 0.05, 0.62))
    ensure_material("gxmesh_death", (0.08, 0.02, 0.02, 0.7))
    ensure_material("gxmesh_mine", (1.0, 0.08, 0.9, 0.7))
    ensure_material("gxmesh_other", (0.9, 0.9, 0.9, 0.38))


def mesh_collision_material_name(entry: dict) -> str:
    source = entry.get("source")
    collider_type = int(entry.get("collider_type", 0))
    surface = str(entry.get("surface", ""))
    if source == "dynamic_scene" and (collider_type & 0x00004000):
        return "gxmesh_mine"
    if surface in {"driveable", "recover"}:
        return "gxmesh_track"
    if surface == "wall":
        return "gxmesh_wall"
    if surface == "dash":
        return "gxmesh_dash"
    if surface == "jump":
        return "gxmesh_jump"
    if surface == "ice":
        return "gxmesh_ice"
    if surface == "dirt":
        return "gxmesh_dirt"
    if surface in {"damage", "lava"}:
        return "gxmesh_damage"
    if surface.startswith("death") or surface == "out_of_bounds":
        return "gxmesh_death"
    return "gxmesh_other"


def add_mesh_collision_helpers(data: dict) -> None:
    entries = data.get("mesh_collision") or []
    if not entries:
        return

    collection = bpy.data.collections.new("GX Mesh Collision Helpers")
    bpy.context.scene.collection.children.link(collection)

    for index, entry in enumerate(entries):
        verts = []
        faces = []
        for tri in entry.get("tris", []):
            if len(tri) != 3:
                continue
            base = len(verts)
            verts.extend((tuple(vec(v)) for v in tri))
            faces.append((base, base + 1, base + 2))
        for quad in entry.get("quads", []):
            if len(quad) != 4:
                continue
            base = len(verts)
            verts.extend((tuple(vec(v)) for v in quad))
            faces.append((base, base + 1, base + 2, base + 3))
        if not faces:
            continue

        name = f"GXMesh.{index:03d}.{entry.get('name', 'collision')}"
        mesh = bpy.data.meshes.new(f"{name}.mesh")
        mesh.from_pydata(verts, [], faces)
        mesh.update()
        obj = bpy.data.objects.new(name, mesh)
        obj.display_type = "WIRE"
        obj.show_wire = True
        obj.show_in_front = True
        obj["gx_source"] = entry.get("source", "")
        obj["gx_surface"] = entry.get("surface", "")
        surface_index = int(entry.get("surface_index", -1))
        collider_type = int(entry.get("collider_type", 0))
        obj["gx_surface_index"] = surface_index if surface_index <= 0x7fffffff else -1
        obj["gx_surface_index_raw"] = str(surface_index)
        obj["gx_collider_type"] = collider_type if collider_type <= 0x7fffffff else -1
        obj["gx_collider_type_raw"] = str(collider_type)

        mat = bpy.data.materials.get(mesh_collision_material_name(entry))
        if mat:
            mesh.materials.append(mat)
            for poly in mesh.polygons:
                poly.material_index = 0
        collection.objects.link(obj)


def clamp01(value: float) -> float:
    return max(0.0, min(1.0, float(value)))


def clamp_tx(value: float) -> float:
    return max(-1.0, min(1.0, float(value)))


def checkpoint_basis(flat) -> Matrix:
    return Matrix((
        Vector(flat[0:3]),
        Vector(flat[3:6]),
        Vector(flat[6:9]),
    )).transposed()


def closest_t_on_segment(point: Vector, a: Vector, b: Vector) -> float:
    delta = b - a
    denom = delta.length_squared
    if denom <= 1.0e-8:
        return 0.0
    return (point - a).dot(delta) / denom


def segment_shape(plugin, segment):
    props = segment.mxt_road_overall_props
    return {
        "FLAT": plugin.RoadShapeFlat(),
        "CYLINDER": plugin.RoadShapeCylinder(),
        "PIPE": plugin.RoadShapePipe(),
        "CYLINDER_OPEN": plugin.RoadShapeCylinderOpen(),
        "PIPE_OPEN": plugin.RoadShapePipeOpen(),
        "ROUNDED_SQUARE": plugin.RoadShapeRoundedSquare(),
        "ROUNDED_SQUARE_OPEN": plugin.RoadShapeRoundedSquareOpen(),
        "TUNNEL": plugin.RoadShapeFlat(),
    }[props.road_shape_type]


def checkpoint_seed_tys(segment, point: Vector) -> list[float]:
    seeds = []
    props = segment.mxt_road_overall_props
    for cp in props.checkpoints:
        p0 = Vector(cp.pos_start)
        p1 = Vector(cp.pos_end)
        b0 = checkpoint_basis(cp.basis_start)
        b1 = checkpoint_basis(cp.basis_end)
        n0 = b0.col[2].normalized()
        n1 = b1.col[2].normalized()
        q0 = point - n0 * (n0.dot(point) - n0.dot(p0))
        q1 = point - n1 * (n1.dot(point) - n1.dot(p1))
        u_raw = closest_t_on_segment(point, q0, q1)
        if u_raw < -0.05 or u_raw > 1.05:
            continue
        u = clamp01(u_raw)

        center = p0.lerp(p1, u)
        right = b0.col[0].lerp(b1.col[0], u)
        up = b0.col[1].lerp(b1.col[1], u)
        x_radius = max(1.0, cp.x_rad_start + (cp.x_rad_end - cp.x_rad_start) * u)
        y_radius = max(1.0, cp.y_rad_start + (cp.y_rad_end - cp.y_rad_start) * u)
        delta = point - center
        if abs(right.normalized().dot(delta)) > x_radius * 1.15:
            continue
        if abs(up.normalized().dot(delta)) > max(20.0, y_radius * 1.15):
            continue
        seeds.append(clamp01(cp.start_t + (cp.end_t - cp.start_t) * u))
    return seeds


def rough_segment_seed(point: Vector, group: list[dict]) -> tuple[float, float]:
    best_tx = 0.0
    best_ty = 0.0
    best_score = float("inf")
    if not group:
        return best_tx, best_ty
    if len(group) == 1:
        center, right, up, _forward, half_width = sample_frame(group, 0, False)
        delta = point - center
        return clamp_tx(right.dot(delta) / max(1.0, half_width)), 0.0

    frames = [sample_frame(group, i, False) for i in range(len(group))]
    for i in range(len(group) - 1):
        c0, right0, up0, _forward0, half_width0 = frames[i]
        c1, right1, up1, _forward1, half_width1 = frames[i + 1]
        center_delta = c1 - c0
        u = clamp01(closest_t_on_segment(point, c0, c1))
        center = c0.lerp(c1, u)
        right = safe_normal(right0.lerp(right1, u), right0)
        up = safe_normal(up0.lerp(up1, u), up0)
        half_width = half_width0 + (half_width1 - half_width0) * u
        delta = point - center
        tx = right.dot(delta) / max(1.0, half_width)
        score = (center - point).length_squared + up.dot(delta) * up.dot(delta)
        if score < best_score:
            ty0 = sample_group_t(group, i)
            ty1 = sample_group_t(group, i + 1)
            best_score = score
            best_tx = clamp_tx(tx)
            best_ty = clamp01(ty0 + (ty1 - ty0) * u)
    return best_tx, best_ty


def surface_frame(shape, helper, tx: float, ty: float) -> tuple[Vector, Vector, Vector, Vector]:
    tx = clamp_tx(tx)
    ty = clamp01(ty)
    base = shape.get_pos(helper, Vector((tx, ty)))
    eps_x = 0.002
    eps_y = 0.002
    right_pos = shape.get_pos(helper, Vector((clamp_tx(tx + eps_x), ty)))
    left_pos = shape.get_pos(helper, Vector((clamp_tx(tx - eps_x), ty)))
    fwd_pos = shape.get_pos(helper, Vector((tx, clamp01(ty + eps_y))))
    back_pos = shape.get_pos(helper, Vector((tx, clamp01(ty - eps_y))))
    right = safe_normal(right_pos - left_pos, Vector((1.0, 0.0, 0.0)))
    forward = safe_normal(fwd_pos - back_pos, Vector((0.0, 0.0, 1.0)))
    normal = safe_normal(right.cross(forward), Vector((0.0, 1.0, 0.0)))
    right = safe_normal(forward.cross(normal), right)
    return base, right, normal, forward


def refine_surface_point(shape, helper, point: Vector, seed_tx: float, seed_ty: float) -> dict | None:
    tx = clamp_tx(seed_tx)
    ty = clamp01(seed_ty)

    def eval_score(x, y):
        pos = shape.get_pos(helper, Vector((clamp_tx(x), clamp01(y))))
        if pos is None:
            return float("inf")
        return (pos - point).length_squared

    best_score = eval_score(tx, ty)
    step_tx = 0.35
    step_ty = 0.035
    for _ in range(18):
        improved = False
        best_candidate = (tx, ty, best_score)
        for dx, dy in (
            (-step_tx, 0.0), (step_tx, 0.0),
            (0.0, -step_ty), (0.0, step_ty),
            (-step_tx, -step_ty), (-step_tx, step_ty),
            (step_tx, -step_ty), (step_tx, step_ty),
        ):
            cx = clamp_tx(tx + dx)
            cy = clamp01(ty + dy)
            score = eval_score(cx, cy)
            if score < best_candidate[2]:
                best_candidate = (cx, cy, score)
                improved = True
        tx, ty, best_score = best_candidate
        if not improved:
            step_tx *= 0.5
            step_ty *= 0.5
            if step_tx < 1.0e-4 and step_ty < 1.0e-5:
                break

    pos, right, normal, forward = surface_frame(shape, helper, tx, ty)
    vertical = abs((point - pos).dot(normal))
    return {
        "tx": tx,
        "ty": ty,
        "score": best_score,
        "vertical": vertical,
        "right": right,
        "up": normal,
        "forward": forward,
        "surface_pos": pos,
    }


def closest_segment_point(
    point: Vector,
    stream_segments: list[list[tuple[list[dict], object]]],
    plugin,
) -> dict | None:
    best = None
    best_score = float("inf")

    candidate_rows = []
    for segment_row in stream_segments:
        for group, segment in segment_row:
            seeds = checkpoint_seed_tys(segment, point)
            if seeds:
                candidate_rows.append((group, segment, seeds))

    if not candidate_rows:
        for segment_row in stream_segments:
            for group, segment in segment_row:
                _seed_tx, seed_ty = rough_segment_seed(point, group)
                candidate_rows.append((group, segment, [seed_ty]))

    for group, segment, seeds in candidate_rows:
        props = segment.mxt_road_overall_props
        helper = props.curve_matrix_helper_empty
        if helper is None:
            continue
        shape = segment_shape(plugin, segment)
        rough_tx, _rough_ty = rough_segment_seed(point, group)
        for seed_ty in seeds:
            for seed_tx in (rough_tx, -0.75, -0.25, 0.25, 0.75):
                refined = refine_surface_point(shape, helper, point, seed_tx, seed_ty)
                if refined is None:
                    continue
                candidate = {
                    "segment": segment,
                    **refined,
                }
                raw_score = refined["score"] + refined["vertical"] * refined["vertical"] * 4.0
                if refined["vertical"] > 2.0:
                    continue
                if refined["tx"] < -1.0 or refined["tx"] > 1.0:
                    continue
                if raw_score < best_score:
                    best_score = raw_score
                    best = candidate

    return best


def fit_dashplate_yaw_and_scale(
    verts: list[Vector],
    center: Vector,
    placement: dict,
    base_extents: Vector = Vector((6.0, 4.0, 12.0)),
) -> tuple[float, tuple[float, float, float]]:
    right = placement["right"]
    up = placement["up"]
    forward = placement["forward"]

    points = []
    for vert in verts:
        rel = vert - center
        points.append((right.dot(rel), forward.dot(rel), up.dot(rel)))

    xx = 0.0
    zz = 0.0
    xz = 0.0
    for x, z, _y in points:
        xx += x * x
        zz += z * z
        xz += x * z

    angle = 0.5 * math.atan2(2.0 * xz, xx - zz)
    axis_x = math.cos(angle)
    axis_z = math.sin(angle)

    # The PCA formula above gives an undirected axis. Select the road-forward
    # half-space to keep asymmetric preview meshes aligned with the road.
    if axis_z < 0.0:
        axis_x = -axis_x
        axis_z = -axis_z

    def score_yaw(yaw: float) -> tuple[float, float, float]:
        f_x = math.sin(yaw)
        f_z = math.cos(yaw)
        r_x = f_z
        r_z = -f_x
        x_extent = 0.0
        z_extent = 0.0
        projected = []
        for x, z, _y in points:
            px = x * r_x + z * r_z
            pz = x * f_x + z * f_z
            projected.append((px, pz))
            x_extent = max(x_extent, abs(px))
            z_extent = max(z_extent, abs(pz))

        error = 0.0
        for px, pz in projected:
            dx = abs(px) - x_extent
            dz = abs(pz) - z_extent
            error += dx * dx + dz * dz
        return error, x_extent, z_extent

    yaw = math.atan2(axis_x, axis_z)
    best_yaw = yaw
    best_error, best_x_extent, best_z_extent = score_yaw(best_yaw)
    step = math.radians(20.0)
    for _ in range(24):
        improved = False
        for candidate_yaw in (best_yaw - step, best_yaw + step):
            if math.cos(candidate_yaw) < 0.0:
                continue
            error, x_extent, z_extent = score_yaw(candidate_yaw)
            if error < best_error:
                best_yaw = candidate_yaw
                best_error = error
                best_x_extent = x_extent
                best_z_extent = z_extent
                improved = True
        if not improved:
            step *= 0.5
            if step < math.radians(0.01):
                break

    y_extent = 0.0
    for _x, _z, y in points:
        y_extent = max(y_extent, abs(y))

    yaw_deg = math.degrees(best_yaw)
    scale = (
        max(0.05, best_x_extent / base_extents.x),
        max(0.25, y_extent / base_extents.y),
        max(0.05, best_z_extent / base_extents.z),
    )
    return yaw_deg, scale


def transform_vec(transform: dict | None, key: str) -> Vector | None:
    if not transform:
        return None
    values = transform.get(key)
    if not values:
        return None
    return vec(values)


def yaw_from_world_axis(axis: Vector | None, placement: dict) -> float | None:
    if axis is None or axis.length_squared <= 1.0e-8:
        return None
    right = placement["right"]
    forward = placement["forward"]
    axis_x = right.dot(axis)
    axis_z = forward.dot(axis)
    length_sq = axis_x * axis_x + axis_z * axis_z
    if length_sq <= 1.0e-8:
        return None
    inv_len = 1.0 / math.sqrt(length_sq)
    axis_x *= inv_len
    axis_z *= inv_len
    if axis_z < 0.0:
        axis_x = -axis_x
        axis_z = -axis_z
    return math.atan2(axis_x, axis_z)


def scale_for_yaw(
    verts: list[Vector],
    center: Vector,
    placement: dict,
    yaw: float,
    base_extents: Vector,
    min_scale: Vector,
) -> tuple[float, float, float]:
    right = placement["right"]
    up = placement["up"]
    forward = placement["forward"]
    f_x = math.sin(yaw)
    f_z = math.cos(yaw)
    r_x = f_z
    r_z = -f_x
    x_extent = 0.0
    y_extent = 0.0
    z_extent = 0.0
    for vert in verts:
        rel = vert - center
        x = right.dot(rel)
        z = forward.dot(rel)
        y = up.dot(rel)
        x_extent = max(x_extent, abs(x * r_x + z * r_z))
        z_extent = max(z_extent, abs(x * f_x + z * f_z))
        y_extent = max(y_extent, abs(y))
    return (
        max(min_scale.x, x_extent / base_extents.x),
        max(min_scale.y, y_extent / base_extents.y),
        max(min_scale.z, z_extent / base_extents.z),
    )


def fit_mine_yaw_and_scale(
    verts: list[Vector],
    center: Vector,
    placement: dict,
    object_transform: dict | None,
) -> tuple[float, tuple[float, float, float]]:
    base_extents = Vector((2.0, 3.0, 2.0))
    yaw = yaw_from_world_axis(transform_vec(object_transform, "basis_y"), placement)
    if yaw is None:
        yaw_deg, _scale = fit_dashplate_yaw_and_scale(verts, center, placement, base_extents)
        return yaw_deg, (1.0, 1.0, 1.0)
    return (
        math.degrees(yaw),
        (1.0, 1.0, 1.0),
    )


def fit_jumpplate_yaw_and_scale(
    verts: list[Vector],
    center: Vector,
    placement: dict,
) -> tuple[float, tuple[float, float, float]]:
    right = placement["right"]
    up = placement["up"]
    forward = placement["forward"]
    base_extents = Vector((12.0, 4.0, 4.0))

    points = []
    for vert in verts:
        rel = vert - center
        points.append((right.dot(rel), forward.dot(rel), up.dot(rel)))

    xx = 0.0
    zz = 0.0
    xz = 0.0
    for x, z, _y in points:
        xx += x * x
        zz += z * z
        xz += x * z

    angle = 0.5 * math.atan2(2.0 * xz, xx - zz)
    major_x = math.cos(angle)
    major_z = math.sin(angle)
    local_z_x = -major_z
    local_z_z = major_x
    if local_z_z < 0.0:
        major_x = -major_x
        major_z = -major_z
        local_z_x = -local_z_x
        local_z_z = -local_z_z

    yaw = math.atan2(local_z_x, local_z_z)
    x_extent = 0.0
    y_extent = 0.0
    z_extent = 0.0
    for x, z, y in points:
        x_extent = max(x_extent, abs(x * major_x + z * major_z))
        z_extent = max(z_extent, abs(x * local_z_x + z * local_z_z))
        y_extent = max(y_extent, abs(y))

    return (
        math.degrees(yaw),
        (
            max(0.05, x_extent / base_extents.x),
            max(0.25, y_extent / base_extents.y),
            max(0.05, z_extent / base_extents.z),
        ),
    )


def vertices_match(a: Vector, b: Vector, epsilon: float = 0.05) -> bool:
    return (a - b).length_squared <= epsilon * epsilon


def quads_share_edge(a: list[Vector], b: list[Vector]) -> bool:
    shared = 0
    for va in a:
        for vb in b:
            if vertices_match(va, vb):
                shared += 1
                break
    return shared >= 2


def unique_vertices(quads: list[list[Vector]]) -> list[Vector]:
    verts: list[Vector] = []
    for quad in quads:
        for vert in quad:
            if not any(vertices_match(vert, existing) for existing in verts):
                verts.append(vert)
    return verts


def jump_quad_components(raw_quads: list) -> list[tuple[int, list[Vector]]]:
    quads = [(index, [vec(v) for v in quad]) for index, quad in enumerate(raw_quads) if len(quad) == 4]
    used = [False] * len(quads)
    components: list[tuple[int, list[Vector]]] = []
    for i in range(len(quads)):
        if used[i]:
            continue
        stack = [i]
        used[i] = True
        component_indices = []
        component_quads = []
        while stack:
            current = stack.pop()
            quad_index, quad = quads[current]
            component_indices.append(quad_index)
            component_quads.append(quad)
            for j in range(len(quads)):
                if used[j]:
                    continue
                if quads_share_edge(quad, quads[j][1]):
                    used[j] = True
                    stack.append(j)
        components.append((min(component_indices), unique_vertices(component_quads)))
    return components


def add_trigger_from_gx_quad(
    ts,
    plugin,
    obj_type: str,
    name_prefix: str,
    created: int,
    entry: dict,
    quad_index: int,
    verts: list[Vector],
    center: Vector,
    placement: dict,
    yaw_deg: float,
    scale: tuple[float, float, float],
) -> None:
    helper = bpy.data.objects.new(f"{name_prefix}_{created:03d}", None)
    helper.empty_display_type = "PLAIN_AXES"
    helper.empty_display_size = 4.0
    bpy.context.collection.objects.link(helper)

    trig = ts.trigger_objects.add()
    trig.label = helper.name
    trig.helper = helper
    trig.obj_type = obj_type
    trig.segment = placement["segment"]
    trig.tx = float(placement["tx"])
    trig.ty = float(placement["ty"])
    trig.yaw_deg = yaw_deg
    trig.scale = (
        max(0.001, float(scale[0])),
        max(0.001, float(scale[1])),
        max(0.001, float(scale[2])),
    )
    helper["gx_source"] = entry.get("source", "")
    helper["gx_mesh_group"] = entry.get("name", "")
    helper["gx_quad_index"] = quad_index

    mesh = bpy.data.meshes.get(f"MESH_{obj_type}")
    if mesh:
        preview = bpy.data.objects.new(f"{helper.name}_Preview", mesh)
        bpy.context.collection.objects.link(preview)
        preview.parent = helper
        trig.preview_mesh = preview

    plugin._update_trigger_helper(trig)


def auto_add_gx_trigger_objects(
    data: dict,
    stream_segments: list[list[tuple[list[dict], object]]],
    plugin,
) -> tuple[int, int, int]:
    ts = bpy.context.scene.mxt_track_settings
    if not ts:
        return 0, 0, 0

    dash_created = 0
    jump_created = 0
    mine_created = 0
    for entry in data.get("mesh_collision") or []:
        source = str(entry.get("source", ""))
        name = str(entry.get("name", ""))
        surface = str(entry.get("surface", ""))
        collider_type = int(entry.get("collider_type", 0))
        search_name = f"{name} {surface}".lower()
        is_dash = source == "static_collider" and "dash" in search_name
        is_jump = source == "static_collider" and "jump" in search_name
        is_mine = source == "dynamic_scene" and (collider_type & 0x00004000) and "mine" in search_name
        if not is_dash and not is_jump and not is_mine:
            continue

        if is_jump:
            for component_index, verts in jump_quad_components(entry.get("quads", [])):
                if len(verts) < 4:
                    continue
                center = sum(verts, Vector((0.0, 0.0, 0.0))) / len(verts)
                placement = closest_segment_point(center, stream_segments, plugin)
                if placement is None:
                    continue
                yaw_deg, scale = fit_jumpplate_yaw_and_scale(verts, center, placement)
                add_trigger_from_gx_quad(
                    ts, plugin, "JUMPPLATE", "GXJumpplate", jump_created,
                    entry, component_index, verts, center, placement, yaw_deg, scale)
                jump_created += 1
            continue

        for quad_index, quad in enumerate(entry.get("quads", [])):
            if len(quad) != 4:
                continue
            verts = [vec(v) for v in quad]
            object_transform = entry.get("object_transform") if is_mine else None
            center = transform_vec(object_transform, "origin") or (sum(verts, Vector((0.0, 0.0, 0.0))) * 0.25)
            placement = closest_segment_point(center, stream_segments, plugin)
            if placement is None:
                continue

            if is_dash:
                yaw_deg, scale = fit_dashplate_yaw_and_scale(verts, center, placement)
                add_trigger_from_gx_quad(
                    ts, plugin, "DASHPLATE", "GXDashplate", dash_created,
                    entry, quad_index, verts, center, placement, yaw_deg, scale)
                dash_created += 1
            else:
                yaw_deg, scale = fit_mine_yaw_and_scale(verts, center, placement, object_transform)
                add_trigger_from_gx_quad(
                    ts, plugin, "MINE", "GXMine", mine_created,
                    entry, quad_index, verts, center, placement, yaw_deg, scale)
                mine_created += 1

    if dash_created or jump_created or mine_created:
        ts.active_trigger_obj_idx = len(ts.trigger_objects) - 1
    return dash_created, jump_created, mine_created


def vec(values) -> Vector:
    return Vector((float(values[0]), float(values[1]), float(values[2])))


def safe_normal(v: Vector, fallback: Vector) -> Vector:
    if v.length_squared < 1.0e-10:
        return fallback.normalized()
    return v.normalized()


def sample_half_width(sample: dict) -> float:
    if "track_width_or_radius" in sample:
        if sample.get("shape") in {"CYLINDER", "CYLINDER_OPEN", "PIPE", "PIPE_OPEN"}:
            return max(1.0, 0.5 * abs(float(sample["track_width_or_radius"])))
        return max(1.0, 0.5 * abs(float(sample["track_width_or_radius"])))
    return 1.0


def sample_is_modulated(sample: dict) -> bool:
    return (int(sample.get("source_piece_word", 0)) & 0x00200000) != 0


def sample_shape_scale(sample: dict) -> float:
    if sample.get("shape") in {"ROUNDED_SQUARE", "ROUNDED_SQUARE_OPEN"}:
        return 1.0
    return sample_half_width(sample)


def sample_round_pipe_y_radius(sample: dict) -> float:
    half_width = sample_half_width(sample)
    current_scale = sample.get("track_current_scale", [1.0, 1.0, 1.0])
    scale_x = abs(float(current_scale[0])) if len(current_scale) > 0 else 1.0
    scale_y = abs(float(current_scale[1])) if len(current_scale) > 1 else scale_x
    if scale_x <= 1.0e-6:
        return half_width
    return max(1.0e-6, half_width * (scale_y / scale_x))


def sample_scale_vector(sample: dict, segment_is_modulated: bool) -> Vector:
    if segment_is_modulated:
        return Vector((
            sample_half_width(sample),
            1.0,
            1.0,
        ))
    if sample_is_round_pipe_family(sample):
        return Vector((
            sample_half_width(sample),
            sample_round_pipe_y_radius(sample),
            1.0,
        ))
    shape_scale = sample_shape_scale(sample)
    return Vector((shape_scale, shape_scale, 1.0))


def sample_rounded_width(sample: dict) -> float:
    width = abs(float(sample.get("track_width_or_radius", 0.0)))
    if width <= 0.0:
        width = abs(float(sample.get("track_scl_x", 0.0))) + abs(float(sample.get("track_scl_y", 0.0)))
    return max(1.0, width)


def sample_rounded_height(sample: dict) -> float:
    height = abs(float(sample.get("track_scl_y", 0.0)))
    if height <= 0.0:
        height = abs(float(sample.get("rounded_height", 0.0)))
    return max(1.0, height)


def sample_rounded_radius(sample: dict) -> float:
    radius = sample_rounded_height(sample) * 0.5
    return max(0.0, radius)


def sample_frame(samples: list[dict], index: int, closed: bool) -> tuple[Vector, Vector, Vector, Vector, float]:
    center = vec(samples[index]["center"])
    right_edge = vec(samples[index]["right"])
    left_edge = vec(samples[index]["left"])
    right = safe_normal(
        vec(samples[index]["basis_right"]) if "basis_right" in samples[index] else left_edge - right_edge,
        Vector((1.0, 0.0, 0.0)),
    )

    if "basis_forward" in samples[index] and "basis_up" in samples[index]:
        forward = safe_normal(vec(samples[index]["basis_forward"]), Vector((0.0, 0.0, 1.0)))
        up = safe_normal(vec(samples[index]["basis_up"]), Vector((0.0, 1.0, 0.0)))
        right = safe_normal(up.cross(forward), right)
        up = safe_normal(forward.cross(right), up)
    else:
        if len(samples) == 1:
            forward = Vector((0.0, 0.0, 1.0))
        elif index == 0:
            forward = vec(samples[1]["center"]) - center
        elif index + 1 == len(samples):
            if closed and len(samples) > 2:
                forward = vec(samples[0]["center"]) - vec(samples[index - 1]["center"])
            else:
                forward = center - vec(samples[index - 1]["center"])
        else:
            forward = vec(samples[index + 1]["center"]) - vec(samples[index - 1]["center"])
        forward = safe_normal(forward, Vector((0.0, 0.0, 1.0)))
        up = safe_normal(forward.cross(right), Vector((0.0, 1.0, 0.0)))
        right = safe_normal(up.cross(forward), right)
    half_width = sample_half_width(samples[index])
    return center, right, up, forward, half_width


def sample_x_scale_sign(sample: dict) -> float:
    if "basis_right" not in sample or "basis_forward" not in sample or "basis_up" not in sample:
        return 1.0

    gx_right = safe_normal(vec(sample["basis_right"]), Vector((1.0, 0.0, 0.0)))
    gx_up = safe_normal(vec(sample["basis_up"]), Vector((0.0, 1.0, 0.0)))
    gx_forward = safe_normal(vec(sample["basis_forward"]), Vector((0.0, 0.0, 1.0)))
    helper_right = safe_normal(gx_up.cross(gx_forward), gx_right)
    return -1.0 if gx_right.dot(helper_right) < 0.0 else 1.0


def sample_curve_time(sample: dict, fallback: float) -> float:
    if "curve_time" in sample:
        return float(sample["curve_time"])
    if "distance" in sample:
        return float(sample["distance"])
    return fallback


def sample_group_t(group: list[dict], index: int) -> float:
    count = len(group)
    if count <= 1:
        return 0.0
    first_time = sample_curve_time(group[0], 0.0)
    last_time = sample_curve_time(group[-1], float(count - 1))
    span = last_time - first_time
    if abs(span) <= 1.0e-6:
        return index / (count - 1)
    t = (sample_curve_time(group[index], float(index)) - first_time) / span
    return max(0.0, min(1.0, t))


def sample_group_frame(group: list[dict], index: int) -> float:
    return sample_group_t(group, index) * 100.0


def ensure_fcurve(action, data_path: str, index: int):
    fcu = action.fcurves.find(data_path, index=index)
    if fcu is None:
        fcu = action.fcurves.new(data_path, index=index)
    return fcu


def insert_fcurve_key(action, data_path: str, index: int, frame: float, value: float) -> None:
    fcu = ensure_fcurve(action, data_path, index)
    fcu.keyframe_points.insert(frame, value, options={"FAST"})


def quat_from_axes(right: Vector, up: Vector, forward: Vector) -> Quaternion:
    mat = Matrix(
        (
            (right.x, up.x, forward.x),
            (right.y, up.y, forward.y),
            (right.z, up.z, forward.z),
        )
    )
    return mat.to_quaternion()


def set_linear_x_fcurves(action) -> None:
    for fcu in action.fcurves:
        keyframes = fcu.keyframe_points
        for index, kp in enumerate(keyframes):
            kp.interpolation = "BEZIER"
            kp.handle_left_type = "LINEAR_X"
            kp.handle_right_type = "LINEAR_X"
            if index > 0:
                prev_kp = keyframes[index - 1]
                dx_prev = kp.co.x - prev_kp.co.x
                slope = (kp.co.y - prev_kp.co.y) / max(1.0e-9, dx_prev)
                kp.handle_left.x = kp.co.x - dx_prev / 3.0
                kp.handle_left.y = kp.co.y - slope * dx_prev / 3.0
            else:
                kp.handle_left = kp.co
            if index + 1 < len(keyframes):
                next_kp = keyframes[index + 1]
                dx_next = next_kp.co.x - kp.co.x
                slope = (next_kp.co.y - kp.co.y) / max(1.0e-9, dx_next)
                kp.handle_right.x = kp.co.x + dx_next / 3.0
                kp.handle_right.y = kp.co.y + slope * dx_next / 3.0
            else:
                kp.handle_right = kp.co
        fcu.update()


def set_interior_auto_then_linear_x_fcurves(action) -> None:
    set_linear_x_fcurves(action)
    for fcu in action.fcurves:
        keyframes = fcu.keyframe_points
        if len(keyframes) <= 2:
            continue
        for kp in keyframes[1:-1]:
            kp.handle_left_type = "AUTO"
            kp.handle_right_type = "AUTO"
        fcu.update()
        for kp in keyframes[1:-1]:
            kp.handle_left_type = "LINEAR_X"
            kp.handle_right_type = "LINEAR_X"
        fcu.update()


def set_plugin_smooth_fcurves(action) -> None:
    for fcu in action.fcurves:
        keyframes = fcu.keyframe_points
        for kp in keyframes:
            kp.interpolation = "BEZIER"
        if len(keyframes) < 2:
            fcu.update()
            continue
        for index, kp in enumerate(keyframes):
            prev_kp = keyframes[index - 1] if index > 0 else None
            next_kp = keyframes[index + 1] if index + 1 < len(keyframes) else None
            if prev_kp and next_kp:
                slope_prev = (kp.co.y - prev_kp.co.y) / max(1.0e-9, kp.co.x - prev_kp.co.x)
                slope_next = (next_kp.co.y - kp.co.y) / max(1.0e-9, next_kp.co.x - kp.co.x)
                slope = 0.5 * (slope_prev + slope_next)
                dx_prev = kp.co.x - prev_kp.co.x
                dx_next = next_kp.co.x - kp.co.x
                kp.handle_left_type = "LINEAR_X"
                kp.handle_left.x = kp.co.x - dx_prev / 3.0
                kp.handle_left.y = kp.co.y - slope * dx_prev / 3.0
                kp.handle_right_type = "LINEAR_X"
                kp.handle_right.x = kp.co.x + dx_next / 3.0
                kp.handle_right.y = kp.co.y + slope * dx_next / 3.0
            elif prev_kp:
                slope = (kp.co.y - prev_kp.co.y) / max(1.0e-9, kp.co.x - prev_kp.co.x)
                dx_prev = kp.co.x - prev_kp.co.x
                kp.handle_left_type = "LINEAR_X"
                kp.handle_left.x = kp.co.x - dx_prev / 3.0
                kp.handle_left.y = kp.co.y - slope * dx_prev / 3.0
                kp.handle_right_type = "LINEAR_X"
                kp.handle_right = kp.co
            elif next_kp:
                slope = (next_kp.co.y - kp.co.y) / max(1.0e-9, next_kp.co.x - kp.co.x)
                dx_next = next_kp.co.x - kp.co.x
                kp.handle_right_type = "LINEAR_X"
                kp.handle_right.x = kp.co.x + dx_next / 3.0
                kp.handle_right.y = kp.co.y + slope * dx_next / 3.0
                kp.handle_left_type = "LINEAR_X"
                kp.handle_left = kp.co
        fcu.update()


def set_fcurve_linear_x(fcu) -> None:
    if not fcu:
        return
    keyframes = fcu.keyframe_points
    for index, kp in enumerate(keyframes):
        kp.interpolation = "BEZIER"
        kp.handle_left_type = "LINEAR_X"
        kp.handle_right_type = "LINEAR_X"
        if index > 0:
            prev_kp = keyframes[index - 1]
            dx_prev = kp.co.x - prev_kp.co.x
            slope = (kp.co.y - prev_kp.co.y) / max(1.0e-9, dx_prev)
            kp.handle_left.x = kp.co.x - dx_prev / 3.0
            kp.handle_left.y = kp.co.y - slope * dx_prev / 3.0
        else:
            kp.handle_left = kp.co
        if index + 1 < len(keyframes):
            next_kp = keyframes[index + 1]
            dx_next = next_kp.co.x - kp.co.x
            slope = (next_kp.co.y - kp.co.y) / max(1.0e-9, dx_next)
            kp.handle_right.x = kp.co.x + dx_next / 3.0
            kp.handle_right.y = kp.co.y + slope * dx_next / 3.0
        else:
            kp.handle_right = kp.co
    fcu.update()


def add_constant_helper(parent, name: str, value: float):
    helper = bpy.data.objects.new(name, None)
    helper.empty_display_type = "SPHERE"
    helper.empty_display_size = 0.0
    bpy.context.collection.objects.link(helper)
    helper.parent = parent
    helper.animation_data_create()
    action = bpy.data.actions.new(f"{name}_curve")
    helper.animation_data.action = action
    for frame in (0.0, 100.0):
        helper.location.x = value
        helper.keyframe_insert(data_path="location", index=0, frame=frame)
    set_linear_x_fcurves(action)
    return helper


def add_sampled_value_helper(parent, name: str, group: list[dict], value_fn):
    helper = bpy.data.objects.new(name, None)
    helper.empty_display_type = "SPHERE"
    helper.empty_display_size = 0.0
    bpy.context.collection.objects.link(helper)
    helper.parent = parent
    helper.animation_data_create()
    action = bpy.data.actions.new(f"{name}_curve")
    helper.animation_data.action = action

    for i, sample in enumerate(group):
        frame = sample_group_frame(group, i)
        helper.location.x = float(value_fn(sample))
        helper.keyframe_insert(data_path="location", index=0, frame=frame)
    set_linear_x_fcurves(action)
    return helper


def add_embed_helper(parent, name: str, entries: list[tuple[float, float, float]]):
    helper = bpy.data.objects.new(name, None)
    helper.empty_display_type = "SPHERE"
    helper.empty_display_size = 0.0
    bpy.context.collection.objects.link(helper)
    helper.parent = parent
    helper.animation_data_create()
    action = bpy.data.actions.new(f"{name}_embedCurves")
    helper.animation_data.action = action

    for ty, left_tx, right_tx in entries:
        frame = max(0.0, min(100.0, ty * 100.0))
        helper.location.y = left_tx
        helper.keyframe_insert(data_path="location", index=1, frame=frame)
        helper.location.z = right_tx
        helper.keyframe_insert(data_path="location", index=2, frame=frame)
    set_linear_x_fcurves(action)
    return helper


def add_gx_modulation_helper(parent, name: str, profile: dict, group: list[dict]):
    helper = bpy.data.objects.new(name, None)
    helper.empty_display_type = "SPHERE"
    helper.empty_display_size = 0.0
    bpy.context.collection.objects.link(helper)
    helper.parent = parent
    helper.animation_data_create()
    action = bpy.data.actions.new(f"{name}_gxModulation")
    helper.animation_data.action = action

    keys = list(profile.get("keys", [])) if profile else []
    if keys:
        t0 = float(keys[0]["time"])
        t1 = float(keys[-1]["time"])
        span = t1 - t0
        if abs(span) < 1.0e-6:
            span = 1.0
        entries = []
        for key in keys:
            gx_time = float(key["time"])
            norm = (gx_time - t0) / span
            frame = max(0.0, min(100.0, (1.0 - norm) * 100.0))
            entries.append((frame, key))
        entries.sort(key=lambda entry: entry[0])
        for frame, key in entries:
            helper.location.y = float(key["value"])
            helper.keyframe_insert(data_path="location", index=1, frame=frame)
        fcu = action.fcurves.find("location", index=1)
        if fcu:
            for i, kp in enumerate(fcu.keyframe_points):
                frame, key = entries[i]
                prev_frame = entries[i - 1][0] if i > 0 else frame
                next_frame = entries[i + 1][0] if i + 1 < len(entries) else frame
                slope_left = (-span / 100.0) * float(key.get("tangent_out", 0.0))
                slope_right = (-span / 100.0) * float(key.get("tangent_in", 0.0))
                kp.interpolation = "BEZIER"
                kp.handle_left_type = "FREE"
                kp.handle_right_type = "FREE"
                if i > 0:
                    dx = (frame - prev_frame) / 3.0
                    kp.handle_left.x = frame - dx
                    kp.handle_left.y = float(key["value"]) - slope_left * dx
                else:
                    kp.handle_left = kp.co
                if i + 1 < len(entries):
                    dx = (next_frame - frame) / 3.0
                    kp.handle_right.x = frame + dx
                    kp.handle_right.y = float(key["value"]) + slope_right * dx
                else:
                    kp.handle_right = kp.co
            fcu.update()
    else:
        fallback = float(profile.get("fallback_height", 0.0)) if profile else 0.0
        for frame in (0.0, 100.0):
            helper.location.y = fallback
            helper.keyframe_insert(data_path="location", index=1, frame=frame)

    for i, sample in enumerate(group):
        frame = sample_group_frame(group, i)
        if sample_is_modulated(sample):
            current_scale = sample.get("track_current_scale", [1.0, 1.0, 1.0])
            effect = float(current_scale[1])
        else:
            effect = 0.0
        helper.location.z = effect
        helper.keyframe_insert(data_path="location", index=2, frame=frame)
    set_fcurve_linear_x(action.fcurves.find("location", index=2))
    if not keys:
        set_fcurve_linear_x(action.fcurves.find("location", index=1))
    return helper


def add_open_helper(parent, name: str, group: list[dict] | None = None):
    helper = bpy.data.objects.new(name, None)
    helper.empty_display_type = "SPHERE"
    helper.empty_display_size = 0.0
    bpy.context.collection.objects.link(helper)
    helper.parent = parent
    helper.animation_data_create()
    action = bpy.data.actions.new(f"{name}_curve")
    helper.animation_data.action = action

    if group and group[0].get("shape") in {"CYLINDER_OPEN", "PIPE_OPEN"}:
        for i, sample in enumerate(group):
            frame = sample_group_frame(group, i)
            helper.location.x = max(0.0, float(sample.get("track_hcylin", 1.0)))
            helper.keyframe_insert(data_path="location", index=0, frame=frame)
            helper.location.y = 0.0
            helper.keyframe_insert(data_path="location", index=1, frame=frame)
        set_linear_x_fcurves(action)
        return helper

    for frame in (0.0, 100.0):
        helper.location.x = 1.0
        helper.keyframe_insert(data_path="location", index=0, frame=frame)
        helper.location.y = 0.0
        helper.keyframe_insert(data_path="location", index=1, frame=frame)
    set_linear_x_fcurves(action)
    return helper


def terrain_bands(sample: dict, embed_type: str) -> list[dict]:
    x_sign = sample_x_scale_sign(sample)
    bands = []
    for terrain in sample.get("terrain", []):
        if terrain.get("type") != embed_type:
            continue
        band = dict(terrain)
        if x_sign < 0.0:
            left_tx = float(band.get("left_tx", -1.0))
            right_tx = float(band.get("right_tx", 1.0))
            band["left_tx"] = -right_tx
            band["right_tx"] = -left_tx
        bands.append(band)
    bands.sort(key=lambda band: (float(band.get("left_tx", -1.0)) + float(band.get("right_tx", 1.0))) * 0.5)
    return bands


def add_terrain_embeds(parent, group: list[dict]) -> None:
    props = parent.mxt_road_overall_props
    count = len(group)
    if count < 2:
        return

    for embed_type in ("RECHARGE", "DIRT", "ICE", "LAVA"):
        max_band_count = max((len(terrain_bands(sample, embed_type)) for sample in group), default=0)
        if max_band_count == 0:
            continue

        run_entries: list[tuple[float, float, float]] = []
        run_start_t = 0.0
        run_index = 0

        def flush_run() -> None:
            nonlocal run_entries, run_start_t, run_index
            if len(run_entries) < 2:
                run_entries = []
                return
            emb = props.embeds.add()
            emb.label = f"GX {embed_type.title()} {run_index}"
            emb.embed_type = embed_type
            emb.start_t = max(0.0, min(1.0, run_start_t))
            emb.end_t = max(0.0, min(1.0, run_entries[-1][0]))
            emb.helper = add_embed_helper(parent, f"{parent.name}_{embed_type}_{run_index:02d}", run_entries)
            run_index += 1
            run_entries = []

        for band_index in range(max_band_count):
            for sample_index, sample in enumerate(group):
                ty = sample_group_t(group, sample_index)
                bands = terrain_bands(sample, embed_type)
                if band_index >= len(bands):
                    flush_run()
                    continue
                band = bands[band_index]
                left_tx = float(band.get("left_tx", -1.0))
                right_tx = float(band.get("right_tx", 1.0))
                if not run_entries:
                    run_start_t = ty
                run_entries.append((ty, left_tx, right_tx))
            flush_run()


def group_rail_height(group: list[dict], key: str) -> float:
    values = []
    for sample in group:
        raw_height = max(0.0, float(sample.get(key, 0.0)))
        if raw_height <= 0.0:
            values.append(0.0)
        elif sample_is_modulated(sample):
            values.append(raw_height)
        else:
            half_width = sample_half_width(sample)
            values.append(raw_height / max(1.0, half_width))
    return max(values) if values else 0.0


def group_uses_mirrored_helper_x(group: list[dict]) -> bool:
    return bool(group and sample_x_scale_sign(group[0]) < 0.0)


def group_mxt_rail_heights(group: list[dict]) -> tuple[float, float]:
    gx_left = group_rail_height(group, "rail_height_left")
    gx_right = group_rail_height(group, "rail_height_right")
    # GX applies rail_height_right to local +X and rail_height_left to local -X.
    # MXT's editor labels tx=+1 as "left", so swap only when helper +X still
    # points along GX +X.
    if group_uses_mirrored_helper_x(group):
        return gx_left, gx_right
    return gx_right, gx_left


def add_segment(name: str, group: list[dict], closed: bool):
    bpy.ops.object.empty_add(type="PLAIN_AXES", radius=1.0, location=(0.0, 0.0, 0.0))
    parent = bpy.context.active_object
    parent.name = name
    props = parent.mxt_road_overall_props
    props.is_mxt_road_segment_parent = True
    props.road_shape_type = group[0]["shape"]
    if sample_is_round_pipe_family(group[0]):
        props.horiz_subdivs = 17
        props.mesh_subdivision_length = 4.0
        props.mesh_subdivision_angle_deg = 2.0
    else:
        props.horiz_subdivs = 9
        props.mesh_subdivision_length = 20.0
        props.mesh_subdivision_angle_deg = 8.0
    left_rail_height, right_rail_height = group_mxt_rail_heights(group)
    props.rail_height_left = left_rail_height
    props.rail_height_right = right_rail_height
    props.rail_start_left = 0.0
    props.rail_end_left = 1.0
    props.rail_start_right = 0.0
    props.rail_end_right = 1.0
    props.disable_auto_rebake = True
    props.draw_embeds = True

    helper = bpy.data.objects.new(f"{name}_CurveMatrixHelper", None)
    helper.empty_display_type = "PLAIN_AXES"
    helper.empty_display_size = 0.0
    helper.rotation_mode = "QUATERNION"
    bpy.context.collection.objects.link(helper)
    helper.hide_set(True)
    helper.parent = parent
    props.curve_matrix_helper_empty = helper
    helper.animation_data_create()
    action = bpy.data.actions.new(f"{name}_CurveMatrix")
    helper.animation_data.action = action

    rounded = props.road_shape_type in {"ROUNDED_SQUARE", "ROUNDED_SQUARE_OPEN"}
    open_shape = props.road_shape_type in {"CYLINDER_OPEN", "PIPE_OPEN", "ROUNDED_SQUARE_OPEN"}
    segment_is_modulated = sample_is_modulated(group[0])
    if rounded:
        props.width_helper = add_sampled_value_helper(
            parent, f"{name}_WidthHelper", group, sample_rounded_width
        )
        props.height_helper = add_sampled_value_helper(
            parent, f"{name}_HeightHelper", group, sample_rounded_height
        )
        props.radius_helper = add_sampled_value_helper(
            parent, f"{name}_RadiusHelper", group, sample_rounded_radius
        )
    if open_shape:
        props.openness_helper = add_open_helper(parent, f"{name}_OpennessHelper", group)
    if segment_is_modulated:
        mod = props.modulations.add()
        mod.label = "GX Profile"
        mod.helper = add_gx_modulation_helper(
            parent,
            f"{name}_GXProfileModulation",
            group[0].get("_road_modulation_profile") or {},
            group,
        )

    count = len(group)
    prev_quat: Quaternion | None = None
    for i, sample in enumerate(group):
        frame = sample_group_frame(group, i)
        center, right, up, forward, half_width = sample_frame(group, i, closed)
        quat = quat_from_axes(right, up, forward)
        if prev_quat is not None and prev_quat.dot(quat) < 0.0:
            quat = Quaternion((-quat.w, -quat.x, -quat.y, -quat.z))
        prev_quat = quat.copy()
        scale_value = sample_scale_vector(sample, segment_is_modulated)

        helper.location = center
        helper.rotation_quaternion = quat
        helper.scale = scale_value
        insert_fcurve_key(action, "location", 0, frame, center.x)
        insert_fcurve_key(action, "location", 1, frame, center.y)
        insert_fcurve_key(action, "location", 2, frame, center.z)
        insert_fcurve_key(action, "rotation_quaternion", 0, frame, quat.w)
        insert_fcurve_key(action, "rotation_quaternion", 1, frame, quat.x)
        insert_fcurve_key(action, "rotation_quaternion", 2, frame, quat.y)
        insert_fcurve_key(action, "rotation_quaternion", 3, frame, quat.z)
        insert_fcurve_key(action, "scale", 0, frame, scale_value.x)
        insert_fcurve_key(action, "scale", 1, frame, scale_value.y)
        insert_fcurve_key(action, "scale", 2, frame, scale_value.z)
    set_interior_auto_then_linear_x_fcurves(action)

    cp_stride = max(1, math.ceil(count / 32))
    cp_indices = list(range(0, count, cp_stride))
    if cp_indices[-1] != count - 1:
        cp_indices.append(count - 1)
    for cp_i, sample_index in enumerate(cp_indices):
        center, right, up, forward, _half_width = sample_frame(group, sample_index, closed)
        cp = bpy.data.objects.new(f"{name}.CP.{cp_i:03d}", None)
        cp.empty_display_type = "PLAIN_AXES"
        cp.empty_display_size = 1.0
        cp.location = center
        cp.rotation_mode = "QUATERNION"
        cp.rotation_quaternion = quat_from_axes(right, up, forward)
        cp.scale = Vector((max(1.0, sample_shape_scale(group[sample_index])),) * 3)
        cp.parent = parent
        bpy.context.collection.objects.link(cp)
        cp.mxt_cp_data.is_mxt_control_point = True
        cp.mxt_cp_data.time = sample_group_t(group, sample_index)
        cp.mxt_cp_data.handle_in_length = 0.001
        cp.mxt_cp_data.handle_out_length = 0.001

    checkpoint_stride = max(1, min(32, count - 1))
    checkpoint_stride = max(1, min(checkpoint_stride, 8))
    i = 0
    while i + 1 < count:
        j = min(count - 1, i + checkpoint_stride)
        start = group[i]
        end = group[j]
        p0, r0, u0, f0, w0 = sample_frame(group, i, closed)
        p1, r1, u1, f1, w1 = sample_frame(group, j, closed)
        cp = props.checkpoints.add()
        cp.start_t = sample_group_t(group, i)
        cp.end_t = sample_group_t(group, j)
        cp.pos_start = p0
        cp.pos_end = p1
        cp.basis_start = [r0.x, r0.y, r0.z, u0.x, u0.y, u0.z, f0.x, f0.y, f0.z]
        cp.basis_end = [r1.x, r1.y, r1.z, u1.x, u1.y, u1.z, f1.x, f1.y, f1.z]
        cp.x_rad_start = w0
        cp.x_rad_end = w1
        cp.y_rad_start = w0
        cp.y_rad_end = w1
        cp.distance = max(0.0, float(end["distance"]) - float(start["distance"])) or (p1 - p0).length
        i = j

    add_terrain_embeds(parent, group)
    return parent


def sample_distance(a: dict, b: dict) -> float:
    return (vec(a["center"]) - vec(b["center"])).length


def sample_is_round_pipe_family(sample: dict) -> bool:
    return sample.get("shape") in {"CYLINDER", "CYLINDER_OPEN", "PIPE", "PIPE_OPEN"}


def samples_can_share_boundary(a: dict, b: dict) -> bool:
    a_round = sample_is_round_pipe_family(a)
    b_round = sample_is_round_pipe_family(b)
    if a_round != b_round:
        return False
    return True


def samples_can_stitch_boundary(a: dict, b: dict) -> bool:
    if samples_can_share_boundary(a, b):
        return True
    return sample_is_round_pipe_family(a) and not sample_is_round_pipe_family(b)


def sample_seam_position(sample: dict) -> Vector:
    if sample_is_round_pipe_family(sample) and "track_anchor" in sample:
        return vec(sample["track_anchor"])
    return vec(sample["center"])


def sample_seam_distance(a: dict, b: dict) -> float:
    return (sample_seam_position(a) - sample_seam_position(b)).length


def split_samples(
    samples: list[dict],
    max_samples: int,
) -> list[list[dict]]:
    groups: list[list[dict]] = []
    current: list[dict] = []
    for sample in samples:
        rail_cut = bool(
            current
            and (
                abs(float(sample.get("rail_height_left", 0.0)) - float(current[-1].get("rail_height_left", 0.0))) > 0.01
                or abs(float(sample.get("rail_height_right", 0.0)) - float(current[-1].get("rail_height_right", 0.0))) > 0.01
            )
        )
        hard_gap = bool(current and sample.get("authored_gap_before", False))
        stream_break = bool(current and sample.get("stream_break_before", False))
        sample_limit_reached = bool(max_samples > 0 and len(current) >= max(2, max_samples))
        soft_cut = bool(
            current
            and (
                sample["shape"] != current[-1]["shape"]
                or rail_cut
                or sample_limit_reached
            )
        )
        if (
            current
            and (hard_gap or stream_break or soft_cut)
        ):
            prev_sample = current[-1]
            share_boundary = bool(
                not hard_gap
                and not stream_break
                and (sample["shape"] != prev_sample["shape"] or rail_cut)
                and samples_can_share_boundary(prev_sample, sample)
            )
            if len(current) >= 2:
                if share_boundary:
                    groups.append(current + [sample])
                else:
                    groups.append(current)
            if hard_gap or stream_break:
                current = [sample]
            elif sample["shape"] != prev_sample["shape"] or rail_cut:
                current = [prev_sample, sample] if share_boundary else [sample]
            else:
                current = [prev_sample, sample]
        else:
            current.append(sample)
    if len(current) >= 2:
        groups.append(current)
    return groups


def append_distinct_sample(group: list[dict], sample: dict, epsilon: float = 0.001) -> bool:
    if not group:
        group.append(sample)
        return True
    if sample_distance(group[-1], sample) <= epsilon:
        return False
    group.append(sample)
    return True


def prepend_distinct_boundary_sample(group: list[dict], sample: dict, epsilon: float = 0.001) -> bool:
    if not group:
        group.insert(0, sample)
        return True
    if sample_distance(sample, group[0]) <= epsilon:
        return False

    boundary = copy.deepcopy(sample)
    boundary["shape"] = group[0]["shape"]
    boundary["source_piece_word"] = group[0].get("source_piece_word", boundary.get("source_piece_word", 0))
    boundary["_synthetic_boundary"] = True
    group.insert(0, boundary)
    return True


def sample_sequence(sample: dict) -> int:
    return int(sample.get("sample_sequence", -1))


def max_cross_extent(sample: dict) -> float:
    return max(1.0, sample_half_width(sample))


def extend_adjacent_boundaries(
    stream_groups: list[tuple[int, list[list[dict]]]],
    closed: bool,
) -> None:
    all_groups = [group for _stream_index, groups in stream_groups for group in groups]
    if len(all_groups) < 2:
        return
    max_sequence = max((sample_sequence(group[-1]) for group in all_groups), default=-1)

    for group in all_groups:
        end_sample = group[-1]
        end_sequence = sample_sequence(end_sample)
        best_next: dict | None = None
        best_next_group: list[dict] | None = None
        best_distance = 1.0e30
        for candidate in all_groups:
            if candidate is group:
                continue
            start_sample = candidate[0]
            if bool(start_sample.get("authored_gap_before", False)):
                continue
            if not samples_can_stitch_boundary(end_sample, start_sample):
                continue
            sequence_delta = sample_sequence(start_sample) - end_sequence
            if sequence_delta < 0 or sequence_delta > 1:
                if not (closed and end_sequence >= max_sequence - 1 and sample_sequence(start_sample) <= 1):
                    continue
            distance = sample_seam_distance(end_sample, start_sample)
            allowed = max(16.0, max_cross_extent(end_sample), max_cross_extent(start_sample))
            if distance <= allowed and distance < best_distance:
                best_distance = distance
                best_next = start_sample
                best_next_group = candidate
        if best_next is not None:
            if best_next_group is not None and sample_is_round_pipe_family(end_sample):
                prepend_distinct_boundary_sample(best_next_group, end_sample)
            else:
                append_distinct_sample(group, best_next)


def add_segment_link(source, target, forward: bool) -> None:
    props = source.mxt_road_overall_props
    refs = props.next_segments if forward else props.prev_segments
    for ref in refs:
        if ref.segment == target:
            return
    ref = refs.add()
    ref.segment = target


def link_segment_pair(prev_segment, next_segment) -> None:
    add_segment_link(prev_segment, next_segment, True)
    add_segment_link(next_segment, prev_segment, False)


def link_close_segment_rows(
    segment_rows: list[list[tuple[list[dict], object]]],
    closed: bool,
    epsilon: float = 0.001,
) -> None:
    for row_index, segment_row in enumerate(segment_rows):
        for i in range(len(segment_row) - 1):
            prev_group, prev_segment = segment_row[i]
            next_group, next_segment = segment_row[i + 1]
            if sample_distance(prev_group[-1], next_group[0]) <= epsilon:
                link_segment_pair(prev_segment, next_segment)
        if closed and row_index == 0 and len(segment_row) > 1:
            last_group, last_segment = segment_row[-1]
            first_group, first_segment = segment_row[0]
            if sample_distance(last_group[-1], first_group[0]) <= epsilon:
                link_segment_pair(last_segment, first_segment)


def link_temporal_rows(segment_rows: list[list[tuple[list[dict], object]]], closed: bool) -> None:
    flat_segments = [
        (group, segment)
        for segment_row in segment_rows
        for group, segment in segment_row
        if group
    ]
    max_sequence = max((sample_sequence(group[-1]) for group, _segment in flat_segments), default=-1)
    for prev_group, prev_segment in flat_segments:
        prev_sequence = sample_sequence(prev_group[-1])
        for next_group, next_segment in flat_segments:
            if prev_segment == next_segment:
                continue
            sequence_delta = sample_sequence(next_group[0]) - prev_sequence
            if sequence_delta < 0 or sequence_delta > 1:
                if not (closed and prev_sequence >= max_sequence - 1 and sample_sequence(next_group[0]) <= 1):
                    continue
            link_segment_pair(prev_segment, next_segment)


def main() -> int:
    args = parse_args()
    plugin = load_plugin(args.plugin)
    clear_scene()
    ensure_preview_materials()

    data = json.loads(args.samples_json.read_text(encoding="utf-8"))
    add_mesh_collision_helpers(data)
    road_entries = data.get("roads") or [{"stream_index": 0, "samples": data.get("samples", [])}]
    primary_samples = road_entries[0].get("samples", []) if road_entries else []
    if len(primary_samples) < 2:
        raise RuntimeError("sampler JSON did not contain enough samples")

    closed = int(data.get("circuit_type", 0)) == 0
    stream_groups: list[tuple[int, list[list[dict]]]] = []
    total_samples = 0
    for road in road_entries:
        stream_index = int(road.get("stream_index", len(stream_groups)))
        road_samples = road.get("samples", [])
        modulation_profile = road.get("modulation_profile")
        if modulation_profile:
            for sample in road_samples:
                sample["_road_modulation_profile"] = modulation_profile
        total_samples += len(road_samples)
        if len(road_samples) < 2:
            continue
        groups = split_samples(
            road_samples,
            args.max_samples_per_segment,
        )
        if groups:
            stream_groups.append((stream_index, groups))
    if not stream_groups:
        raise RuntimeError("no segment groups produced")

    ts = bpy.context.scene.mxt_track_settings
    ts.track_name = args.track_name or f"FZGX Course {int(data.get('authored_track_id', 0)):02d}"
    ts.track_description = "Loose analytic conversion from F-ZERO GX COLI_COURSE data."
    ts.track_difficulty = 5

    segments = []
    stream_segments: list[list[tuple[list[dict], object]]] = []
    for stream_index, groups in stream_groups:
        group_closed = closed and stream_index == 0 and len(groups) == 1
        segment_row = []
        for index, group in enumerate(groups):
            seg = add_segment(f"FZGXStream{stream_index}.{index:03d}", group, group_closed)
            segments.append(seg)
            segment_row.append((group, seg))
        stream_segments.append(segment_row)
    ts.first_segment = segments[0]

    link_close_segment_rows(stream_segments, closed)
    link_temporal_rows(stream_segments, closed)
    dash_trigger_count, jump_trigger_count, mine_trigger_count = auto_add_gx_trigger_objects(data, stream_segments, plugin)
    if dash_trigger_count:
        print(f"MXT FZGX: added {dash_trigger_count} dashplate triggers from static dash quads")
    if jump_trigger_count:
        print(f"MXT FZGX: added {jump_trigger_count} jumpplate triggers from static jump quad components")
    if mine_trigger_count:
        print(f"MXT FZGX: added {mine_trigger_count} mine triggers from dynamic scene mine objects")

    for seg in segments:
        try:
            plugin._build_mesh_direct(seg)
        except Exception as exc:
            print(f"MXT FZGX: preview mesh build failed for {seg.name}: {exc}")

    args.blend_out.parent.mkdir(parents=True, exist_ok=True)
    bpy.ops.wm.save_as_mainfile(filepath=str(args.blend_out))
    if args.export is not None:
        args.export.parent.mkdir(parents=True, exist_ok=True)
        plugin._export_stage(bpy.context, str(args.export))
    print(f"MXT FZGX: wrote {args.blend_out} with {len(segments)} segments from {total_samples} samples")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
