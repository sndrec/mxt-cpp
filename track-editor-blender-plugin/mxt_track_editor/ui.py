"""Blender sidebar panel for the track editor."""

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
    MXTRoad_OT_AddControlPoint,
    MXTRoad_OT_CreateRoadSegment,
    MXTRoad_OT_UpdatePathVisuals,
    get_active_mxt_road_segment_parent,
)

class MXTRoad_PT_MainPanel(Panel):
    bl_label = "MXT Road Creator"; bl_idname = "MXTROAD_PT_main_panel"; bl_space_type = 'VIEW_3D'
    bl_region_type = 'UI'; bl_category = "MXT Road"
    @staticmethod
    def section(layout, ui_state, state_prop, label, *, icon='NONE'):
        box = layout.box()
        is_open = bool(getattr(ui_state, state_prop))
        row = box.row(align=True)
        row.prop(ui_state, state_prop, text=label, icon='TRIA_DOWN' if is_open else 'TRIA_RIGHT', emboss=False)
        return box if is_open else None

    def draw_cp_empty_props(self, layout, cp_empty, ui_state):
        cp_data = cp_empty.mxt_cp_data; layout.prop(cp_data, "time")
        layout.label(text="Transform (on Empty):")
        col = layout.column(align=True); col.prop(cp_empty, "location", text=""); col.prop(cp_empty, "rotation_euler", text=""); col.prop(cp_empty, "scale", text="")
        layout.separator(); layout.label(text="Handle Lengths")
        row = layout.row(align=True); row.prop(cp_data, "handle_in_length", text="In"); row.prop(cp_data, "handle_out_length", text="Out")
        easing_box = self.section(layout, ui_state, "show_cp_easing", "Property Easing F-Curves")
        if easing_box:
            easing_box.label(text="Select this CP Empty, then open the"); easing_box.label(text="Graph Editor to visually edit easing.")
            col = easing_box.column(align=True)
            col.label(text="- Rotation Ease: 'Rotation Ease Factor'", icon='FCURVE')
            col.label(text="- Scale Ease: 'Scale Ease Factor'", icon='FCURVE')
            col.label(text="- Twist Ease: 'Twist Ease Factor'", icon='FCURVE')
            if cp_empty.animation_data and cp_empty.animation_data.action:
                action = cp_empty.animation_data.action
                for prop_name_ui, data_path_str_part in { "Rotation": 'rotation_ease_factor_channel', "Scale": 'scale_ease_factor_channel', "Twist": 'twist_ease_factor_channel'}.items():
                    fcu = action.fcurves.find(f'mxt_cp_data.{data_path_str_part}')
                    if fcu and len(fcu.keyframe_points) > 0 : easing_box.label(text=f"{prop_name_ui} Factor @ t=0.5: {fcu.evaluate(0.5 * 100):.2f}")

    def draw(self, context):
        layout = self.layout; obj = context.active_object
        ui_state = context.window_manager.mxt_track_editor_ui_state
        layout.operator(MXTRoad_OT_CreateRoadSegment.bl_idname)
        ts = context.scene.mxt_track_settings
        if ts:
            track_box = self.section(layout, ui_state, "show_track_identity", "Track", icon='WORLD')
            if track_box:
                track_box.prop(ts, "first_segment")
                track_box.prop(ts, "track_name")
                track_box.prop(ts, "track_description")
                track_box.prop(ts, "visual_scene_path")
                track_box.prop(ts, "song")
                if ts.song == 'CUSTOM':
                    track_box.prop(ts, "song_custom_resource")
                track_box.prop(ts, "track_difficulty")
            # Global track parameters
            global_box = self.section(layout, ui_state, "show_global_params", "Global Track Params", icon='WORLD_DATA')
            if global_box:
                global_box.prop(ts, "fog_distance")
                global_box.prop(ts, "sky_top_color")
                global_box.prop(ts, "sky_horizon_color")
                global_box.prop(ts, "sky_ground_color")
                global_box.prop(ts, "ground_color_global")
                global_box.prop(ts, "ground_height")
                global_box.prop(ts, "cloud_color")
                global_box.prop(ts, "cloud_height")
                global_box.separator()
                global_box.prop(ts, "draw_checkpoints")
                global_box.prop(ts, "export_cpu_nav")
                global_box.prop(ts, "draw_cpu_nav")
                global_box.prop(ts, "draw_cpu_nav_routes")
                nav_box = global_box.box()
                nav_box.enabled = bool(ts.export_cpu_nav)
                nav_box.prop(ts, "cpu_nav_lateral_samples")
                nav_box.prop(ts, "cpu_nav_row_spacing_meters")
                nav_box.prop(ts, "cpu_nav_transition_distance")
                nav_box.prop(ts, "cpu_nav_branch_distance")
                nav_box.prop(ts, "cpu_nav_mesh_sample_spacing_meters")
                nav_box.prop(ts, "draw_mesh_collision_nav")
                route_box = global_box.box()
                route_box.enabled = bool(ts.draw_cpu_nav_routes)
                route_box.prop(ts, "draw_cpu_nav_route_default")
                route_box.prop(ts, "draw_cpu_nav_route_safe")
                route_box.prop(ts, "draw_cpu_nav_route_aggressive")
                route_box.prop(ts, "draw_cpu_nav_route_dash")
                route_box.prop(ts, "draw_cpu_nav_route_recharge")
                route_box.prop(ts, "draw_cpu_nav_route_reachable")
                lighting_box = self.section(global_box, ui_state, "show_lighting", "Lighting")
                if lighting_box:
                    if hasattr(ts, "light_color") and hasattr(ts, "light_intensity") and hasattr(ts, "ambient_intensity") and hasattr(ts, "ambient_color") and hasattr(ts, "light_direction"):
                        lighting_box.prop(ts, "light_color")
                        lighting_box.prop(ts, "light_intensity")
                        lighting_box.prop(ts, "ambient_intensity")
                        lighting_box.prop(ts, "ambient_color")
                        lighting_box.prop(ts, "light_direction")
                    else:
                        lighting_box.label(text="(Reload the MXT add-on or restart Blender to see lighting params)")
            trig_box = self.section(layout, ui_state, "show_triggers", "Trigger Objects", icon='EMPTY_AXIS')
            if trig_box:
                row = trig_box.row()
                row.template_list("MXT_UL_TriggerObjects", "", ts, "trigger_objects", ts, "active_trigger_obj_idx", rows=3)
                col = row.column(align=True)
                col.operator("mxt_track.add_trigger", icon='ADD', text="")
                col.operator("mxt_track.remove_trigger", icon='REMOVE', text="")
                if ts.trigger_objects and 0 <= ts.active_trigger_obj_idx < len(ts.trigger_objects):
                    trig = ts.trigger_objects[ts.active_trigger_obj_idx]
                    active_trigger_box = self.section(trig_box, ui_state, "show_active_trigger", f"Active Trigger: {trig.label}")
                    if active_trigger_box:
                        active_trigger_box.prop(trig, "obj_type")
                        active_trigger_box.prop(trig, "segment")
                        active_trigger_box.prop(trig, "tx")
                        active_trigger_box.prop(trig, "ty")
                        active_trigger_box.prop(trig, "scale")
                        active_trigger_box.prop(trig, "yaw_deg")
                        active_trigger_box.prop(trig, "checkpoint_index")
        layout.separator()

        if obj and obj.type == 'MESH' and getattr(obj, "mxt_mesh_collision_props", None):
            mesh_collision_box = self.section(layout, ui_state, "show_mesh_export", "MXT Mesh Export", icon='MESH_DATA')
            if mesh_collision_box:
                mesh_props = obj.mxt_mesh_collision_props
                mesh_collision_box.prop(mesh_props, "is_mxt_visual_mesh")
                mesh_collision_box.prop(mesh_props, "is_mxt_collision_mesh")
                collision_col = mesh_collision_box.column()
                collision_col.enabled = bool(mesh_props.is_mxt_collision_mesh)
                collision_col.prop(mesh_props, "surface_type")
                collision_col.prop(mesh_props, "double_sided")

        active_road_parent = get_active_mxt_road_segment_parent(context)
        if not active_road_parent:
            layout.label(text="Select an MXT Road Segment Parent or CP, or create new.")
            return

        road_props = active_road_parent.mxt_road_overall_props


        parent_box = self.section(layout, ui_state, "show_segment", f"Segment: {active_road_parent.name}", icon='CURVE_PATH')
        if parent_box:
            header_row = parent_box.row(align=True)
            if road_props.segment_type == 'BEZIER':
                header_row.operator(MXTRoad_OT_AddControlPoint.bl_idname, text="", icon='ADD')
                header_row.operator('mxt_road.respace_cp_times', text="", icon='TIME')
                header_row.operator(MXTRoad_OT_UpdatePathVisuals.bl_idname, text="", icon='FILE_REFRESH')

            parent_box.prop(road_props, "segment_type")
            if road_props.segment_type == 'BEZIER':
                parent_box.prop(road_props, "rotation_mode")

            conn_box = self.section(parent_box, ui_state, "show_connections", "Connections", icon='LINKED')
            if conn_box:
                row = conn_box.row()
                row.label(text="Previous")
                col = row.column(align=True)
                col.template_list("MXT_UL_SegmentRefs", "", road_props, "prev_segments", road_props, "active_prev_seg_idx", rows=2)
                buttons = col.column(align=True)
                buttons.operator("mxt_road.add_prev_segment", icon='ADD', text="")
                buttons.operator("mxt_road.remove_prev_segment", icon='REMOVE', text="")
                row = conn_box.row()
                row.label(text="Next")
                col = row.column(align=True)
                col.template_list("MXT_UL_SegmentRefs", "", road_props, "next_segments", road_props, "active_next_seg_idx", rows=2)
                buttons = col.column(align=True)
                buttons.operator("mxt_road.add_next_segment", icon='ADD', text="")
                buttons.operator("mxt_road.remove_next_segment", icon='REMOVE', text="")

            if road_props.segment_type == 'BEZIER':
                selected_cp = None
                if obj and obj.parent == active_road_parent and hasattr(obj, "mxt_cp_data") and obj.mxt_cp_data.is_mxt_control_point:
                    selected_cp = obj

                cp_box = self.section(parent_box, ui_state, "show_cp_controls", f"Control Point: {selected_cp.name if selected_cp else 'None'}", icon='EMPTY_SINGLE_ARROW')
                if cp_box:
                    if selected_cp:
                        self.draw_cp_empty_props(cp_box, selected_cp, ui_state)
                    else:
                        cp_box.label(text="Select a child CP Empty to edit its properties.")

            elif road_props.segment_type == 'LINE':
                line_box = self.section(parent_box, ui_state, "show_line_controls", "Line Segment Controls", icon='EMPTY_ARROWS')
                if line_box:
                    line_box.prop(road_props, "line_start_point")
                    line_box.prop(road_props, "line_end_point")

                    if obj and obj == road_props.line_start_point:
                        easing_box = self.section(line_box, ui_state, "show_line_easing", "Edit Easing in Graph Editor")
                        if easing_box:
                            col = easing_box.column(align=True)
                            col.label(text="- Rotation Ease: 'Rotation Ease Factor'", icon='FCURVE')
                            col.label(text="- Scale Ease: 'Scale Ease Factor'", icon='FCURVE')

            elif road_props.segment_type == 'SPIRAL':
                spiral_box = self.section(parent_box, ui_state, "show_spiral_controls", "Spiral Segment Controls", icon='MOD_SCREW')
                if spiral_box:
                    spiral_box.prop(road_props, "spiral_axis_helper")
                    spiral_box.prop(road_props, "spiral_degrees")
                    spiral_box.prop(road_props, "spiral_axis")
                    spiral_box.prop(road_props, "spiral_helper")

                    info_box = self.section(spiral_box, ui_state, "show_spiral_fcurves", "Edit F-Curves on Spiral Helper")
                    if info_box:
                        col = info_box.column(align=True)
                        col.label(text="- Radius: Location X", icon='FCURVE')
                        col.label(text="- Height: Location Y", icon='FCURVE')
                        col.label(text="- Twist: Location Z (degrees)", icon='FCURVE')
                        col.separator()
                        col.label(text="- Road Width: Scale X", icon='FCURVE')
                        col.label(text="- Road Thickness: Scale Y", icon='FCURVE')

                        select_box = info_box.row()
                        op = select_box.operator("mxt_road.select_helper", text="Edit Spiral Curves", icon='GRAPH')
                        op.helper_name = road_props.spiral_helper.name if road_props.spiral_helper else ""
                        select_box.enabled = bool(road_props.spiral_helper)


        common_box = self.section(layout, ui_state, "show_shape_mesh", "Shape and Mesh", icon='MESH_GRID')
        if common_box:
            common_box.prop(road_props, "road_shape_type")

            if road_props.road_shape_type in ('CYLINDER_OPEN', 'PIPE_OPEN', 'ROUNDED_SQUARE_OPEN'):
                open_box = self.section(common_box, ui_state, "show_open_shape", "Open Shape", icon='MOD_SIMPLEDEFORM')
                if open_box:
                    open_box.prop(road_props, "openness_helper")

                    select_row = open_box.row()
                    op = select_row.operator("mxt_road.select_helper", text="Edit Openness Curve", icon='GRAPH')
                    op.helper_name = road_props.openness_helper.name if road_props.openness_helper else ""
                    select_row.enabled = bool(road_props.openness_helper)
                    if road_props.road_shape_type == 'ROUNDED_SQUARE_OPEN':
                        select_row = open_box.row()
                        op = select_row.operator("mxt_road.select_helper", text="Edit Seam Rotation Curve", icon='GRAPH')
                        op.helper_name = road_props.openness_helper.name if road_props.openness_helper else ""
                        select_row.enabled = bool(road_props.openness_helper)

            if road_props.road_shape_type in ('ROUNDED_SQUARE', 'ROUNDED_SQUARE_OPEN'):
                sq_box = self.section(common_box, ui_state, "show_rounded_square", "Rounded Square Helpers", icon='MESH_CUBE')
                if sq_box:
                    sq_box.prop(road_props, "width_helper")
                    width_row = sq_box.row()
                    op = width_row.operator("mxt_road.select_helper", text="Edit Width Curve", icon='GRAPH')
                    op.helper_name = road_props.width_helper.name if road_props.width_helper else ""
                    width_row.enabled = bool(road_props.width_helper)

                    sq_box.prop(road_props, "height_helper")
                    height_row = sq_box.row()
                    op = height_row.operator("mxt_road.select_helper", text="Edit Height Curve", icon='GRAPH')
                    op.helper_name = road_props.height_helper.name if road_props.height_helper else ""
                    height_row.enabled = bool(road_props.height_helper)

                    sq_box.prop(road_props, "radius_helper")
                    radius_row = sq_box.row()
                    op = radius_row.operator("mxt_road.select_helper", text="Edit Radius Curve", icon='GRAPH')
                    op.helper_name = road_props.radius_helper.name if road_props.radius_helper else ""
                    radius_row.enabled = bool(road_props.radius_helper)

            common_box.prop(road_props, "horiz_subdivs")
            common_box.prop(road_props, "road_uv_multiplier")
            mesh_gen_box = self.section(common_box, ui_state, "show_adaptive_mesh", "Adaptive Mesh Settings", icon='MOD_DECIM')
            if mesh_gen_box:
                mesh_gen_box.prop(road_props, "mesh_subdivision_length")
                mesh_gen_box.prop(road_props, "mesh_subdivision_angle_deg")

            rails_box = self.section(common_box, ui_state, "show_rails", "Rails", icon='MOD_SOLIDIFY')
            if rails_box:
                rails_box.prop(road_props, "rail_height_left")
                rails_box.prop(road_props, "rail_height_right")
                row = rails_box.row(align=True)
                row.prop(road_props, "rail_start_left")
                row.prop(road_props, "rail_end_left")
                row = rails_box.row(align=True)
                row.prop(road_props, "rail_start_right")
                row.prop(road_props, "rail_end_right")

            color_box = self.section(common_box, ui_state, "show_colors", "Colors", icon='COLOR')
            if color_box:
                color_box.prop(road_props, "ground_color")
                color_box.prop(road_props, "rail_color")

        mods_box = self.section(layout, ui_state, "show_mods_embeds", "Vertical Modulations & Embeds", icon='FCURVE')
        if mods_box:
            mods_box.prop(road_props, "draw_embeds")
            row = mods_box.row()
            row.template_list("MXT_UL_Modulations", "", road_props, "modulations", road_props, "active_mod_index", rows=3)
            col = row.column(align=True); col.operator("mxt_road.add_modulation", icon='ADD', text=""); col.operator("mxt_road.remove_modulation", icon='REMOVE', text="")

            row = mods_box.row()
            row.template_list("MXT_UL_Embeds", "", road_props, "embeds", road_props, "active_embed_idx", rows=3)
            col = row.column(align=True); col.operator("mxt_road.add_embed", icon='ADD', text=""); col.operator("mxt_road.remove_embed", icon='REMOVE', text="")

            if road_props.embeds and 0 <= road_props.active_embed_idx < len(road_props.embeds):
                emb = road_props.embeds[road_props.active_embed_idx]
                emb_box = self.section(mods_box, ui_state, "show_active_embed", f"Active Embed: {emb.label}", icon='DOT')
                if emb_box:
                    emb_box.prop(emb, "label"); emb_box.prop(emb, "embed_type")
                    emb_box.prop(emb, "start_t"); emb_box.prop(emb, "end_t")
                    emb_box.prop(emb, "helper", text="Helper Empty")

        data_box = self.section(layout, ui_state, "show_data_generation", "Data and Generation", icon='EXPORT')
        if data_box:
            data_box.prop(road_props, "num_checkpoints_per_segment")
            data_box.prop(road_props, "analytic_collision_enabled")
            data_box.prop(road_props, "cpu_nav_allow_glide_drops")
            nav_settings_box = data_box.box()
            nav_settings_box.label(text="CPU Nav Segment Overrides")
            nav_settings_box.prop(road_props, "cpu_nav_lateral_samples")
            nav_settings_box.prop(road_props, "cpu_nav_row_spacing_meters")
            nav_settings_box.prop(road_props, "cpu_nav_transition_distance")
            nav_settings_box.prop(road_props, "cpu_nav_branch_distance")
            data_box.prop(road_props, "disable_preview_mesh_generation")
            preview_collision_row = data_box.row()
            preview_collision_row.enabled = not bool(road_props.disable_preview_mesh_generation)
            preview_collision_row.prop(road_props, "export_preview_mesh_collision")
            data_box.prop(road_props, "disable_auto_rebake")
            data_box.separator()
            data_box.operator("mxt_road.generate_curve_matrix", text="Generate CurveMatrix", icon='FCURVE')
            data_box.operator("mxt_road.generate_mesh", text="Generate/Update Mesh", icon='MESH_PLANE')
            data_box.operator("mxt_road.generate_checkpoints", text="Generate Checkpoints", icon='OUTLINER_OB_EMPTY')
            data_box.operator("mxt_road.preview_cpu_nav_graph", text="Preview CPU Nav Graph", icon='PARTICLE_POINT')
            data_box.operator("mxt_road.bake_cpu_nav", text="Bake CPU Nav", icon='TRACKING_FORWARDS')
            data_box.operator("mxt_road.export_track", text="Export Track", icon='EXPORT')
