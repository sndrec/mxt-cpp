"""Curve-matrix sampling, baking, and vectorized evaluation."""

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
    _curve_matrix_channel_values,
    _ensure_fcurve,
    _evaluate_fcurve_continued,
    _linearize_curve_matrix_handles_with_extension,
    _linearize_fcurve_handles_smooth,
    _mxt_profile_scope,
    get_active_mxt_road_segment_parent,
    get_mxt_control_point_empties,
    schedule_mesh_build,
)

def _add_key(fcu, frame, value):
    kp = fcu.keyframe_points.insert(frame, value, options={'FAST'})
    kp.interpolation = 'BEZIER'
    kp.handle_left_type = "LINEAR_X"
    kp.handle_right_type = "LINEAR_X"
    return kp

_CURVE_MATRIX_SAMPLER_CACHE = {}
_CURVE_MATRIX_BASIS_PROP_NAMES = tuple(f"mxt_cm_basis_{i}" for i in range(9))
_CURVE_MATRIX_BASIS_DATA_PATHS = tuple(f'["{name}"]' for name in _CURVE_MATRIX_BASIS_PROP_NAMES)
_CURVE_MATRIX_BASIS_DEFAULTS = (1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0)

def _curve_matrix_action(helper_obj):
    if not (helper_obj and helper_obj.animation_data and helper_obj.animation_data.action):
        return None
    return helper_obj.animation_data.action

def _invalidate_curve_matrix_sampler(helper_obj):
    act = _curve_matrix_action(helper_obj)
    if act:
        _CURVE_MATRIX_SAMPLER_CACHE.pop(act.as_pointer(), None)

def _ensure_curve_matrix_basis_props(helper_obj):
    for name, default in zip(_CURVE_MATRIX_BASIS_PROP_NAMES, _CURVE_MATRIX_BASIS_DEFAULTS):
        if name not in helper_obj:
            helper_obj[name] = default

def _ensure_scalar_fcurve(act, data_path):
    fcu = act.fcurves.find(data_path)
    if fcu:
        return fcu
    return act.fcurves.new(data_path)

def _remove_curve_matrix_legacy_quaternion_fcurves(act):
    for index in range(4):
        fcu = act.fcurves.find("rotation_quaternion", index=index)
        if fcu:
            act.fcurves.remove(fcu)

def _set_fcurve_keys_fast(fcu, frames, values):
    fcu.keyframe_points.clear()
    count = len(frames)
    if count <= 0:
        fcu.update()
        return
    fcu.keyframe_points.add(count)
    co = np.empty(count * 2, dtype=np.float32)
    co[0::2] = np.asarray(frames, dtype=np.float32)
    co[1::2] = np.asarray(values, dtype=np.float32)
    fcu.keyframe_points.foreach_set("co", co)
    for kp in fcu.keyframe_points:
        kp.interpolation = 'BEZIER'
        kp.handle_left_type = "LINEAR_X"
        kp.handle_right_type = "LINEAR_X"
    fcu.update()

def _set_curve_matrix_keys_fast(curves, frames, samples_by_channel):
    for key, fcu in curves.items():
        _set_fcurve_keys_fast(fcu, frames, samples_by_channel[key])

class _CurveMatrixSampler:
    def __init__(self, helper_obj):
        act = _curve_matrix_action(helper_obj)
        if not act:
            raise RuntimeError("CurveMatrix helper has no action")
        self.loc = [self._read_fcurve(act.fcurves.find("location", index=i)) for i in range(3)]
        self.basis = [self._read_fcurve(act.fcurves.find(path)) for path in _CURVE_MATRIX_BASIS_DATA_PATHS]
        self.legacy_rot = [self._read_fcurve(act.fcurves.find("rotation_quaternion", index=i)) for i in range(4)]
        self.scl = [self._read_fcurve(act.fcurves.find("scale", index=i)) for i in range(3)]

    @staticmethod
    def _read_fcurve(fcu):
        if not fcu or len(fcu.keyframe_points) == 0:
            return None
        count = len(fcu.keyframe_points)
        times = np.empty(count, dtype=np.float64)
        values = np.empty(count, dtype=np.float64)
        handle_right = np.empty(count, dtype=np.float64)
        handle_left = np.empty(count, dtype=np.float64)
        for i, kp in enumerate(fcu.keyframe_points):
            times[i] = float(kp.co.x)
            values[i] = float(kp.co.y)
            handle_right[i] = float(kp.handle_right.y)
            handle_left[i] = float(kp.handle_left.y)
        return times, values, handle_left, handle_right

    @staticmethod
    def _sample_channel(data, frames, default_value=0.0):
        frames = np.asarray(frames, dtype=np.float64)
        if data is None:
            return np.full(frames.shape, default_value, dtype=np.float64)
        times, values, handle_left, handle_right = data
        count = len(times)
        if count == 1:
            return np.full(frames.shape, values[0], dtype=np.float64)

        idx = np.searchsorted(times, frames, side="right") - 1
        idx = np.clip(idx, 0, count - 2)
        t0 = times[idx]
        t1 = times[idx + 1]
        dt = t1 - t0
        u = np.divide(frames - t0, dt, out=np.zeros_like(frames, dtype=np.float64), where=np.abs(dt) > 1.0e-9)
        u = np.clip(u, 0.0, 1.0)
        omt = 1.0 - u
        p0 = values[idx]
        p1 = handle_right[idx]
        p2 = handle_left[idx + 1]
        p3 = values[idx + 1]
        return (
            p0 * (omt ** 3) +
            p1 * (3.0 * omt * omt * u) +
            p2 * (3.0 * omt * u * u) +
            p3 * (u ** 3)
        )

    def sample(self, t_values_1d):
        t_values_1d = np.asarray(t_values_1d, dtype=np.float64)
        frames = t_values_1d * 100.0
        positions = np.stack([self._sample_channel(channel, frames) for channel in self.loc], axis=-1)
        scales = np.stack([
            self._sample_channel(self.scl[0], frames, 1.0),
            self._sample_channel(self.scl[1], frames, 1.0),
            self._sample_channel(self.scl[2], frames, 1.0),
        ], axis=-1)

        if all(channel is not None for channel in self.basis):
            basis_values = [self._sample_channel(channel, frames) for channel in self.basis]
            basis_mats = np.empty((len(t_values_1d), 3, 3), dtype=np.float64)
            basis_mats[:, 0, 0] = basis_values[0]
            basis_mats[:, 1, 0] = basis_values[1]
            basis_mats[:, 2, 0] = basis_values[2]
            basis_mats[:, 0, 1] = basis_values[3]
            basis_mats[:, 1, 1] = basis_values[4]
            basis_mats[:, 2, 1] = basis_values[5]
            basis_mats[:, 0, 2] = basis_values[6]
            basis_mats[:, 1, 2] = basis_values[7]
            basis_mats[:, 2, 2] = basis_values[8]
            return positions, basis_mats, scales

        quaternions = np.stack([
            self._sample_channel(self.legacy_rot[0], frames, 1.0),
            self._sample_channel(self.legacy_rot[1], frames),
            self._sample_channel(self.legacy_rot[2], frames),
            self._sample_channel(self.legacy_rot[3], frames),
        ], axis=-1)
        norms = np.linalg.norm(quaternions, axis=1, keepdims=True)
        norms[norms == 0.0] = 1.0
        quaternions /= norms
        return positions, quaternions_to_rotation_matrices_numpy(quaternions), scales

def _curve_matrix_sampler(helper_obj):
    act = _curve_matrix_action(helper_obj)
    if not act:
        return None
    key = act.as_pointer()
    sampler = _CURVE_MATRIX_SAMPLER_CACHE.get(key)
    if sampler is None:
        sampler = _CurveMatrixSampler(helper_obj)
        _CURVE_MATRIX_SAMPLER_CACHE[key] = sampler
    return sampler

class MXTRoad_OT_GenerateCurveMatrix(Operator):
    bl_idname = "mxt_road.generate_curve_matrix"
    bl_label  = "Generate CurveMatrix"
    bl_options = {'REGISTER', 'UNDO'}
    @staticmethod
    def _control_points(parent_obj):
        cps = [c for c in parent_obj.children
            if c.type == 'EMPTY'
            and getattr(c, "mxt_cp_data", None)
            and c.mxt_cp_data.is_mxt_control_point]
        cps.sort(key=lambda cp: cp.mxt_cp_data.time)
        return cps
    @staticmethod
    def _signed_angle(v_from: Vector, v_to: Vector, axis: Vector) -> float:
        cross_to = v_from.cross(v_to)
        unsigned = math.atan2(cross_to.length, max(v_from.dot(v_to), -1.0))
        sign     = cross_to.dot(axis)
        return -unsigned if sign < 0.0 else unsigned
    @staticmethod
    def _eval_channel(cp_empty, channel_name, t_norm):
        ad = cp_empty.animation_data
        if not (ad and ad.action):
            return t_norm
        fcu = ad.action.fcurves.find(f"mxt_cp_data.{channel_name}")
        return _evaluate_fcurve_continued(fcu, t_norm * 100) if fcu else t_norm
    @staticmethod
    def _bezier_pos(p0, p1, p2, p3, t):
        omt = 1.0 - t
        return (p0 * (omt**3) +
            p1 * (3 * omt**2 * t) +
            p2 * (3 * omt * t**2) +
            p3 * t**3)
    @staticmethod
    def _quat_from_to(v_from, v_to):
        try:
            return v_from.rotation_difference(v_to)
        except ZeroDivisionError:
            alt_axis = Vector((1,0,0)) if abs(v_from.x) < .9 else Vector((0,1,0))
            return Quaternion(alt_axis, math.pi)
    @staticmethod
    def bake_for_parent_bezier(road_parent, *, report_fn=None):
        if not road_parent:
            if report_fn: report_fn({'ERROR'}, "No road‑segment parent")
            return False
        cps = MXTRoad_OT_GenerateCurveMatrix._control_points(road_parent)
        if len(cps) < 2:
            if report_fn: report_fn({'ERROR'}, "Need at least two control points")
            return False
        helper = road_parent.mxt_road_overall_props.curve_matrix_helper_empty
        if not helper:
            if report_fn: report_fn({'ERROR'}, "CurveMatrix helper empty not set")
            return False
        subdiv = 64
        t_samples = []
        t_samples.append(0.0)
        for i in range(len(cps) - 1):
            t0, t1 = cps[i].mxt_cp_data.time, cps[i+1].mxt_cp_data.time
            step = (t1 - t0) / subdiv
            for n in range(subdiv):
                t_samples.append(t0 + n * step)
        t_samples.append(1.0)
        if not helper.animation_data:
            helper.animation_data_create()
        act = helper.animation_data.action or \
            bpy.data.actions.new(f"{helper.name}_CurveMatrix")
        helper.animation_data.action = act
        _ensure_curve_matrix_basis_props(helper)
        _remove_curve_matrix_legacy_quaternion_fcurves(act)
        curves = {
            ("location",i): _ensure_fcurve(act, "location", i)        for i in range(3)}
        curves |= {("basis",i): _ensure_scalar_fcurve(act, _CURVE_MATRIX_BASIS_DATA_PATHS[i]) for i in range(9)}
        curves |= {("scale",i): _ensure_fcurve(act, "scale", i)      for i in range(3)}


        for fcu in curves.values():
            fcu.keyframe_points.clear()
        rot_mode = road_parent.mxt_road_overall_props.rotation_mode
        def eval_bezier_sample(t):
            if t <= cps[1].mxt_cp_data.time:
                span = 0
            elif t >= cps[-2].mxt_cp_data.time:
                span = len(cps) - 2
            else:
                span = next(i for i in range(len(cps)-1) if t <= cps[i+1].mxt_cp_data.time)
            a, b = cps[span], cps[span+1]
            span_len = b.mxt_cp_data.time - a.mxt_cp_data.time
            if span_len <= 1e-12:
                return a.location.copy(), a.rotation_euler.to_quaternion(), a.scale.copy()
            bt = (t - a.mxt_cp_data.time) / span_len
            az = (a.rotation_euler.to_matrix().col[2]).normalized()
            bz = (b.rotation_euler.to_matrix().col[2]).normalized()
            p0 = a.location
            p1 = p0 + az * a.mxt_cp_data.handle_out_length
            p3 = b.location
            p2 = p3 - bz * b.mxt_cp_data.handle_in_length
            pos = MXTRoad_OT_GenerateCurveMatrix._bezier_pos(p0, p1, p2, p3, bt)
            dp = (
                3.0 * (1 - bt)**2 * (p1 - p0) +
                6.0 * (1 - bt) * bt * (p2 - p1) +
                3.0 * bt**2 * (p3 - p2)
            )
            forward_dir = dp.normalized()
            rot_fac   = MXTRoad_OT_GenerateCurveMatrix._eval_channel(a, "rotation_ease_factor_channel", bt)
            scale_fac = MXTRoad_OT_GenerateCurveMatrix._eval_channel(a, "scale_ease_factor_channel", bt)
            ra = a.rotation_euler.to_quaternion()
            rb = b.rotation_euler.to_quaternion()
            if rot_mode == 'SIMPLE':
                final_rot = ra.slerp(rb, rot_fac)
            else:
                twist_fac = MXTRoad_OT_GenerateCurveMatrix._eval_channel(a, "twist_ease_factor_channel", bt)
                z_start       = ra @ Vector((0, 0, 1))
                q_to_fwd      = MXTRoad_OT_GenerateCurveMatrix._quat_from_to(
                                   z_start, forward_dir)
                rot_fwd_start = q_to_fwd @ ra
                z_end         = rb @ Vector((0, 0, 1))
                q_align_end   = MXTRoad_OT_GenerateCurveMatrix._quat_from_to(
                                   z_start, z_end)
                rot_fwd_end   = q_align_end @ ra
                z_fwd_end_fix = rot_fwd_end @ Vector((0, 0, 1))
                q_fix_end     = MXTRoad_OT_GenerateCurveMatrix._quat_from_to(
                                   z_fwd_end_fix, z_end)
                rot_fwd_end   = q_fix_end @ rot_fwd_end
                y_fixed = rot_fwd_end @ Vector((0, 1, 0))
                y_real  = rb          @ Vector((0, 1, 0))
                axis_z  = z_end
                twist_end = MXTRoad_OT_GenerateCurveMatrix._signed_angle(
                               y_fixed, y_real, axis_z)
                twist_cur = twist_end * twist_fac
                q_twist   = Quaternion(forward_dir, twist_cur)
                final_rot = q_twist @ rot_fwd_start
            scale = a.scale.lerp(b.scale, scale_fac)
            return pos, final_rot.to_matrix(), scale

        frames = [float(t) * 100.0 for t in t_samples]
        samples_by_channel = {key: [] for key in curves.keys()}
        for t in t_samples:
            pos, basis, scale = eval_bezier_sample(t)
            values = _curve_matrix_channel_values(pos, basis, scale)
            for key, value in values.items():
                samples_by_channel[key].append(float(value))
        _set_curve_matrix_keys_fast(curves, frames, samples_by_channel)
        for fc in act.fcurves:
            for kp in fc.keyframe_points:
                kp.interpolation = 'BEZIER'
        _linearize_curve_matrix_handles_with_extension(curves, t_samples)
        for fc in act.fcurves:
            fc.update()
        _invalidate_curve_matrix_sampler(helper)
        if isinstance(MXTRoad_OT_GenerateCurveMatrix, Operator):
            if report_fn: report_fn({'INFO'}, f"Baked {len(t_samples)} keys with {rot_mode.lower()} rotation.")
        return True
    @staticmethod
    def _auto_calc_line_easing(start_point, start_quat, end_quat, start_scl, end_scl):
        act = start_point.animation_data.action
        fcu_rot = act.fcurves.find('mxt_line_handle_data.rotation_ease_factor_channel')
        fcu_scl = act.fcurves.find('mxt_line_handle_data.scale_ease_factor_channel')
        if not (fcu_rot and fcu_scl):
            return
        fcu_rot.keyframe_points.clear()
        fcu_scl.keyframe_points.clear()
        from mathutils import Vector
        axis = Vector((1, 0, 0))
        vec0 = start_quat @ axis * start_scl.x
        vec1 = end_quat   @ axis * end_scl.x
        total_angle = vec0.angle(vec1)
        subdiv = 32
        for i in range(subdiv + 1):
            t = i / subdiv
            frame = t * 100.0
            vec_t = vec0.lerp(vec1, t)
            scl_ease = (vec_t.length - start_scl.x) / (end_scl.x - start_scl.x) if end_scl.x != start_scl.x else t
            angle_t = vec0.angle(vec_t)
            rot_ease = angle_t / total_angle if total_angle != 0.0 else 0.0
            fcu_rot.keyframe_points.insert(frame, rot_ease)
            fcu_scl.keyframe_points.insert(frame, scl_ease)
        _linearize_fcurve_handles_smooth(fcu_rot)
        _linearize_fcurve_handles_smooth(fcu_scl)
    @staticmethod
    def bake_for_parent_line(road_parent, *, report_fn=None):
        props = road_parent.mxt_road_overall_props
        start_point, end_point = props.line_start_point, props.line_end_point

        if not (start_point and end_point and hasattr(start_point, "mxt_line_handle_data")):
            if report_fn: report_fn({'ERROR'}, "Line segment is not set up correctly for easing.")
            return False

        helper = props.curve_matrix_helper_empty
        if not helper:
            if report_fn: report_fn({'ERROR'}, "CurveMatrix helper empty not set.")
            return False

        if not helper.animation_data: helper.animation_data_create()
        act = helper.animation_data.action or bpy.data.actions.new(f"{helper.name}_CurveMatrix")
        helper.animation_data.action = act
        act.fcurves.clear()
        _ensure_curve_matrix_basis_props(helper)
        curves = {("location",i): _ensure_fcurve(act, "location", i) for i in range(3)}
        curves |= {("basis",i): _ensure_scalar_fcurve(act, _CURVE_MATRIX_BASIS_DATA_PATHS[i]) for i in range(9)}
        curves |= {("scale",i): _ensure_fcurve(act, "scale", i) for i in range(3)}


        easing_action = start_point.animation_data.action
        fcu_rot_ease = easing_action.fcurves.find('mxt_line_handle_data.rotation_ease_factor_channel')
        fcu_scl_ease = easing_action.fcurves.find('mxt_line_handle_data.scale_ease_factor_channel')

        if not (fcu_rot_ease and fcu_scl_ease):
            if report_fn: report_fn({'ERROR'}, "Easing F-Curves not found on Line Start Point.")
            return False

        start_loc, end_loc = start_point.location.copy(), end_point.location.copy()
        start_quat, end_quat = start_point.rotation_quaternion.copy(), end_point.rotation_quaternion.copy()
        start_scl, end_scl = start_point.scale.copy(), end_point.scale.copy()

        subdiv = 64
        t_samples = [i/subdiv for i in range(subdiv + 1)]
        MXTRoad_OT_GenerateCurveMatrix._auto_calc_line_easing(start_point, start_quat, end_quat, start_scl, end_scl)
        def eval_line_sample(t):
            frame = t * 100.0
            pos = start_loc.lerp(end_loc, t)
            rot_t = _evaluate_fcurve_continued(fcu_rot_ease, frame)
            scl_t = _evaluate_fcurve_continued(fcu_scl_ease, frame)
            rot = start_quat.slerp(end_quat, rot_t)
            scl = start_scl.lerp(end_scl, scl_t)
            return pos, rot.to_matrix(), scl

        frames = [float(t) * 100.0 for t in t_samples]
        samples_by_channel = {key: [] for key in curves.keys()}
        for t in t_samples:
            pos, basis, scl = eval_line_sample(t)
            values = _curve_matrix_channel_values(pos, basis, scl)
            for key, value in values.items():
                samples_by_channel[key].append(float(value))
        _set_curve_matrix_keys_fast(curves, frames, samples_by_channel)

        _linearize_curve_matrix_handles_with_extension(curves, t_samples)
        for fc in curves.values():
            fc.update()
        _invalidate_curve_matrix_sampler(helper)

        if report_fn: report_fn({'INFO'}, f"Baked Eased Line segment with {len(t_samples)} keys.")
        return True

    @staticmethod
    def bake_for_parent_spiral(road_parent, *, report_fn=None):
        import math

        props = road_parent.mxt_road_overall_props
        spiral_helper = props.spiral_helper
        axis_helper = props.spiral_axis_helper
        if not (spiral_helper and spiral_helper.animation_data
                and spiral_helper.animation_data.action):
            if report_fn: report_fn({'ERROR'}, "Spiral helper / curves missing.")
            return False
        if not axis_helper:
            if report_fn: report_fn({'ERROR'}, "Assign a Spiral Axis Helper.")
            return False

        act_s = spiral_helper.animation_data.action
        fcu_rad = act_s.fcurves.find("location", index=0)
        fcu_h = act_s.fcurves.find("location", index=1)
        fcu_tw = act_s.fcurves.find("location", index=2)
        fcu_sx = act_s.fcurves.find("scale", index=0)
        fcu_sy = act_s.fcurves.find("scale", index=1)
        if not (fcu_rad and fcu_h and fcu_tw and fcu_sx and fcu_sy):
            if report_fn: report_fn({'ERROR'}, "Spiral helper missing curves.")
            return False

        cm = props.curve_matrix_helper_empty
        if not cm:
            if report_fn: report_fn({'ERROR'}, "CurveMatrix helper not set.")
            return False
        if not cm.animation_data: cm.animation_data_create()

        act_cm = cm.animation_data.action or bpy.data.actions.new(
            f"{cm.name}_CurveMatrix")
        cm.animation_data.action = act_cm
        act_cm.fcurves.clear()
        _ensure_curve_matrix_basis_props(cm)
        curves = {("location", i): _ensure_fcurve(act_cm, "location", i)
                  for i in range(3)}
        curves |= {("basis", i): _ensure_scalar_fcurve(
            act_cm, _CURVE_MATRIX_BASIS_DATA_PATHS[i]) for i in range(9)}
        curves |= {("scale", i): _ensure_fcurve(act_cm, "scale", i)
                   for i in range(3)}


        axis_vec = Vector(props.spiral_axis).normalized()

        def canon_matrix(t):
            frame = t * 100.0

            r = _evaluate_fcurve_continued(fcu_rad, frame)
            h = _evaluate_fcurve_continued(fcu_h, frame)

            ang = math.radians(props.spiral_degrees) * t

            about = Vector((axis_vec.y, -axis_vec.x, 0.0)) * r
            p = -about

            qr = Quaternion(axis_vec, ang)
            p = qr @ p

            basis = qr.to_matrix()

            q_tw = Quaternion(basis.col[2], math.radians(_evaluate_fcurve_continued(fcu_tw, frame)))
            basis = (q_tw.to_matrix() @ basis)



            eps = 0.001
            frame_eps = (t + eps) * 100.0
            r2 = _evaluate_fcurve_continued(fcu_rad, frame_eps)
            h2 = _evaluate_fcurve_continued(fcu_h, frame_eps)
            ang2 = math.radians(props.spiral_degrees) * (t + eps)
            about2 = Vector((axis_vec.y, -axis_vec.x, 0.0)) * r2
            p2 = -(Quaternion(axis_vec, ang2) @ about2) + axis_vec * h2
            tangent = (p2 - (p + axis_vec * h)).normalized()

            current_z = (basis.col[2]).normalized()
            axis_x = basis.col[0].normalized()


            z_proj = current_z - axis_x * current_z.dot(axis_x)
            tan_proj = tangent - axis_x * tangent.dot(axis_x)
            z_proj.normalize()
            tan_proj.normalize()

            angle = z_proj.angle(tan_proj)
            if z_proj.cross(tan_proj).dot(axis_x) < 0:
                angle = -angle


            q_adj = Quaternion(axis_x, angle)
            basis = (q_adj.to_matrix() @ basis)



            m = basis.to_4x4()
            m.translation = p + axis_vec * h

            m = Matrix.Translation(m.translation) @ basis.to_4x4()
            return m

        subdiv = 64
        ts = [0.0] + [i / subdiv for i in range(1, subdiv)] + [1.0]

        raw_transforms = []
        raw_s = []
        last_locked = axis_vec

        for t in ts:
            frame = t * 100.0
            trans = canon_matrix(t)
            raw_transforms.append(trans)
            raw_s.append(Vector((
                _evaluate_fcurve_continued(fcu_sx, frame),
                _evaluate_fcurve_continued(fcu_sy, frame),
                1.0)))

        Mcorr = axis_helper.matrix_local @ raw_transforms[0].inverted()
        basis_corr = Mcorr.to_3x3()


        def eval_spiral_sample(t):
            frame = t * 100.0
            trans = canon_matrix(t)
            pos = Mcorr @ trans.translation
            basis = basis_corr @ trans.to_3x3()
            scl = Vector((
                _evaluate_fcurve_continued(fcu_sx, frame),
                _evaluate_fcurve_continued(fcu_sy, frame),
                1.0))
            return pos, basis, scl

        frames = [float(t) * 100.0 for t in ts]
        samples_by_channel = {key: [] for key in curves.keys()}
        for i, t in enumerate(ts):
            pos, basis, scl = eval_spiral_sample(t)
            values = _curve_matrix_channel_values(pos, basis, scl)
            for key, value in values.items():
                samples_by_channel[key].append(float(value))
        _set_curve_matrix_keys_fast(curves, frames, samples_by_channel)

        _linearize_curve_matrix_handles_with_extension(curves, ts)
        for fcu in curves.values():
            fcu.update()
        _invalidate_curve_matrix_sampler(cm)

        if report_fn:
            report_fn({'INFO'}, f"Baked spiral with axis‑locked orientation ({len(ts)} keys).")
        return True


    @staticmethod
    def bake_for_parent(road_parent, *, report_fn=None):
        props = road_parent.mxt_road_overall_props
        if props.segment_type == 'BEZIER':
            return MXTRoad_OT_GenerateCurveMatrix.bake_for_parent_bezier(road_parent, report_fn=report_fn)
        elif props.segment_type == 'LINE':
            return MXTRoad_OT_GenerateCurveMatrix.bake_for_parent_line(road_parent, report_fn=report_fn)
        elif props.segment_type == 'SPIRAL':
            return MXTRoad_OT_GenerateCurveMatrix.bake_for_parent_spiral(road_parent, report_fn=report_fn)

        if report_fn: report_fn({'ERROR'}, f"Unknown segment type: {props.segment_type}")
        return False

    def execute(self, context):
        road_parent = get_active_mxt_road_segment_parent(context)
        if not road_parent:
            self.report({'ERROR'}, "Select an MXT road-segment parent")
            return {'CANCELLED'}

        with _mxt_profile_scope(f"manual_bake_curvematrix {road_parent.name}"):
            ok = MXTRoad_OT_GenerateCurveMatrix.bake_for_parent(road_parent, report_fn=self.report)


        if ok:
            schedule_mesh_build(road_parent)

        return {'FINISHED'} if ok else {'CANCELLED'}
def _surface(helper, tx, ty, seg_len, shape):
    eps  = 0.0005
    base = Vector((tx, ty))
    p0   = shape.get_pos(helper, base)
    if p0 is None:
        return None
    pr = shape.get_pos(helper, base - Vector((eps, 0)))
    pl = shape.get_pos(helper, base - Vector((-eps, 0)))



    seg_parent = helper.parent
    cps = get_mxt_control_point_empties(seg_parent, sorted_by_time=True)
    if len(cps) < 2:
        step_y = eps
    else:

        if ty <= 0: a, b = cps[0], cps[1]
        elif ty >= 1: a, b = cps[-2], cps[-1]
        else:
            a_idx = max(i for i in range(len(cps)-1) if ty >= cps[i].mxt_cp_data.time)
            a, b = cps[a_idx], cps[a_idx+1]
        span_len = (b.mxt_cp_data.time - a.mxt_cp_data.time) or 1e-6
        bt = (ty - a.mxt_cp_data.time) / span_len
        p0_c = a.location
        p3_c = b.location
        z_a = a.rotation_euler.to_matrix().col[2].normalized()
        z_b = b.rotation_euler.to_matrix().col[2].normalized()
        p1_c = p0_c + z_a * a.mxt_cp_data.handle_out_length
        p2_c = p3_c - z_b * b.mxt_cp_data.handle_in_length


        dp = (
            3.0 * (1-bt)**2 * (p1_c - p0_c) +
            6.0 * (1-bt) * bt * (p2_c - p1_c) +
            3.0 * bt**2 * (p3_c - p2_c)
        )

        step_y = 1.0 / (dp.length + 1e-8)
    pr = shape.get_pos(helper, base - Vector((eps, 0)))
    pf = shape.get_pos(helper, base - Vector((0, step_y)))

    pl = shape.get_pos(helper, base + Vector((eps, 0)))
    pb = shape.get_pos(helper, base + Vector((0, step_y)))
    props = seg_parent.mxt_road_overall_props
    normal1 = ((pr - p0).cross(pf - p0)).normalized()
    normal2 = ((pl - p0).cross(pb - p0)).normalized()
    return p0, ((normal1 + normal2) * 0.5)

def _cubic(p0, p1, p2, p3, t: float):
    omt = 1.0 - t
    return (p0 * (omt**3) +
            p1 * (3*omt*omt*t) +
            p2 * (3*omt*t*t) +
            p3 * (t**3))
def _centerline_pos(seg_parent, ty: float):
    cps = get_mxt_control_point_empties(seg_parent, sorted_by_time=True)
    if len(cps) < 2:
        return seg_parent.location



    a_i = 0
    if ty >= 1.0:

        a_i = len(cps) - 2
    elif ty > 0.0:


        a_i = max(i for i in range(len(cps) - 1) if ty >= cps[i].mxt_cp_data.time)



    a = cps[a_i]
    b = cps[a_i+1]

    span_len = (b.mxt_cp_data.time - a.mxt_cp_data.time) or 1e-6

    bt = (ty - a.mxt_cp_data.time) / span_len

    p0 = a.location
    p3 = b.location
    z_a = a.rotation_euler.to_matrix().col[2].normalized()
    z_b = b.rotation_euler.to_matrix().col[2].normalized()
    p1 = p0 + z_a * a.mxt_cp_data.handle_out_length
    p2 = p3 - z_b * b.mxt_cp_data.handle_in_length

    return _cubic(p0, p1, p2, p3, bt)

def quaternions_to_rotation_matrices_numpy(q):
    w, x, y, z = q[:, 0], q[:, 1], q[:, 2], q[:, 3]
    N = q.shape[0]
    R = np.empty((N, 3, 3), dtype=np.float64)

    R[:, 0, 0] = 1 - 2*y**2 - 2*z**2
    R[:, 0, 1] = 2*x*y - 2*z*w
    R[:, 0, 2] = 2*x*z + 2*y*w
    R[:, 1, 0] = 2*x*y + 2*z*w
    R[:, 1, 1] = 1 - 2*x**2 - 2*z**2
    R[:, 1, 2] = 2*y*z - 2*x*w
    R[:, 2, 0] = 2*x*z - 2*y*w
    R[:, 2, 1] = 2*y*z + 2*x*w
    R[:, 2, 2] = 1 - 2*x**2 - 2*y**2
    return R

def _evaluate_modulation_numpy(props, ty_1d):
    num_y = len(ty_1d)
    total_offset = np.zeros(num_y, dtype=np.float64)
    frames = ty_1d * 100.0

    for mod in props.modulations:
        helper = mod.helper
        if not (helper and helper.animation_data and helper.animation_data.action):
            continue

        act = helper.animation_data.action
        f_h = act.fcurves.find("location", index=1)
        f_e = act.fcurves.find("location", index=2)
        if not (f_h and f_e):
            continue


        height_vals = np.array([f_h.evaluate(f) for f in frames], dtype=np.float64)
        effect_vals = np.array([f_e.evaluate(f) for f in frames], dtype=np.float64)
        total_offset += height_vals * effect_vals

    return total_offset

def _calculate_vertex_positions_numpy(props, centerline_pos, centerline_basis, centerline_scl, tx_grid, ty_grid):
    num_y, num_x = tx_grid.shape
    ty_1d = ty_grid[:, 0]


    total_mod_offset_grid = np.zeros((num_y, num_x), dtype=np.float64)


    mod_t_grid = 0.5 * (1.0 - tx_grid)

    for mod in props.modulations:
        helper = mod.helper
        if not (helper and helper.animation_data and helper.animation_data.action):
            continue

        act = helper.animation_data.action
        f_h = act.fcurves.find("location", index=1)
        f_e = act.fcurves.find("location", index=2)
        if not (f_h and f_e):
            continue



        effect_frames = ty_1d * 100.0
        effect_vals_1d = np.array([f_e.evaluate(f) for f in effect_frames], dtype=np.float64)

        if np.allclose(mod_t_grid, mod_t_grid[0:1], rtol=0.0, atol=1.0e-12):
            height_frames = mod_t_grid[0] * 100.0
            height_vals_grid = np.array([f_h.evaluate(f) for f in height_frames], dtype=np.float64).reshape(1, num_x)
        else:
            height_frames = mod_t_grid.ravel() * 100.0
            height_vals_grid = np.array([f_h.evaluate(f) for f in height_frames], dtype=np.float64).reshape(num_y, num_x)


        mod_grid = effect_vals_1d[:, np.newaxis] * height_vals_grid
        total_mod_offset_grid += mod_grid


    local_space_offsets = np.zeros((num_y, num_x, 3), dtype=np.float64)


    shape_type = props.road_shape_type
    angle_tx_grid = tx_grid

    if shape_type in ('CYLINDER_OPEN', 'PIPE_OPEN', 'ROUNDED_SQUARE_OPEN'):
        open_vals_1d = np.ones(num_y, dtype=np.float64)
        helper = props.openness_helper
        if helper and helper.animation_data and helper.animation_data.action:
            fcu = helper.animation_data.action.fcurves.find("location", index=0)
            if fcu:
                frames = ty_1d * 100.0
                open_vals_1d = np.array([fcu.evaluate(f) for f in frames], dtype=np.float64)
        angle_tx_grid = tx_grid * open_vals_1d.reshape(num_y, 1)
        # For Open Rounded Square only, apply seam rotation from Y-location fcurve
        if shape_type == 'ROUNDED_SQUARE_OPEN' and helper and helper.animation_data and helper.animation_data.action:
            fcur = helper.animation_data.action.fcurves.find("location", index=1)
            if fcur:
                rot_vals_1d = np.array([fcur.evaluate(f) for f in frames], dtype=np.float64)
                angle_tx_grid = angle_tx_grid + rot_vals_1d.reshape(num_y, 1)
                # wrap to [-1, 1]
                angle_tx_grid = np.where(angle_tx_grid > 1.0, angle_tx_grid - 2.0, angle_tx_grid)
                angle_tx_grid = np.where(angle_tx_grid < -1.0, angle_tx_grid + 2.0, angle_tx_grid)

    if shape_type in ('ROUNDED_SQUARE', 'ROUNDED_SQUARE_OPEN'):
        width_vals_1d = np.ones(num_y, dtype=np.float64)
        height_vals_1d = np.ones(num_y, dtype=np.float64)
        radius_vals_1d = np.zeros(num_y, dtype=np.float64)
        helpers = [props.width_helper, props.height_helper, props.radius_helper]
        arrays = [width_vals_1d, height_vals_1d, radius_vals_1d]
        for arr, helper in zip(arrays, helpers):
            if helper and helper.animation_data and helper.animation_data.action:
                fcu = helper.animation_data.action.fcurves.find("location", index=0)
                if fcu:
                    frames = ty_1d * 100.0
                    arr[:] = np.array([fcu.evaluate(f) for f in frames], dtype=np.float64)
        width_grid = width_vals_1d.reshape(num_y,1)
        height_grid = height_vals_1d.reshape(num_y,1)
        radius_grid = radius_vals_1d.reshape(num_y,1)


    if shape_type in ('FLAT', 'TUNNEL'):
        local_space_offsets[..., 0] = tx_grid
        local_space_offsets[..., 1] = total_mod_offset_grid

    else:
        if shape_type in ('CYLINDER', 'CYLINDER_OPEN'):
            angle = angle_tx_grid * np.pi
            radial_x, radial_y = np.sin(angle), np.cos(angle)
            radius = 1.0 + total_mod_offset_grid
            local_space_offsets[..., 0] = radial_x * radius
            local_space_offsets[..., 1] = radial_y * radius
        elif shape_type in ('ROUNDED_SQUARE', 'ROUNDED_SQUARE_OPEN'):
            angle = (2.0 - angle_tx_grid) * np.pi
            dir_x = -np.sin(angle)
            dir_y = -np.cos(angle)

            w2        = width_grid * 0.5
            h2        = height_grid * 0.5
            radius_cl = np.clip(radius_grid, 0.0, np.minimum(w2, h2))

            # Ensure w2, h2, radius_cl match shape of angle
            w2 = np.broadcast_to(w2, angle.shape)
            h2 = np.broadcast_to(h2, angle.shape)
            radius_cl = np.broadcast_to(radius_cl, angle.shape)

            abs_dx = np.abs(dir_x)
            abs_dy = np.abs(dir_y)

            # --- vertical side candidates ------------------------------------------------
            t_v = np.divide(w2, abs_dx, out=np.full_like(w2, np.inf), where=abs_dx > 1.0e-9)
            y_v = t_v * abs_dy
            vert_ok = y_v <= (h2 - radius_cl)

            # --- horizontal side candidates ---------------------------------------------
            t_h = np.divide(h2, abs_dy, out=np.full_like(h2, np.inf), where=abs_dy > 1.0e-9)
            x_h = t_h * abs_dx
            horiz_ok = x_h <= (w2 - radius_cl)

            t_side = np.where(vert_ok & (t_v <= t_h), t_v,
                     np.where(horiz_ok,                      t_h, np.inf))

            # --- corner circle candidates ------------------------------------------------
            w_in = np.maximum(w2 - radius_cl, 0.0)
            h_in = np.maximum(h2 - radius_cl, 0.0)

            b      = -2.0 * (abs_dx * w_in + abs_dy * h_in)
            c      = w_in ** 2 + h_in ** 2 - radius_cl ** 2
            disc   = b ** 2 - 4.0 * c
            sqrt_d = np.sqrt(np.maximum(disc, 0.0))

            root1  = (-b - sqrt_d) * 0.5
            root2  = (-b + sqrt_d) * 0.5
            root   = np.where((root1 > 0.0) & (root1 * abs_dx >= w_in) & (root1 * abs_dy >= h_in),
                              root1, np.inf)
            root   = np.where((root2 > 0.0) & (root2 * abs_dx >= w_in) & (root2 * abs_dy >= h_in) &
                              (root2 < root), root2, root)

            t_final = np.minimum(t_side, root)
            t_final = np.where(np.isfinite(t_final), t_final, np.minimum(w2, h2))

            scale = 1.0 + total_mod_offset_grid

            # ---- assign to final local space offsets ----
            local_space_offsets[..., 0] = dir_x * t_final * scale
            local_space_offsets[..., 1] = dir_y * t_final * scale
        else:
            angle = (angle_tx_grid - 0.5) * np.pi
            radial_x, radial_y = np.cos(angle), np.sin(angle)
            radius = 1.0 + total_mod_offset_grid
            local_space_offsets[..., 0] = radial_x * radius
            local_space_offsets[..., 1] = radial_y * radius


    cl_pos = centerline_pos[:, np.newaxis, :]
    cl_scl = centerline_scl[:, np.newaxis, :]
    cl_rot = centerline_basis[:, np.newaxis, :, :]
    scaled_offsets = local_space_offsets * cl_scl
    rotated_offsets = np.einsum('yxij,yxj->yxi', cl_rot, scaled_offsets)
    final_positions = cl_pos + rotated_offsets

    return final_positions
