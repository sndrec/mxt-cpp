import json
import math
import sys
from pathlib import Path

import bpy
from mathutils import Quaternion, Vector


def vec(values):
    return Vector((float(values[0]), float(values[1]), float(values[2])))


def safe_normal(v, fallback):
    if v.length_squared < 1.0e-10:
        return fallback.normalized()
    return v.normalized()


def sample_group_t(group, index):
    if len(group) <= 1:
        return 0.0
    first = float(group[0].get("curve_time", 0.0))
    last = float(group[-1].get("curve_time", len(group) - 1))
    span = last - first
    if abs(span) <= 1.0e-6:
        return index / (len(group) - 1)
    return max(0.0, min(1.0, (float(group[index].get("curve_time", index)) - first) / span))


def sample_frame(sample):
    gx_right = safe_normal(vec(sample["basis_right"]), Vector((1.0, 0.0, 0.0)))
    gx_up = safe_normal(vec(sample["basis_up"]), Vector((0.0, 1.0, 0.0)))
    gx_forward = safe_normal(vec(sample["basis_forward"]), Vector((0.0, 0.0, 1.0)))
    helper_right = safe_normal(gx_up.cross(gx_forward), gx_right)
    helper_up = safe_normal(gx_forward.cross(helper_right), gx_up)
    x_sign = -1.0 if gx_right.dot(helper_right) < 0.0 else 1.0
    half_width = max(1.0, 0.5 * abs(float(sample.get("track_width_or_radius", 2.0))))
    return vec(sample["center"]), helper_right, helper_up, gx_forward, x_sign * half_width, half_width


def load_samples(path):
    data = json.loads(Path(path).read_text(encoding="utf-8"))
    return {int(road["stream_index"]): road["samples"] for road in data["roads"]}


def sample_helper(helper, t):
    action = helper.animation_data.action
    loc = [action.fcurves.find("location", index=i) for i in range(3)]
    rot = [action.fcurves.find("rotation_quaternion", index=i) for i in range(4)]
    scl = [action.fcurves.find("scale", index=i) for i in range(3)]
    frame = t * 100.0
    pos = Vector((loc[0].evaluate(frame), loc[1].evaluate(frame), loc[2].evaluate(frame)))
    quat = Quaternion((
        rot[0].evaluate(frame), rot[1].evaluate(frame), rot[2].evaluate(frame), rot[3].evaluate(frame)
    )).normalized()
    basis = quat.to_matrix().to_3x3()
    scale = Vector((scl[0].evaluate(frame), scl[1].evaluate(frame), scl[2].evaluate(frame)))
    basis.col[0] *= scale.x
    basis.col[1] *= scale.y
    basis.col[2] *= scale.z
    return pos, basis, scale


def main():
    samples_by_stream = load_samples(sys.argv[-1])
    for stream_index, samples in samples_by_stream.items():
        helper = bpy.data.objects.get(f"FZGXStream{stream_index}.000_CurveMatrixHelper")
        if helper is None:
            print("MISSING", stream_index)
            continue
        max_pos = 0.0
        max_x = 0.0
        max_y = 0.0
        max_z = 0.0
        action = helper.animation_data.action
        loc_x = action.fcurves.find("location", index=0)
        first_frame = loc_x.keyframe_points[0].co.x if loc_x and loc_x.keyframe_points else -1.0
        last_frame = loc_x.keyframe_points[-1].co.x if loc_x and loc_x.keyframe_points else -1.0
        key_count = len(loc_x.keyframe_points) if loc_x else 0
        worst_index = 0
        for index, sample in enumerate(samples):
            t = sample_group_t(samples, index)
            expected_pos, right, up, forward, signed_width, _ = sample_frame(sample)
            pos, basis, _scale = sample_helper(helper, t)
            pos_error = (pos - expected_pos).length
            if pos_error > max_pos:
                max_pos = pos_error
                worst_index = index
            max_x = max(max_x, (basis.col[0] - right * signed_width).length)
            max_y = max(max_y, (basis.col[1] - up * abs(signed_width)).length)
            max_z = max(max_z, (basis.col[2] - forward).length)
        print(
            "CHECK",
            stream_index,
            "n",
            len(samples),
            "keys",
            key_count,
            "frames",
            round(first_frame, 4),
            round(last_frame, 4),
            "max_pos",
            round(max_pos, 6),
            "worst",
            worst_index,
            "max_x",
            round(max_x, 6),
            "max_y",
            round(max_y, 6),
            "max_z",
            round(max_z, 6),
        )


main()
