import struct
import sys
from dataclasses import dataclass
import matplotlib.pyplot as plt


@dataclass
class Curve:
    points: list

    def sample(self, t: float) -> float:
        if not self.points:
            return 0.0
        if len(self.points) == 1:
            return self.points[0][1]
        if t <= self.points[0][0]:
            return self.points[0][1]
        if t >= self.points[-1][0]:
            return self.points[-1][1]
        i = 0
        pts = self.points
        while i + 1 < len(pts) and t > pts[i + 1][0]:
            i += 1
        t0, v0, tan_in0, tan_out0 = pts[i]
        t1, v1, tan_in1, tan_out1 = pts[i + 1]
        dt = t1 - t0
        if dt == 0:
            return v1
        u = (t - t0) / dt
        p0_handle = v0 + (dt / 3.0) * tan_out0
        p1_handle = v1 - (dt / 3.0) * tan_in1
        omt = 1.0 - u
        return (
            omt * omt * omt * v0
            + 3 * omt * omt * u * p0_handle
            + 3 * omt * u * u * p1_handle
            + u * u * u * v1
        )


def read_u32(f):
    return struct.unpack("<I", f.read(4))[0]


def read_f32(f):
    return struct.unpack("<f", f.read(4))[0]


def read_vec3(f):
    return struct.unpack("<3f", f.read(12))


def read_curve(f):
    count = read_u32(f)
    points = [struct.unpack("<4f", f.read(16)) for _ in range(count)]
    return Curve(points)


def parse_track(path):
    with open(path, "rb") as f:
        header_size = read_u32(f)
        version = f.read(4).decode("ascii")
        cp_count = read_u32(f)
        seg_count = read_u32(f)
        trig_count = read_u32(f) if version not in ("v0.1", "v0.2") else 0

        checkpoints = []
        for _ in range(cp_count):
            pos_start = read_vec3(f)
            pos_end = read_vec3(f)
            f.read(36)  # orientation_start
            f.read(36)  # orientation_end
            xr_s, yr_s, xr_e, yr_e = struct.unpack("<4f", f.read(16))
            f.read(12)  # y_start, y_end, distance
            seg_idx = read_u32(f)
            f.read(12)  # start_plane.normal
            f.read(4)  # start_plane.d
            f.read(12)  # end_plane.normal
            f.read(4)  # end_plane.d
            conn_count = read_u32(f)
            f.read(conn_count * 4)
            checkpoints.append((seg_idx, (xr_s + xr_e) * 0.5))

        segments = []
        for _ in range(seg_count):
            index = read_u32(f)
            road_type = read_u32(f)
            openness = read_curve(f) if road_type in (2, 4) else None
            mod_count = read_u32(f)
            for _ in range(mod_count):
                read_curve(f)
                read_curve(f)
            emb_count = read_u32(f)
            for _ in range(emb_count):
                f.read(8)  # road_start, road_end
                read_u32(f)
                read_curve(f)
                read_curve(f)
            pos_x = read_curve(f)
            pos_y = read_curve(f)
            pos_z = read_curve(f)
            rot_xx = read_curve(f)
            rot_xy = read_curve(f)
            rot_xz = read_curve(f)
            rot_yx = read_curve(f)
            rot_yy = read_curve(f)
            rot_yz = read_curve(f)
            rot_zx = read_curve(f)
            rot_zy = read_curve(f)
            rot_zz = read_curve(f)
            scale_x = read_curve(f)
            scale_y = read_curve(f)
            scale_z = read_curve(f)
            left_rail = read_f32(f)
            right_rail = read_f32(f)
            segments.append(
                {
                    "index": index,
                    "pos_x": pos_x,
                    "pos_y": pos_y,
                    "pos_z": pos_z,
                    "rot_xx": rot_xx,
                    "rot_xy": rot_xy,
                    "rot_xz": rot_xz,
                    "width": 1.0,
                }
            )

        widths = [0.0] * seg_count
        counts = [0] * seg_count
        for seg_idx, w in checkpoints:
            if seg_idx < seg_count:
                widths[seg_idx] += w
                counts[seg_idx] += 1
        for i in range(seg_count):
            if counts[i]:
                widths[i] /= counts[i]
            else:
                widths[i] = 2.0
            segments[i]["width"] = widths[i]

        return segments


def iso(p):
    x, y, z = p
    return x - z, (x + z) * 0.5 - y


def render_track(path, out_path=None):
    segments = parse_track(path)
    left = []
    right = []
    for seg in segments:
        w = seg["width"]
        for i in range(21):
            t = i / 20.0
            x = seg["pos_x"].sample(t)
            y = seg["pos_y"].sample(t)
            z = seg["pos_z"].sample(t)
            ox = seg["rot_xx"].sample(t)
            oy = seg["rot_xy"].sample(t)
            oz = seg["rot_xz"].sample(t)
            norm = (ox * ox + oy * oy + oz * oz) ** 0.5 or 1.0
            ox /= norm
            oy /= norm
            oz /= norm
            left.append((x - ox * w, y - oy * w, z - oz * w))
            right.append((x + ox * w, y + oy * w, z + oz * w))
    poly = [iso(p) for p in left + right[::-1]]
    xs, ys = zip(*poly)
    plt.figure(figsize=(8, 8))
    plt.fill(xs, ys, color="gray")
    plt.axis("equal")
    if out_path:
        plt.savefig(out_path, bbox_inches="tight")
    else:
        plt.show()


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: python track_preview.py <path_to_mxt_track> [output.png]")
    else:
        out = sys.argv[2] if len(sys.argv) > 2 else None
        render_track(sys.argv[1], out)
