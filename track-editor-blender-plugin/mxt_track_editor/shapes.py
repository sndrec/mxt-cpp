"""Road cross-section geometry and viewport drawing."""

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
    _MXT_NAV_DRAW_CACHE,
    _mxt_helper_positions,
    _root,
    _vertical_offset,
    get_active_mxt_road_segment_parent,
    get_selected_mxt_road_segment_parents,
)

from .curve_matrix import (
    _curve_matrix_sampler,
)

class RoadShape:
    def get_pos(self, helper, t: Vector): raise NotImplementedError
class RoadShapeFlat(RoadShape):
    def get_pos(self, helper, t):
        basis, pos = _root(helper, t.y)
        seg_parent = helper.parent

        mod_t = 0.5 * (1.0 - t.x)
        y_off = _vertical_offset(seg_parent, mod_t, t.y)

        local = Vector((t.x, y_off, 0.0))
        return pos + basis @ local


class RoadShapeCylinder(RoadShape):
    def get_pos(self, helper, t):
        basis, pos = _root(helper, t.y)
        seg_parent = helper.parent

        theta   = t.x * math.pi
        radial  = Vector((math.sin(theta), math.cos(theta), 0.0)).normalized()

        mod_t   = 0.5 * (1.0 - t.x)
        r_off   = _vertical_offset(seg_parent, mod_t, t.y)

        local   = radial + radial * r_off
        return pos + basis @ local


class RoadShapePipe(RoadShape):
    def __init__(self): self.inner = 0.8
    def get_pos(self, helper, t):
        basis, pos = _root(helper, t.y)
        seg_parent = helper.parent

        tx_angle = (t.x - 0.5) * math.pi
        radial   = Vector((math.cos(tx_angle), math.sin(tx_angle), 0.0)).normalized()

        mod_t    = 0.5 * (1.0 - t.x)
        r_off    = _vertical_offset(seg_parent, mod_t, t.y)

        local    = radial + radial * r_off
        return pos + basis @ local


class RoadShapeCylinderOpen(RoadShapeCylinder):
    def get_pos(self, helper, t):
        basis, pos = _root(helper, t.y)
        seg_parent = helper.parent
        props = seg_parent.mxt_road_overall_props

        open_val = 1.0
        helper_open = getattr(props, 'openness_helper', None)
        if helper_open and helper_open.animation_data and helper_open.animation_data.action:
            fcu = helper_open.animation_data.action.fcurves.find("location", index=0)
            if fcu:
                open_val = fcu.evaluate(t.y * 100.0)
        mod_tx = t.x * open_val
        theta = mod_tx * math.pi
        radial = Vector((math.sin(theta), math.cos(theta), 0.0)).normalized()

        mod_t = 0.5 * (1.0 - t.x)  # modulation uses original tx
        r_off = _vertical_offset(seg_parent, mod_t, t.y)

        local = radial + radial * r_off
        return pos + basis @ local


class RoadShapePipeOpen(RoadShapePipe):
    def get_pos(self, helper, t):
        basis, pos = _root(helper, t.y)
        seg_parent = helper.parent
        props = seg_parent.mxt_road_overall_props

        open_val = 1.0
        helper_open = getattr(props, 'openness_helper', None)
        if helper_open and helper_open.animation_data and helper_open.animation_data.action:
            fcu = helper_open.animation_data.action.fcurves.find("location", index=0)
            if fcu:
                open_val = fcu.evaluate(t.y * 100.0)
        mod_tx = t.x * open_val
        tx_angle = (mod_tx - 0.5) * math.pi
        radial = Vector((math.cos(tx_angle), math.sin(tx_angle), 0.0)).normalized()

        mod_t = 0.5 * (1.0 - t.x)  # modulation uses original tx
        r_off = _vertical_offset(seg_parent, mod_t, t.y)

        local = radial + radial * r_off
        return pos + basis @ local

class RoadShapeRoundedSquare(RoadShape):
    def get_pos(self, helper, t):
        basis, pos = _root(helper, t.y)
        seg_parent = helper.parent
        # Save original tx for modulation mapping
        tx_orig = t.x
        # Invert for geometry winding to match C++
        t = Vector((1.0 - tx_orig, t.y))

        # Modulation uses the original tx, not the inverted
        mod_t = 0.5 * (1.0 - tx_orig)
        r_off = _vertical_offset(seg_parent, mod_t, t.y)

        props = seg_parent.mxt_road_overall_props

        def _sample(helper_obj, default):
            if helper_obj and helper_obj.animation_data and helper_obj.animation_data.action:
                fcu = helper_obj.animation_data.action.fcurves.find("location", index=0)
                if fcu:
                    return fcu.evaluate(t.y * 100.0)
            return default

        width  = _sample(props.width_helper,  1.0)
        height = _sample(props.height_helper, 1.0)
        radius = _sample(props.radius_helper, 0.0)

        # ---------- exact rounded-rectangle tracing ----------
        w2, h2 = width * 0.5, height * 0.5
        radius = max(0.0, min(radius, min(w2, h2)))  # clamp

        theta   = t.x * math.pi
        dir     = Vector((math.sin(theta), math.cos(theta)))
        abs_dx, abs_dy = abs(dir.x), abs(dir.y)

        candidates = []

        # vertical sides (x = ±w2)
        if abs_dx > 1.0e-9:
            t_v = w2 / abs_dx
            if t_v * abs_dy <= h2 - radius + 1.0e-9:
                candidates.append(t_v)

        # horizontal sides (y = ±h2)
        if abs_dy > 1.0e-9:
            t_h = h2 / abs_dy
            if t_h * abs_dx <= w2 - radius + 1.0e-9:
                candidates.append(t_h)

        # corner circle |(x,y) – corner| = r
        if radius > 1.0e-9:
            w_in, h_in = w2 - radius, h2 - radius
            w_in = max(w_in, 0.0)
            h_in = max(h_in, 0.0)

            a = 1.0  # dir is unit length
            b = -2.0 * (abs_dx * w_in + abs_dy * h_in)
            c = w_in ** 2 + h_in ** 2 - radius ** 2
            disc = b * b - 4.0 * a * c
            if disc >= 0.0:
                sqrt_disc = math.sqrt(disc)
                for root in ((-b - sqrt_disc) * 0.5, (-b + sqrt_disc) * 0.5):
                    if root > 0.0 and \
                       root * abs_dx >= w_in - 1.0e-9 and \
                       root * abs_dy >= h_in - 1.0e-9:
                        candidates.append(root)

        if not candidates:
            # degenerate (e.g. r > min(w2,h2)); treat as circle
            t_len = min(w2, h2)
        else:
            t_len = min(candidates)

        p = dir * t_len
        p *= (1.0 + r_off)

        return pos + basis @ Vector((p.x, p.y, 0.0))

class RoadShapeRoundedSquareOpen(RoadShapeRoundedSquare):
    def get_pos(self, helper, t):
        basis, pos = _root(helper, t.y)
        seg_parent = helper.parent
        props = seg_parent.mxt_road_overall_props

        # Sample openness at this ty
        open_val = 1.0
        helper_open = getattr(props, 'openness_helper', None)
        if helper_open and helper_open.animation_data and helper_open.animation_data.action:
            fcu = helper_open.animation_data.action.fcurves.find("location", index=0)
            if fcu:
                open_val = fcu.evaluate(t.y * 100.0)

        # Sample rotation (seam offset) in same domain as t.x for this shape ([-1,1])
        rot = 0.0
        if helper_open and helper_open.animation_data and helper_open.animation_data.action:
            fcur = helper_open.animation_data.action.fcurves.find("location", index=1)
            if fcur:
                rot = fcur.evaluate(t.y * 100.0)

        tx_orig = t.x
        mod_tx = tx_orig * open_val + rot
        if mod_tx < -1.0:
            mod_tx += 2.0
        if mod_tx > 1.0:
            mod_tx -= 2.0

        # Geometry uses inverted mod_tx for winding
        t_geom_x = 1.0 - mod_tx

        # Modulation uses original tx
        mod_t = 0.5 * (1.0 - tx_orig)
        r_off = _vertical_offset(seg_parent, mod_t, t.y)

        # ---------- exact rounded-rectangle tracing (same as base, but with t_geom_x) ----------
        props = seg_parent.mxt_road_overall_props

        def _sample(helper_obj, default):
            if helper_obj and helper_obj.animation_data and helper_obj.animation_data.action:
                fcu = helper_obj.animation_data.action.fcurves.find("location", index=0)
                if fcu:
                    return fcu.evaluate(t.y * 100.0)
            return default

        width  = _sample(props.width_helper,  1.0)
        height = _sample(props.height_helper, 1.0)
        radius = _sample(props.radius_helper, 0.0)

        w2, h2 = width * 0.5, height * 0.5
        radius = max(0.0, min(radius, min(w2, h2)))

        theta   = t_geom_x * math.pi
        dir2    = Vector((math.sin(theta), math.cos(theta)))
        abs_dx, abs_dy = abs(dir2.x), abs(dir2.y)

        candidates = []
        if abs_dx > 1.0e-9:
            t_v = w2 / abs_dx
            if t_v * abs_dy <= h2 - radius + 1.0e-9:
                candidates.append(t_v)
        if abs_dy > 1.0e-9:
            t_h = h2 / abs_dy
            if t_h * abs_dx <= w2 - radius + 1.0e-9:
                candidates.append(t_h)
        if radius > 1.0e-9:
            w_in, h_in = max(w2 - radius, 0.0), max(h2 - radius, 0.0)
            a = 1.0
            b = -2.0 * (abs_dx * w_in + abs_dy * h_in)
            c = w_in ** 2 + h_in ** 2 - radius ** 2
            disc = b * b - 4.0 * a * c
            if disc >= 0.0:
                sqrt_disc = math.sqrt(disc)
                for root in ((-b - sqrt_disc) * 0.5, (-b + sqrt_disc) * 0.5):
                    if root > 0.0 and root * abs_dx >= w_in - 1.0e-9 and root * abs_dy >= h_in - 1.0e-9:
                        candidates.append(root)
        t_len = min(candidates) if candidates else min(w2, h2)
        p = dir2 * t_len
        p *= (1.0 + r_off)

        return pos + basis @ Vector((p.x, p.y, 0.0))

def _sample_curve_matrix(helper_obj, t: float):
    sampler = _curve_matrix_sampler(helper_obj)
    if sampler is None:
        return Matrix.Identity(3), Vector((0.0, 0.0, 0.0)), Vector((1.0, 1.0, 1.0))
    positions, basis_mats, scales = sampler.sample(np.array([t], dtype=np.float64))
    pos = Vector(tuple(float(v) for v in positions[0]))
    basis = Matrix(tuple(tuple(float(v) for v in row) for row in basis_mats[0])).to_3x3()
    scale = Vector(tuple(float(v) for v in scales[0]))
    basis.col[0] *= scale.x
    basis.col[1] *= scale.y
    basis.col[2] *= scale.z
    return basis, pos, scale
def _sample_curve_matrix_numpy(helper_obj, t_values_1d):
    sampler = _curve_matrix_sampler(helper_obj)
    if sampler is None:
        t_values_1d = np.asarray(t_values_1d, dtype=np.float64)
        return (
            np.zeros((len(t_values_1d), 3), dtype=np.float64),
            np.tile(np.eye(3, dtype=np.float64), (len(t_values_1d), 1, 1)),
            np.ones((len(t_values_1d), 3), dtype=np.float64),
        )
    return sampler.sample(t_values_1d)
def mxt_draw_callback():
    parent = get_active_mxt_road_segment_parent(bpy.context)
    if not parent:
        return
    track_settings = getattr(bpy.context.scene, "mxt_track_settings", None)

    shader = gpu.shader.from_builtin('UNIFORM_COLOR')
    gpu.state.blend_set('ALPHA')
    gpu.state.line_width_set(2.0)



    props = parent.mxt_road_overall_props
    if not props.is_mxt_road_segment_parent:
        return

    helper = props.curve_matrix_helper_empty
    pts = _mxt_helper_positions(helper)
    if len(pts) < 2:
        pass
    else:
        pts_world = [parent.matrix_world @ p for p in pts]
        batch = batch_for_shader(shader, 'LINE_STRIP', {"pos": pts_world})
        shader.bind()
        shader.uniform_float("color", (1.0, 1.0, 0.0, 1.0))
        batch.draw(shader)

    if track_settings and track_settings.draw_checkpoints:
        shader.uniform_float("color", (1.0, 0.2, 0.2, 1.0))
        checkpoint_parents = get_selected_mxt_road_segment_parents(bpy.context)
        if not checkpoint_parents:
            checkpoint_parents = [parent]
        for checkpoint_parent in checkpoint_parents:
            checkpoint_props = checkpoint_parent.mxt_road_overall_props
            if not getattr(checkpoint_props, "checkpoints", None):
                continue
            for cp in checkpoint_props.checkpoints:
                for pos, basis_flat, xr, yr in (
                    (cp.pos_start, cp.basis_start, cp.x_rad_start, cp.y_rad_start),
                    (cp.pos_end, cp.basis_end, cp.x_rad_end, cp.y_rad_end)):

                    B = mathutils.Matrix((
                        Vector(basis_flat[0:3]),
                        Vector(basis_flat[3:6]),
                        Vector(basis_flat[6:9]))).transposed()


                    c = Vector(pos)

                    p_x0 = c - B.col[0].normalized() * xr
                    p_x1 = c + B.col[0].normalized() * xr
                    p_y0 = c - B.col[1].normalized() * yr
                    p_y1 = c + B.col[1].normalized() * yr

                    world = [checkpoint_parent.matrix_world @ p for p in (p_x0, p_x1, p_y0, p_y1)]

                    batch = batch_for_shader(shader, 'LINES', {"pos": world[0:2]})
                    batch.draw(shader)
                    batch = batch_for_shader(shader, 'LINES', {"pos": world[2:4]})
                    batch.draw(shader)

    if track_settings and _MXT_NAV_DRAW_CACHE and (
        track_settings.draw_cpu_nav or track_settings.draw_cpu_nav_routes or track_settings.draw_mesh_collision_nav):
        if track_settings.draw_cpu_nav_routes:
            route_enabled = {
                "default": track_settings.draw_cpu_nav_route_default,
                "safe": track_settings.draw_cpu_nav_route_safe,
                "aggressive": track_settings.draw_cpu_nav_route_aggressive,
                "boost_dash": track_settings.draw_cpu_nav_route_dash,
                "dash": track_settings.draw_cpu_nav_route_dash,
                "recharge": track_settings.draw_cpu_nav_route_recharge,
                "reachable": track_settings.draw_cpu_nav_route_reachable,
            }
            route_colors = {
                "default": (1.0, 0.9, 0.05, 0.95),
                "safe": (0.1, 1.0, 0.35, 0.85),
                "aggressive": (1.0, 0.15, 0.08, 0.92),
                "boost_dash": (0.0, 0.65, 1.0, 0.9),
                "dash": (0.0, 0.65, 1.0, 0.9),
                "recharge": (1.0, 0.1, 0.85, 0.9),
                "reachable": (1.0, 0.35, 0.0, 0.95),
            }
            old_line_width = 2.0
            gpu.state.line_width_set(2.0)
            for alt in _MXT_NAV_DRAW_CACHE.get("route_alternative_positions", []):
                if not route_enabled.get(alt.get("route", ""), True):
                    continue
                route_positions = alt.get("positions", [])
                if len(route_positions) < 2:
                    continue
                base_color = route_colors.get(alt.get("route", ""), (1.0, 1.0, 1.0, 0.45))
                batch = batch_for_shader(shader, 'LINE_STRIP', {"pos": route_positions})
                shader.bind()
                shader.uniform_float("color", (base_color[0], base_color[1], base_color[2], 0.42))
                batch.draw(shader)
            gpu.state.line_width_set(4.0)
            for route_name, route_positions in _MXT_NAV_DRAW_CACHE.get("route_positions", {}).items():
                if not route_enabled.get(route_name, True):
                    continue
                if len(route_positions) < 2:
                    continue
                batch = batch_for_shader(shader, 'LINE_STRIP', {"pos": route_positions})
                shader.bind()
                shader.uniform_float("color", route_colors.get(route_name, (1.0, 1.0, 1.0, 0.8)))
                batch.draw(shader)
            gpu.state.line_width_set(old_line_width)
        if track_settings.draw_cpu_nav:
            selected_nav_segments = {
                segment.name for segment in get_selected_mxt_road_segment_parents(bpy.context)
            }
            if selected_nav_segments:
                edge_positions = []
                for entry in _MXT_NAV_DRAW_CACHE.get("edge_entries", []):
                    if entry.get("from_segment") in selected_nav_segments and entry.get("to_segment") in selected_nav_segments:
                        edge_positions.extend(entry.get("positions", []))
                node_positions = [
                    entry["position"]
                    for entry in _MXT_NAV_DRAW_CACHE.get("node_entries", [])
                    if entry.get("segment") in selected_nav_segments
                ]
            else:
                edge_positions = _MXT_NAV_DRAW_CACHE.get("edge_positions", [])
                node_positions = _MXT_NAV_DRAW_CACHE.get("node_positions", [])
            if edge_positions:
                batch = batch_for_shader(shader, 'LINES', {"pos": edge_positions})
                shader.bind()
                shader.uniform_float("color", (0.0, 0.75, 1.0, 0.24))
                batch.draw(shader)
            if node_positions:
                gpu.state.point_size_set(4.0)
                batch = batch_for_shader(shader, 'POINTS', {"pos": node_positions})
                shader.bind()
                shader.uniform_float("color", (0.1, 1.0, 0.25, 0.85))
                batch.draw(shader)
        if track_settings.draw_mesh_collision_nav:
            mesh_node_positions = _MXT_NAV_DRAW_CACHE.get("mesh_node_positions", [])
            if mesh_node_positions:
                gpu.state.point_size_set(5.0)
                batch = batch_for_shader(shader, 'POINTS', {"pos": mesh_node_positions})
                shader.bind()
                shader.uniform_float("color", (1.0, 0.55, 0.05, 0.9))
                batch.draw(shader)
            mesh_blocker_edges = _MXT_NAV_DRAW_CACHE.get("mesh_blocker_edges", [])
            if mesh_blocker_edges:
                gpu.state.line_width_set(2.0)
                batch = batch_for_shader(shader, 'LINES', {"pos": mesh_blocker_edges})
                shader.bind()
                shader.uniform_float("color", (1.0, 0.05, 0.05, 0.72))
                batch.draw(shader)


    if props.draw_embeds and getattr(props, "embeds", None):
        shape_map = {
            'FLAT': RoadShapeFlat(),
            'CYLINDER': RoadShapeCylinder(),
            'PIPE': RoadShapePipe(),
            'CYLINDER_OPEN': RoadShapeCylinderOpen(),
            'PIPE_OPEN': RoadShapePipeOpen(),
            'ROUNDED_SQUARE': RoadShapeRoundedSquare(),
            'ROUNDED_SQUARE_OPEN': RoadShapeRoundedSquareOpen(),
            'TUNNEL': RoadShapeFlat(),
        }
        shape = shape_map[props.road_shape_type]
        for emb in props.embeds:
            helper = emb.helper
            if not (helper and helper.animation_data and helper.animation_data.action):
                continue
            act = helper.animation_data.action
            f_left = act.fcurves.find("location", index=1)
            f_right = act.fcurves.find("location", index=2)
            if not (f_left and f_right): continue

            steps = 32
            verts_left = []
            verts_right = []
            for i in range(steps + 1):
                ty = emb.start_t + (emb.end_t - emb.start_t) * (i / steps)
                tx_l = f_left.evaluate(ty * 100)
                tx_r = f_right.evaluate(ty * 100)
                for tx, coll in ((tx_l, verts_left), (tx_r, verts_right)):
                    pos = shape.get_pos(props.curve_matrix_helper_empty, Vector((tx, ty)))
                    if pos is not None:
                        coll.append(parent.matrix_world @ pos)

            shader.uniform_float("color", (0.0, 0.8, 0.2, 1.0))
            if len(verts_left) > 1:
                batch = batch_for_shader(shader, 'LINE_STRIP', {"pos": verts_left})
                batch.draw(shader)
            shader.uniform_float("color", (0.8, 0.4, 0.0, 1.0))
            if len(verts_right) > 1:
                batch = batch_for_shader(shader, 'LINE_STRIP', {"pos": verts_right})
                batch.draw(shader)

    if hasattr(props, "modulations"):
        for mod in props.modulations:
            helper_mod = mod.helper
            if not (helper_mod and helper_mod.select_get()):
                continue

            act = helper_mod.animation_data.action if helper_mod.animation_data else None
            if not act:
                continue
            f_e = act.fcurves.find("location", index=2)
            if not f_e:
                continue

            shape = {
                'FLAT': RoadShapeFlat(),
                'CYLINDER': RoadShapeCylinder(),
                'PIPE': RoadShapePipe(),
                'CYLINDER_OPEN': RoadShapeCylinderOpen(),
                'PIPE_OPEN': RoadShapePipeOpen(),
                'ROUNDED_SQUARE': RoadShapeRoundedSquare(),
                'ROUNDED_SQUARE_OPEN': RoadShapeRoundedSquareOpen(),
                'TUNNEL': RoadShapeFlat(),
            }[props.road_shape_type]


            steps = 128
            for kp in f_e.keyframe_points:
                ty = kp.co.x * 0.01

                verts = []
                for i in range(steps):
                    tx = -1.0 + 2.0 * i / (steps - 1)
                    p = shape.get_pos(props.curve_matrix_helper_empty,
                                      Vector((tx, ty)))
                    if p is not None:
                        verts.append(parent.matrix_world @ p)

                if len(verts) > 1:
                    shader.bind()
                    shader.uniform_float("color", (0.0, 1.0, 1.0, 1.0))
                    batch = batch_for_shader(shader, 'LINE_STRIP',
                                             {"pos": verts})
                    batch.draw(shader)

    ts = bpy.context.scene.mxt_track_settings
    if ts:
        # Draw global preview planes for Ground and Cloud heights (approx. 1000 units diameter)
        try:
            # Determine an oriented frame: use active segment's parent up/right as reference
            up_dir = None
            right_dir = None
            origin = None
            if parent:
                mw = parent.matrix_world.to_3x3()
                up_dir = Vector(mw.col[1]).normalized()  # local Y is "up" in this tool
                right_dir = Vector(mw.col[0]).normalized()
                origin = parent.matrix_world.translation
            else:
                up_dir = Vector((0.0, 0.0, 1.0))
                right_dir = Vector((1.0, 0.0, 0.0))
                origin = Vector((0.0, 0.0, 0.0))

            # Make an orthonormal basis in the preview plane
            u = (right_dir - up_dir * right_dir.dot(up_dir)).normalized()
            v = up_dir.cross(u).normalized()
            half = 500.0  # 1000 units diameter

            def draw_plane_at(height: float, color_rgb: tuple[float, float, float], alpha: float):
                c = origin + up_dir * float(height)
                corners = [
                    c + u * (-half) + v * (-half),
                    c + u * ( half) + v * (-half),
                    c + u * ( half) + v * ( half),
                    c + u * (-half) + v * ( half),
                ]
                shader.bind()
                shader.uniform_float("color", (color_rgb[0], color_rgb[1], color_rgb[2], alpha))
                # Outline
                batch = batch_for_shader(shader, 'LINES', {"pos": [
                    corners[0], corners[1],
                    corners[1], corners[2],
                    corners[2], corners[3],
                    corners[3], corners[0],
                ]})
                batch.draw(shader)

            # Ground plane
            gc = getattr(ts, 'ground_color_global', (0.2, 0.2, 0.2))
            draw_plane_at(getattr(ts, 'ground_height', 0.0), gc, 0.7)
            # Cloud plane
            cc = getattr(ts, 'cloud_color', (1.0, 1.0, 1.0))
            draw_plane_at(getattr(ts, 'cloud_height', 800.0), cc, 0.5)
        except Exception as _e:
            pass

        ext_map = {
            'DASHPLATE': Vector((6.0,4.0,12.0)),
            'JUMPPLATE': Vector((12.0,4.0,4.0)),
            'MINE': Vector((2.0,3.0,2.0)),
        }
        for trig in ts.trigger_objects:
            h = trig.helper
            if not (h and h.select_get()):
                continue
            ext = ext_map.get(trig.obj_type, Vector((1,1,1)))
            corners = [Vector((sx*ext.x, sy*ext.y, sz*ext.z))
                       for sx in (-1,1) for sy in (-1,1) for sz in (-1,1)]
            world = [h.matrix_world @ c for c in corners]
            edges = [(0,1),(0,2),(0,4),(3,1),(3,2),(3,7),(5,1),(5,4),(5,7),(6,2),(6,4),(6,7)]
            shader.bind()
            shader.uniform_float("color", (1.0,1.0,0.0,1.0))
            for a,b in edges:
                batch = batch_for_shader(shader, 'LINES', {"pos": [world[a], world[b]]})
                batch.draw(shader)


    gpu.state.line_width_set(1.0)
    gpu.state.blend_set('NONE')
