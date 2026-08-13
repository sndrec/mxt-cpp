"""Track serialization and Blender export operators."""

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
    MXT_NAV_FORMAT_VERSION,
    _bake_curve_matrix_direct,
    _build_mesh_direct,
    _mxt_profile_scope,
    _update_trigger_helper,
)

from .curve_matrix import (
    _CURVE_MATRIX_BASIS_DATA_PATHS,
)

from .mesh import (
    _fcurve_to_points,
    _generate_checkpoints_for_segment,
    _pack_curve,
    _pack_mesh_collision_triangles,
)

from .nav_generate import (
    _mxt_bake_cpu_nav_preview,
    _mxt_load_cpu_nav_bake,
    _mxt_nav_generate,
    _mxt_preview_cpu_nav_graph,
    _mxt_store_cpu_nav_bake,
    _mxt_track_reachable_segment_order,
)

def _export_stage(context, filepath):
    import struct
    import os
    ts = context.scene.mxt_track_settings
    first = ts.first_segment
    if not first:
        raise RuntimeError("First segment not set")

    base_path = os.path.splitext(filepath)[0]

    metadata = {
        "name": ts.track_name,
        "description": ts.track_description,
        "difficulty": ts.track_difficulty,
        # Global track params
        "fog_distance": ts.fog_distance,
        "sky_top_color": list(ts.sky_top_color),
        "sky_horizon_color": list(ts.sky_horizon_color),
        "sky_ground_color": list(ts.sky_ground_color),
        "ground_color": list(ts.ground_color_global),
        "ground_height": ts.ground_height,
        "cloud_color": list(ts.cloud_color),
        "cloud_height": ts.cloud_height,
        # Lighting
        "light_color": list(ts.light_color),
        "light_intensity": ts.light_intensity,
        "ambient_intensity": ts.ambient_intensity,
        "ambient_color": list(ts.ambient_color),
        "light_direction": list(ts.light_direction),
    }
    if ts.visual_scene_path.strip():
        metadata["visual_scene"] = ts.visual_scene_path.strip()
    if ts.song == 'CUSTOM':
        song_path = ts.song_custom_resource.strip()
    else:
        song_path = ts.song.strip()
    if song_path:
        metadata["song"] = song_path

    seg_order, visited = _mxt_track_reachable_segment_order(first)

    with _mxt_profile_scope("export_rebake_curvematrix_checkpoints"):
        for seg in seg_order:
            _bake_curve_matrix_direct(seg)
            _generate_checkpoints_for_segment(seg)

    for trig in ts.trigger_objects:
        if trig.segment in visited:
            _update_trigger_helper(trig)
    context.view_layer.update()

    # Build preview meshes so they can be exported
    for seg in seg_order:
        try:
            _build_mesh_direct(seg)
        except Exception:
            pass

    seg_index = {s: i for i, s in enumerate(seg_order)}

    # Per-segment color metadata
    segment_meta = []
    for s in seg_order:
        props = s.mxt_road_overall_props
        segment_meta.append({
            "seg_index": seg_index[s],
            "name": s.name,
            "ground_color": list(getattr(props, 'ground_color', (0.5, 0.5, 0.5))),
            "rail_color": list(getattr(props, 'rail_color', (0.8, 0.8, 0.8))),
            "rail_start_left": float(getattr(props, "rail_start_left", 0.0)),
            "rail_end_left": float(getattr(props, "rail_end_left", 1.0)),
            "rail_start_right": float(getattr(props, "rail_start_right", 0.0)),
            "rail_end_right": float(getattr(props, "rail_end_right", 1.0)),
            "cpu_nav_allow_glide_drops": bool(getattr(props, "cpu_nav_allow_glide_drops", True)),
        })
    metadata["segments"] = segment_meta

    cp_list = []
    seg_cp_start = {}
    cp_counts = {}
    for seg in seg_order:
        props = seg.mxt_road_overall_props
        seg_cp_start[seg] = len(cp_list)
        cp_counts[seg] = len(props.checkpoints)
        for cp in props.checkpoints:
            cp_list.append((seg, cp))

    # compute neighbour indices
    cp_indices = {}
    for idx, (seg, cp) in enumerate(cp_list):
        cp_indices[(seg, cp)] = idx

    neighbours = [[] for _ in cp_list]
    for seg in seg_order:
        props = seg.mxt_road_overall_props
        cps = props.checkpoints
        for i, cp in enumerate(cps):
            gidx = cp_indices[(seg, cp)]
            def add_neighbour(idx):
                if idx not in neighbours[gidx]:
                    neighbours[gidx].append(idx)
            if i == 0:
                for prev in props.prev_segments:
                    ps = prev.segment
                    if ps and ps in seg_cp_start:
                        last_idx = seg_cp_start[ps] + cp_counts[ps] - 1
                        if last_idx < gidx or ps == first:
                            add_neighbour(last_idx)
            if i > 0:
                add_neighbour(gidx - 1)
            if i + 1 < len(cps):
                add_neighbour(gidx + 1)
            if i == len(cps) - 1:
                for nxt in props.next_segments:
                    ns = nxt.segment
                    if ns and ns in seg_cp_start:
                        nidx = seg_cp_start[ns]
                        if nidx > gidx or ns == first:
                            add_neighbour(nidx)

    with open(filepath, 'wb') as f:
        data = bytearray()

        # checkpoints
        for idx, (seg, cp) in enumerate(cp_list):
            props = seg.mxt_road_overall_props
            basis_start = Matrix((Vector(cp.basis_start[0:3]),
                                 Vector(cp.basis_start[3:6]),
                                 Vector(cp.basis_start[6:9]))).transposed()
            basis_end = Matrix((Vector(cp.basis_end[0:3]),
                               Vector(cp.basis_end[3:6]),
                               Vector(cp.basis_end[6:9]))).transposed()
            start_plane_n = basis_start.col[2].normalized()
            end_plane_n = basis_end.col[2].normalized()
            start_plane_d = start_plane_n.dot(Vector(cp.pos_start))
            end_plane_d = end_plane_n.dot(Vector(cp.pos_end))

            data += struct.pack('<3f', *cp.pos_start)
            data += struct.pack('<3f', *cp.pos_end)
            data += struct.pack('<9f', *cp.basis_start)
            data += struct.pack('<9f', *cp.basis_end)
            data += struct.pack('<f', cp.x_rad_start)
            data += struct.pack('<f', cp.y_rad_start)
            data += struct.pack('<f', cp.x_rad_end)
            data += struct.pack('<f', cp.y_rad_end)
            data += struct.pack('<f', cp.start_t)
            data += struct.pack('<f', cp.end_t)
            data += struct.pack('<f', cp.distance)
            data += struct.pack('<I', seg_index[seg])
            data += struct.pack('<3f', *start_plane_n)
            data += struct.pack('<f', start_plane_d)
            data += struct.pack('<3f', *end_plane_n)
            data += struct.pack('<f', end_plane_d)
            nbs = sorted(set(neighbours[idx]))
            data += struct.pack('<I', len(nbs))
            for nb in nbs:
                data += struct.pack('<I', nb)

        # segment data
        seg_data = bytearray()
        type_map = {'FLAT':0, 'CYLINDER':1, 'CYLINDER_OPEN':2, 'PIPE':3, 'PIPE_OPEN':4, 'ROUNDED_SQUARE':5, 'ROUNDED_SQUARE_OPEN':6, 'TUNNEL':7}
        for seg in seg_order:
            props = seg.mxt_road_overall_props
            seg_data += struct.pack('<I', seg_index[seg])
            road_type = type_map.get(props.road_shape_type, 0)
            seg_data += struct.pack('<I', road_type)
            seg_data += struct.pack('<I', 1 if getattr(props, "analytic_collision_enabled", True) else 0)
            if road_type in (5,6):
                helpers = [props.width_helper, props.height_helper, props.radius_helper]
                for helper in helpers:
                    fcu = None
                    if helper and helper.animation_data and helper.animation_data.action:
                        fcu = helper.animation_data.action.fcurves.find('location', index=0)
                    seg_data += _pack_curve(_fcurve_to_points(fcu))
            if road_type in (2,4,6):
                helper = props.openness_helper
                fcu = None
                if helper and helper.animation_data and helper.animation_data.action:
                    fcu = helper.animation_data.action.fcurves.find('location', index=0)
                seg_data += _pack_curve(_fcurve_to_points(fcu))
            # For Open Rounded Square, export seam rotation curve (Y axis of openness helper)
            if road_type == 6:
                helper = props.openness_helper
                fcur = None
                if helper and helper.animation_data and helper.animation_data.action:
                    fcur = helper.animation_data.action.fcurves.find('location', index=1)
                seg_data += _pack_curve(_fcurve_to_points(fcur))

            seg_data += struct.pack('<I', len(props.modulations))
            for mod in props.modulations:
                act = mod.helper.animation_data.action if mod.helper and mod.helper.animation_data else None
                f_h = act.fcurves.find('location', index=1) if act else None
                f_e = act.fcurves.find('location', index=2) if act else None
                seg_data += _pack_curve(_fcurve_to_points(f_e))
                seg_data += _pack_curve(_fcurve_to_points(f_h))

            seg_data += struct.pack('<I', len(props.embeds))
            for emb in props.embeds:
                seg_data += struct.pack('<f', emb.start_t)
                seg_data += struct.pack('<f', emb.end_t)
                embed_type_map = {
                    'RECHARGE':0,'DIRT':1,'ICE':2,'LAVA':3,'HOLE':4}
                seg_data += struct.pack('<I', embed_type_map.get(emb.embed_type,0))
                act = emb.helper.animation_data.action if emb.helper and emb.helper.animation_data else None
                f_l = act.fcurves.find('location', index=1) if act else None
                f_r = act.fcurves.find('location', index=2) if act else None
                seg_data += _pack_curve(_fcurve_to_points(f_l))
                seg_data += _pack_curve(_fcurve_to_points(f_r))

            cm_helper = props.curve_matrix_helper_empty
            act = cm_helper.animation_data.action if cm_helper and cm_helper.animation_data else None
            fc_loc = [act.fcurves.find('location', index=i) for i in range(3)] if act else [None]*3
            fc_basis = [act.fcurves.find(_CURVE_MATRIX_BASIS_DATA_PATHS[i]) for i in range(9)] if act else [None]*9
            fc_scl = [act.fcurves.find('scale', index=i) for i in range(3)] if act else [None]*3

            for fcu in fc_loc:
                seg_data += _pack_curve(_fcurve_to_points(fcu))

            for fcu in fc_basis:
                seg_data += _pack_curve(_fcurve_to_points(fcu))

            for fcu in fc_scl:
                seg_data += _pack_curve(_fcurve_to_points(fcu))

            # Store the rail heights for this segment
            seg_data += struct.pack('<f', getattr(props, "rail_height_left", 5.0))
            seg_data += struct.pack('<f', getattr(props, "rail_height_right", 5.0))
            for attr, default in (
                ("rail_start_left", 0.0),
                ("rail_end_left", 1.0),
                ("rail_start_right", 0.0),
                ("rail_end_right", 1.0),
            ):
                seg_data += struct.pack('<f', max(0.0, min(1.0, float(getattr(props, attr, default)))))

        trigger_data = bytearray()
        trig_count = 0
        type_map_trig = {'DASHPLATE':0,'JUMPPLATE':1,'MINE':2}
        ext_map = {
            'DASHPLATE': Vector((6.0,4.0,12.0)),
            'JUMPPLATE': Vector((12.0,4.0,4.0)),
            'MINE': Vector((2.0,3.0,2.0)),
        }
        for trig in ts.trigger_objects:
            seg = trig.segment
            if not seg or seg not in seg_index:
                continue
            seg_idx = seg_index[seg]
            cp_start = seg_cp_start[seg]
            cp_count = max(1, cp_counts[seg])
            cp_idx = cp_start + min(int(trig.ty * cp_count), cp_count-1)
            mat = trig.helper.matrix_world
            inv = mat.inverted()
            trigger_data += struct.pack('<I', type_map_trig.get(trig.obj_type,0))
            trigger_data += struct.pack('<I', seg_idx)
            trigger_data += struct.pack('<I', cp_idx)
            for c in range(3):
                col = inv.col[c]
                trigger_data += struct.pack('<3f', col[0], col[1], col[2])
            origin = inv.translation
            trigger_data += struct.pack('<3f', origin.x, origin.y, origin.z)
            ext = ext_map.get(trig.obj_type, Vector((1,1,1)))
            trigger_data += struct.pack('<3f', ext.x, ext.y, ext.z)
            trig_count += 1

        mesh_collision_triangle_count, mesh_collision_data = _pack_mesh_collision_triangles(context, seg_index, seg_cp_start, cp_counts)

        header = struct.pack('<I4sIIII', 0, b'v0.9', len(cp_list), len(seg_order), trig_count, mesh_collision_triangle_count)
        header = struct.pack('<I', len(header)) + header[4:]
        f.write(header)
        f.write(data)
        f.write(seg_data)
        f.write(trigger_data)
        f.write(mesh_collision_data)

    # Export preview meshes as glTF so runtime builds can load visuals without
    # Godot editor import sidecars.
    gltf_path = base_path + ".glb"
    preview_meshes = []
    for seg in seg_order:
        props = seg.mxt_road_overall_props
        if getattr(props, "disable_preview_mesh_generation", False):
            continue
        mesh_name = f"{seg.name}_PreviewMesh"
        mesh_obj = next((c for c in seg.children if c.name == mesh_name), None)
        if mesh_obj:
            preview_meshes.append(mesh_obj)
    for obj in bpy.data.objects:
        props = getattr(obj, "mxt_mesh_collision_props", None)
        if obj.type == 'MESH' and props and not obj.hide_render and (props.is_mxt_collision_mesh or props.is_mxt_visual_mesh):
            if obj not in preview_meshes:
                preview_meshes.append(obj)

    if preview_meshes:
        prev_mode = None
        orig_active = bpy.context.view_layer.objects.active
        orig_sel = [obj for obj in bpy.context.selected_objects]
        try:
            if bpy.context.object and bpy.context.object.mode != 'OBJECT':
                prev_mode = bpy.context.object.mode
                bpy.ops.object.mode_set(mode='OBJECT')

            bpy.ops.object.select_all(action='DESELECT')
            for obj in preview_meshes:
                obj.select_set(True)
            bpy.context.view_layer.objects.active = preview_meshes[0]
            if not hasattr(bpy.ops.export_scene, "gltf"):
                raise RuntimeError("glTF export operator not found")
            ok = False
            try:
                bpy.ops.export_scene.gltf(
                    filepath=gltf_path,
                    export_format='GLB',
                    use_selection=True,
                    export_apply=True,
                    export_yup=False,
                    export_materials='EXPORT',
                    export_vertex_color='ACTIVE',
                    export_all_vertex_colors=True,
                    export_active_vertex_color_when_no_material=True,
                )
                ok = True
            except TypeError:
                pass
            if not ok:
                try:
                    bpy.ops.export_scene.gltf(
                        filepath=gltf_path,
                        export_format='GLB',
                        use_selection=True,
                        export_yup=False,
                    )
                    ok = True
                except TypeError:
                    pass
            if not ok:
                try:
                    bpy.ops.export_scene.gltf(filepath=gltf_path, export_format='GLB', use_selection=True)
                    ok = True
                except TypeError:
                    pass
            if not ok:
                raise RuntimeError("glTF export failed: unsupported parameters")
        finally:
            bpy.ops.object.select_all(action='DESELECT')
            for obj in orig_sel:
                if obj.name in bpy.data.objects:
                    obj.select_set(True)
            if orig_active and orig_active.name in bpy.data.objects:
                bpy.context.view_layer.objects.active = orig_active
            if prev_mode and bpy.context.object:
                bpy.ops.object.mode_set(mode=prev_mode)

    import json

    if getattr(ts, "export_cpu_nav", False):
        nav_path = base_path + ".mxt_nav"
        nav = _mxt_load_cpu_nav_bake(context)
        if nav is None:
            with _mxt_profile_scope("export_cpu_nav"):
                nav = _mxt_nav_generate(context, filepath, seg_order, seg_index)
        nav["source_track"] = filepath
        with open(nav_path, "w", encoding="utf-8") as nf:
            json.dump(nav, nf, indent=2, separators=(",", ": "))
        metadata["cpu_nav"] = {
            "path": os.path.basename(nav_path),
            "version": MXT_NAV_FORMAT_VERSION,
            "nodes": int(nav["diagnostics"]["node_count"]),
            "edges": int(nav["diagnostics"]["edge_count"]),
            "routes": {name: len(path) for name, path in nav["routes"].items()},
            "runtime_routes": {name: len(path) for name, path in nav.get("runtime_routes", {}).items()},
            "route_alternatives": {name: len(paths) for name, paths in nav.get("route_alternatives", {}).items()},
            "choice_points": int(nav["diagnostics"].get("choice_point_count", 0)),
            "diagnostic_routes": {name: len(path) for name, path in nav.get("diagnostic_routes", {}).items()},
        }

    json_path = base_path + ".json"
    with open(json_path, "w", encoding="utf-8") as jf:
        json.dump(metadata, jf, indent=2)


class MXTRoad_OT_PreviewCpuNavGraph(Operator):
    """Build the CPU navigation graph overlay without generating full routes"""

    bl_idname = "mxt_road.preview_cpu_nav_graph"
    bl_label = "Preview CPU Nav Graph"
    bl_options = {'REGISTER'}

    @classmethod
    def poll(cls, ctx):
        ts = getattr(ctx.scene, "mxt_track_settings", None)
        return ts and ts.first_segment is not None

    def execute(self, context):
        try:
            nav = _mxt_preview_cpu_nav_graph(context)
        except Exception as e:
            self.report({'ERROR'}, f"CPU nav graph preview failed: {e}")
            return {'CANCELLED'}
        diag = nav.get("diagnostics", {})
        self.report(
            {'INFO'},
            f"CPU nav graph preview: {diag.get('node_count', 0)} nodes, {diag.get('edge_count', 0)} edges"
        )
        return {'FINISHED'}


class MXTRoad_OT_BakeCpuNav(Operator):
    """Bake the CPU navigation graph and routes into the viewport overlay and .blend"""

    bl_idname = "mxt_road.bake_cpu_nav"
    bl_label = "Bake CPU Nav"
    bl_options = {'REGISTER'}

    @classmethod
    def poll(cls, ctx):
        ts = getattr(ctx.scene, "mxt_track_settings", None)
        return ts and ts.first_segment is not None

    def execute(self, context):
        try:
            nav = _mxt_bake_cpu_nav_preview(context)
            _mxt_store_cpu_nav_bake(context, nav)
        except Exception as e:
            self.report({'ERROR'}, f"CPU nav bake failed: {e}")
            return {'CANCELLED'}
        diag = nav.get("diagnostics", {})
        route_lengths = diag.get("route_lengths", {})
        route_summary = ", ".join(
            f"{name}={length}" for name, length in route_lengths.items() if name != "dash"
        )
        if not route_summary:
            route_summary = "no full route"
        self.report(
            {'INFO'},
            f"CPU nav baked: {diag.get('node_count', 0)} nodes, {diag.get('edge_count', 0)} edges, "
            f"choices: {diag.get('choice_point_count', 0)}, routes: {route_summary}"
        )
        return {'FINISHED'}


class MXTRoad_OT_ExportTrack(Operator):
    """Export the currently edited stage to an .mxt_track file"""

    bl_idname = "mxt_road.export_track"
    bl_label = "Export Track"
    filename_ext = ".mxt_track"

    filepath: StringProperty(subtype='FILE_PATH')
    filter_glob: StringProperty(default="*.mxt_track", options={'HIDDEN'})

    @classmethod
    def poll(cls, ctx):
        ts = getattr(ctx.scene, "mxt_track_settings", None)
        return ts and ts.first_segment is not None

    def invoke(self, context, event):
        context.window_manager.fileselect_add(self)
        return {'RUNNING_MODAL'}

    def execute(self, context):
        try:
            filepath = self.filepath
            if not filepath.lower().endswith('.mxt_track'):
                filepath += '.mxt_track'
            _export_stage(context, filepath)
        except Exception as e:
            self.report({'ERROR'}, f"Export failed: {e}")
            return {'CANCELLED'}
        self.report({'INFO'}, "Track exported")
        return {'FINISHED'}
