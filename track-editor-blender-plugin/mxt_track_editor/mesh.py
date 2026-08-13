"""Track mesh, checkpoint, and collision-data generation."""

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
    _MXTProfiler,
    get_active_mxt_road_segment_parent,
    get_mxt_control_point_empties,
)

from .shapes import (
    _sample_curve_matrix,
    _sample_curve_matrix_numpy,
)

from .curve_matrix import (
    _calculate_vertex_positions_numpy,
    _cubic,
    _invalidate_curve_matrix_sampler,
)

class MXTRoad_OT_GenerateMesh(Operator):
    bl_idname = "mxt_road.generate_mesh"
    bl_label  = "Generate/Update Mesh"
    bl_options = {'REGISTER', 'UNDO'}

    @staticmethod
    def _append_fcurve_key_times(out, helper, *, indices=None):
        if not (helper and helper.animation_data and helper.animation_data.action):
            return
        wanted = None if indices is None else set(indices)
        for fcu in helper.animation_data.action.fcurves:
            if fcu.data_path != "location":
                continue
            if wanted is not None and fcu.array_index not in wanted:
                continue
            for kp in fcu.keyframe_points:
                t = float(kp.co.x) / 100.0
                if -1.0e-6 <= t <= 1.0 + 1.0e-6:
                    out.append(max(0.0, min(1.0, t)))

    @staticmethod
    def _append_action_key_times(out, helper):
        if not (helper and helper.animation_data and helper.animation_data.action):
            return
        for fcu in helper.animation_data.action.fcurves:
            for kp in fcu.keyframe_points:
                t = float(kp.co.x) / 100.0
                if -1.0e-6 <= t <= 1.0 + 1.0e-6:
                    out.append(max(0.0, min(1.0, t)))

    @staticmethod
    def _unique_sorted_ty(values):
        values = sorted(max(0.0, min(1.0, float(v))) for v in values)
        if not values:
            return [0.0, 1.0]
        out = []
        for v in values:
            if not out or abs(v - out[-1]) > 1.0e-7:
                out.append(v)
            else:
                out[-1] = v
        if out[0] > 0.0:
            out.insert(0, 0.0)
        else:
            out[0] = 0.0
        if out[-1] < 1.0:
            out.append(1.0)
        else:
            out[-1] = 1.0
        return out

    @staticmethod
    def _mandatory_mesh_ty_samples(props, cm_helper):
        times = [0.0, 1.0]
        MXTRoad_OT_GenerateMesh._append_action_key_times(times, cm_helper)

        for helper in (
            getattr(props, "width_helper", None),
            getattr(props, "height_helper", None),
            getattr(props, "radius_helper", None),
            getattr(props, "openness_helper", None),
        ):
            MXTRoad_OT_GenerateMesh._append_fcurve_key_times(times, helper)

        if hasattr(props, "modulations"):
            for mod in props.modulations:
                # location.z is the ty-keyed effect curve. location.y is a tx profile.
                MXTRoad_OT_GenerateMesh._append_fcurve_key_times(times, mod.helper, indices=(2,))

        if hasattr(props, "embeds"):
            for embed in props.embeds:
                times.append(float(embed.start_t))
                times.append(float(embed.end_t))
                MXTRoad_OT_GenerateMesh._append_fcurve_key_times(times, embed.helper)

        for start_name, end_name in (
            ("rail_start_left", "rail_end_left"),
            ("rail_start_right", "rail_end_right"),
        ):
            times.append(float(getattr(props, start_name, 0.0)))
            times.append(float(getattr(props, end_name, 1.0)))

        return MXTRoad_OT_GenerateMesh._unique_sorted_ty(times)

    @staticmethod
    def _mesh_rows_at_ty(props, cm_helper, tx_1d, ty_1d):
        ty_1d = np.asarray(ty_1d, dtype=np.float64)
        tx_grid, ty_grid = np.meshgrid(tx_1d, ty_1d)
        centerline_pos, centerline_basis, centerline_scl = _sample_curve_matrix_numpy(cm_helper, ty_1d)
        return _calculate_vertex_positions_numpy(
            props, centerline_pos, centerline_basis, centerline_scl, tx_grid, ty_grid
        )

    @staticmethod
    def _adaptive_ty_samples_from_mesh_rows(cm_helper, props, tx_1d, max_len, max_ang_rad):
        mandatory = MXTRoad_OT_GenerateMesh._mandatory_mesh_ty_samples(props, cm_helper)
        max_len = max(float(max_len), 1.0e-4)
        max_ang_rad = max(float(max_ang_rad), 1.0e-4)
        min_dt = 1.0e-7
        max_depth = 24
        row_cache = {}

        def row_at(t):
            key = round(float(t), 12)
            row = row_cache.get(key)
            if row is None:
                row = MXTRoad_OT_GenerateMesh._mesh_rows_at_ty(props, cm_helper, tx_1d, [key])[0]
                row_cache[key] = row
            return row

        def interval_needs_split(t0, t1, row0, row1):
            tm = (t0 + t1) * 0.5
            rowm = row_at(tm)
            d0 = rowm - row0
            d1 = row1 - rowm
            len0 = np.linalg.norm(d0, axis=1)
            len1 = np.linalg.norm(d1, axis=1)
            if float(max(np.max(len0), np.max(len1))) > max_len:
                return True, tm, rowm

            denom = len0 * len1
            valid = denom > 1.0e-9
            if np.any(valid):
                dots = np.einsum("ij,ij->i", d0, d1)
                cosang = np.ones_like(dots)
                cosang[valid] = np.clip(dots[valid] / denom[valid], -1.0, 1.0)
                if float(np.max(np.arccos(cosang[valid]))) > max_ang_rad:
                    return True, tm, rowm

            return False, tm, rowm

        samples = [mandatory[0]]

        def append_interval(t0, t1, row0, row1, depth):
            if (t1 - t0) <= min_dt or depth >= max_depth:
                samples.append(t1)
                return
            should_split, tm, rowm = interval_needs_split(t0, t1, row0, row1)
            if not should_split:
                samples.append(t1)
                return
            append_interval(t0, tm, row0, rowm, depth + 1)
            append_interval(tm, t1, rowm, row1, depth + 1)

        for i in range(len(mandatory) - 1):
            t0 = mandatory[i]
            t1 = mandatory[i + 1]
            if t1 <= t0:
                continue
            append_interval(t0, t1, row_at(t0), row_at(t1), 0)

        samps = MXTRoad_OT_GenerateMesh._unique_sorted_ty(samples)
        centerline_pos, _centerline_basis, _centerline_scl = _sample_curve_matrix_numpy(
            cm_helper, np.array(samps, dtype=np.float64)
        )
        dists = [0.0]
        total_dist = 0.0
        for i in range(1, len(centerline_pos)):
            total_dist += float(np.linalg.norm(centerline_pos[i] - centerline_pos[i - 1]))
            dists.append(total_dist)
        return samps, dists

    def _adaptive_ty_samples_from_fcurves(cm_helper, max_len, max_ang_rad):
        samps = [0.0]; dists = [0.0]
        if not (cm_helper and cm_helper.animation_data and cm_helper.animation_data.action):
            return samps, dists

        act = cm_helper.animation_data.action
        fc_x = act.fcurves.find("location", index=0)
        fc_y = act.fcurves.find("location", index=1)
        fc_z = act.fcurves.find("location", index=2)
        if not (fc_x and fc_y and fc_z):
            return samps, dists

        def get_pos_from_fcurve(t_norm):
            frame = t_norm * 100.0
            return Vector((fc_x.evaluate(frame), fc_y.evaluate(frame), fc_z.evaluate(frame)))

        t = 0.001


        h = 1e-3
        p_prev = get_pos_from_fcurve(0.0)
        total_dist = 0.0

        while t < 1.0 - 1e-6:
            p_m = get_pos_from_fcurve(max(0.0, t - h))
            p_0 = get_pos_from_fcurve(t)
            p_p = get_pos_from_fcurve(min(1.0, t + h))

            r1 = (p_p - p_m) / (2 * h)
            r2 = (p_p - 2 * p_0 + p_m) / (h * h)

            speed = r1.length
            if speed < 1e-6:
                dt = 0.01
            else:
                curv_numerator = (r1.cross(r2)).length
                curv = curv_numerator / (speed**3 + 1e-12)

                dt_ang = max_ang_rad / (curv * speed + 1e-9)
                dt_len = max_len / speed
                dt = max(1e-5, min(dt_ang, dt_len))
            next_t = min(t + dt, 1.0)
            p_next = get_pos_from_fcurve(next_t)
            total_dist += (p_next - p_prev).length

            samps.append(next_t)
            dists.append(total_dist)

            t = next_t
            p_prev = p_next

        return samps, dists

    def _adaptive_ty_samples(helper, seg_parent, max_len, max_ang_rad):
        samps = [0.0]; dists = [0.0]
        cps = get_mxt_control_point_empties(seg_parent, sorted_by_time=True)
        if len(cps) < 2:
            return samps + [1.0], dists + [0.0]
        cp_times = [float(cp.mxt_cp_data.time) for cp in cps]
        cp_positions = [cp.location.copy() for cp in cps]
        cp_forward = [cp.rotation_euler.to_matrix().col[2].normalized() for cp in cps]
        cp_out_len = [float(cp.mxt_cp_data.handle_out_length) for cp in cps]
        cp_in_len = [float(cp.mxt_cp_data.handle_in_length) for cp in cps]

        def centerline_pos_fast(ty):
            if ty >= 1.0:
                a_i = len(cps) - 2
            elif ty <= 0.0:
                a_i = 0
            else:
                a_i = 0
                while a_i + 1 < len(cp_times) - 1 and ty >= cp_times[a_i + 1]:
                    a_i += 1
            span_len = (cp_times[a_i + 1] - cp_times[a_i]) or 1.0e-6
            bt = (ty - cp_times[a_i]) / span_len
            p0 = cp_positions[a_i]
            p3 = cp_positions[a_i + 1]
            p1 = p0 + cp_forward[a_i] * cp_out_len[a_i]
            p2 = p3 - cp_forward[a_i + 1] * cp_in_len[a_i + 1]
            return _cubic(p0, p1, p2, p3, bt)

        t = 0.0
        h = 1e-2
        p_prev = centerline_pos_fast(0.0)
        total = 0.0
        while t < 1.0 - 1e-6:
            p_m = centerline_pos_fast(t-h)
            p_0 = centerline_pos_fast(t)
            p_p = centerline_pos_fast(t+h)
            r1 = (p_p - p_m) / (2*h)
            r2 = (p_p - 2*p_0 + p_m) / (h*h)
            speed = r1.length
            curv  = (r1.cross(r2)).length / (speed**3 + 1e-12)
            dt_ang = max_ang_rad / max(curv*speed, 1e-9)
            dt_len = max_len       / max(speed,       1e-9)
            dt = max(1e-5, min(dt_ang, dt_len, 1.0 - t))
            next_t = min(t + dt, 1.0)
            p_next = centerline_pos_fast(next_t)
            total += (p_next - p_prev).length
            samps.append(next_t); dists.append(total)
            t      = next_t
            p_prev = p_next
        return samps, dists

    @staticmethod
    def _get_smooth_strip_normals(v_all, faces):
        return MXTRoad_OT_GenerateMesh._get_smooth_strip_normals_array(v_all, np.asarray(faces, dtype=np.int32)).reshape(-1, 3).tolist()

    @staticmethod
    def _get_smooth_strip_normals_array(v_all, faces_np):
        faces_np = np.asarray(faces_np, dtype=np.int32)
        if len(faces_np) == 0:
            return np.zeros((0, 4, 3), dtype=np.float64)
        v_all = np.asarray(v_all, dtype=np.float64)
        face_pts = v_all[faces_np]
        face_normals = np.cross(face_pts[:, 1] - face_pts[:, 0], face_pts[:, 2] - face_pts[:, 0])
        norms = np.linalg.norm(face_normals, axis=1, keepdims=True)
        norms[norms == 0.0] = 1.0
        face_normals /= norms

        vert_count = int(faces_np.max()) + 1
        vert_normals = np.zeros((vert_count, 3), dtype=np.float64)
        flat_faces = faces_np.reshape(-1)
        repeated_normals = np.repeat(face_normals, 4, axis=0)
        np.add.at(vert_normals, flat_faces, repeated_normals)
        vert_norms = np.linalg.norm(vert_normals, axis=1, keepdims=True)
        vert_norms[vert_norms == 0.0] = 1.0
        vert_normals /= vert_norms
        return vert_normals[faces_np]

    @staticmethod
    def build_for_parent(road_parent, context, *, report_fn=None):
        if report_fn:
            if not road_parent:
                report_fn({'ERROR'},"Select a road-segment parent"); return False
        profiler = _MXTProfiler(f"mesh {road_parent.name if road_parent else '<none>'}")
        props  = road_parent.mxt_road_overall_props
        mesh_name = f"{road_parent.name}_PreviewMesh"
        mesh_obj = next((c for c in road_parent.children if c.name == mesh_name), None)
        if getattr(props, "disable_preview_mesh_generation", False):
            if mesh_obj and mesh_obj.type == 'MESH':
                if context and context.view_layer.objects.active == mesh_obj:
                    road_parent.select_set(True)
                    context.view_layer.objects.active = road_parent
                mesh_obj.select_set(False)
                mesh_obj.data.clear_geometry()
                mesh_obj.hide_viewport = True
                mesh_obj.hide_render = True
            props.preview_mesh_exists = False
            if report_fn:
                report_fn({'INFO'}, f"Preview mesh generation disabled for {road_parent.name}.")
            return True

        helper = props.curve_matrix_helper_empty
        if not (helper and helper.animation_data and helper.animation_data.action):
            if report_fn: report_fn({'ERROR'},"Bake CurveMatrix first: no Action found on helper.");
            return False
        _invalidate_curve_matrix_sampler(helper)
        if props.horiz_subdivs < 2:
            if report_fn: report_fn({'ERROR'}, "Horizontal subdivisions must be >= 2");
            return False


        if not mesh_obj:
            mesh_data = bpy.data.meshes.new(mesh_name)
            mesh_obj = bpy.data.objects.new(mesh_name, mesh_data)
            mesh_obj.parent = road_parent
            context.collection.objects.link(mesh_obj)
        mesh_obj.hide_viewport = False
        mesh_obj.hide_render = False
        props.preview_mesh_exists = True

        mesh_obj.data.materials.clear()
        material_map = {}
        required_materials = [
            'track_surface', 'track_rail', 'embed_border', 'embed_ice', 'embed_recharge',
            'embed_dirt', 'embed_lava', 'embed_hole'
        ]
        for mat_name in required_materials:
            mat = bpy.data.materials.get(mat_name)
            if mat:
                mesh_obj.data.materials.append(mat)
                material_map[mat_name] = len(mesh_obj.data.materials) - 1
            else:
                if report_fn: report_fn({'WARNING'}, f"Material '{mat_name}' not found. Skipping.")

        def get_mat_idx(name):
            return material_map.get(name, 0)


        num_x = props.horiz_subdivs
        tx_1d = np.linspace(-1.0, 1.0, num_x, dtype=np.float64)
        if props.segment_type == 'BEZIER':
            ys, dist_1d = MXTRoad_OT_GenerateMesh._adaptive_ty_samples(
                helper,
                road_parent,
                props.mesh_subdivision_length,
                math.radians(props.mesh_subdivision_angle_deg),
            )
        else:
            ys, dist_1d = MXTRoad_OT_GenerateMesh._adaptive_ty_samples_from_fcurves(
                helper,
                props.mesh_subdivision_length,
                math.radians(props.mesh_subdivision_angle_deg),
            )

        def append_hole_boundary_crossing_times(times, fcurve, start_t, end_t):
            if not fcurve:
                return
            samples = [start_t, end_t]
            for kfp in fcurve.keyframe_points:
                t = max(start_t, min(end_t, float(kfp.co.x) / 100.0))
                samples.append(t)
            samples = sorted(set(round(float(t), 9) for t in samples))

            def eval_tx(t):
                return float(fcurve.evaluate(float(t) * 100.0))

            for x_target in tx_1d:
                for sample_idx in range(len(samples) - 1):
                    seg_start = samples[sample_idx]
                    seg_end = samples[sample_idx + 1]
                    if seg_end - seg_start <= 1.0e-7:
                        continue
                    prev_t = seg_start
                    prev_v = eval_tx(prev_t) - x_target
                    if abs(prev_v) <= 1.0e-7 and start_t < prev_t < end_t:
                        times.append(prev_t)
                    for step in range(1, 9):
                        cur_t = seg_start + (seg_end - seg_start) * (float(step) / 8.0)
                        cur_v = eval_tx(cur_t) - x_target
                        if abs(cur_v) <= 1.0e-7:
                            if start_t < cur_t < end_t:
                                times.append(cur_t)
                        elif (prev_v < 0.0 and cur_v > 0.0) or (prev_v > 0.0 and cur_v < 0.0):
                            lo = prev_t
                            hi = cur_t
                            lo_v = prev_v
                            for _ in range(24):
                                mid = (lo + hi) * 0.5
                                mid_v = eval_tx(mid) - x_target
                                if (lo_v < 0.0 and mid_v > 0.0) or (lo_v > 0.0 and mid_v < 0.0):
                                    hi = mid
                                else:
                                    lo = mid
                                    lo_v = mid_v
                            root = (lo + hi) * 0.5
                            if start_t < root < end_t:
                                times.append(root)
                        prev_t = cur_t
                        prev_v = cur_v

        if hasattr(props, "embeds"):
            extra_hole_rows = []
            HOLE_BOUNDARY_STEPS = 32
            for embed in props.embeds:
                if embed.embed_type != 'HOLE':
                    continue
                if not (embed.helper and embed.helper.animation_data and embed.helper.animation_data.action):
                    continue
                act = embed.helper.animation_data.action
                f_left = act.fcurves.find("location", index=1)
                f_right = act.fcurves.find("location", index=2)
                if not (f_left and f_right):
                    continue
                start_t = max(0.0, min(1.0, float(embed.start_t)))
                end_t = max(0.0, min(1.0, float(embed.end_t)))
                if end_t < start_t:
                    start_t, end_t = end_t, start_t
                if end_t - start_t <= 1.0e-7:
                    continue
                for step in range(1, HOLE_BOUNDARY_STEPS):
                    extra_hole_rows.append(start_t + (end_t - start_t) * (float(step) / float(HOLE_BOUNDARY_STEPS)))
                append_hole_boundary_crossing_times(extra_hole_rows, f_left, start_t, end_t)
                append_hole_boundary_crossing_times(extra_hole_rows, f_right, start_t, end_t)
            if extra_hole_rows:
                ys = MXTRoad_OT_GenerateMesh._unique_sorted_ty(list(ys) + extra_hole_rows)
        profiler.mark("rows")

        ty_1d = np.array(ys, dtype=np.float64)
        num_y = len(ty_1d)
        if num_y < 2:
            if report_fn: report_fn({'ERROR'}, "Not enough vertical samples to build mesh.");
            return False

        centerline_dist_pos, _centerline_dist_basis, _centerline_dist_scl = _sample_curve_matrix_numpy(helper, ty_1d)
        dist_1d = [0.0]
        total_dist_actual = 0.0
        for row in range(1, num_y):
            total_dist_actual += float(np.linalg.norm(centerline_dist_pos[row] - centerline_dist_pos[row - 1]))
            dist_1d.append(total_dist_actual)
        profiler.mark("dist")


        tx_grid, ty_grid = np.meshgrid(tx_1d, ty_1d)
        centerline_pos, centerline_basis, centerline_scl = _sample_curve_matrix_numpy(helper, ty_1d)
        P0 = _calculate_vertex_positions_numpy(props, centerline_pos, centerline_basis, centerline_scl, tx_grid, ty_grid)

        verts_co = P0.reshape(-1, 3)
        uv_x = np.linspace(0.0, 1.0, num_x, dtype=np.float64)
        uv_tile_world_length = 50.0 / props.road_uv_multiplier
        uv_y_initial = np.array(dist_1d, dtype=np.float64) / uv_tile_world_length

        total_v_length = uv_y_initial[-1]
        if total_v_length > 1e-6:
            snapped_v_length = max(1.0, round(total_v_length))
            correction_factor = snapped_v_length / total_v_length
            uv_y = uv_y_initial * correction_factor
        else:
            uv_y = uv_y_initial
        uv_grid_x, uv_grid_y = np.meshgrid(uv_x, uv_y)
        uvs_per_vert = np.stack((uv_grid_x, uv_grid_y), axis=2).reshape(-1, 2)

        all_verts = list(verts_co)
        all_faces = []
        all_uvs_per_vert = list(uvs_per_vert)
        rail_face_indices = set()
        rail_top_vert_indices = set()
        all_material_indices = []
        left_rail_top_indices = []
        right_rail_top_indices = []


        epsilon = 0.0001
        cl_pos_f, cl_basis_f, cl_scl_f = _sample_curve_matrix_numpy(helper, np.minimum(ty_1d + epsilon, 1.0))
        PF = _calculate_vertex_positions_numpy(props, cl_pos_f, cl_basis_f, cl_scl_f, tx_grid, ty_grid + epsilon)
        PR = _calculate_vertex_positions_numpy(props, centerline_pos, centerline_basis, centerline_scl, tx_grid + epsilon, ty_grid)
        cl_rot_mats = centerline_basis
        N_main = np.cross(PF - P0, PR - P0)
        norms = np.linalg.norm(N_main, axis=2, keepdims=True); norms[norms==0]=1.0; N_main /= norms
        main_road_vertex_normals = N_main.reshape(-1, 3)
        profiler.mark("surface_and_normals")

        hole_embeds = []
        if hasattr(props, "embeds"):
            for embed in props.embeds:
                if embed.embed_type != 'HOLE':
                    continue
                if not (embed.helper and embed.helper.animation_data and embed.helper.animation_data.action):
                    continue
                act = embed.helper.animation_data.action
                f_left = act.fcurves.find("location", index=1)
                f_right = act.fcurves.find("location", index=2)
                if not (f_left and f_right):
                    continue
                start_t = max(0.0, min(1.0, float(embed.start_t)))
                end_t = max(0.0, min(1.0, float(embed.end_t)))
                if end_t < start_t:
                    start_t, end_t = end_t, start_t
                if end_t - start_t <= 1.0e-7:
                    continue
                hole_embeds.append((start_t, end_t, f_left, f_right))

        def raw_hole_bounds_at(embed_tuple, ty):
            start_t, end_t, f_left, f_right = embed_tuple
            if ty < start_t or ty > end_t:
                return None
            frame = ty * 100.0
            left = float(f_left.evaluate(frame))
            right = float(f_right.evaluate(frame))
            if right < left:
                left, right = right, left
            return left, right

        def hole_bounds_at(embed_tuple, ty):
            raw_bounds = raw_hole_bounds_at(embed_tuple, ty)
            if raw_bounds is None:
                return None
            left, right = raw_bounds
            if right < -1.0 or left > 1.0:
                return None
            return max(-1.0, min(1.0, left)), max(-1.0, min(1.0, right))

        def tx_ty_inside_hole(tx, ty):
            for hole in hole_embeds:
                bounds = hole_bounds_at(hole, ty)
                if bounds is None:
                    continue
                if tx >= bounds[0] and tx <= bounds[1]:
                    return True
            return False

        surface_position_cache = {}

        def surface_position_at(tx, ty):
            key = (round(float(tx), 9), round(float(ty), 9))
            cached = surface_position_cache.get(key)
            if cached is not None:
                return cached
            tx_point = np.array([[tx]], dtype=np.float64)
            ty_point = np.array([[ty]], dtype=np.float64)
            cl_pos_p, cl_basis_p, cl_scl_p = _sample_curve_matrix_numpy(helper, np.array([ty], dtype=np.float64))
            pos = _calculate_vertex_positions_numpy(props, cl_pos_p, cl_basis_p, cl_scl_p, tx_point, ty_point)[0, 0]
            surface_position_cache[key] = pos
            return pos

        def surface_normal_at(tx, ty):
            pos = surface_position_at(tx, ty)
            tx_side = min(tx + epsilon, 1.0) if tx < 0.0 else max(tx - epsilon, -1.0)
            ty_side = min(ty + epsilon, 1.0) if ty < 0.5 else max(ty - epsilon, 0.0)
            pos_side = surface_position_at(tx_side, ty)
            pos_forward = surface_position_at(tx, ty_side)
            tangent_x = (pos_side - pos) if tx < 0.0 else (pos - pos_side)
            tangent_y = (pos_forward - pos) if ty < 0.5 else (pos - pos_forward)
            normal = np.cross(tangent_y, tangent_x)
            length = float(np.linalg.norm(normal))
            if length <= 1.0e-9:
                return np.array([0.0, 1.0, 0.0], dtype=np.float64)
            return normal / length

        row_vertex_maps = []
        normal_by_vert_idx = {}
        for row in range(num_y):
            row_map = {}
            for col, tx in enumerate(tx_1d):
                vert_idx = row * num_x + col
                row_map[round(float(tx), 9)] = vert_idx
                normal_by_vert_idx[vert_idx] = main_road_vertex_normals[vert_idx]
            row_vertex_maps.append(row_map)

        def road_vertex_at(row, tx):
            tx = max(-1.0, min(1.0, float(tx)))
            key = round(tx, 9)
            row_map = row_vertex_maps[row]
            existing = row_map.get(key)
            if existing is not None:
                return existing
            ty = float(ty_1d[row])
            all_verts.append(surface_position_at(tx, ty).tolist())
            all_uvs_per_vert.append([(tx + 1.0) * 0.5, float(uv_y[row])])
            vert_idx = len(all_verts) - 1
            base_col = int(np.argmin(np.abs(tx_1d - tx)))
            if abs(float(tx_1d[base_col]) - tx) <= 1.0e-7:
                normal_by_vert_idx[vert_idx] = main_road_vertex_normals[row * num_x + base_col]
            else:
                normal_by_vert_idx[vert_idx] = surface_normal_at(tx, ty)
            row_map[key] = vert_idx
            return vert_idx

        def active_hole_boundary_columns(ty):
            columns = []
            for hole in hole_embeds:
                if hole_bounds_at(hole, ty) is None:
                    continue
                columns.append(('hole_left', hole))
                columns.append(('hole_right', hole))
            return columns

        def column_tx(column, ty):
            kind = column[0]
            if kind == 'const':
                return column[1]
            hole = column[1]
            bounds = hole_bounds_at(hole, ty)
            if bounds is None:
                start_t, end_t = hole[0], hole[1]
                sample_ty = max(start_t, min(end_t, ty))
                raw_bounds = raw_hole_bounds_at(hole, sample_ty)
                if raw_bounds is None:
                    return -1.0 if kind == 'hole_left' else 1.0
                if raw_bounds[1] < -1.0:
                    return -1.0
                if raw_bounds[0] > 1.0:
                    return 1.0
                bounds = max(-1.0, min(1.0, raw_bounds[0])), max(-1.0, min(1.0, raw_bounds[1]))
            return bounds[0] if kind == 'hole_left' else bounds[1]

        track_surface_mat_idx_cached = get_mat_idx('track_surface')
        if not hole_embeds:
            grid_indices = np.arange(num_y * num_x, dtype=np.int32).reshape(num_y, num_x)
            q0 = grid_indices[:-1, :-1]
            q1 = grid_indices[1:, :-1]
            q2 = grid_indices[1:, 1:]
            q3 = grid_indices[:-1, 1:]
            road_faces_np = np.stack((q0, q1, q2, q3), axis=2).reshape(-1, 4)
            all_faces.extend(road_faces_np.tolist())
            all_material_indices.extend([track_surface_mat_idx_cached] * len(road_faces_np))
        else:
            for row in range(num_y - 1):
                ty0 = float(ty_1d[row])
                ty1 = float(ty_1d[row + 1])
                mid_ty = (ty0 + ty1) * 0.5
                columns = [('const', float(tx)) for tx in tx_1d]
                columns.extend(active_hole_boundary_columns(mid_ty))
                columns.sort(key=lambda column: column_tx(column, mid_ty))
                for col in range(len(columns) - 1):
                    left_column = columns[col]
                    right_column = columns[col + 1]
                    mid_left = column_tx(left_column, mid_ty)
                    mid_right = column_tx(right_column, mid_ty)
                    if mid_right - mid_left <= 1.0e-7:
                        continue
                    mid_tx = (mid_left + mid_right) * 0.5
                    if tx_ty_inside_hole(mid_tx, mid_ty):
                        continue
                    tx0_left = column_tx(left_column, ty0)
                    tx0_right = column_tx(right_column, ty0)
                    tx1_left = column_tx(left_column, ty1)
                    tx1_right = column_tx(right_column, ty1)
                    if max(abs(tx0_right - tx0_left), abs(tx1_right - tx1_left), mid_right - mid_left) <= 1.0e-7:
                        continue
                    face = [
                        road_vertex_at(row, tx0_left),
                        road_vertex_at(row + 1, tx1_left),
                        road_vertex_at(row + 1, tx1_right),
                        road_vertex_at(row, tx0_right),
                    ]
                    all_faces.append(face)
                    all_material_indices.append(track_surface_mat_idx_cached)
        profiler.mark("road_faces")

        rail_faces = []
        tunnel_roof_faces = []

        def rail_span(start_value, end_value):
            start = max(0.0, min(1.0, float(start_value)))
            end = max(0.0, min(1.0, float(end_value)))
            if end < start:
                start, end = end, start
            return start, end

        def rail_interval_active(t0, t1, start, end):
            mid = (float(t0) + float(t1)) * 0.5
            return mid >= start and mid <= end

        def append_unique_time(out, value, t_min, t_max):
            value = float(value)
            if value <= t_min + 1.0e-7 or value >= t_max - 1.0e-7:
                return
            for existing in out:
                if abs(existing - value) <= 1.0e-7:
                    return
            out.append(value)

        def boundary_crossings_for_tx(fcurve, target_tx, t0, t1):
            crossings = []
            if not fcurve or t1 - t0 <= 1.0e-7:
                return crossings
            samples = [t0, t1]
            for kfp in fcurve.keyframe_points:
                t = float(kfp.co.x) / 100.0
                if t0 < t < t1:
                    samples.append(t)
            samples = sorted(set(round(float(t), 9) for t in samples))

            def value_at(t):
                return float(fcurve.evaluate(float(t) * 100.0)) - target_tx

            for sample_idx in range(len(samples) - 1):
                seg_start = samples[sample_idx]
                seg_end = samples[sample_idx + 1]
                if seg_end - seg_start <= 1.0e-7:
                    continue
                prev_t = seg_start
                prev_v = value_at(prev_t)
                if abs(prev_v) <= 1.0e-7:
                    append_unique_time(crossings, prev_t, t0, t1)
                for step in range(1, 33):
                    cur_t = seg_start + (seg_end - seg_start) * (float(step) / 32.0)
                    cur_v = value_at(cur_t)
                    if abs(cur_v) <= 1.0e-7:
                        append_unique_time(crossings, cur_t, t0, t1)
                    elif (prev_v < 0.0 and cur_v > 0.0) or (prev_v > 0.0 and cur_v < 0.0):
                        lo = prev_t
                        hi = cur_t
                        lo_v = prev_v
                        for _ in range(28):
                            mid = (lo + hi) * 0.5
                            mid_v = value_at(mid)
                            if (lo_v < 0.0 and mid_v > 0.0) or (lo_v > 0.0 and mid_v < 0.0):
                                hi = mid
                            else:
                                lo = mid
                                lo_v = mid_v
                        append_unique_time(crossings, (lo + hi) * 0.5, t0, t1)
                    prev_t = cur_t
                    prev_v = cur_v
            return crossings

        rail_edge_split_cache = {}

        def rail_edge_hole_split_times(edge_tx):
            key = float(edge_tx)
            cached = rail_edge_split_cache.get(key)
            if cached is not None:
                return cached
            times = []
            for hole in hole_embeds:
                start_t, end_t, f_left, f_right = hole
                if end_t - start_t <= 1.0e-7:
                    continue
                append_unique_time(times, start_t, 0.0, 1.0)
                append_unique_time(times, end_t, 0.0, 1.0)
                for root in boundary_crossings_for_tx(f_left, edge_tx, start_t, end_t):
                    append_unique_time(times, root, 0.0, 1.0)
                for root in boundary_crossings_for_tx(f_right, edge_tx, start_t, end_t):
                    append_unique_time(times, root, 0.0, 1.0)
            cached = sorted(times)
            rail_edge_split_cache[key] = cached
            return cached

        def rail_hole_split_times(edge_tx, t0, t1):
            t0 = float(t0)
            t1 = float(t1)
            if not hole_embeds:
                return [t0, t1]
            times = [t0, t1]
            for split_t in rail_edge_hole_split_times(edge_tx):
                append_unique_time(times, split_t, t0, t1)
            return sorted(times)

        rail_scale_y_cache = {}

        def scale_y_at(ty):
            key = round(float(ty), 9)
            cached = rail_scale_y_cache.get(key)
            if cached is not None:
                return cached
            _pos, _quat, scl = _sample_curve_matrix_numpy(helper, np.array([float(ty)], dtype=np.float64))
            value = float(scl[0, 1])
            rail_scale_y_cache[key] = value
            return value

        def rail_inward_winding(edge_tx, ty):
            base = surface_position_at(edge_tx, ty)
            up_n = surface_normal_at(edge_tx, ty)
            forward_t = min(float(ty) + epsilon, 1.0) if ty < 0.5 else max(float(ty) - epsilon, 0.0)
            forward_vec = surface_position_at(edge_tx, forward_t) - base
            if ty >= 0.5:
                forward_vec = -forward_vec
            forward_n = normalized_vec(forward_vec, np.array([0.0, 0.0, 1.0], dtype=np.float64))
            rail_n = normalized_vec(np.cross(up_n, forward_n), np.array([1.0, 0.0, 0.0], dtype=np.float64))
            interior_vec = surface_position_at(0.0, ty) - base
            return float(np.dot(rail_n, interior_vec)) >= 0.0

        def rail_vertex(edge_tx, ty, h, uv_u, top):
            base = surface_position_at(edge_tx, ty)
            pos = base + surface_normal_at(edge_tx, ty) * (float(h) * scale_y_at(ty)) if top else base
            all_verts.append(pos.tolist())
            all_uvs_per_vert.append([float(uv_u), float(np.interp(ty, ty_1d, uv_y))])
            idx = len(all_verts) - 1
            if top:
                rail_top_vert_indices.add(idx)
            return idx

        def append_rail_face(face, inward):
            if not inward:
                face = list(reversed(face))
            all_faces.append(face)
            rail_faces.append(face)
            rail_face_indices.add(len(all_faces) - 1)
            all_material_indices.append(get_mat_idx('track_rail'))

        def append_rail_segment(edge_tx, h, t0, t1, bottom_u, top_u):
            if t1 - t0 <= 1.0e-7:
                return
            mid = (float(t0) + float(t1)) * 0.5
            if tx_ty_inside_hole(edge_tx, mid):
                return
            b0 = rail_vertex(edge_tx, t0, h, bottom_u, False)
            t0_idx = rail_vertex(edge_tx, t0, h, top_u, True)
            t1_idx = rail_vertex(edge_tx, t1, h, top_u, True)
            b1 = rail_vertex(edge_tx, t1, h, bottom_u, False)
            face = [b0, t0_idx, t1_idx, b1]
            append_rail_face(face, rail_inward_winding(edge_tx, mid))

        def append_rail_row_segment(edge_tx, row, bottom_dup_indices, top_indices, inward_winding):
            mid = (float(ty_1d[row]) + float(ty_1d[row + 1])) * 0.5
            if tx_ty_inside_hole(edge_tx, mid):
                return
            append_rail_face(
                [
                    bottom_dup_indices[row],
                    top_indices[row],
                    top_indices[row + 1],
                    bottom_dup_indices[row + 1],
                ],
                inward_winding[row],
            )

        def normalized_vec(v, fallback):
            length = float(np.linalg.norm(v))
            if length > 1.0e-9:
                return v / length
            return fallback

        def edge_rail_top(row, base_idx, height):
            base_vert = verts_co[base_idx]
            up_n = normalized_vec(N_main[row, base_idx - row * num_x], cl_rot_mats[row] @ np.array([0.0, 1.0, 0.0], dtype=np.float64))
            forward_n = normalized_vec(PF[row, base_idx - row * num_x] - P0[row, base_idx - row * num_x], cl_rot_mats[row] @ np.array([0.0, 0.0, 1.0], dtype=np.float64))
            rail_n = normalized_vec(np.cross(up_n, forward_n), cl_rot_mats[row] @ np.array([1.0, 0.0, 0.0], dtype=np.float64))
            interior_idx = row * num_x + (num_x // 2)
            interior_vec = verts_co[interior_idx] - base_vert
            scaled_height = float(height) * float(centerline_scl[row, 1])
            return base_vert + up_n * scaled_height, float(np.dot(rail_n, interior_vec)) >= 0.0

        if getattr(props, "rail_height_left", 0.0) > 0.0:
            h = props.rail_height_left
            span_start, span_end = rail_span(
                getattr(props, "rail_start_left", 0.0),
                getattr(props, "rail_end_left", 1.0),
            )
            top_indices = []
            bottom_dup_indices = []
            inward_winding = []
            offset = num_x - 1
            for row in range(num_y):
                base_idx = row * num_x + offset
                base_vert = verts_co[base_idx]
                # Duplicate the base vertex for the rail so it doesn't share with the road surface
                all_verts.append(base_vert.tolist())
                base_uv = uvs_per_vert[base_idx]
                # For rail UVs, base U=0.0; V from base UV
                all_uvs_per_vert.append([0.0, base_uv[1]])
                new_bottom_idx = len(all_verts) - 1
                bottom_dup_indices.append(new_bottom_idx)

                top_pos, row_inward_winding = edge_rail_top(row, base_idx, h)
                all_verts.append(top_pos.tolist())
                all_uvs_per_vert.append([1.0, base_uv[1]])
                new_top_idx = len(all_verts) - 1
                top_indices.append(new_top_idx)
                inward_winding.append(row_inward_winding)
                rail_top_vert_indices.add(new_top_idx)
            left_rail_top_indices = top_indices
            for row in range(num_y-1):
                if not rail_interval_active(ty_1d[row], ty_1d[row + 1], span_start, span_end):
                    continue
                row_t0 = float(ty_1d[row])
                row_t1 = float(ty_1d[row + 1])
                split_times = rail_hole_split_times(1.0, row_t0, row_t1)
                if len(split_times) == 2 and abs(split_times[0] - row_t0) <= 1.0e-9 and abs(split_times[1] - row_t1) <= 1.0e-9:
                    append_rail_row_segment(1.0, row, bottom_dup_indices, top_indices, inward_winding)
                else:
                    for split_idx in range(len(split_times) - 1):
                        append_rail_segment(1.0, h, split_times[split_idx], split_times[split_idx + 1], 0.0, 1.0)

        if getattr(props, "rail_height_right", 0.0) > 0.0:
            h = props.rail_height_right
            span_start, span_end = rail_span(
                getattr(props, "rail_start_right", 0.0),
                getattr(props, "rail_end_right", 1.0),
            )
            top_indices = []
            bottom_dup_indices = []
            inward_winding = []
            offset = 0
            for row in range(num_y):
                base_idx = row * num_x + offset
                base_vert = verts_co[base_idx]
                # Duplicate bottom base vertex for rail side
                all_verts.append(base_vert.tolist())
                base_uv = uvs_per_vert[base_idx]
                # For right rail, we maintain U mapping: base U=1.0, V from base
                all_uvs_per_vert.append([1.0, base_uv[1]])
                new_bottom_idx = len(all_verts) - 1
                bottom_dup_indices.append(new_bottom_idx)

                top_pos, row_inward_winding = edge_rail_top(row, base_idx, h)
                all_verts.append(top_pos.tolist())
                all_uvs_per_vert.append([0.0, base_uv[1]])
                new_top_idx = len(all_verts) - 1
                top_indices.append(new_top_idx)
                inward_winding.append(row_inward_winding)
                rail_top_vert_indices.add(new_top_idx)
            right_rail_top_indices = top_indices
            for row in range(num_y-1):
                if not rail_interval_active(ty_1d[row], ty_1d[row + 1], span_start, span_end):
                    continue
                row_t0 = float(ty_1d[row])
                row_t1 = float(ty_1d[row + 1])
                split_times = rail_hole_split_times(-1.0, row_t0, row_t1)
                if len(split_times) == 2 and abs(split_times[0] - row_t0) <= 1.0e-9 and abs(split_times[1] - row_t1) <= 1.0e-9:
                    append_rail_row_segment(-1.0, row, bottom_dup_indices, top_indices, inward_winding)
                else:
                    for split_idx in range(len(split_times) - 1):
                        append_rail_segment(-1.0, h, split_times[split_idx], split_times[split_idx + 1], 1.0, 0.0)

        if props.road_shape_type == 'TUNNEL' and left_rail_top_indices and right_rail_top_indices:
            roof_segments = 10
            left_start, left_end = rail_span(
                getattr(props, "rail_start_left", 0.0),
                getattr(props, "rail_end_left", 1.0),
            )
            right_start, right_end = rail_span(
                getattr(props, "rail_start_right", 0.0),
                getattr(props, "rail_end_right", 1.0),
            )
            roof_start = max(left_start, right_start)
            roof_end = min(left_end, right_end)
            if roof_end >= roof_start:
                roof_rows = []
                inv_scale = np.divide(
                    1.0,
                    centerline_scl,
                    out=np.ones_like(centerline_scl),
                    where=np.abs(centerline_scl) > 1.0e-9,
                )
                for row in range(num_y):
                    right_top = np.array(all_verts[right_rail_top_indices[row]], dtype=np.float64)
                    left_top = np.array(all_verts[left_rail_top_indices[row]], dtype=np.float64)
                    right_top_local = (cl_rot_mats[row].T @ (right_top - centerline_pos[row])) * inv_scale[row]
                    left_top_local = (cl_rot_mats[row].T @ (left_top - centerline_pos[row])) * inv_scale[row]
                    center = (right_top_local + left_top_local) * 0.5
                    side = left_top_local - right_top_local
                    radius = np.linalg.norm(side) * 0.5
                    if radius > 1.0e-9:
                        side /= radius * 2.0
                    else:
                        side = np.array([1.0, 0.0, 0.0], dtype=np.float64)
                    up = np.array([0.0, 1.0, 0.0], dtype=np.float64)
                    row_indices = []
                    for arc in range(roof_segments + 1):
                        if arc == 0:
                            row_indices.append(right_rail_top_indices[row])
                            continue
                        if arc == roof_segments:
                            row_indices.append(left_rail_top_indices[row])
                            continue
                        theta = (float(arc) / float(roof_segments)) * math.pi
                        local_pos = center - side * (math.cos(theta) * radius) + up * (math.sin(theta) * radius)
                        pos = centerline_pos[row] + cl_rot_mats[row] @ (local_pos * centerline_scl[row])
                        all_verts.append(pos.tolist())
                        all_uvs_per_vert.append([float(arc) / float(roof_segments), uvs_per_vert[row * num_x][1]])
                        row_indices.append(len(all_verts) - 1)
                    roof_rows.append(row_indices)
                for row in range(num_y - 1):
                    if not rail_interval_active(ty_1d[row], ty_1d[row + 1], roof_start, roof_end):
                        continue
                    for arc in range(roof_segments):
                        face = [
                            roof_rows[row][arc + 1],
                            roof_rows[row + 1][arc + 1],
                            roof_rows[row + 1][arc],
                            roof_rows[row][arc],
                        ]
                        all_faces.append(face)
                        rail_faces.append(face)
                        tunnel_roof_faces.append(face)
                        rail_face_indices.add(len(all_faces) - 1)
                        all_material_indices.append(get_mat_idx('track_rail'))
        profiler.mark("rails")

        if hasattr(props, "embeds"):
            EMBED_INSET_UNITS = 1.0
            EMBED_PUSH_DISTANCE = 0.5
            EMBED_X_DIVS = 8

            embeds_for_bmesh = []

            for embed in props.embeds:
                if not (embed.helper and embed.helper.animation_data and embed.helper.animation_data.action): continue
                act = embed.helper.animation_data.action
                f_left, f_right = act.fcurves.find("location", index=1), act.fcurves.find("location", index=2)
                if not (f_left and f_right): continue


                keyframe_times_t = []
                for fcurve in [f_left, f_right]:
                    for kfp in fcurve.keyframe_points:
                        t = kfp.co.x / 100.0
                        if embed.start_t < t < embed.end_t:
                            keyframe_times_t.append(t - 0.0001)
                            keyframe_times_t.append(t + 0.0001)
                ty_subset = ty_1d[(ty_1d > embed.start_t) & (ty_1d < embed.end_t)]
                all_t_samples = np.concatenate(([embed.start_t], ty_subset, [embed.end_t], keyframe_times_t))
                ty_embed_1d = np.unique(all_t_samples)

                if len(ty_embed_1d) < 2: continue

                cl_pos_e, cl_basis_e, cl_scl_e = _sample_curve_matrix_numpy(helper, ty_embed_1d)
                cl_rot_mats_e = cl_basis_e
                scl_ones = np.ones_like(cl_scl_e)
                frames_e = ty_embed_1d * 100.0
                tx_left, tx_right = np.array([f_left.evaluate(f) for f in frames_e]), np.array([f_right.evaluate(f) for f in frames_e])
                tx_embed_linspace = np.linspace(0, 1, EMBED_X_DIVS)[np.newaxis, :]
                tx_embed_grid = tx_left[:, np.newaxis] + (tx_right - tx_left)[:, np.newaxis] * tx_embed_linspace
                ty_embed_grid = np.repeat(ty_embed_1d[:, np.newaxis], EMBED_X_DIVS, axis=1)
                P_footprint_unscaled = _calculate_vertex_positions_numpy(props, cl_pos_e, cl_basis_e, scl_ones, tx_embed_grid, ty_embed_grid)

                def apply_scale(points_unscaled, centers, rotations, scales):
                    points_centered = points_unscaled - centers[:, np.newaxis, :]
                    S = np.apply_along_axis(np.diag, -1, scales)
                    T = rotations @ S @ np.transpose(rotations, (0, 2, 1))
                    points_scaled_centered = np.einsum('yij,ykj->yki', T, points_centered)
                    return points_scaled_centered + centers[:, np.newaxis, :]

                P_footprint = apply_scale(P_footprint_unscaled, cl_pos_e, cl_rot_mats_e, cl_scl_e)


                if embed.embed_type == 'HOLE':
                    continue


                current_face_idx = len(all_faces)
                base_vert_idx = len(all_verts)
                all_verts.extend(P_footprint.reshape(-1, 3).tolist())
                footprint_indices = np.arange(base_vert_idx, len(all_verts)).reshape(P_footprint.shape[:2])
                q0,q1,q2,q3 = footprint_indices[:-1,:-1],footprint_indices[:-1,1:],footprint_indices[1:,1:],footprint_indices[1:,:-1]
                footprint_faces = np.stack((q0,q3,q2,q1),axis=2).reshape(-1,4)
                all_faces.extend(footprint_faces.tolist())


                embed_type_name = embed.embed_type.lower()
                surface_mat_idx = get_mat_idx(f'embed_{embed_type_name}')
                all_material_indices.extend([surface_mat_idx] * len(footprint_faces))

                uv_y_embed = np.interp(ty_embed_1d, ty_1d, uv_y)
                uv_x_foot = np.linspace(0,1,EMBED_X_DIVS); uv_grid_x_foot,uv_grid_y_foot=np.meshgrid(uv_x_foot,uv_y_embed)
                all_uvs_per_vert.extend(np.stack((uv_grid_x_foot,uv_grid_y_foot),axis=2).reshape(-1,2).tolist())
                num_new_faces = len(footprint_faces)
                border_mat_idx = get_mat_idx('embed_border')
                embeds_for_bmesh.append( (current_face_idx, num_new_faces, border_mat_idx) )
        profiler.mark("embeds")


        faces_np_pre = np.asarray(all_faces, dtype=np.int32)
        verts_np_pre = np.asarray(all_verts, dtype=np.float64)
        if len(faces_np_pre) > 0:
            face_pts = verts_np_pre[faces_np_pre]
            unique_mask = (
                (faces_np_pre[:, 0] != faces_np_pre[:, 1]) &
                (faces_np_pre[:, 0] != faces_np_pre[:, 2]) &
                (faces_np_pre[:, 0] != faces_np_pre[:, 3]) &
                (faces_np_pre[:, 1] != faces_np_pre[:, 2]) &
                (faces_np_pre[:, 1] != faces_np_pre[:, 3]) &
                (faces_np_pre[:, 2] != faces_np_pre[:, 3])
            )
            diag_02 = face_pts[:, 2] - face_pts[:, 0]
            area_vec = np.cross(face_pts[:, 1] - face_pts[:, 0], diag_02)
            area_vec += np.cross(diag_02, face_pts[:, 3] - face_pts[:, 0])
            keep_mask = unique_mask & (np.einsum('ij,ij->i', area_vec, area_vec) > 1.0e-12)
        else:
            keep_mask = np.zeros(0, dtype=bool)

        old_to_new = np.full(len(all_faces), -1, dtype=np.int32)
        kept_old_indices = np.nonzero(keep_mask)[0]
        old_to_new[kept_old_indices] = np.arange(len(kept_old_indices), dtype=np.int32)
        kept_faces_np = faces_np_pre[keep_mask]
        all_faces = kept_faces_np.tolist()
        all_material_indices = np.asarray(all_material_indices, dtype=np.int32)[keep_mask].tolist()
        rail_face_indices = {
            int(old_to_new[face_idx])
            for face_idx in rail_face_indices
            if face_idx < len(old_to_new) and old_to_new[face_idx] >= 0
        }
        filtered_embeds_for_bmesh = []
        for face_start_idx, num_faces, border_mat_idx in embeds_for_bmesh:
            mapped = old_to_new[face_start_idx:face_start_idx + num_faces]
            mapped = mapped[mapped >= 0]
            if len(mapped) > 0:
                filtered_embeds_for_bmesh.append((int(mapped[0]), int(len(mapped)), border_mat_idx))
        embeds_for_bmesh = filtered_embeds_for_bmesh
        profiler.mark("filter_faces")

        used_vert_indices_np = np.unique(kept_faces_np.reshape(-1)) if len(kept_faces_np) > 0 else np.zeros(0, dtype=np.int32)
        if len(used_vert_indices_np) != len(all_verts):
            vert_remap_np = np.full(len(all_verts), -1, dtype=np.int32)
            vert_remap_np[used_vert_indices_np] = np.arange(len(used_vert_indices_np), dtype=np.int32)
            all_verts = verts_np_pre[used_vert_indices_np].tolist()
            all_uvs_per_vert = np.asarray(all_uvs_per_vert, dtype=np.float64)[used_vert_indices_np].tolist()
            all_faces = vert_remap_np[kept_faces_np].tolist()
            normal_by_vert_idx = {
                int(vert_remap_np[int(v_idx)]): normal
                for v_idx, normal in normal_by_vert_idx.items()
                if 0 <= int(v_idx) < len(vert_remap_np) and vert_remap_np[int(v_idx)] >= 0
            }
            rail_top_vert_indices = {
                int(vert_remap_np[int(v_idx)])
                for v_idx in rail_top_vert_indices
                if 0 <= int(v_idx) < len(vert_remap_np) and vert_remap_np[int(v_idx)] >= 0
            }
        profiler.mark("compact")

        all_verts_np_for_normals = np.array(all_verts, dtype=np.float64)
        all_faces_np_for_normals = np.asarray(all_faces, dtype=np.int32)
        all_material_indices_np = np.asarray(all_material_indices, dtype=np.int32)
        if len(all_faces_np_for_normals) > 0:
            face_pts_for_normals = all_verts_np_for_normals[all_faces_np_for_normals]
            face_normals = np.cross(
                face_pts_for_normals[:, 1] - face_pts_for_normals[:, 0],
                face_pts_for_normals[:, 2] - face_pts_for_normals[:, 0],
            )
            face_normal_lengths = np.linalg.norm(face_normals, axis=1, keepdims=True)
            face_normal_lengths[face_normal_lengths == 0.0] = 1.0
            face_normals /= face_normal_lengths
            all_loop_normals_np = np.repeat(face_normals[:, np.newaxis, :], 4, axis=1)
        else:
            all_loop_normals_np = np.zeros((0, 4, 3), dtype=np.float64)

        track_surface_mat_idx = get_mat_idx('track_surface')
        if len(all_faces_np_for_normals) > 0 and normal_by_vert_idx:
            has_vert_normal = np.zeros(len(all_verts), dtype=bool)
            vert_normals_np = np.zeros((len(all_verts), 3), dtype=np.float64)
            for v_idx, normal in normal_by_vert_idx.items():
                v_idx = int(v_idx)
                if 0 <= v_idx < len(all_verts):
                    has_vert_normal[v_idx] = True
                    vert_normals_np[v_idx] = normal
            road_normal_mask = (
                (all_material_indices_np == track_surface_mat_idx) &
                np.all(has_vert_normal[all_faces_np_for_normals], axis=1)
            )
            if np.any(road_normal_mask):
                all_loop_normals_np[road_normal_mask] = vert_normals_np[all_faces_np_for_normals[road_normal_mask]]

        if rail_face_indices:
            sorted_rail_face_indices = np.fromiter(sorted(rail_face_indices), dtype=np.int32)
            rail_faces_np = all_faces_np_for_normals[sorted_rail_face_indices]
            all_loop_normals_np[sorted_rail_face_indices] = MXTRoad_OT_GenerateMesh._get_smooth_strip_normals_array(
                all_verts_np_for_normals,
                rail_faces_np,
            )
        all_loop_normals = all_loop_normals_np.reshape(-1, 3)
        profiler.mark("normals_prepare")


        mesh = mesh_obj.data
        mesh.clear_geometry()
        num_total_verts, num_total_faces, num_loops = len(all_verts), len(all_faces), len(all_faces) * 4
        final_verts_co = np.array(all_verts, dtype=np.float32).ravel()
        final_faces_as_indices = np.array(all_faces, dtype=np.int32)
        final_uvs_per_vert = np.array(all_uvs_per_vert, dtype=np.float32)

        mesh.vertices.add(num_total_verts)
        mesh.polygons.add(num_total_faces)
        mesh.loops.add(num_loops)

        mesh.vertices.foreach_set("co", final_verts_co)
        mesh.polygons.foreach_set("loop_start", np.arange(0, num_loops, 4, dtype=np.int32))
        mesh.polygons.foreach_set("loop_total", np.full(num_total_faces, 4, dtype=np.int32))
        mesh.loops.foreach_set("vertex_index", final_faces_as_indices.ravel())
        mesh.polygons.foreach_set("material_index", np.array(all_material_indices, dtype=np.int32))

        mesh.update()
        profiler.mark("mesh_upload")

        if not mesh.uv_layers: mesh.uv_layers.new(name="UVMap")
        loop_uvs = final_uvs_per_vert[final_faces_as_indices].reshape(-1, 2).astype(np.float32, copy=False)
        mesh.uv_layers.active.data.foreach_set('uv', loop_uvs.ravel())
        profiler.mark("uvs")

        if len(all_loop_normals) != len(mesh.loops):
            mesh_vertices_np = np.array([v.co[:] for v in mesh.vertices], dtype=np.float64)
            all_loop_normals = MXTRoad_OT_GenerateMesh._get_smooth_strip_normals_array(mesh_vertices_np, final_faces_as_indices).reshape(-1, 3)

        mesh.normals_split_custom_set(all_loop_normals)
        mesh.update()
        profiler.mark("custom_normals")


        if embeds_for_bmesh:
            bm = bmesh.new()
            bm.from_mesh(mesh)
            bm.faces.ensure_lookup_table()
            for face_start_idx, num_faces, border_mat_idx in embeds_for_bmesh:
                faces_to_process = bm.faces[face_start_idx : face_start_idx + num_faces]

                inset_result = bmesh.ops.inset_region(
                    bm,
                    faces=faces_to_process,
                    thickness=EMBED_INSET_UNITS,
                    use_even_offset=True,
                    use_boundary=True,
                    depth=EMBED_PUSH_DISTANCE
                )


                if 'faces' in inset_result:
                    for face in inset_result['faces']:
                        face.material_index = border_mat_idx


            bm.to_mesh(mesh)
            bm.free()
            mesh.update()
        profiler.mark("bmesh_embed_borders")


        # Apply per-segment vertex colors (ground vs rail) based on material assignment
        try:
            rail_mat_idx = None
            # Prefer local material map if available
            try:
                rail_mat_idx = material_map.get('track_rail', None)
            except Exception:
                rail_mat_idx = None
            # Determine colors (RGBA) from props
            gc = tuple(float(c) for c in getattr(props, 'ground_color', (0.5, 0.5, 0.5)))
            rc = tuple(float(c) for c in getattr(props, 'rail_color', (0.8, 0.8, 0.8)))
            ground_rgba = (gc[0], gc[1], gc[2], 1.0)
            rail_rgba   = (rc[0], rc[1], rc[2], 1.0)
            # Ensure color layer exists
            color_layer = None
            if hasattr(mesh, 'color_attributes'):
                color_layer = mesh.color_attributes.get('Col')
                if color_layer is None:
                    color_layer = mesh.color_attributes.new(name='Col', domain='CORNER', type='FLOAT_COLOR')
            else:
                # Fallback for older Blender versions
                if not mesh.vertex_colors:
                    mesh.vertex_colors.new(name='Col')
                color_layer = mesh.vertex_colors['Col']
            # Fill per-loop colors
            if hasattr(mesh, 'color_attributes'):
                if not embeds_for_bmesh:
                    mat_indices_np = np.asarray(all_material_indices, dtype=np.int32)
                    face_colors = np.empty((len(mat_indices_np), 4), dtype=np.float32)
                    face_colors[:] = ground_rgba
                    if rail_mat_idx is not None:
                        face_colors[mat_indices_np == rail_mat_idx] = rail_rgba
                    colors = np.repeat(face_colors, 4, axis=0).ravel()
                else:
                    total_loops = len(mesh.loops)
                    colors = [0.0] * (total_loops * 4)
                    for poly in mesh.polygons:
                        use_rgba = rail_rgba if (rail_mat_idx is not None and poly.material_index == rail_mat_idx) else ground_rgba
                        for li in poly.loop_indices:
                            base = li * 4
                            colors[base + 0] = use_rgba[0]
                            colors[base + 1] = use_rgba[1]
                            colors[base + 2] = use_rgba[2]
                            colors[base + 3] = use_rgba[3]
                color_layer.data.foreach_set('color', colors)
                # Make sure this layer is active for exporters that rely on the active color attribute
                try:
                    mesh.color_attributes.active_color = color_layer
                except Exception:
                    pass

                # Additionally, create a POINT-domain color layer for exporters that only emit per-vertex colors
                try:
                    point_layer = mesh.color_attributes.get('ColV')
                    if point_layer is None or point_layer.domain != 'POINT':
                        point_layer = mesh.color_attributes.new(name='ColV', domain='POINT', type='FLOAT_COLOR')
                    if not embeds_for_bmesh:
                        vcol_np = np.empty((len(mesh.vertices), 4), dtype=np.float32)
                        vcol_np[:] = ground_rgba
                        if rail_mat_idx is not None and len(final_faces_as_indices) > 0:
                            mat_indices_np = np.asarray(all_material_indices, dtype=np.int32)
                            rail_faces_mask = mat_indices_np == rail_mat_idx
                            if np.any(rail_faces_mask):
                                rail_verts_np = np.unique(final_faces_as_indices[rail_faces_mask].ravel())
                                vcol_np[rail_verts_np] = rail_rgba
                        vcol = vcol_np.ravel()
                    else:
                        vcol = [0.0] * (len(mesh.vertices) * 4)
                        rail_verts = set()
                        if rail_mat_idx is not None:
                            for poly in mesh.polygons:
                                if poly.material_index == rail_mat_idx:
                                    for vi in poly.vertices:
                                        rail_verts.add(int(vi))
                        for vi in range(len(mesh.vertices)):
                            base = vi * 4
                            use = rail_rgba if vi in rail_verts else ground_rgba
                            vcol[base + 0] = use[0]
                            vcol[base + 1] = use[1]
                            vcol[base + 2] = use[2]
                            vcol[base + 3] = use[3]
                    point_layer.data.foreach_set('color', vcol)
                    # Prefer the vertex color layer as active for glTF export
                    mesh.color_attributes.active_color = point_layer
                except Exception:
                    pass
            else:
                # vertex_colors API
                data = color_layer.data
                for poly in mesh.polygons:
                    use_rgba = rail_rgba if (rail_mat_idx is not None and poly.material_index == rail_mat_idx) else ground_rgba
                    for li in poly.loop_indices:
                        data[li].color = use_rgba
                try:
                    mesh.vertex_colors.active = color_layer
                except Exception:
                    pass
        except Exception as e:
            if report_fn:
                report_fn({'WARNING'}, f"Failed to assign vertex colors: {e}")
        profiler.mark("colors")
        profiler.finish()

        if report_fn:
            report_fn({'INFO'}, f"NumPy+Bmesh build complete. Verts: {len(mesh.vertices)}, Faces: {len(mesh.polygons)}")
        return True

    def execute(self, context):
        parent = get_active_mxt_road_segment_parent(context)
        if not parent:
            self.report({'ERROR'}, "Select a road-segment parent")
            return {'CANCELLED'}
        ok = MXTRoad_OT_GenerateMesh.build_for_parent(parent, context, report_fn=self.report)
        return {'FINISHED'} if ok else {'CANCELLED'}

def _generate_checkpoints_for_segment(seg):
    props = seg.mxt_road_overall_props
    helper = props.curve_matrix_helper_empty
    if not (helper and helper.animation_data):
        raise RuntimeError(f"{seg.name}: Bake CurveMatrix first")

    props.checkpoints.clear()

    num = max(0, props.num_checkpoints_per_segment)
    if num <= 0:
        return 0

    step = 1.0 / num
    for i in range(num):
        t0 = i * step
        t1 = (i + 1) * step

        b0, p0, _ = _sample_curve_matrix(helper, t0)
        b1, p1, _ = _sample_curve_matrix(helper, min(t1, 1.0))

        B0 = b0.copy(); B0.col[0].normalize(); B0.col[1].normalize(); B0.col[2].normalize()
        B1 = b1.copy(); B1.col[0].normalize(); B1.col[1].normalize(); B1.col[2].normalize()

        cp = props.checkpoints.add()
        cp.start_t = t0
        cp.end_t = t1

        cp.pos_start = p0
        cp.pos_end = p1

        cp.basis_start = sum([list(B0.col[c]) for c in range(3)], [])
        cp.basis_end = sum([list(B1.col[c]) for c in range(3)], [])

        cp.x_rad_start = b0.col[0].length
        cp.x_rad_end = b1.col[0].length
        cp.y_rad_start = b0.col[1].length
        cp.y_rad_end = b1.col[1].length
        cp.distance = (p1 - p0).length

    return len(props.checkpoints)


class MXTRoad_OT_GenerateCheckpoints(Operator):
    bl_idname  = "mxt_road.generate_checkpoints"
    bl_label   = "Generate Checkpoints"
    bl_options = {'REGISTER', 'UNDO'}

    @classmethod
    def poll(cls, ctx):
        return get_active_mxt_road_segment_parent(ctx) is not None

    def execute(self, ctx):
        seg    = get_active_mxt_road_segment_parent(ctx)
        try:
            count = _generate_checkpoints_for_segment(seg)
        except RuntimeError as e:
            self.report({'ERROR'}, str(e))
            return {'CANCELLED'}

        self.report({'INFO'}, f"{count} checkpoints generated")
        return {'FINISHED'}


def _pack_curve(points):
    import struct
    data = struct.pack('<I', len(points))
    for t, v, tan_l, tan_r in points:
        data += struct.pack('<4f', t, v, tan_l, tan_r)
    return data


def _fcurve_to_points(fcu):
    pts = []
    if not fcu:
        return pts
    for kp in fcu.keyframe_points:
        t = kp.co.x / 100.0
        dt_l = (kp.co.x - kp.handle_left.x) / 100.0
        dt_r = (kp.handle_right.x - kp.co.x) / 100.0
        tan_l = ((kp.co.y - kp.handle_left.y) / dt_l) if dt_l != 0 else 0.0
        tan_r = ((kp.handle_right.y - kp.co.y) / dt_r) if dt_r != 0 else 0.0
        pts.append((t, kp.co.y, tan_l, tan_r))
    return pts

def _pack_mesh_collision_triangles(context, seg_index, seg_cp_start, cp_counts):
    import struct

    terrain_map = {
        'TRACK': 0,
        'RAIL': 0x100,
        'RECHARGE': 0x4,
        'DIRT': 0x8,
        'ICE': 0x40,
        'LAVA': 0x20,
        'HOLE': 0x200,
        'FALL': 0x400,
        'KILL': 0x800,
        'DASH': 0x2,
        'JUMP': 0x10,
    }
    preview_material_map = {
        'track_surface': 'TRACK',
        'track_rail': 'RAIL',
    }

    def base_material_name(name):
        if len(name) > 4 and name[-4] == '.' and name[-3:].isdigit():
            return name[:-4]
        return name

    skipped_bad_triangles = {}
    skipped_bad_triangle_indices = []
    candidate_triangle_index = [0]

    def note_skipped_triangle(source_name, tri_index):
        skipped_bad_triangles[source_name] = skipped_bad_triangles.get(source_name, 0) + 1
        if len(skipped_bad_triangle_indices) < 32:
            skipped_bad_triangle_indices.append(tri_index)

    def f32(v):
        return struct.unpack('<f', struct.pack('<f', float(v)))[0]

    def quantize_vec3_for_track(v):
        if not (math.isfinite(v.x) and math.isfinite(v.y) and math.isfinite(v.z)):
            return None
        try:
            return struct.unpack('<3f', struct.pack('<3f', v.x, v.y, v.z))
        except (OverflowError, struct.error):
            return None

    def sub3(a, b):
        return (f32(a[0] - b[0]), f32(a[1] - b[1]), f32(a[2] - b[2]))

    def cross3(a, b):
        return (
            f32(f32(a[1] * b[2]) - f32(a[2] * b[1])),
            f32(f32(a[2] * b[0]) - f32(a[0] * b[2])),
            f32(f32(a[0] * b[1]) - f32(a[1] * b[0])),
        )

    def dot3(a, b):
        return f32(f32(f32(a[0] * b[0]) + f32(a[1] * b[1])) + f32(a[2] * b[2]))

    def append_triangle_record(records, terrain_key, positions, normals, source_name, terrain_flags=0):
        tri_index = candidate_triangle_index[0]
        candidate_triangle_index[0] += 1
        if terrain_key not in terrain_map:
            raise RuntimeError(f"{source_name} has unsupported mesh collision surface {terrain_key}")
        if len(positions) != 3 or len(normals) != 3:
            raise RuntimeError(f"{source_name} produced an invalid mesh collision triangle")
        quantized_positions = [quantize_vec3_for_track(v) for v in positions]
        quantized_normals = [quantize_vec3_for_track(v) for v in normals]
        if any(v is None for v in quantized_positions + quantized_normals):
            note_skipped_triangle(source_name, tri_index)
            return False
        edge0 = sub3(quantized_positions[1], quantized_positions[0])
        edge1 = sub3(quantized_positions[2], quantized_positions[0])
        face = cross3(edge0, edge1)
        if dot3(face, face) <= 1.0e-8:
            note_skipped_triangle(source_name, tri_index)
            return False
        d00 = dot3(edge0, edge0)
        d01 = dot3(edge0, edge1)
        d11 = dot3(edge1, edge1)
        denom = f32(f32(d00 * d11) - f32(d01 * d01))
        if abs(denom) <= 1.0e-8:
            note_skipped_triangle(source_name, tri_index)
            return False
        for n in quantized_normals:
            if dot3(n, n) <= 1.0e-8:
                note_skipped_triangle(source_name, tri_index)
                return False
        record = bytearray()
        record += struct.pack('<I', terrain_map[terrain_key] | terrain_flags)
        for p in quantized_positions:
            record += struct.pack('<3f', p[0], p[1], p[2])
        for n in quantized_normals:
            record += struct.pack('<3f', n[0], n[1], n[2])
        records.append(record)
        return True

    def append_object_triangles(records, obj, surface_for_polygon, depsgraph, required, terrain_flags=0):
        eval_obj = obj.evaluated_get(depsgraph)
        mesh = eval_obj.to_mesh()
        try:
            mesh.calc_loop_triangles()
            object_tri_count = 0
            normal_matrix = obj.matrix_world.to_3x3().inverted().transposed()
            for loop_tri in mesh.loop_triangles:
                terrain_key = surface_for_polygon(mesh, loop_tri.polygon_index)
                if terrain_key is None:
                    continue
                loop_indices = list(loop_tri.loops)
                if len(loop_indices) != 3:
                    raise RuntimeError(f"{obj.name} produced a non-triangle loop triangle")
                positions = []
                normals = []
                for loop_index in loop_indices:
                    loop = mesh.loops[loop_index]
                    vert = mesh.vertices[loop.vertex_index]
                    positions.append(obj.matrix_world @ vert.co)
                    normals.append((normal_matrix @ loop.normal).normalized())
                if append_triangle_record(records, terrain_key, positions, normals, obj.name, terrain_flags):
                    object_tri_count += 1
            if required and object_tri_count == 0:
                raise RuntimeError(f"{obj.name} is marked for mesh collision but has no triangles")
            return object_tri_count
        finally:
            eval_obj.to_mesh_clear()

    def append_segment_embed_terrain_triangles(records, seg):
        props = seg.mxt_road_overall_props
        if not hasattr(props, "embeds") or len(props.embeds) == 0:
            return 0
        helper = props.curve_matrix_helper_empty
        if not (helper and helper.animation_data and helper.animation_data.action):
            raise RuntimeError(f"{seg.name} has terrain embeds but no baked CurveMatrix action")

        embed_x_divs = 8
        terrain_offset = 0.03
        tx_probe = np.linspace(-1.0, 1.0, max(2, int(getattr(props, "horiz_subdivs", 2))), dtype=np.float64)
        ty_base, _dist_base = MXTRoad_OT_GenerateMesh._adaptive_ty_samples_from_mesh_rows(
            helper,
            props,
            tx_probe,
            props.mesh_subdivision_length,
            math.radians(props.mesh_subdivision_angle_deg),
        )
        ty_base = np.array(ty_base, dtype=np.float64)
        emitted = 0

        for embed in props.embeds:
            terrain_key = embed.embed_type
            if terrain_key == 'HOLE':
                continue
            if terrain_key not in terrain_map:
                raise RuntimeError(f"{seg.name} embed {embed.label} has unsupported terrain {terrain_key}")
            if not (embed.helper and embed.helper.animation_data and embed.helper.animation_data.action):
                raise RuntimeError(f"{seg.name} embed {embed.label} has no helper action")
            act = embed.helper.animation_data.action
            f_left = act.fcurves.find("location", index=1)
            f_right = act.fcurves.find("location", index=2)
            if not (f_left and f_right):
                raise RuntimeError(f"{seg.name} embed {embed.label} has no left/right border curves")

            start_t = max(0.0, min(1.0, float(embed.start_t)))
            end_t = max(0.0, min(1.0, float(embed.end_t)))
            if end_t <= start_t:
                raise RuntimeError(f"{seg.name} embed {embed.label} has invalid t range")

            samples = [start_t, end_t]
            for t in ty_base:
                if start_t < t < end_t:
                    samples.append(float(t))
            for fcurve in (f_left, f_right):
                for kfp in fcurve.keyframe_points:
                    t = float(kfp.co.x) / 100.0
                    if start_t < t < end_t:
                        samples.append(max(start_t, t - 0.0001))
                        samples.append(min(end_t, t + 0.0001))
            ty_embed_1d = np.array(MXTRoad_OT_GenerateMesh._unique_sorted_ty(samples), dtype=np.float64)
            ty_embed_1d = ty_embed_1d[(ty_embed_1d >= start_t) & (ty_embed_1d <= end_t)]
            if len(ty_embed_1d) < 2:
                raise RuntimeError(f"{seg.name} embed {embed.label} produced too few terrain samples")

            cl_pos, cl_basis, cl_scl = _sample_curve_matrix_numpy(helper, ty_embed_1d)
            frames = ty_embed_1d * 100.0
            tx_left = np.array([f_left.evaluate(f) for f in frames], dtype=np.float64)
            tx_right = np.array([f_right.evaluate(f) for f in frames], dtype=np.float64)
            tx_lerp = np.linspace(0.0, 1.0, embed_x_divs, dtype=np.float64)[np.newaxis, :]
            tx_grid = tx_left[:, np.newaxis] + (tx_right - tx_left)[:, np.newaxis] * tx_lerp
            ty_grid = np.repeat(ty_embed_1d[:, np.newaxis], embed_x_divs, axis=1)
            points = _calculate_vertex_positions_numpy(props, cl_pos, cl_basis, cl_scl, tx_grid, ty_grid)
            d_ty = np.gradient(points, axis=0)
            d_tx = np.gradient(points, axis=1)
            normals = np.cross(d_ty, d_tx)
            normal_len = np.linalg.norm(normals, axis=2, keepdims=True)
            if np.any(normal_len <= 1.0e-9):
                raise RuntimeError(f"{seg.name} embed {embed.label} produced degenerate terrain normals")
            normals /= normal_len
            points = points + normals * terrain_offset

            num_y = points.shape[0]
            for row in range(num_y - 1):
                for col in range(embed_x_divs - 1):
                    tris = (
                        ((row, col), (row + 1, col), (row + 1, col + 1)),
                        ((row, col), (row + 1, col + 1), (row, col + 1)),
                    )
                    for tri in tris:
                        tri_positions = [Vector(tuple(points[r, c])) for r, c in tri]
                        tri_normals = [Vector(tuple(normals[r, c])).normalized() for r, c in tri]
                        if append_triangle_record(
                            records,
                            terrain_key,
                            tri_positions,
                            tri_normals,
                            f"{seg.name} embed {embed.label}"):
                            emitted += 1
        return emitted

    depsgraph = context.evaluated_depsgraph_get()
    records = []
    for obj in bpy.data.objects:
        props = getattr(obj, "mxt_mesh_collision_props", None)
        if obj.type != 'MESH' or not props or not props.is_mxt_collision_mesh:
            continue
        surface_type = props.surface_type
        terrain_flags = 0x80 if props.double_sided else 0
        append_object_triangles(
            records,
            obj,
            lambda _mesh, _poly_index, surface_type=surface_type: surface_type,
            depsgraph,
            True,
            terrain_flags)

    for seg, segment_index in seg_index.items():
        props = seg.mxt_road_overall_props
        if not getattr(props, "export_preview_mesh_collision", False):
            continue
        if getattr(props, "disable_preview_mesh_generation", False):
            raise RuntimeError(f"{seg.name} has Preview Mesh Collision enabled but preview mesh generation is disabled")
        mesh_name = f"{seg.name}_PreviewMesh"
        preview_obj = next((c for c in seg.children if c.name == mesh_name), None)
        if not preview_obj or preview_obj.type != 'MESH':
            raise RuntimeError(f"{seg.name} has Preview Mesh Collision enabled but no preview mesh")

        def preview_surface(mesh, polygon_index):
            poly = mesh.polygons[polygon_index]
            if poly.material_index >= len(mesh.materials):
                raise RuntimeError(f"{preview_obj.name} polygon has invalid material index")
            mat_name = base_material_name(mesh.materials[poly.material_index].name)
            if mat_name not in preview_material_map:
                if mat_name.startswith('embed_'):
                    return None
                raise RuntimeError(f"{preview_obj.name} material {mat_name} cannot be exported as mesh collision")
            return preview_material_map[mat_name]

        append_object_triangles(
            records,
            preview_obj,
            preview_surface,
            depsgraph,
            True)
        append_segment_embed_terrain_triangles(records, seg)

    mesh_data = bytearray()
    tri_count = 0
    for record in records:
        mesh_data += record
        tri_count += 1
    if skipped_bad_triangles:
        summary = ", ".join(
            f"{name}: {count}" for name, count in sorted(skipped_bad_triangles.items()))
        index_summary = ", ".join(str(i) for i in skipped_bad_triangle_indices)
        print(f"MXT exporter skipped degenerate mesh collision triangles: {summary}; first indices: {index_summary}")
    return tri_count, mesh_data
