bl_info = {
    "name": "MXT Racetrack Road Creator",
    "author": "Twilight",
    "version": (0, 1, 1),
    "blender": (4, 0, 0),
    "location": "3D View > Sidebar (N-Panel) > MXT Road Creator",
    "description": "Design a racetrack for Maxx Throttle!",
    "warning": "",
    "doc_url": "",
    "category": "Object",
}
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

@contextmanager
def _no_undo():
    prefs = bpy.context.preferences.edit
    orig  = prefs.use_global_undo
    try:
        if orig:
            prefs.use_global_undo = False
        yield
    finally:
        if orig:
            prefs.use_global_undo = True

MXT_PROFILE_BUILDS = True

MXT_NAV_FORMAT_VERSION = 1
MXT_NAV_TERRAIN_BITS = {
    "normal": 0x1,
    "dash": 0x2,
    "recharge": 0x4,
    "dirt": 0x8,
    "jump": 0x10,
    "lava": 0x20,
    "ice": 0x40,
    "hole": 0x200,
}
MXT_NAV_NODE_FLAGS = {
    "preferred": 0x1,
    "avoid": 0x2,
    "dash": 0x4,
    "recharge": 0x8,
    "jump": 0x10,
    "branch": 0x20,
    "manual": 0x40,
    "mine": 0x80,
    "dirt": 0x100,
    "lava": 0x200,
    "ice": 0x400,
}
MXT_NAV_EDGE_FLAGS = {
    "normal": 0x1,
    "transition": 0x2,
    "branch": 0x4,
    "jump": 0x8,
    "manual": 0x10,
    "lateral": 0x20,
    "glide_drop": 0x40,
}

@contextmanager
def _mxt_profile_scope(label):
    start = time.perf_counter()
    yield
    if MXT_PROFILE_BUILDS:
        elapsed_ms = (time.perf_counter() - start) * 1000.0
        print(f"MXT_PROFILE {label}: {elapsed_ms:.2f} ms")

class _MXTProfiler:
    def __init__(self, label):
        self.label = label
        self.start = time.perf_counter()
        self.last = self.start
        self.parts = []

    def mark(self, name):
        if not MXT_PROFILE_BUILDS:
            return
        now = time.perf_counter()
        self.parts.append((name, (now - self.last) * 1000.0))
        self.last = now

    def finish(self):
        if not MXT_PROFILE_BUILDS:
            return
        total_ms = (time.perf_counter() - self.start) * 1000.0
        detail = ", ".join(f"{name}={ms:.1f}" for name, ms in self.parts)
        print(f"MXT_PROFILE {self.label}: total={total_ms:.1f} ms" + (f" ({detail})" if detail else ""))

def _disallow_deletion(obj):
    if obj and hasattr(obj, "can_user_delete"):
        obj.can_user_delete = False
ROAD_SHAPE_TYPE_ITEMS = [
    ('FLAT', "Flat", "Flat Road Segment"),
    ('CYLINDER', "Cylinder", "Cylindrical Shape (exterior"),
    ('PIPE', "Pipe", "Pipe Shape (interior)"),
    ('CYLINDER_OPEN', "Open Cylinder", "Open Cylindrical Shape"),
    ('PIPE_OPEN', "Open Pipe", "Open Pipe Shape (interior)"),
    ('ROUNDED_SQUARE', "Rounded Square", "Rounded square cross-section"),
    ('ROUNDED_SQUARE_OPEN', "Open Rounded Square", "Rounded square with gap"),
    ('TUNNEL', "Tunnel", "Flat road with side rails and a half-pipe ceiling"),
]

class MXTEmbed(bpy.types.PropertyGroup):
    label:          StringProperty(name="Label", default="Embed")
    helper:         PointerProperty(type=bpy.types.Object)
    start_t:        FloatProperty(name="Start t", min=0.0, max=1.0, default=0.25)
    end_t:          FloatProperty(name="End t",   min=0.0, max=1.0, default=0.75)
    embed_type: EnumProperty(
        name="Type",
        items=[('RECHARGE',"Recharge",""), ('DIRT',"Dirt",""),
               ('ICE',"Ice",""), ('LAVA',"Lava",""), ('HOLE',"Hole","")],
        default='RECHARGE')
class MXTCheckpoint(bpy.types.PropertyGroup):
    
    start_t:    FloatProperty(name="t₀", min=0.0, max=1.0)
    end_t:      FloatProperty(name="t₁", min=0.0, max=1.0)
    
    pos_start:  FloatVectorProperty(size=3)
    pos_end:    FloatVectorProperty(size=3)
    basis_start:    FloatVectorProperty(size=9) 
    basis_end:  FloatVectorProperty(size=9)
    x_rad_start:    FloatProperty()
    x_rad_end:  FloatProperty()
    y_rad_start:    FloatProperty()
    y_rad_end:  FloatProperty()
    distance:   FloatProperty()
class MXTModulation(PropertyGroup):
    label: StringProperty(name="Name", default="Modulation")
    helper: PointerProperty(type=bpy.types.Object)

class MXTTriggerObject(PropertyGroup):
    label: StringProperty(name="Label", default="Trigger")
    helper: PointerProperty(type=bpy.types.Object)
    preview_mesh: PointerProperty(type=bpy.types.Object)
    obj_type: EnumProperty(
        name="Type",
        items=[('DASHPLATE',"Dashplate",""),
               ('JUMPPLATE',"Jumpplate",""),
               ('MINE',"Mine","")],
        default='DASHPLATE')
    segment: PointerProperty(
        name="Segment",
        type=bpy.types.Object,
        poll=lambda self,obj: (obj and getattr(obj, "mxt_road_overall_props", None) \
            and obj.mxt_road_overall_props.is_mxt_road_segment_parent),
        update=lambda self,ctx:_update_trigger_helper(self)
    )
    tx: FloatProperty(name="t_x", default=0.0, min=-1.0, max=1.0,
                      update=lambda self,ctx:_update_trigger_helper(self))
    ty: FloatProperty(name="t_y", default=0.0, min=0.0, max=1.0,
                      update=lambda self,ctx:_update_trigger_helper(self))
    scale: FloatVectorProperty(name="Scale", size=3, default=(1.0,1.0,1.0),
                               update=lambda self,ctx:_update_trigger_helper(self))
    yaw_deg: FloatProperty(name="Add Yaw", default=0.0,
                           update=lambda self,ctx:_update_trigger_helper(self))
    checkpoint_index: IntProperty(name="Checkpoint", default=0, min=0)

MESH_COLLISION_SURFACE_ITEMS = [
    ('TRACK', "Track", "Drivable mesh surface"),
    ('RAIL', "Rail", "Rail or wall mesh surface"),
    ('RECHARGE', "Recharge", "Recharge terrain mesh surface"),
    ('DIRT', "Dirt", "Dirt terrain mesh surface"),
    ('ICE', "Ice", "Ice terrain mesh surface"),
    ('LAVA', "Lava", "Lava terrain mesh surface"),
    ('HOLE', "Hole", "Hole terrain mesh surface"),
    ('FALL', "Fall", "Fallout trigger mesh surface with no collision response"),
    ('KILL', "Kill", "Fatal rail-like mesh collision surface"),
    ('DASH', "Dash", "Dash terrain mesh surface"),
    ('JUMP', "Jump", "Jump terrain mesh surface"),
]

class MXTMeshCollisionProperties(PropertyGroup):
    is_mxt_collision_mesh: BoolProperty(name="Export Mesh Collision", default=False)
    is_mxt_visual_mesh: BoolProperty(
        name="Export Visual Mesh",
        description="Include this mesh in the exported OBJ without using it as track collision",
        default=False)
    surface_type: EnumProperty(
        name="Surface",
        items=MESH_COLLISION_SURFACE_ITEMS,
        default='TRACK')
    double_sided: BoolProperty(
        name="Double Sided",
        description="Allow this authored mesh collision to be hit from either triangle side",
        default=False)

class MXT_UL_Modulations(bpy.types.UIList):
    def draw_item(self, context, layout, data, item, icon, active_data, active_propname, index):
        layout.prop(item, "label", text="", emboss=False, icon='FCURVE')

class MXT_UL_TriggerObjects(bpy.types.UIList):
    def draw_item(self, ctx, layout, data, item, icon, active_data, active_propname, index):
        row = layout.row(align=True)
        row.prop(item, "label", text="", emboss=False, icon='EMPTY_AXIS')
        row.prop(item, "obj_type", text="", emboss=False, icon='DOT')

class MXTTrackEditorUIState(PropertyGroup):
    show_track_identity: BoolProperty(name="Track", default=True)
    show_global_params: BoolProperty(name="Global Track Params", default=False)
    show_lighting: BoolProperty(name="Lighting", default=False)
    show_triggers: BoolProperty(name="Trigger Objects", default=False)
    show_active_trigger: BoolProperty(name="Active Trigger", default=True)
    show_mesh_export: BoolProperty(name="MXT Mesh Export", default=True)
    show_segment: BoolProperty(name="Segment", default=True)
    show_connections: BoolProperty(name="Connections", default=False)
    show_cp_controls: BoolProperty(name="Control Point", default=True)
    show_cp_easing: BoolProperty(name="CP Easing F-Curves", default=False)
    show_line_controls: BoolProperty(name="Line Controls", default=True)
    show_line_easing: BoolProperty(name="Line Easing F-Curves", default=False)
    show_spiral_controls: BoolProperty(name="Spiral Controls", default=True)
    show_spiral_fcurves: BoolProperty(name="Spiral F-Curves", default=False)
    show_shape_mesh: BoolProperty(name="Shape and Mesh", default=False)
    show_open_shape: BoolProperty(name="Open Shape", default=False)
    show_rounded_square: BoolProperty(name="Rounded Square Helpers", default=False)
    show_adaptive_mesh: BoolProperty(name="Adaptive Mesh Settings", default=False)
    show_rails: BoolProperty(name="Rails", default=False)
    show_colors: BoolProperty(name="Colors", default=False)
    show_mods_embeds: BoolProperty(name="Vertical Modulations & Embeds", default=False)
    show_active_embed: BoolProperty(name="Active Embed", default=True)
    show_data_generation: BoolProperty(name="Data and Generation", default=True)

mxt_roads_pending_visual_update = set()
mxt_timer_is_active = False
_build_in_progress  = False     
_ignore_updates     = False     
_MXT_NAV_DRAW_CACHE = None
class MXTRoad_ControlPointData(PropertyGroup):
    is_mxt_control_point: BoolProperty(default=False)
    time: FloatProperty(
        name="Time", default=0.0, min=0.0, max=1.0,
        description="Normalized time (0-1) for this control point. Modifying this flags for visual update.",
        update=lambda self, context: schedule_road_parent_visual_update(self.id_data, context)
    )
    handle_in_length: FloatProperty(
        name="Handle In Length", default=100.0, min=0.001,
        update=lambda self, context: schedule_road_parent_visual_update(self.id_data, context)
    )
    handle_out_length: FloatProperty(
        name="Handle Out Length", default=100.0, min=0.001,
        update=lambda self, context: schedule_road_parent_visual_update(self.id_data, context)
    )
    rotation_ease_factor_channel: FloatProperty(
        name="Rotation Ease Factor", subtype='NONE', unit='NONE', default=0.0,
        description="Animatable channel for rotation easing F-Curve (0-1 output expected)",
        options={'ANIMATABLE'}
    )
    scale_ease_factor_channel: FloatProperty(
        name="Scale Ease Factor", subtype='NONE', unit='NONE', default=0.0,
        description="Animatable channel for scale easing F-Curve (0-1 output expected)",
        options={'ANIMATABLE'}
    )
    twist_ease_factor_channel: FloatProperty(
        name="Twist Ease Factor", subtype='NONE', unit='NONE', default=0.0,
        description="Animatable channel for twist easing F-Curve (0-1 output expected)",
        options={'ANIMATABLE'}
    )

class MXTRoad_LineHandleData(PropertyGroup):
    is_mxt_line_handle: BoolProperty(default=False)
    
    rotation_ease_factor_channel: FloatProperty(
        name="Rotation Ease Factor", subtype='NONE', unit='NONE', default=0.0,
        description="Animatable channel for rotation easing F-Curve (0-1 output expected)",
        options={'ANIMATABLE'}
    )
    scale_ease_factor_channel: FloatProperty(
        name="Scale Ease Factor", subtype='NONE', unit='NONE', default=0.0,
        description="Animatable channel for scale easing F-Curve (0-1 output expected)",
        options={'ANIMATABLE'}
    )


def mxt_segment_ref_update(self, context):
    obj = self.segment
    if not obj:
        return
    # Allow selecting a PreviewMesh and automatically use its parent segment
    if obj.type == 'MESH' and obj.name.endswith('_PreviewMesh') and obj.parent:
        parent = obj.parent
        if getattr(parent, "mxt_road_overall_props", None) and \
                parent.mxt_road_overall_props.is_mxt_road_segment_parent:
            if self.segment != parent:
                self.segment = parent
        else:
            self.segment = None
    elif not (getattr(obj, "mxt_road_overall_props", None) and \
              obj.mxt_road_overall_props.is_mxt_road_segment_parent):
        parent = obj.parent
        if parent and getattr(parent, "mxt_road_overall_props", None) and \
                parent.mxt_road_overall_props.is_mxt_road_segment_parent:
            self.segment = parent
        else:
            self.segment = None


class MXTSegmentRef(PropertyGroup):
    segment: PointerProperty(
        name="Segment",
        type=bpy.types.Object,
        poll=lambda self, obj: ((obj.type == 'EMPTY' and \
            getattr(obj, "mxt_road_overall_props", None) and \
            obj.mxt_road_overall_props.is_mxt_road_segment_parent) or \
            (obj.type == 'MESH' and obj.name.endswith('_PreviewMesh'))),
        update=mxt_segment_ref_update
    )


class MXTTrackSettings(PropertyGroup):
    first_segment: PointerProperty(
        name="First Segment",
        type=bpy.types.Object,
        poll=lambda self, obj: obj.type == 'EMPTY'
    )

    track_name: StringProperty(
        name="Track Name",
        default="New Track"
    )

    track_description: StringProperty(
        name="Description",
        default=""
    )

    visual_scene_path: StringProperty(
        name="Visual Scene",
        description="Optional track-local Godot scene path to load for visuals, e.g. track.tscn",
        default=""
    )

    track_difficulty: IntProperty(
        name="Difficulty",
        default=1,
        min=1,
        max=10
    )

    # Global track parameters for export (JSON)
    fog_distance: FloatProperty(
        name="Fog Distance",
        description="Distance at which fog fully obscures the scene",
        default=2000.0,
        min=0.0,
        soft_max=10000.0,
    )
    sky_top_color: FloatVectorProperty(
        name="Sky Top Color",
        description="Sky color at zenith",
        subtype='COLOR',
        size=3,
        min=0.0,
        max=1.0,
        default=(0.0, 0.1, 0.25)
    )
    sky_horizon_color: FloatVectorProperty(
        name="Sky Horizon Color",
        description="Sky color at the horizon",
        subtype='COLOR',
        size=3,
        min=0.0,
        max=1.0,
        default=(0.2, 0.25, 0.3)
    )
    sky_ground_color: FloatVectorProperty(
        name="Sky Ground Color",
        description="Sky gradient color at the ground",
        subtype='COLOR',
        size=3,
        min=0.0,
        max=1.0,
        default=(0.02, 0.02, 0.02)
    )
    ground_color_global: FloatVectorProperty(
        name="Ground Color",
        description="Base ground color (global)",
        subtype='COLOR',
        size=3,
        min=0.0,
        max=1.0,
        default=(0.08, 0.08, 0.08)
    )
    ground_height: FloatProperty(
        name="Ground Height",
        description="World height of the ground plane relative to track origin",
        default=0.0,
        soft_min=-5000.0,
        soft_max=5000.0,
    )
    cloud_color: FloatVectorProperty(
        name="Cloud Color",
        description="Cloud tint color",
        subtype='COLOR',
        size=3,
        min=0.0,
        max=1.0,
        default=(1.0, 1.0, 1.0)
    )
    cloud_height: FloatProperty(
        name="Cloud Height",
        description="Approximate cloud layer height",
        default=800.0,
        min=-10000.0,
        soft_max=10000.0,
    )

    light_color: FloatVectorProperty(
        name="Light Color",
        description="Directional light color",
        subtype='COLOR',
        size=3,
        min=0.0,
        max=1.0,
        default=(1.0, 0.95, 0.9)
    )
    light_intensity: FloatProperty(
        name="Light Intensity",
        description="Directional light intensity (arbitrary units)",
        default=1.0,
        min=0.0,
        soft_max=10.0,
    )
    ambient_intensity: FloatProperty(
        name="Ambient Intensity",
        description="Ambient light intensity",
        default=0.1,
        min=0.0,
        soft_max=5.0,
    )
    ambient_color: FloatVectorProperty(
        name="Ambient Color",
        description="Ambient light color",
        subtype='COLOR',
        size=3,
        min=0.0,
        max=1.0,
        default=(0.15, 0.15, 0.18)
    )
    light_direction: FloatVectorProperty(
        name="Light Direction",
        description="World-space direction the light points from (not auto-normalized)",
        size=3,
        subtype='DIRECTION',
        default=(0.3, -1.0, 0.4),
        soft_min=-1.0,
        soft_max=1.0,
    )

    draw_checkpoints: BoolProperty(name="Draw Checkpoints", default=False)
    export_cpu_nav: BoolProperty(
        name="Export CPU Nav",
        description="Bake an autogenerated CPU navigation graph next to the .mxt_track file",
        default=True
    )
    draw_cpu_nav: BoolProperty(
        name="Draw CPU Nav",
        description="Draw the most recently baked CPU navigation graph in the viewport",
        default=False
    )
    draw_cpu_nav_routes: BoolProperty(
        name="Draw CPU Nav Routes",
        description="Draw pathfinder routes from the most recently baked CPU navigation graph",
        default=True
    )
    cpu_nav_lateral_samples: IntProperty(
        name="Nav Lateral Samples",
        description="Number of tx lanes sampled across each road segment for CPU navigation",
        default=9,
        min=3,
        max=33
    )
    cpu_nav_row_spacing_meters: FloatProperty(
        name="Nav Row Spacing",
        description="Approximate meters between longitudinal nav rows; segment starts and ends always get rows",
        default=30.0,
        min=1.0,
        soft_max=250.0
    )
    cpu_nav_transition_distance: FloatProperty(
        name="Nav Transition Distance",
        description="Maximum world-space distance for connecting authored segment transitions",
        default=32.0,
        min=0.0,
        soft_max=500.0
    )
    cpu_nav_branch_distance: FloatProperty(
        name="Nav Branch Distance",
        description="Maximum world-space distance for connecting physically touching branch roads",
        default=20.0,
        min=0.0,
        soft_max=500.0
    )
    trigger_objects: CollectionProperty(type=MXTTriggerObject)
    active_trigger_obj_idx: IntProperty(default=0)


def mxt_segment_type_update(self, context):
    if get_active_mxt_road_segment_parent(context):
        
         bpy.ops.mxt_road.convert_segment_type('EXEC_DEFAULT')
    return None


_object_to_select_deferred = None

def _deferred_select():
    global _object_to_select_deferred
    obj_to_select = _object_to_select_deferred
    _object_to_select_deferred = None  

    if obj_to_select and bpy.data.objects.get(obj_to_select.name):
        try:
            
            if bpy.context.object and bpy.context.object.mode != 'OBJECT':
                bpy.ops.object.mode_set(mode='OBJECT')
            
            bpy.ops.object.select_all(action='DESELECT')
            obj_to_select.select_set(True)
            bpy.context.view_layer.objects.active = obj_to_select
        except Exception as e:
            print(f"MXT Deferred Select Error: {e}")
    return None  

def schedule_deferred_select(obj):
    global _object_to_select_deferred
    _object_to_select_deferred = obj
    
    if not bpy.app.timers.is_registered(_deferred_select):
        bpy.app.timers.register(_deferred_select)

def mxt_active_mod_index_update(self, context):
    if 0 <= self.active_mod_index < len(self.modulations):
        mod = self.modulations[self.active_mod_index]
        if mod.helper:
            schedule_deferred_select(mod.helper)

def mxt_active_embed_idx_update(self, context):
    if 0 <= self.active_embed_idx < len(self.embeds):
        emb = self.embeds[self.active_embed_idx]
        if emb.helper:
            schedule_deferred_select(emb.helper)

def mxt_road_shape_type_update(self, context):
    
    parent = self.id_data 

    
    if self.road_shape_type in ('CYLINDER_OPEN', 'PIPE_OPEN', 'ROUNDED_SQUARE_OPEN'):

        if parent.name in _openness_helper_to_destroy:
             _openness_helper_to_destroy.remove(parent.name)
        _openness_helper_to_create.add(parent.name)

    else:

        if parent.name in _openness_helper_to_create:
            _openness_helper_to_create.remove(parent.name)
        _openness_helper_to_destroy.add(parent.name)

    if self.road_shape_type in ('ROUNDED_SQUARE', 'ROUNDED_SQUARE_OPEN'):
        _square_helpers_to_destroy.discard(parent.name)
        _square_helpers_to_create.add(parent.name)
    else:
        _square_helpers_to_create.discard(parent.name)
        _square_helpers_to_destroy.add(parent.name)
    
    
    schedule_mesh_build(parent)

class MXTRoad_RoadSegmentOverallProperties(PropertyGroup):
    is_mxt_road_segment_parent: BoolProperty(default=False)
    curve_matrix_helper_empty: PointerProperty(type=bpy.types.Object, poll=lambda self, object: object.type == 'EMPTY')
    visual_guide_curve: PointerProperty(type=bpy.types.Object, poll=lambda self, object: object.type == 'CURVE')
    road_shape_type: EnumProperty(name="Road Shape Type", items=ROAD_SHAPE_TYPE_ITEMS, default='FLAT',
    update=mxt_road_shape_type_update)
    horiz_subdivs: IntProperty(
        name="Horizontal Subdivisions",
        description="How many vertex columns across the road width",
        default=5,
        min=1,
        soft_max=65,
        update=lambda self, ctx: schedule_mesh_build(self.id_data)
    )
    road_uv_multiplier: FloatProperty(name="UV Y-Multiplier", default=1.0,
    update=lambda self, ctx: schedule_mesh_build(self.id_data))
    mesh_subdivision_length: FloatProperty(name="Mesh Subdiv Length", default=20.0, min=0.1,
    update=lambda self, ctx: schedule_mesh_build(self.id_data))
    mesh_subdivision_angle_deg: FloatProperty(name="Mesh Subdiv Angle", default=8.0, min=0.1, max=90.0,
    update=lambda self, ctx: schedule_mesh_build(self.id_data))
    num_checkpoints_per_segment: IntProperty(name="Checkpoints in Segment", default=8, min=0)
    analytic_collision_enabled: BoolProperty(
        name="Analytic Collision",
        description="Use this road segment's procedural shape for collision; disable for mesh-only collision while keeping checkpoints and CPU navigation",
        default=True
    )
    cpu_nav_allow_glide_drops: BoolProperty(
        name="CPU Nav Glide Drops",
        description="Allow this segment to participate in authored higher-to-lower CPU nav drop/glide links; disable to use only spatial proximity links",
        default=True
    )
    export_preview_mesh_collision: BoolProperty(
        name="Preview Mesh Collision",
        description="Export this segment's generated preview mesh as authored mesh collision",
        default=False
    )
    disable_preview_mesh_generation: BoolProperty(
        name="Disable Preview Mesh",
        description="Do not generate or export this segment's procedural preview mesh",
        default=False,
        update=lambda self, ctx: schedule_mesh_build(self.id_data)
    )

    rail_height_left: FloatProperty(
        name="Left Rail Height",
        description="Height of the left rail above the road surface",
        default=0.15,
        min=0.0,
        update=lambda self, ctx: schedule_mesh_build(self.id_data)
    )

    rail_height_right: FloatProperty(
        name="Right Rail Height",
        description="Height of the right rail above the road surface",
        default=0.15,
        min=0.0,
        update=lambda self, ctx: schedule_mesh_build(self.id_data)
    )
    rail_start_left: FloatProperty(
        name="Left Rail Start",
        description="t_y where the left rail starts",
        default=0.0,
        min=0.0,
        max=1.0,
        update=lambda self, ctx: schedule_mesh_build(self.id_data)
    )
    rail_end_left: FloatProperty(
        name="Left Rail End",
        description="t_y where the left rail ends",
        default=1.0,
        min=0.0,
        max=1.0,
        update=lambda self, ctx: schedule_mesh_build(self.id_data)
    )
    rail_start_right: FloatProperty(
        name="Right Rail Start",
        description="t_y where the right rail starts",
        default=0.0,
        min=0.0,
        max=1.0,
        update=lambda self, ctx: schedule_mesh_build(self.id_data)
    )
    rail_end_right: FloatProperty(
        name="Right Rail End",
        description="t_y where the right rail ends",
        default=1.0,
        min=0.0,
        max=1.0,
        update=lambda self, ctx: schedule_mesh_build(self.id_data)
    )
    # Appearance per-segment
    ground_color: FloatVectorProperty(
        name="Ground Color",
        description="Color for main road surface (vertex color)",
        subtype='COLOR',
        size=3,
        min=0.0,
        max=1.0,
        default=(0.5, 0.5, 0.5),
        update=lambda self, ctx: schedule_mesh_build(self.id_data)
    )
    rail_color: FloatVectorProperty(
        name="Rail Color",
        description="Color for rail surfaces (vertex color)",
        subtype='COLOR',
        size=3,
        min=0.0,
        max=1.0,
        default=(0.8, 0.8, 0.8),
        update=lambda self, ctx: schedule_mesh_build(self.id_data)
    )
    modulations: CollectionProperty(type=MXTModulation)
    active_mod_index: IntProperty(
        name="Active Modulation Index",
        default=0,
        update=mxt_active_mod_index_update
    )
    checkpoints:    CollectionProperty(type=MXTCheckpoint)
    draw_checkpoints: BoolProperty(name="Draw Checkpoints", default=False)
    active_cp_idx:  IntProperty(default=-1)
    embeds:             CollectionProperty(type=MXTEmbed)
    active_embed_idx:  IntProperty(
        name="Active Embed Index",
        default=0,
        update=mxt_active_embed_idx_update
    )
    draw_embeds:        BoolProperty(name="Draw Embeds", default=False)
    
    
    segment_type: EnumProperty(
        name="Segment Type",
        items=[('BEZIER', "Bezier", "Use multiple empties to define a Bezier path"),
               ('LINE', "Line", "Linearly interpolate between two points"),
               ('SPIRAL', "Spiral", "A procedural spiral/helix segment")],
        default='BEZIER',
        description="Choose the method for generating the road segment's path",

        update=mxt_segment_type_update
    )

    rotation_mode: EnumProperty(
        name="Rotation Mode",
        items=[('SMART', "Smart", "Solve orientation along path"),
               ('SIMPLE', "Simple", "Direct slerp between control point rotations")],
        default='SMART',
        description="Orientation calculation method for Bezier segments",
        update=lambda self, ctx: schedule_cm_rebake(self.id_data)
    )

    
    line_start_point: PointerProperty(
        name="Start Point",
        type=bpy.types.Object,
        description="The object controlling the start transform of the line segment",
        poll=lambda self, object: object.type == 'EMPTY'
    )
    line_end_point: PointerProperty(
        name="End Point",
        type=bpy.types.Object,
        description="The object controlling the end transform of the line segment",
        poll=lambda self, object: object.type == 'EMPTY'
    )

    
    spiral_degrees: FloatProperty(
        name="Total Degrees", default=90.0,
        description="How many degrees to rotate around the axis over the segment length",
        update=lambda self, ctx: schedule_cm_rebake(self.id_data)
    )
    spiral_axis: FloatVectorProperty(
        name="Axis",
        description="The axis of rotation for the spiral (will be normalized)",
        default=(0.0, 1.0, 0.0),
        size=3,
        update=lambda self, ctx: schedule_cm_rebake(self.id_data)
    )
    spiral_helper: PointerProperty(
        name="Spiral Helper",
        description="Empty containing F-Curves for Radius (Loc.X), Height (Loc.Y), and Twist (Loc.Z)",
        type=bpy.types.Object,
        poll=lambda self, object: object.type == 'EMPTY'
    )
    spiral_axis_helper: PointerProperty( 
        name="Axis Helper",
        description="Optional Empty to define the spiral's origin and axis (its local Z-axis)",
        type=bpy.types.Object,
        poll=lambda self, object: object.type == 'EMPTY'
    )
    openness_helper: PointerProperty(
        name="Openness Helper",
        description="Empty containing an F-Curve on its X-Location to control the gap size (0-1)",
        type=bpy.types.Object,
        poll=lambda self, object: object.type == 'EMPTY'
    )
    width_helper: PointerProperty(
        name="Width Helper",
        description="Empty with X-Location F-Curve controlling flat width",
        type=bpy.types.Object,
        poll=lambda self, object: object.type == 'EMPTY',
        update=lambda self, ctx: schedule_mesh_build(self.id_data)
    )
    height_helper: PointerProperty(
        name="Height Helper",
        description="Empty with X-Location F-Curve controlling flat height",
        type=bpy.types.Object,
        poll=lambda self, object: object.type == 'EMPTY',
        update=lambda self, ctx: schedule_mesh_build(self.id_data)
    )
    radius_helper: PointerProperty(
        name="Radius Helper",
        description="Empty with X-Location F-Curve controlling corner radius",
        type=bpy.types.Object,
        poll=lambda self, object: object.type == 'EMPTY',
        update=lambda self, ctx: schedule_mesh_build(self.id_data)
    )
    preview_mesh_exists: BoolProperty(
        name="Preview Mesh Exists",
        description="Internal flag tracking if this segment has a preview mesh",
        default=False
    )
    disable_auto_rebake: BoolProperty(
        name="Disable Auto Rebake",
        description="Do not automatically rebake this segment's curve matrix when primary controls change",
        default=False
    )
    prev_segments: CollectionProperty(type=MXTSegmentRef)
    next_segments: CollectionProperty(type=MXTSegmentRef)
    active_prev_seg_idx: IntProperty(default=0)
    active_next_seg_idx: IntProperty(default=0)


class MXT_UL_Embeds(bpy.types.UIList):
    def draw_item(self, ctx, layout, data, item, icon, active_data, active_propname, index):
        row = layout.row(align=True)
        row.prop(item, "label", text="", emboss=False, icon='FCURVE')
        row.prop(item, "embed_type", text="", emboss=False, icon='NODE')

class MXTRoad_OT_AddEmbed(Operator):
    bl_idname = "mxt_road.add_embed"; bl_label = "Add Embed"
    bl_options = {'REGISTER', 'UNDO'}

    def execute(self, ctx):
        seg = get_active_mxt_road_segment_parent(ctx)
        if not seg: return {'CANCELLED'}
        props = seg.mxt_road_overall_props

        bpy.ops.object.empty_add(type='SPHERE', radius=0, location=seg.location)
        helper = ctx.active_object
        _disallow_deletion(helper)
        helper.name = f"{seg.name}_Embed_{len(props.embeds):02d}"
        helper.parent = seg

        helper.animation_data_create()
        act = bpy.data.actions.new(f"{helper.name}_embedCurves")
        helper.animation_data.action = act
        
        
        for idx,val in ((1,-1.0), (2,1.0)): 
            helper.location[idx] = val * 0.5
            helper.keyframe_insert(data_path="location", index=idx, frame=0.0)
            helper.keyframe_insert(data_path="location", index=idx, frame=100.0)
            
            fcu = act.fcurves.find("location", index=idx)
            if fcu:
                for kp in fcu.keyframe_points:
                    kp.interpolation = 'BEZIER'
                    kp.handle_left_type = "LINEAR_X"
                    kp.handle_right_type = "LINEAR_X"
                _linearize_fcurve_handles_smooth(fcu)

        emb = props.embeds.add()
        emb.label = f"Embed {len(props.embeds)}"
        emb.helper = helper
        props.active_embed_idx = len(props.embeds)-1
        return {'FINISHED'}

class MXTRoad_OT_RemoveEmbed(Operator):
    bl_idname = "mxt_road.remove_embed"; bl_label = "Remove Embed"
    bl_options = {'REGISTER', 'UNDO'}

    def execute(self, ctx):
        seg = get_active_mxt_road_segment_parent(ctx)
        props = seg.mxt_road_overall_props
        idx = props.active_embed_idx
        if idx < 0 or idx >= len(props.embeds): return {'CANCELLED'}
        emb = props.embeds[idx]
        if emb.helper:
            if emb.helper.animation_data and emb.helper.animation_data.action:
                act = emb.helper.animation_data.action
                if act.users == 1: bpy.data.actions.remove(act)
            bpy.data.objects.remove(emb.helper, do_unlink=True)
        props.embeds.remove(idx)
        props.active_embed_idx = min(max(0, idx-1), len(props.embeds)-1)
        return {'FINISHED'}

class MXT_OT_AddTrigger(Operator):
    bl_idname = "mxt_track.add_trigger"
    bl_label = "Add Trigger Object"
    bl_options = {'REGISTER', 'UNDO'}

    def execute(self, context):
        ts = context.scene.mxt_track_settings
        if not ts:
            return {'CANCELLED'}
        seg = get_active_mxt_road_segment_parent(context)

        bpy.ops.object.empty_add(type='PLAIN_AXES', radius=4.0, location=context.scene.cursor.location)
        helper = context.active_object
        _disallow_deletion(helper)
        helper.name = f"Trigger_{len(ts.trigger_objects):02d}"

        trig = ts.trigger_objects.add()
        trig.label = helper.name
        trig.helper = helper
        trig.segment = seg

        mesh_name = f"MESH_{trig.obj_type}"
        mesh = bpy.data.meshes.get(mesh_name)
        if mesh:
            mobj = bpy.data.objects.new(f"{helper.name}_Preview", mesh)
            context.collection.objects.link(mobj)
            mobj.parent = helper
            trig.preview_mesh = mobj

        ts.active_trigger_obj_idx = len(ts.trigger_objects) - 1
        _update_trigger_helper(trig)
        return {'FINISHED'}

class MXT_OT_RemoveTrigger(Operator):
    bl_idname = "mxt_track.remove_trigger"
    bl_label = "Remove Trigger Object"
    bl_options = {'REGISTER', 'UNDO'}

    def execute(self, context):
        ts = context.scene.mxt_track_settings
        idx = ts.active_trigger_obj_idx
        if idx < 0 or idx >= len(ts.trigger_objects):
            return {'CANCELLED'}
        trig = ts.trigger_objects[idx]
        if trig.preview_mesh and bpy.data.objects.get(trig.preview_mesh.name):
            bpy.data.objects.remove(trig.preview_mesh, do_unlink=True)
        if trig.helper and bpy.data.objects.get(trig.helper.name):
            bpy.data.objects.remove(trig.helper, do_unlink=True)
        ts.trigger_objects.remove(idx)
        ts.active_trigger_obj_idx = min(max(0, idx-1), len(ts.trigger_objects)-1)
        return {'FINISHED'}

class MXT_UL_SegmentRefs(bpy.types.UIList):
    def draw_item(self, ctx, layout, data, item, icon, active_data, active_propname, index):
        layout.prop(item, "segment", text="", emboss=False, icon='EMPTY_AXIS')

class MXTRoad_OT_AddPrevSegment(Operator):
    bl_idname = "mxt_road.add_prev_segment"; bl_label = "Add Prev Segment"
    bl_options = {'REGISTER', 'UNDO'}

    def execute(self, ctx):
        seg = get_active_mxt_road_segment_parent(ctx)
        if not seg: return {'CANCELLED'}
        props = seg.mxt_road_overall_props
        props.prev_segments.add()
        props.active_prev_seg_idx = len(props.prev_segments) - 1
        return {'FINISHED'}

class MXTRoad_OT_RemovePrevSegment(Operator):
    bl_idname = "mxt_road.remove_prev_segment"; bl_label = "Remove Prev Segment"
    bl_options = {'REGISTER', 'UNDO'}

    def execute(self, ctx):
        seg = get_active_mxt_road_segment_parent(ctx)
        props = seg.mxt_road_overall_props
        idx = props.active_prev_seg_idx
        if idx < 0 or idx >= len(props.prev_segments):
            return {'CANCELLED'}
        props.prev_segments.remove(idx)
        props.active_prev_seg_idx = min(max(0, idx-1), len(props.prev_segments)-1)
        return {'FINISHED'}

class MXTRoad_OT_AddNextSegment(Operator):
    bl_idname = "mxt_road.add_next_segment"; bl_label = "Add Next Segment"
    bl_options = {'REGISTER', 'UNDO'}

    def execute(self, ctx):
        seg = get_active_mxt_road_segment_parent(ctx)
        if not seg: return {'CANCELLED'}
        props = seg.mxt_road_overall_props
        props.next_segments.add()
        props.active_next_seg_idx = len(props.next_segments) - 1
        return {'FINISHED'}

class MXTRoad_OT_RemoveNextSegment(Operator):
    bl_idname = "mxt_road.remove_next_segment"; bl_label = "Remove Next Segment"
    bl_options = {'REGISTER', 'UNDO'}

    def execute(self, ctx):
        seg = get_active_mxt_road_segment_parent(ctx)
        props = seg.mxt_road_overall_props
        idx = props.active_next_seg_idx
        if idx < 0 or idx >= len(props.next_segments):
            return {'CANCELLED'}
        props.next_segments.remove(idx)
        props.active_next_seg_idx = min(max(0, idx-1), len(props.next_segments)-1)
        return {'FINISHED'}

class MXTRoad_OT_AddModulation(Operator):
    bl_idname = "mxt_road.add_modulation"
    bl_label = "Add Modulation"
    bl_options = {'REGISTER', 'UNDO'}
    def execute(self, context):
        seg = get_active_mxt_road_segment_parent(context)
        if not seg: return {'CANCELLED'}
        props = seg.mxt_road_overall_props
        
        bpy.ops.object.empty_add(type='SPHERE', radius=0, location=seg.location)
        helper = context.active_object
        _disallow_deletion(helper)
        helper.name = f"{seg.name}_Mod_{len(props.modulations):02d}"
        helper.parent = seg

        helper.animation_data_create()
        act = bpy.data.actions.new(f"{helper.name}_modCurves")
        helper.animation_data.action = act

        
        helper.location[1] = 0.0
        helper.keyframe_insert(data_path="location", index=1, frame=0.0)
        helper.keyframe_insert(data_path="location", index=1, frame=100.0)

        
        helper.location[2] = 0.0
        helper.keyframe_insert(data_path="location", index=2, frame=0.0)
        helper.location[2] = 1.0
        helper.keyframe_insert(data_path="location", index=2, frame=100.0)

        
        for idx in range(1, 3):
            fcu = act.fcurves.find("location", index=idx)
            if fcu:
                for kp in fcu.keyframe_points:
                    kp.interpolation = 'BEZIER'
                    kp.handle_left_type = "LINEAR_X"
                    kp.handle_right_type = "LINEAR_X"
                _linearize_fcurve_handles_smooth(fcu)

        mod = props.modulations.add()
        mod.label = f"Mod {len(props.modulations)}"
        mod.helper = helper
        props.active_mod_index = len(props.modulations) - 1
        return {'FINISHED'}

class MXTRoad_OT_RemoveModulation(Operator):
    bl_idname = "mxt_road.remove_modulation"
    bl_label = "Remove Modulation"
    bl_options = {'REGISTER', 'UNDO'}
    def execute(self, context):
        seg = get_active_mxt_road_segment_parent(context)
        props = seg.mxt_road_overall_props
        idx = props.active_mod_index
        if idx < 0 or idx >= len(props.modulations):
            return {'CANCELLED'}
        mod = props.modulations[idx]
        helper = mod.helper
        if helper:
            
            if helper.animation_data and helper.animation_data.action:
                act = helper.animation_data.action
                if act.users == 1:
                    bpy.data.actions.remove(act)
            bpy.data.objects.remove(helper, do_unlink=True)
        props.modulations.remove(idx)
        props.active_mod_index = min(max(0, idx-1), len(props.modulations)-1)
        return {'FINISHED'}

class MXTRoad_OT_SelectHelper(Operator):
    bl_idname = "mxt_road.select_helper"
    bl_label = "Select MXT Helper"
    bl_options = {'REGISTER', 'UNDO'}

    helper_name: StringProperty(
        name="Helper Name",
        description="The name of the helper object to select"
    )

    @classmethod
    def poll(cls, context):
        
        
        return context.mode == 'OBJECT'

    def execute(self, context):
        if not self.helper_name:
            self.report({'WARNING'}, "No helper name provided")
            return {'CANCELLED'}

        helper_obj = bpy.data.objects.get(self.helper_name)
        if not helper_obj:
            self.report({'WARNING'}, f"Helper object '{self.helper_name}' not found")
            return {'CANCELLED'}
        
        bpy.ops.object.select_all(action='DESELECT')
        helper_obj.select_set(True)
        context.view_layer.objects.active = helper_obj
        
        return {'FINISHED'}

class MXTRoad_OT_ConvertSegmentType(Operator):
    bl_idname = "mxt_road.convert_segment_type"
    bl_label = "Convert Segment Type"
    bl_options = {'REGISTER', 'UNDO'}

    @classmethod
    def poll(cls, context):
        return get_active_mxt_road_segment_parent(context) is not None

    def execute(self, context):
        parent = get_active_mxt_road_segment_parent(context)
        if not parent: return {'CANCELLED'}
        
        props = parent.mxt_road_overall_props
        target_type = props.segment_type

        
        
        start_loc, start_rot, start_scl = parent.matrix_world.decompose()
        end_loc, end_rot, end_scl = None, None, None
        found_source_transform = False

        
        
        cm_helper = props.curve_matrix_helper_empty
        if cm_helper and cm_helper.animation_data and cm_helper.animation_data.action and len(cm_helper.animation_data.action.fcurves) > 0:
            try:
                print("real curve")
                
                basis_start, pos_start, _ = _sample_curve_matrix(cm_helper, 0.0)
                local_mat_start = Matrix.Translation(pos_start) @ basis_start.to_4x4()
                world_mat_start = parent.matrix_world @ local_mat_start
                start_loc, start_rot, start_scl = world_mat_start.decompose()

                
                basis_end, pos_end, _ = _sample_curve_matrix(cm_helper, 1.0)
                local_mat_end = Matrix.Translation(pos_end) @ basis_end.to_4x4()
                world_mat_end = parent.matrix_world @ local_mat_end
                end_loc, end_rot, end_scl = world_mat_end.decompose()
                
                print(pos_start)
                print(pos_end)
                print(start_loc)
                print(end_loc)
                found_source_transform = True
            except Exception as e:
                
                print(f"MXT: Could not sample Curve Matrix during conversion, falling back to controls. Error: {e}")

        
        if not found_source_transform:
            existing_cps = get_mxt_control_point_empties(parent, sorted_by_time=True)
            print("fallback")
            
            if existing_cps:
                start_cp = existing_cps[0]
                start_loc, start_rot, start_scl = start_cp.matrix_world.decompose()
                if len(existing_cps) >= 2:
                    end_cp = existing_cps[-1]
                    end_loc, end_rot, end_scl = end_cp.matrix_world.decompose()
            
            
            elif props.line_start_point and props.line_end_point:
                start_loc, start_rot, start_scl = props.line_start_point.matrix_world.decompose()
                end_loc, end_rot, end_scl = props.line_end_point.matrix_world.decompose()

        
        if end_loc is None:
            print("poop")
            end_loc = start_loc + (start_rot @ Vector((0,0,100)))
            end_rot, end_scl = start_rot.copy(), start_scl.copy()

        
        existing_cps = get_mxt_control_point_empties(parent, sorted_by_time=True) 
        for cp in existing_cps: bpy.data.objects.remove(cp, do_unlink=True)
        if props.line_start_point: bpy.data.objects.remove(props.line_start_point, do_unlink=True)
        if props.line_end_point: bpy.data.objects.remove(props.line_end_point, do_unlink=True)
        if props.spiral_helper: bpy.data.objects.remove(props.spiral_helper, do_unlink=True)
        if props.spiral_axis_helper: bpy.data.objects.remove(props.spiral_axis_helper, do_unlink=True)
        if props.openness_helper:
            helper = props.openness_helper
            if helper.animation_data and helper.animation_data.action and helper.animation_data.action.users <= 1:
                bpy.data.actions.remove(helper.animation_data.action)
            bpy.data.objects.remove(helper, do_unlink=True)
        props.line_start_point, props.line_end_point, props.spiral_helper, props.spiral_axis_helper, props.openness_helper, props.width_helper, props.height_helper, props.radius_helper = None, None, None, None, None, None, None, None
        
        context.view_layer.objects.active = parent

        if target_type == 'BEZIER':
            cp0 = _create_cp_empty(context, parent, f"{parent.name}.CP.000", parent.matrix_world.inverted() @ start_loc, 0.0)
            cp0.rotation_quaternion = parent.matrix_world.to_quaternion().inverted() @ start_rot
            cp0.scale = start_scl
            cp1 = _create_cp_empty(context, parent, f"{parent.name}.CP.001", parent.matrix_world.inverted() @ end_loc, 1.0)
            cp1.rotation_quaternion = parent.matrix_world.to_quaternion().inverted() @ end_rot
            cp1.scale = end_scl
            schedule_road_parent_visual_update(parent, context)

        elif target_type == 'LINE':
            print("yay line")
            print(start_loc)
            start_point = bpy.data.objects.new(f"{parent.name}.LineStart", None)
            _disallow_deletion(start_point)
            start_point.empty_display_type, start_point.empty_display_size = 'CUBE', 1
            start_point.matrix_world = Matrix.Translation(start_loc) @ start_rot.to_matrix().to_4x4() @ Matrix.Diagonal((*start_scl, 1.0))
            context.collection.objects.link(start_point)
            start_point.parent, start_point.rotation_mode = parent, 'QUATERNION'
            end_point = bpy.data.objects.new(f"{parent.name}.LineEnd", None)
            _disallow_deletion(end_point)
            end_point.empty_display_type, end_point.empty_display_size = 'CUBE', 1
            end_point.matrix_world = Matrix.Translation(end_loc) @ end_rot.to_matrix().to_4x4() @ Matrix.Diagonal((*end_scl, 1.0))
            context.collection.objects.link(end_point)
            end_point.parent, end_point.rotation_mode = parent, 'QUATERNION'
            
            props.line_start_point, props.line_end_point = start_point, end_point
            start_point.mxt_line_handle_data.is_mxt_line_handle = True
            start_point.animation_data_create()
            action = bpy.data.actions.new(f"{start_point.name}_MXTEasingAction")
            start_point.animation_data.action = action
            for prop_name in ['rotation_ease_factor_channel', 'scale_ease_factor_channel']:
                start_point.mxt_line_handle_data[prop_name] = 0.0
                start_point.keyframe_insert(data_path=f'mxt_line_handle_data.{prop_name}', frame=0.0)
                start_point.mxt_line_handle_data[prop_name] = 1.0
                start_point.keyframe_insert(data_path=f'mxt_line_handle_data.{prop_name}', frame=100.0)
                fcu = action.fcurves.find(f'mxt_line_handle_data.{prop_name}')
                if fcu: _linearize_fcurve_handles_smooth(fcu)

        elif target_type == 'SPIRAL':
            
            axis_helper = bpy.data.objects.new(f"{parent.name}.SpiralAxisHelper", None)
            _disallow_deletion(axis_helper)
            axis_helper.empty_display_type = 'ARROWS'
            axis_helper.empty_display_size = start_scl.x
            
            axis_helper.matrix_world = Matrix.Translation(start_loc) @ start_rot.to_matrix().to_4x4()
            context.collection.objects.link(axis_helper)
            axis_helper.parent = parent
            props.spiral_axis_helper = axis_helper

            
            fcurve_helper = bpy.data.objects.new(f"{parent.name}.SpiralHelper", None)
            _disallow_deletion(fcurve_helper)
            fcurve_helper.empty_display_type = 'SPHERE'
            fcurve_helper.empty_display_size = 0
            context.collection.objects.link(fcurve_helper)
            fcurve_helper.parent = parent
            fcurve_helper.location = parent.location 
            props.spiral_helper = fcurve_helper
            
            fcurve_helper.animation_data_create()
            act = bpy.data.actions.new(f"{fcurve_helper.name}_spiralCurves")
            fcurve_helper.animation_data.action = act

            
            loc_defs = { 0: (50.0, 100.0), 1: (0.0, 0.0), 2: (0.0, 0.0) }
            for index, (start_val, end_val) in loc_defs.items():
                fcurve_helper.location[index] = start_val
                fcurve_helper.keyframe_insert(data_path="location", index=index, frame=0)
                fcurve_helper.location[index] = end_val
                fcurve_helper.keyframe_insert(data_path="location", index=index, frame=100)
                fcu = act.fcurves.find("location", index=index)
                if fcu: _linearize_fcurve_handles_smooth(fcu)

            
            scl_defs = { 0: end_scl.x, 1: end_scl.y }
            for index, value in scl_defs.items():
                
                fcurve_helper.scale[index] = value
                fcurve_helper.keyframe_insert(data_path="scale", index=index, frame=0)
                fcurve_helper.keyframe_insert(data_path="scale", index=index, frame=100)
                fcu = act.fcurves.find("scale", index=index)
                if fcu: _linearize_fcurve_handles_smooth(fcu)
        
        _bake_curve_matrix_direct(parent)
        return {'FINISHED'}
class MXTRoad_OT_RespaceCPTimes(Operator):
    bl_idname = "mxt_road.respace_cp_times"
    bl_label = "Respace CP Times"
    bl_description = "Re-distribute Control Point times evenly from 0.0 to 1.0 based on their current order"
    bl_options = {'REGISTER', 'UNDO'}

    @classmethod
    def poll(cls, context):
        parent = get_active_mxt_road_segment_parent(context)
        return parent and parent.mxt_road_overall_props.segment_type == 'BEZIER'

    def execute(self, context):
        parent_obj = get_active_mxt_road_segment_parent(context)
        cps = get_mxt_control_point_empties(parent_obj, sorted_by_time=True)
        count = len(cps)
        if count < 2:
            self.report({'INFO'}, "Not enough control points to respace."); return {'CANCELLED'}

        step = 1.0 / (count - 1)
        for i, cp in enumerate(cps):
            if hasattr(cp, "mxt_cp_data"): cp.mxt_cp_data.time = i * step
        
        schedule_road_parent_visual_update(parent_obj, context)
        self.report({'INFO'}, f"Respaced times for {count} control points."); return {'FINISHED'}


def get_active_mxt_road_segment_parent(context):
    obj = context.active_object
    while obj:
        if getattr(obj, "mxt_road_overall_props", None) \
                and obj.mxt_road_overall_props.is_mxt_road_segment_parent:
            return obj
        obj = obj.parent            
    return None

def get_selected_mxt_road_segment_parents(context):
    parents = []
    seen = set()
    for selected in getattr(context, "selected_objects", []):
        obj = selected
        while obj:
            if getattr(obj, "mxt_road_overall_props", None) \
                    and obj.mxt_road_overall_props.is_mxt_road_segment_parent:
                if obj.name not in seen:
                    parents.append(obj)
                    seen.add(obj.name)
                break
            obj = obj.parent
    return parents

def get_mxt_control_point_empties(parent_obj, sorted_by_time=True):
    cps = []
    if not parent_obj: return cps
    for child in parent_obj.children:
        if child.type == 'EMPTY' and hasattr(child, "mxt_cp_data") and child.mxt_cp_data.is_mxt_control_point:
            cps.append(child)
    if sorted_by_time:
        cps.sort(key=lambda cp_empty: cp_empty.mxt_cp_data.time if hasattr(cp_empty, "mxt_cp_data") else 0.0)
    return cps
def _linearize_fcurve_handles(fcu: bpy.types.FCurve):
    kps = fcu.keyframe_points
    if len(kps) < 2:
        return

    for idx, kp in enumerate(kps):
        # ensure handle type first so Blender doesn't reset lengths when changed
        kp.handle_left_type = 'LINEAR_X'
        kp.handle_right_type = 'LINEAR_X'

        left_vec = kp.handle_left - kp.co
        right_vec = kp.handle_right - kp.co

        if idx > 0:
            prev = kps[idx - 1]
            dx_prev = kp.co.x - prev.co.x
            target_dx = -dx_prev / 3.0
            if abs(left_vec.x) > 1e-6:
                scale = target_dx / left_vec.x
                kp.handle_left = kp.co + left_vec * scale
            else:
                kp.handle_left.x = kp.co.x + target_dx

        if idx < len(kps) - 1:
            nxt = kps[idx + 1]
            dx_next = nxt.co.x - kp.co.x
            target_dx = dx_next / 3.0
            if abs(right_vec.x) > 1e-6:
                scale = target_dx / right_vec.x
                kp.handle_right = kp.co + right_vec * scale
            else:
                kp.handle_right.x = kp.co.x + target_dx
    fcu.update()

def _linearize_fcurve_handles_smooth(fcu: bpy.types.FCurve):
    _linearize_fcurve_handles_smooth_with_end_samples(fcu)

def _linearize_fcurve_handles_smooth_with_end_samples(fcu: bpy.types.FCurve, prev_sample=None, next_sample=None):
    kps = fcu.keyframe_points
    if len(kps) < 2:
        return

    for idx, kp in enumerate(kps):
        prev_x = prev_y = next_x = next_y = None
        if idx > 0:
            kp_prev = kps[idx - 1]
            prev_x = kp_prev.co.x
            prev_y = kp_prev.co.y
        elif prev_sample is not None:
            prev_x, prev_y = prev_sample

        if idx < len(kps) - 1:
            kp_next = kps[idx + 1]
            next_x = kp_next.co.x
            next_y = kp_next.co.y
        elif next_sample is not None:
            next_x, next_y = next_sample

        slope = 0.0
        slope_count = 0
        dx_prev = 0.0
        dx_next = 0.0
        if prev_x is not None:
            dx_prev = kp.co.x - prev_x
            if abs(dx_prev) > 1e-6:
                slope += (kp.co.y - prev_y) / dx_prev
                slope_count += 1
        if next_x is not None:
            dx_next = next_x - kp.co.x
            if abs(dx_next) > 1e-6:
                slope += (next_y - kp.co.y) / dx_next
                slope_count += 1
        if slope_count > 0:
            slope /= slope_count

        if abs(dx_prev) <= 1e-6:
            dx_prev = dx_next
        if abs(dx_next) <= 1e-6:
            dx_next = dx_prev

        kp.handle_left_type = 'LINEAR_X'
        kp.handle_right_type = 'LINEAR_X'
        if abs(dx_prev) > 1e-6:
            kp.handle_left.x = kp.co.x - dx_prev / 3.0
            kp.handle_left.y = kp.co.y - slope * dx_prev / 3.0
        else:
            kp.handle_left[:] = kp.co
        if abs(dx_next) > 1e-6:
            kp.handle_right.x = kp.co.x + dx_next / 3.0
            kp.handle_right.y = kp.co.y + slope * dx_next / 3.0
        else:
            kp.handle_right[:] = kp.co
    fcu.update()

def _curve_matrix_endpoint_step(t_samples):
    unique = sorted(set(float(t) for t in t_samples))
    if len(unique) < 2:
        return 1.0 / 64.0
    first_step = None
    last_step = None
    for i in range(len(unique) - 1):
        step = unique[i + 1] - unique[i]
        if step > 1e-8 and first_step is None:
            first_step = step
        if step > 1e-8:
            last_step = step
    return min(first_step or (1.0 / 64.0), last_step or (1.0 / 64.0))

def _curve_matrix_channel_values(pos, basis, scale):
    c0 = basis.col[0]
    c1 = basis.col[1]
    c2 = basis.col[2]
    return {
        ("location", 0): pos.x,
        ("location", 1): pos.y,
        ("location", 2): pos.z,
        ("basis", 0): c0.x,
        ("basis", 1): c0.y,
        ("basis", 2): c0.z,
        ("basis", 3): c1.x,
        ("basis", 4): c1.y,
        ("basis", 5): c1.z,
        ("basis", 6): c2.x,
        ("basis", 7): c2.y,
        ("basis", 8): c2.z,
        ("scale", 0): scale.x,
        ("scale", 1): scale.y,
        ("scale", 2): scale.z,
    }

def _linearize_curve_matrix_handles_with_extension(curves, t_samples):
    step = _curve_matrix_endpoint_step(t_samples)
    pre_t = min(t_samples) - step
    post_t = max(t_samples) + step
    pre_frame = pre_t * 100.0
    post_frame = post_t * 100.0

    for fcu in curves.values():
        _linearize_fcurve_handles_smooth_with_end_samples(fcu)

    for fcu in curves.values():
        _linearize_fcurve_handles_smooth_with_end_samples(
            fcu,
            (pre_frame, _evaluate_fcurve_continued(fcu, pre_frame)),
            (post_frame, _evaluate_fcurve_continued(fcu, post_frame)))

def _cubic_bezier_value(p0, p1, p2, p3, u):
    omt = 1.0 - u
    return (
        p0 * omt * omt * omt +
        3.0 * p1 * omt * omt * u +
        3.0 * p2 * omt * u * u +
        p3 * u * u * u)

def _cubic_bezier_derivative(p0, p1, p2, p3, u):
    omt = 1.0 - u
    return (
        3.0 * (p1 - p0) * omt * omt +
        6.0 * (p2 - p1) * omt * u +
        3.0 * (p3 - p2) * u * u)

def _evaluate_fcurve_bezier_segment_by_x(k0, k1, frame):
    x0 = k0.co.x
    x1 = k0.handle_right.x
    x2 = k1.handle_left.x
    x3 = k1.co.x
    y0 = k0.co.y
    y1 = k0.handle_right.y
    y2 = k1.handle_left.y
    y3 = k1.co.y

    if abs(x3 - x0) <= 1e-8:
        return y0

    u = (frame - x0) / (x3 - x0)
    for _i in range(12):
        x = _cubic_bezier_value(x0, x1, x2, x3, u)
        dx = _cubic_bezier_derivative(x0, x1, x2, x3, u)
        if abs(dx) <= 1e-8:
            break
        next_u = u - (x - frame) / dx
        if abs(next_u - u) <= 1e-8:
            u = next_u
            break
        u = next_u
    return _cubic_bezier_value(y0, y1, y2, y3, u)

def _evaluate_fcurve_continued(fcu, frame):
    kps = fcu.keyframe_points
    if len(kps) == 0:
        return fcu.evaluate(frame)
    if len(kps) == 1:
        return kps[0].co.y
    if frame < kps[0].co.x:
        return _evaluate_fcurve_bezier_segment_by_x(kps[0], kps[1], frame)
    if frame > kps[-1].co.x:
        return _evaluate_fcurve_bezier_segment_by_x(kps[-2], kps[-1], frame)
    return fcu.evaluate(frame)
def _update_road_segment_visual_guide_logic(road_parent_empty, report_fn=None):
    if not road_parent_empty or not hasattr(road_parent_empty, "mxt_road_overall_props"):
        if report_fn: report_fn({'WARNING'}, "Invalid road parent empty for visual update.")
        return
    props = road_parent_empty.mxt_road_overall_props
    if not props.visual_guide_curve:
        if report_fn: report_fn({'WARNING'}, "No visual guide curve linked to road parent.")
        return
    guide_curve_obj = props.visual_guide_curve; curve_data = guide_curve_obj.data
    cp_empties = get_mxt_control_point_empties(road_parent_empty, sorted_by_time=True)
    guide_curve_obj.location = (0,0,0); guide_curve_obj.rotation_euler = (0,0,0); guide_curve_obj.scale = (1,1,1)
    while curve_data.splines: curve_data.splines.remove(curve_data.splines[0])
    if len(cp_empties) < 2: return
    spline = curve_data.splines.new('BEZIER'); spline.bezier_points.add(len(cp_empties) - 1)
    for i, cp_empty in enumerate(cp_empties):
        bp = spline.bezier_points[i]; cp_local_pos = cp_empty.location
        cp_local_rot_mat = cp_empty.rotation_euler.to_matrix(); cp_data = cp_empty.mxt_cp_data
        bp.co = cp_local_pos
        local_z_axis_of_cp = cp_local_rot_mat.col[2].normalized()
        handle_out_offset_local = local_z_axis_of_cp * cp_data.handle_out_length
        handle_in_offset_local  = -local_z_axis_of_cp * cp_data.handle_in_length
        bp.handle_right = bp.co + handle_out_offset_local; bp.handle_left  = bp.co + handle_in_offset_local
        bp.handle_left_type = 'ALIGNED'; bp.handle_right_type = 'ALIGNED'
    curve_data.update_gpu_tag()
def _find_road_parent(obj):
    while obj:
        if getattr(obj, "mxt_road_overall_props", None) \
                and obj.mxt_road_overall_props.is_mxt_road_segment_parent:
            return obj
        obj = obj.parent
    return None
def schedule_road_parent_visual_update(cp_empty_obj, context):
    global mxt_roads_pending_visual_update, mxt_timer_is_active
    if not cp_empty_obj: return
    road_parent_to_update = None
    if hasattr(cp_empty_obj, "mxt_cp_data") and cp_empty_obj.mxt_cp_data.is_mxt_control_point:
        if cp_empty_obj.parent and hasattr(cp_empty_obj.parent, "mxt_road_overall_props") and \
           cp_empty_obj.parent.mxt_road_overall_props.is_mxt_road_segment_parent:
            road_parent_to_update = cp_empty_obj.parent
    elif hasattr(cp_empty_obj, "mxt_road_overall_props") and \
         cp_empty_obj.mxt_road_overall_props.is_mxt_road_segment_parent:
        road_parent_to_update = cp_empty_obj
    if road_parent_to_update:
        if road_parent_to_update.name not in mxt_roads_pending_visual_update:
            mxt_roads_pending_visual_update.add(road_parent_to_update.name)
        if not mxt_timer_is_active:
            if bpy.context.screen:
                 try:
                    bpy.app.timers.register(_process_pending_visual_updates, first_interval=0.01666)
                    mxt_timer_is_active = True
                 except Exception as e:
                    print(f"MXT: Error registering timer: {e}")
def _process_pending_visual_updates():
    global mxt_roads_pending_visual_update, mxt_timer_is_active
    if not bpy.context.scene: mxt_timer_is_active=False; mxt_roads_pending_visual_update.clear(); return None
    if not mxt_roads_pending_visual_update: mxt_timer_is_active=False; return None
    roads_to_process_now = list(mxt_roads_pending_visual_update); mxt_roads_pending_visual_update.clear()
    for road_parent_name in roads_to_process_now:
        road_parent_obj = bpy.data.objects.get(road_parent_name)
        if road_parent_obj:
            try: _update_road_segment_visual_guide_logic(road_parent_obj)
            except Exception as e: print(f"MXT Error in timer visual update for {road_parent_name}: {e}")
    if mxt_roads_pending_visual_update: return 0.01
    mxt_timer_is_active = False; return None
_cm_pending   = set()
_mesh_pending = set()
_openness_helper_to_create = set()
_openness_helper_to_destroy = set()
_square_helpers_to_create = set()
_square_helpers_to_destroy = set()
_square_helper_inherit_vals = {}
_timer_live   = False

def _ensure_timer():
    global _timer_live
    if not _timer_live:
        bpy.app.timers.register(_process_live_updates, first_interval=0.01666)
        _timer_live = True
def schedule_cm_rebake(obj):
    parent = _find_road_parent(obj)
    if parent:
        if parent.mxt_road_overall_props.disable_auto_rebake:
            return
        _cm_pending.add(parent.name)
        _mesh_pending.add(parent.name)
        _ensure_timer()
def schedule_mesh_build(obj):
    parent = _find_road_parent(obj)
    if parent:
        _mesh_pending.add(parent.name)
        _ensure_timer()
def _process_live_updates():
    global _timer_live, _build_in_progress, _ignore_updates

    if _build_in_progress:
        return 0.05

    _build_in_progress = True
    _ignore_updates      = True

    try:
        while _openness_helper_to_destroy:
            name = _openness_helper_to_destroy.pop()
            parent = bpy.data.objects.get(name)
            if parent and parent.mxt_road_overall_props.openness_helper:
                helper = parent.mxt_road_overall_props.openness_helper
                if helper.animation_data and helper.animation_data.action and helper.animation_data.action.users <= 1:
                    bpy.data.actions.remove(helper.animation_data.action)
                bpy.data.objects.remove(helper, do_unlink=True)
                parent.mxt_road_overall_props.openness_helper = None

        while _openness_helper_to_create:
            name = _openness_helper_to_create.pop()
            parent = bpy.data.objects.get(name)
            if parent and not parent.mxt_road_overall_props.openness_helper:
                helper_data = bpy.data.objects.new(f"{parent.name}_OpennessHelper", None)
                _disallow_deletion(helper_data)
                helper_data.empty_display_type, helper_data.empty_display_size = 'SPHERE', 0
                parent.users_collection[0].objects.link(helper_data)
                helper_data.parent, helper_data.location = parent, parent.location

                helper_data.animation_data_create()
                action = bpy.data.actions.new(f"{helper_data.name}_OpennessCurve")
                helper_data.animation_data.action = action
                helper_data.location[0] = 1.0
                helper_data.keyframe_insert(data_path="location", index=0, frame=0.0)
                helper_data.keyframe_insert(data_path="location", index=0, frame=100.0)
                # Initialize rotation curve (Y) to 0 across segment
                helper_data.location[1] = 0.0
                helper_data.keyframe_insert(data_path="location", index=1, frame=0.0)
                helper_data.keyframe_insert(data_path="location", index=1, frame=100.0)
                fcu = action.fcurves.find("location", index=0)
                if fcu:
                    fcu.keyframe_points[0].interpolation = fcu.keyframe_points[1].interpolation = 'CONSTANT'
                    _linearize_fcurve_handles_smooth(fcu)
                fcur = action.fcurves.find("location", index=1)
                if fcur:
                    fcur.keyframe_points[0].interpolation = fcur.keyframe_points[1].interpolation = 'CONSTANT'
                    _linearize_fcurve_handles_smooth(fcur)
                parent.mxt_road_overall_props.openness_helper = helper_data

        while _square_helpers_to_destroy:
            name = _square_helpers_to_destroy.pop()
            parent = bpy.data.objects.get(name)
            if parent:
                props = parent.mxt_road_overall_props
                for attr in ('width_helper', 'height_helper', 'radius_helper'):
                    helper = getattr(props, attr)
                    if helper:
                        if helper.animation_data and helper.animation_data.action and helper.animation_data.action.users <= 1:
                            bpy.data.actions.remove(helper.animation_data.action)
                        bpy.data.objects.remove(helper, do_unlink=True)
                        setattr(props, attr, None)

        while _square_helpers_to_create:
            name = _square_helpers_to_create.pop()
            parent = bpy.data.objects.get(name)
            if parent:
                props = parent.mxt_road_overall_props
                defaults = _square_helper_inherit_vals.pop(name, (1.0, 1.0, 0.0))
                helper_info = [
                    ('Width',  'width_helper',  defaults[0]),
                    ('Height', 'height_helper', defaults[1]),
                    ('Radius', 'radius_helper', defaults[2]),
                ]
                for suffix, attr, default in helper_info:
                    if getattr(props, attr):
                        continue
                    helper_data = bpy.data.objects.new(f"{parent.name}_{suffix}Helper", None)
                    _disallow_deletion(helper_data)
                    helper_data.empty_display_type, helper_data.empty_display_size = 'SPHERE', 0
                    parent.users_collection[0].objects.link(helper_data)
                    helper_data.parent, helper_data.location = parent, parent.location

                    helper_data.animation_data_create()
                    action = bpy.data.actions.new(f"{helper_data.name}_{suffix}Curve")
                    helper_data.animation_data.action = action
                    helper_data.location[0] = default
                    helper_data.keyframe_insert(data_path="location", index=0, frame=0.0)
                    helper_data.keyframe_insert(data_path="location", index=0, frame=100.0)
                    fcu = action.fcurves.find("location", index=0)
                    if fcu:
                        fcu.keyframe_points[0].interpolation = fcu.keyframe_points[1].interpolation = 'CONSTANT'
                        _linearize_fcurve_handles_smooth(fcu)
                    setattr(props, attr, helper_data)
        
        while _cm_pending:
            name = _cm_pending.pop()
            parent = bpy.data.objects.get(name)
            if parent:
                if parent.mxt_road_overall_props.disable_auto_rebake:
                    continue
                try: _bake_curve_matrix_direct(parent)
                except Exception as e: print(f"CurvBake {name}: {e}")

        while _mesh_pending:
            name = _mesh_pending.pop()
            parent = bpy.data.objects.get(name)
            if parent:
                try: _build_mesh_direct(parent)
                except Exception as e: print(f"MeshBuild {name}: {e}")

    finally:
        _ignore_updates, _build_in_progress = False, False

    _timer_live = bool(
        _cm_pending or _mesh_pending or _openness_helper_to_create or _openness_helper_to_destroy
        or _square_helpers_to_create or _square_helpers_to_destroy
    )
    return 0.05 if _timer_live else None
@persistent
def mxt_on_depsgraph_update(scene, depsgraph):
    global _ignore_updates
    if _ignore_updates:
        return

    
    parents_to_rebake_cm = set()
    parents_to_rebuild_mesh = set()

    
    def check_and_schedule(obj):
        if not obj: return
        
        
        if obj.name.endswith(("_PreviewMesh", "_CurveMatrixHelper")): return
        if getattr(obj, "mxt_road_overall_props", None) and obj.mxt_road_overall_props.is_mxt_road_segment_parent: return

        parent = _find_road_parent(obj)
        if not parent: return
        if parent in parents_to_rebake_cm: return 

        props = parent.mxt_road_overall_props
        
        
        is_primary_control = False
        if props.segment_type == 'BEZIER' and hasattr(obj, "mxt_cp_data") and obj.mxt_cp_data.is_mxt_control_point:
            is_primary_control = True
        elif props.segment_type == 'LINE' and (obj == props.line_start_point or obj == props.line_end_point):
            is_primary_control = True
        elif props.segment_type == 'SPIRAL' and obj in (props.spiral_helper, props.spiral_axis_helper):
             is_primary_control = True

        if is_primary_control:
            if props.disable_auto_rebake:
                return
            parents_to_rebake_cm.add(parent)
            parents_to_rebuild_mesh.discard(parent) 
            return

        
        is_secondary_control = False
        if any(mod.helper == obj for mod in props.modulations):
            is_secondary_control = True
        elif any(emb.helper == obj for emb in props.embeds):
            is_secondary_control = True
        elif props.openness_helper == obj or obj in (props.width_helper, props.height_helper, props.radius_helper):
            is_secondary_control = True
        
        if is_secondary_control:
            parents_to_rebuild_mesh.add(parent)
            return

    
    for upd in depsgraph.updates:
        
        if upd.is_updated_transform and isinstance(upd.id, bpy.types.Object):
            check_and_schedule(upd.id)

        
        elif isinstance(upd.id, bpy.types.Action):
            action = upd.id
            
            
            for o in bpy.data.objects:
                if o.animation_data and o.animation_data.action == action:
                    check_and_schedule(o)
                    break 

    
    for parent in parents_to_rebake_cm:
        schedule_cm_rebake(parent)
    
    for parent in parents_to_rebuild_mesh:
        schedule_mesh_build(parent)

    # If a preview mesh was removed manually, delete the entire segment
    parents_to_check = [obj for obj in bpy.data.objects
                        if getattr(obj, "mxt_road_overall_props", None)
                        and obj.mxt_road_overall_props.is_mxt_road_segment_parent]
    for parent in parents_to_check:
        props = parent.mxt_road_overall_props
        mesh_name = f"{parent.name}_PreviewMesh"
        mesh_exists = bpy.data.objects.get(mesh_name) is not None
        if props.preview_mesh_exists:
            if not mesh_exists:
                _delete_road_segment(parent)
        else:
            if mesh_exists:
                props.preview_mesh_exists = True
class MXTRoad_OT_LinearizeSelectedFCurves(Operator):
    bl_idname = "mxt_road.linearize_selected_fcurves"
    bl_label  = "Enforce ⅓ Handles"
    bl_description = "Force selected Bézier keys to use -1/3 · +1/3 handles"

    @classmethod
    def poll(cls, context):
        area = context.area
        return area and area.type == 'GRAPH_EDITOR' \
            and context.selected_editable_fcurves

    def execute(self, context):
        for fcu in context.selected_editable_fcurves:
            _linearize_fcurve_handles_smooth(fcu)
        self.report({'INFO'}, "Handles linearised")
        return {'FINISHED'}
def _respace_cp_times(parent_obj):
    cps = get_mxt_control_point_empties(parent_obj, sorted_by_time=False)
    if len(cps) < 2:
        return
    cps.sort(key=lambda c: c.mxt_cp_data.time)
    step = 1.0 / (len(cps) - 1)
    for i, cp in enumerate(cps):
        cp.mxt_cp_data.time = i * step
def _create_cp_empty(context, parent_obj, name, location_in_parent_space, time_val):
    bpy.ops.object.empty_add(type='PLAIN_AXES', radius=1.0, location=location_in_parent_space)
    cp_empty = context.active_object
    cp_empty.name = name
    cp_empty.parent = parent_obj
    cp_empty.mxt_cp_data.is_mxt_control_point = True
    cp_empty.mxt_cp_data.time = time_val
    _respace_cp_times(parent_obj)
    if not cp_empty.animation_data:
        cp_empty.animation_data_create()
    action_name = f"{cp_empty.name}_MXTEasingAction"
    action = bpy.data.actions.get(action_name)
    if not action:
        action = bpy.data.actions.new(name=action_name)
    cp_empty.animation_data.action = action
    prop_details = [
        ('rotation_ease_factor_channel', 0.0, 1.0),
        ('scale_ease_factor_channel', 0.0, 1.0),
        ('twist_ease_factor_channel', 0.0, 1.0),
    ]
    easing_group_name = "MXT Easing Factors"
    fcurve_group = action.groups.get(easing_group_name)
    if not fcurve_group:
        fcurve_group = action.groups.new(name=easing_group_name)
    for prop_name, start_val, end_val in prop_details:
        data_path = f'mxt_cp_data.{prop_name}'
        fcu = action.fcurves.find(data_path)
        if not fcu:
            setattr(cp_empty.mxt_cp_data, prop_name, start_val)
            cp_empty.keyframe_insert(data_path=data_path, frame=0.0)
            setattr(cp_empty.mxt_cp_data, prop_name, end_val)
            cp_empty.keyframe_insert(data_path=data_path, frame=100.0)
            fcu = action.fcurves.find(data_path)
            if fcu:
                _linearize_fcurve_handles_smooth(fcu)
                fcu.group = fcurve_group
                for kp in fcu.keyframe_points:
                    kp.interpolation = 'BEZIER'
                    kp.handle_left_type = "ALIGNED"
                    kp.handle_right_type = "ALIGNED"
                fcu.update()
            else:
                print(f"MXT Error: Failed to create or find F-Curve for {data_path} on {cp_empty.name}")
        else:
            _linearize_fcurve_handles_smooth(fcu)
            if not fcu.group or fcu.group.name != easing_group_name:
                fcu.group = fcurve_group
    context.view_layer.objects.active = parent_obj
    return cp_empty

def _update_trigger_helper(trig):
    helper = trig.helper
    seg = trig.segment
    if not helper or not seg:
        return
    props = seg.mxt_road_overall_props
    helper.parent = seg
    helper.matrix_parent_inverse = seg.matrix_world.inverted()
    cm_helper = props.curve_matrix_helper_empty
    if not (cm_helper and cm_helper.animation_data):
        return
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
    base = shape.get_pos(cm_helper, Vector((trig.tx, trig.ty)))
    if base is None:
        return
    eps = 0.002
    tx_forward = min(trig.tx + eps, 1.0)
    tx_backward = max(trig.tx - eps, -1.0)
    ty_forward = min(trig.ty + eps, 1.0)
    ty_backward = max(trig.ty - eps, 0.0)
    pr = shape.get_pos(cm_helper, Vector((tx_forward, trig.ty)))
    use_right_backward = abs(tx_forward - trig.tx) <= 1.0e-6
    if use_right_backward:
        pr = shape.get_pos(cm_helper, Vector((tx_backward, trig.ty)))
    pf = shape.get_pos(cm_helper, Vector((trig.tx, ty_forward)))
    use_forward_backward = abs(ty_forward - trig.ty) <= 1.0e-6
    if use_forward_backward:
        pf = shape.get_pos(cm_helper, Vector((trig.tx, ty_backward)))
    if pr is None or pf is None:
        return
    right = (base - pr).normalized() if use_right_backward else (pr - base).normalized()
    forward = (base - pf).normalized() if use_forward_backward else (pf - base).normalized()
    normal = (right.cross(forward)).normalized()
    right = forward.cross(normal).normalized()
    B = (Matrix((right, -normal, forward))).transposed()
    mat = Matrix.Translation(base) @ B.to_4x4()
    mat = mat @ Matrix.Rotation(math.radians(trig.yaw_deg), 4, 'Y')
    S = Matrix.Diagonal((*trig.scale, 1.0))
    helper.matrix_world = mat @ S

class MXT_OT_set_handle_length(bpy.types.Operator):
    bl_idname = "mxt.set_handle_length"
    bl_label = "Set CP Handle Length"
    bl_options = {'REGISTER', 'UNDO_GROUPED'}
    bl_undo_group  = "MXT_CP_HANDLE"

    is_in:   bpy.props.BoolProperty()
    length:  bpy.props.FloatProperty()

    @classmethod
    def poll(cls, context):
        obj = context.active_object
        return obj and getattr(obj, "mxt_cp_data", None) and obj.mxt_cp_data.is_mxt_control_point

    def execute(self, context):
        cp = context.active_object.mxt_cp_data
        if self.is_in:
            cp.handle_in_length  = self.length
        else:
            cp.handle_out_length = self.length
        return {'FINISHED'}

class MXT_GGT_CPHandleGizmos(bpy.types.GizmoGroup):
    bl_idname = "MXT_GGT_cp_handle_gizmos"
    bl_label = "MXT CP Handle Gizmos"
    bl_space_type = 'VIEW_3D'
    bl_region_type = 'WINDOW'
    bl_options = {'3D', 'PERSISTENT'}

    @classmethod
    def poll(cls, context):
        obj = context.active_object
        return (obj and obj.mode == 'OBJECT' and
                getattr(obj, "mxt_cp_data", None) and
                obj.mxt_cp_data.is_mxt_control_point)

    def setup(self, context):
        # --- OUT ---
        gz = self.gizmos.new("GIZMO_GT_arrow_3d")
        gz.use_draw_scale = gz.use_draw_offset_scale = False
        gz.draw_style = 'BOX'
        gz.color, gz.alpha = (1.0, .6, .2), .8
        def get_out():
            return context.active_object.mxt_cp_data.handle_out_length
        def set_out(val):
            # one grouped‑undo operator call per mouse‑move
            bpy.ops.mxt.set_handle_length(True, is_in=False, length=val)
        gz.target_set_handler("offset", get=get_out, set=set_out)
        self.handle_out = gz

        # --- IN ---
        gz = self.gizmos.new("GIZMO_GT_arrow_3d")
        gz.use_draw_scale = gz.use_draw_offset_scale = False
        gz.draw_style = 'BOX'
        gz.color, gz.alpha = (.2, .6, 1.0), .8
        def get_in():
            return context.active_object.mxt_cp_data.handle_in_length
        def set_in(val):
            bpy.ops.mxt.set_handle_length(True, is_in=True, length=val)
        gz.target_set_handler("offset", get=get_in, set=set_in)
        self.handle_in = gz

    def draw_prepare(self, ctx):
        obj, cp = ctx.active_object, ctx.active_object.mxt_cp_data
        loc, rot, _ = obj.matrix_world.decompose()
        use_mat  = Matrix.LocRotScale(loc, rot, Vector((obj.scale[0],)*3))
        scale_inv = 1.0 / obj.scale[0]

        # OUT
        self.handle_out.matrix_basis = use_mat
        self.handle_out.length       = cp.handle_out_length * scale_inv
        self.handle_out.matrix_offset = Matrix.Translation((0,0,-cp.handle_out_length))

        # IN
        self.handle_in.matrix_basis  = use_mat @ Matrix.Rotation(math.pi, 4, 'Y')
        self.handle_in.length        = cp.handle_in_length * scale_inv
        self.handle_in.matrix_offset = Matrix.Translation((0,0,-cp.handle_in_length))

def _delete_road_segment(parent_obj):
    if not parent_obj:
        return

    # Remove all child objects and their actions
    for child in list(parent_obj.children):
        if child.animation_data and child.animation_data.action and child.animation_data.action.users <= 1:
            bpy.data.actions.remove(child.animation_data.action)
        bpy.data.objects.remove(child, do_unlink=True)

    if parent_obj.animation_data and parent_obj.animation_data.action and parent_obj.animation_data.action.users <= 1:
        bpy.data.actions.remove(parent_obj.animation_data.action)

    _cm_pending.discard(parent_obj.name)
    _mesh_pending.discard(parent_obj.name)
    _openness_helper_to_create.discard(parent_obj.name)
    _openness_helper_to_destroy.discard(parent_obj.name)
    _square_helpers_to_create.discard(parent_obj.name)
    _square_helpers_to_destroy.discard(parent_obj.name)
    mxt_roads_pending_visual_update.discard(parent_obj.name)

    bpy.data.objects.remove(parent_obj, do_unlink=True)
class MXTRoad_OT_CreateRoadSegment(Operator):
    bl_idname = "mxt_road.create_segment_empties"
    bl_label  = "New Road Segment (Empties)"
    bl_options = {'REGISTER', 'UNDO'}

    def execute(self, context):
        prev_seg = get_active_mxt_road_segment_parent(context)
        global _square_helper_inherit_vals

        
        bpy.ops.object.empty_add(type='PLAIN_AXES', radius=1.0,
                                 location=context.scene.cursor.location)
        seg_par = context.active_object
        _disallow_deletion(seg_par)
        seg_par.name = "MXTRoadSegment.%03d" % len(
            [o for o in bpy.data.objects if o.name.startswith("MXTRoadSegment")])
        props = seg_par.mxt_road_overall_props
        props.is_mxt_road_segment_parent = True
        ts = context.scene.mxt_track_settings
        if ts and not ts.first_segment:
            ts.first_segment = seg_par

        
        bpy.ops.object.empty_add(type='PLAIN_AXES', radius=0.0, location=(0,0,0))
        helper = context.active_object
        _disallow_deletion(helper)
        helper.name = f"{seg_par.name}_CurveMatrixHelper"
        helper.parent = seg_par
        helper.hide_set(True)
        props.curve_matrix_helper_empty = helper
        helper.matrix_parent_inverse = seg_par.matrix_world.inverted()

        
        cp0 = _create_cp_empty(context, seg_par, "MXTCP.000",
                               Vector((0,0,0)), 0.0)
        cp1 = _create_cp_empty(context, seg_par, "MXTCP.001",
                               Vector((0,0,500)), 1.0)

        cp0.scale = Vector((45, 45, 1))
        cp1.scale = Vector((45, 45, 1))
        # Initialize rail heights
        props.rail_height_left = 0.15
        props.rail_height_right = 0.15
        props.rail_start_left = 0.0
        props.rail_end_left = 1.0
        props.rail_start_right = 0.0
        props.rail_end_right = 1.0


        if prev_seg:
            prev_props = prev_seg.mxt_road_overall_props
            prev_helper = prev_props.curve_matrix_helper_empty
            if prev_helper and prev_helper.animation_data:
                basis, pos, scale = _sample_curve_matrix(prev_helper, 1.0)

                
                eul = basis.to_euler()
                cp0.location = pos
                cp0.rotation_euler = eul
                cp0.scale = scale
                cp1.location = pos + basis.col[2].normalized() * 250
                cp1.rotation_euler = eul
                cp1.scale = scale


            for attr in ("road_shape_type", "horiz_subdivs",
                         "road_uv_multiplier", "mesh_subdivision_length",
                         "mesh_subdivision_angle_deg",
                         "num_checkpoints_per_segment",
                         "rail_height_left", "rail_height_right",
                         "rail_start_left", "rail_end_left",
                         "rail_start_right", "rail_end_right",
                         "rotation_mode"):
                setattr(props, attr, getattr(prev_props, attr))

            if props.road_shape_type in ('ROUNDED_SQUARE', 'ROUNDED_SQUARE_OPEN'):
                def _eval(helper_obj, default):
                    if helper_obj and helper_obj.animation_data and helper_obj.animation_data.action:
                        fcu = helper_obj.animation_data.action.fcurves.find("location", index=0)
                        if fcu:
                            return fcu.evaluate(100.0)
                    return default
                w_end = _eval(prev_props.width_helper, 1.0)
                h_end = _eval(prev_props.height_helper, 1.0)
                r_end = _eval(prev_props.radius_helper, 0.0)
                _square_helper_inherit_vals[seg_par.name] = (w_end, h_end, r_end)
                
            
            for mod_prev in prev_props.modulations:
                helper_prev = mod_prev.helper
                if not (helper_prev and helper_prev.animation_data and helper_prev.animation_data.action):
                    continue
                act_prev = helper_prev.animation_data.action

                f_h_prev = act_prev.fcurves.find("location", index=1)
                f_e_prev = act_prev.fcurves.find("location", index=2)
                if not f_e_prev:
                    continue

                bpy.ops.object.empty_add(type='SPHERE', radius=0, location=seg_par.location)
                helper_new = bpy.context.active_object
                _disallow_deletion(helper_new)
                helper_new.name = f"{seg_par.name}_Mod_{len(props.modulations):02d}"
                helper_new.parent = seg_par
                
                helper_new.animation_data_create()
                act_new = bpy.data.actions.new(f"{helper_new.name}_modCurves")
                helper_new.animation_data.action = act_new

                
                
                if f_h_prev and f_h_prev.keyframe_points:
                    
                    for kp_src in f_h_prev.keyframe_points:
                        helper_new.location[1] = kp_src.co.y
                        helper_new.keyframe_insert(data_path="location", index=1, frame=kp_src.co.x)
                    
                    
                    f_h_new = act_new.fcurves.find(data_path="location", index=1)
                    if f_h_new and len(f_h_new.keyframe_points) == len(f_h_prev.keyframe_points):
                        for i, kp_src in enumerate(f_h_prev.keyframe_points):
                            kp_new = f_h_new.keyframe_points[i]
                            
                            kp_new.handle_left = kp_src.handle_left
                            kp_new.handle_right = kp_src.handle_right
                            kp_new.handle_left_type = kp_src.handle_left_type
                            kp_new.handle_right_type = kp_src.handle_right_type
                            kp_new.interpolation = kp_src.interpolation
                            kp_new.easing = kp_src.easing
                        f_h_new.update()

                
                effect_end = f_e_prev.evaluate(100.0)
                helper_new.location[2] = effect_end
                helper_new.keyframe_insert(data_path="location", index=2, frame=0.0)
                helper_new.keyframe_insert(data_path="location", index=2, frame=100.0)

                
                f_e_new = act_new.fcurves.find("location", index=2)
                if f_e_new:
                    for kp in f_e_new.keyframe_points:
                        kp.interpolation = 'BEZIER'
                        kp.handle_left_type = "LINEAR_X"
                        kp.handle_right_type = "LINEAR_X"
                    _linearize_fcurve_handles_smooth(f_e_new)
                

                mod_new = props.modulations.add()
                mod_new.label = mod_prev.label
                mod_new.helper = helper_new
                props.active_mod_index = len(props.modulations) - 1
            for emb_prev in prev_props.embeds:
                helper_prev = emb_prev.helper
                if not helper_prev or not helper_prev.animation_data:
                    continue
                act_prev = helper_prev.animation_data.action
                if not act_prev:
                    continue

                f_left = act_prev.fcurves.find("location", index=1)
                f_right = act_prev.fcurves.find("location", index=2)
                if not (f_left and f_right):
                    continue

                
                tx_left_end = f_left.evaluate(100.0)
                tx_right_end = f_right.evaluate(100.0)

                
                bpy.ops.object.empty_add(type='SPHERE', radius=0, location=seg_par.location)
                helper_new = bpy.context.active_object
                _disallow_deletion(helper_new)
                helper_new.name = f"{seg_par.name}_Embed_{len(props.embeds):02d}"
                helper_new.parent = seg_par

                helper_new.animation_data_create()
                act_new = bpy.data.actions.new(f"{helper_new.name}_embedCurves")
                helper_new.animation_data.action = act_new

                
                
                helper_new.location[1] = tx_left_end
                helper_new.keyframe_insert(data_path="location", index=1, frame=0.0)
                helper_new.keyframe_insert(data_path="location", index=1, frame=100.0)
                
                
                helper_new.location[2] = tx_right_end
                helper_new.keyframe_insert(data_path="location", index=2, frame=0.0)
                helper_new.keyframe_insert(data_path="location", index=2, frame=100.0)

                
                for idx in [1, 2]:
                    fcu = act_new.fcurves.find("location", index=idx)
                    if fcu:
                        for kp in fcu.keyframe_points:
                            kp.interpolation = 'BEZIER'
                            kp.handle_left_type = "LINEAR_X"
                            kp.handle_right_type = "LINEAR_X"
                        _linearize_fcurve_handles_smooth(fcu)
                

                
                emb_new = props.embeds.add()
                emb_new.label = emb_prev.label
                emb_new.helper = helper_new
                emb_new.embed_type = emb_prev.embed_type
                emb_new.start_t = 0.0
                emb_new.end_t = 1.0
                props.active_embed_idx = len(props.embeds) - 1


        
        context.view_layer.objects.active = seg_par
        self.report({'INFO'},
                    f"Created MXT Road Segment docked to previous end" if prev_seg
                    else "Created standalone MXT Road Segment")
        return {'FINISHED'}

class MXTRoad_OT_AddControlPoint(Operator):
    bl_idname = "mxt_road.add_control_point_empty"; bl_label = "Add CP Empty"; bl_options = {'REGISTER', 'UNDO'}
    @classmethod
    def poll(cls, context): return get_active_mxt_road_segment_parent(context) is not None
    def execute(self, context):
        road_parent_empty = get_active_mxt_road_segment_parent(context)
        cp_empties = get_mxt_control_point_empties(road_parent_empty, sorted_by_time=True)
        new_loc_local = mathutils.Vector((0,0,0)); new_time = 1.0
        if len(cp_empties) > 0:
            last_cp = cp_empties[-1]
            offset_in_last_cp_space = mathutils.Vector((0, 0, 250.0))
            new_loc_world = last_cp.matrix_world @ offset_in_last_cp_space
            new_loc_local = road_parent_empty.matrix_world.inverted() @ new_loc_world
            new_time = last_cp.mxt_cp_data.time + 0.1
            new_orientation = last_cp.rotation_euler
            new_scale = last_cp.scale
            
        new_cp_name = f"MXTCP.{len(cp_empties):03d}"
        new_cp_empty = _create_cp_empty(context, road_parent_empty, new_cp_name, new_loc_local, new_time)
        new_cp_empty.rotation_euler = new_orientation
        new_cp_empty.scale = new_scale
        _update_road_segment_visual_guide_logic(road_parent_empty, self.report)
        self.report({'INFO'}, f"Added Control Point to {road_parent_empty.name}"); return {'FINISHED'}
class MXTRoad_OT_UpdatePathVisuals(Operator):
    bl_idname = "mxt_road.update_path_visuals"; bl_label = "Update Path Visuals"; bl_options = {'REGISTER', 'UNDO'}
    @classmethod
    def poll(cls, context): return get_active_mxt_road_segment_parent(context) is not None
    def execute(self, context):
        _update_road_segment_visual_guide_logic(get_active_mxt_road_segment_parent(context), self.report)
        return {'FINISHED'}
_mxt_draw_handle = None
def _mxt_helper_positions(helper, samples=256):
    if not (helper and helper.animation_data and helper.animation_data.action):
        return []
    act = helper.animation_data.action
    fx = act.fcurves.find("location", index=0)
    fy = act.fcurves.find("location", index=1)
    fz = act.fcurves.find("location", index=2)
    if not (fx and fy and fz):
        return []
    out = []
    for i in range(samples + 1):
        t = i / samples
        out.append(Vector((fx.evaluate(t * 100), fy.evaluate(t * 100), fz.evaluate(t * 100))))
    return out

def _collect_modulations(seg_parent):
    if not (seg_parent and seg_parent.animation_data
            and seg_parent.animation_data.action):
        return []
    act = seg_parent.animation_data.action
    mods = []
    idx  = 0
    while True:
        f_h = act.fcurves.find(f'["mod_height_{idx}"]')
        f_e = act.fcurves.find(f'["mod_effect_{idx}"]')
        if not (f_h and f_e):
            break
        mods.append((f_h, f_e))
        idx += 1
    return mods

def _vertical_offset(seg_parent, mod_t: float, ty: float) -> float:
    off = 0.0
    if hasattr(seg_parent.mxt_road_overall_props, "modulations"):
        for mod in seg_parent.mxt_road_overall_props.modulations:
            helper = mod.helper
            if not (helper and helper.animation_data and helper.animation_data.action):
                continue
            act = helper.animation_data.action
            f_h = act.fcurves.find("location", index=1)
            f_e = act.fcurves.find("location", index=2)
            if not (f_h and f_e):
                continue
            aff = f_e.evaluate(ty * 100)
            if abs(aff) < 1e-6:
                continue
            off += f_h.evaluate(mod_t * 100) * aff
    return off

def _isolate_modulation_graph_editor():
    area = next((a for a in bpy.context.screen.areas if a.type == 'GRAPH_EDITOR'), None)
    if not area:
        return
    obj = bpy.context.active_object
    if not obj or not obj.parent:
        return
    props = getattr(obj.parent, "mxt_road_overall_props", None)
    if not props or not hasattr(props, "modulations"):
        return
    for mod in props.modulations:
        if mod.helper == obj:
            if obj.animation_data and obj.animation_data.action:
                for fcu in obj.animation_data.action.fcurves:
                    fcu.select = True
            area.spaces.active.show_only_selected = True
            return
def _ensure_fcurve(act, data_path, array_index):
    for fcu in act.fcurves:
        if fcu.data_path == data_path and fcu.array_index == array_index:
            return fcu
    return act.fcurves.new(data_path, index=array_index)
def _bake_curve_matrix_direct(parent_obj):
    with _no_undo():
        with _mxt_profile_scope(f"bake_curvematrix {parent_obj.name}"):
            MXTRoad_OT_GenerateCurveMatrix.bake_for_parent(parent_obj)

def _build_mesh_direct(parent_obj):
    with _no_undo():
        MXTRoad_OT_GenerateMesh.build_for_parent(parent_obj, bpy.context)

def _ease(x, c):
    return x ** c if c != 0 else 0.0
def _remap(x, a, b, c, d):
    return (x - a) / (b - a + 1e-9) * (d - c) + c
def _root(helper, ty):
    basis, pos, _ = _sample_curve_matrix(helper, ty)
    return basis, pos
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

    if track_settings and _MXT_NAV_DRAW_CACHE and (track_settings.draw_cpu_nav or track_settings.draw_cpu_nav_routes):
        if track_settings.draw_cpu_nav_routes:
            route_colors = {
                "default": (1.0, 0.9, 0.05, 0.95),
                "safe": (0.1, 1.0, 0.35, 0.85),
                "boost_dash": (0.0, 0.65, 1.0, 0.9),
                "dash": (0.0, 0.65, 1.0, 0.9),
                "recharge": (1.0, 0.1, 0.85, 0.9),
                "reachable": (1.0, 0.35, 0.0, 0.95),
            }
            old_line_width = 2.0
            gpu.state.line_width_set(2.0)
            for alt in _MXT_NAV_DRAW_CACHE.get("route_alternative_positions", []):
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
                if len(route_positions) < 2:
                    continue
                batch = batch_for_shader(shader, 'LINE_STRIP', {"pos": route_positions})
                shader.bind()
                shader.uniform_float("color", route_colors.get(route_name, (1.0, 1.0, 1.0, 0.8)))
                batch.draw(shader)
            gpu.state.line_width_set(old_line_width)
        if track_settings.draw_cpu_nav:
            edge_positions = _MXT_NAV_DRAW_CACHE.get("edge_positions", [])
            if edge_positions:
                batch = batch_for_shader(shader, 'LINES', {"pos": edge_positions})
                shader.bind()
                shader.uniform_float("color", (0.0, 0.75, 1.0, 0.24))
                batch.draw(shader)
            node_positions = _MXT_NAV_DRAW_CACHE.get("node_positions", [])
            if node_positions:
                gpu.state.point_size_set(4.0)
                batch = batch_for_shader(shader, 'POINTS', {"pos": node_positions})
                shader.bind()
                shader.uniform_float("color", (0.1, 1.0, 0.25, 0.85))
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
            data_box.prop(road_props, "disable_preview_mesh_generation")
            preview_collision_row = data_box.row()
            preview_collision_row.enabled = not bool(road_props.disable_preview_mesh_generation)
            preview_collision_row.prop(road_props, "export_preview_mesh_collision")
            data_box.prop(road_props, "disable_auto_rebake")
            data_box.separator()
            data_box.operator("mxt_road.generate_curve_matrix", text="Generate CurveMatrix", icon='FCURVE')
            data_box.operator("mxt_road.generate_mesh", text="Generate/Update Mesh", icon='MESH_PLANE')
            data_box.operator("mxt_road.generate_checkpoints", text="Generate Checkpoints", icon='OUTLINER_OB_EMPTY')
            data_box.operator("mxt_road.bake_cpu_nav", text="Bake CPU Nav", icon='TRACKING_FORWARDS')
            data_box.operator("mxt_road.export_track", text="Export Track", icon='EXPORT')


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
                    # Prefer the vertex color layer as active for OBJ export
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

    def append_triangle_record(records, terrain_key, positions, normals, source_name, terrain_flags=0):
        if terrain_key not in terrain_map:
            raise RuntimeError(f"{source_name} has unsupported mesh collision surface {terrain_key}")
        if len(positions) != 3 or len(normals) != 3:
            raise RuntimeError(f"{source_name} produced an invalid mesh collision triangle")
        record = bytearray()
        record += struct.pack('<I', terrain_map[terrain_key] | terrain_flags)
        for p in positions:
            record += struct.pack('<3f', p.x, p.y, p.z)
        for n in normals:
            record += struct.pack('<3f', n.x, n.y, n.z)
        records.append(record)

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
                append_triangle_record(records, terrain_key, positions, normals, obj.name, terrain_flags)
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
                        append_triangle_record(
                            records,
                            terrain_key,
                            tri_positions,
                            tri_normals,
                            f"{seg.name} embed {embed.label}")
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
    return tri_count, mesh_data


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
        elif route_kind == "dash":
            edge_penalty *= 0.75
        penalty += edge_penalty
    if terrain & MXT_NAV_TERRAIN_BITS["lava"]:
        penalty += 600.0
    if flags & MXT_NAV_NODE_FLAGS["mine"]:
        penalty += 450.0
    if terrain & MXT_NAV_TERRAIN_BITS["dirt"]:
        penalty += 180.0 if route_kind != "dash" else 35.0
    if terrain & MXT_NAV_TERRAIN_BITS["ice"]:
        penalty += 90.0
    if route_kind == "safe":
        if terrain & MXT_NAV_TERRAIN_BITS["dash"]:
            penalty += 80.0
        if terrain & MXT_NAV_TERRAIN_BITS["jump"]:
            penalty += 130.0
    elif route_kind == "dash":
        if terrain & MXT_NAV_TERRAIN_BITS["dash"]:
            penalty -= 45.0
        if terrain & MXT_NAV_TERRAIN_BITS["jump"]:
            penalty -= 12.0
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
    from_ty = float(from_node.get("ty", 0.0))
    to_ty = float(to_node.get("ty", 0.0))
    flags = int(edge.get("flags", 0))

    if (flags & MXT_NAV_EDGE_FLAGS["lateral"]) != 0:
        return from_seg == to_seg and abs(to_ty - from_ty) <= 1.0e-5
    if to_seg > from_seg:
        return True
    if to_seg == from_seg:
        return to_ty >= from_ty - 1.0e-5
    return False


def _mxt_nav_route_outgoing(nodes, edges):
    nodes_by_id = {int(n["id"]): n for n in nodes}
    outgoing = {}
    for edge in edges:
        if not _mxt_nav_edge_allows_route(nodes_by_id, edge):
            continue
        outgoing.setdefault(int(edge["from"]), []).append(edge)
    return nodes_by_id, outgoing


def _mxt_nav_shortest_route(nodes, edges, start_node_id, finish_node_ids, route_kind,
                            extra_node_penalties=None, extra_edge_penalties=None,
                            nodes_by_id=None, route_outgoing=None):
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
            extra = _mxt_nav_cost_for_node(to_node, route_kind)
            edge_key = (int(edge["from"]), int(edge["to"]))
            extra += float(extra_node_penalties.get(to_id, 0.0))
            extra += float(extra_edge_penalties.get(edge_key, 0.0))
            cost = max(0.001, float(edge["cost"]) + extra)
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


def _mxt_nav_route_alternatives(nodes, edges, start_node_id, finish_node_ids, route_kind, max_routes=4,
                                nodes_by_id=None, route_outgoing=None):
    alternatives = []
    seen_paths = set()
    node_penalties = {}
    edge_penalties = {}
    for _ in range(max_routes):
        path = _mxt_nav_shortest_route(
            nodes,
            edges,
            start_node_id,
            finish_node_ids,
            route_kind,
            node_penalties,
            edge_penalties,
            nodes_by_id,
            route_outgoing,
        )
        if len(path) < 2:
            break
        path_key = tuple(int(node_id) for node_id in path)
        if path_key in seen_paths:
            break
        seen_paths.add(path_key)
        alternatives.append(path)

        for node_id in path[1:-1]:
            node_penalties[int(node_id)] = node_penalties.get(int(node_id), 0.0) + 180.0
        for i in range(len(path) - 1):
            edge_key = (int(path[i]), int(path[i + 1]))
            edge_penalties[edge_key] = edge_penalties.get(edge_key, 0.0) + 120.0

    return alternatives


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
            cost = max(0.001, float(edge["cost"]) + extra)
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
    score = -float(first_edge["cost"])
    visited = {path[0]}

    for _ in range(max_steps):
        node = by_id.get(cur)
        if node is None:
            break
        terrain = int(node.get("terrain", 0))
        flags = int(node.get("flags", 0))
        if terrain & MXT_NAV_TERRAIN_BITS["dash"]:
            terrain_counts["dash"] += 1
            score += 75.0 if route_kind in ("dash", "boost_dash") else 35.0
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
        candidates.sort(key=lambda edge: float(edge["cost"]) + _mxt_nav_cost_for_node(by_id.get(int(edge["to"]), {}), cost_route_kind))
        next_edge = candidates[0]
        cur = int(next_edge["to"])
        visited.add(cur)
        path.append(cur)
        score -= float(next_edge["cost"])

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


def _mxt_nav_generate(context, filepath, seg_order, seg_index):
    ts = context.scene.mxt_track_settings
    lateral_count = max(3, int(getattr(ts, "cpu_nav_lateral_samples", 9)))
    row_spacing_meters = max(1.0, float(getattr(ts, "cpu_nav_row_spacing_meters", 30.0)))
    transition_distance = max(0.0, float(getattr(ts, "cpu_nav_transition_distance", 32.0)))
    branch_distance = max(0.0, float(getattr(ts, "cpu_nav_branch_distance", 20.0)))

    tx_1d = np.linspace(-0.92, 0.92, lateral_count, dtype=np.float64)
    nodes = []
    edges = []
    edge_keys = set()
    segment_records = []
    segment_grids = {}
    skipped_segments = []
    triggers_by_segment = {seg: [] for seg in seg_order}
    for trig in ts.trigger_objects:
        if trig.segment in triggers_by_segment:
            triggers_by_segment[trig.segment].append(trig)

    def add_edge(from_id, to_id, cost, flags):
        if from_id is None or to_id is None or from_id == to_id:
            return False
        key = (int(from_id), int(to_id), int(flags))
        if key in edge_keys:
            return False
        edge_keys.add(key)
        edges.append({
            "from": int(from_id),
            "to": int(to_id),
            "cost": round(max(0.001, float(cost)), 6),
            "flags": int(flags),
        })
        return True

    for seg in seg_order:
        props = seg.mxt_road_overall_props
        helper = props.curve_matrix_helper_empty
        if not (helper and helper.animation_data and helper.animation_data.action):
            skipped_segments.append(seg.name)
            continue
        cps = getattr(props, "checkpoints", [])
        cp_count = max(1, len(cps))
        ty_1d = _mxt_nav_ty_samples_by_spacing(helper, row_spacing_meters)
        row_count = len(ty_1d)
        tx_grid, ty_grid = np.meshgrid(tx_1d, ty_1d)
        centerline_pos, centerline_basis, centerline_scl = _sample_curve_matrix_numpy(helper, ty_1d)
        positions_local = _calculate_vertex_positions_numpy(props, centerline_pos, centerline_basis, centerline_scl, tx_grid, ty_grid)

        row_nodes = [[None for _ in range(lateral_count)] for _ in range(row_count)]
        for row in range(row_count):
            for col in range(lateral_count):
                tx = float(tx_grid[row, col])
                ty = float(ty_grid[row, col])
                if _mxt_nav_sample_is_hole(props, tx, ty):
                    continue
                pos_local = positions_local[row, col]
                pos_world_v = seg.matrix_world @ Vector((float(pos_local[0]), float(pos_local[1]), float(pos_local[2])))
                pos_world = np.array((pos_world_v.x, pos_world_v.y, pos_world_v.z), dtype=np.float64)
                if row == 0:
                    fwd = positions_local[min(row + 1, row_count - 1), col] - positions_local[row, col]
                elif row == row_count - 1:
                    fwd = positions_local[row, col] - positions_local[row - 1, col]
                else:
                    fwd = positions_local[row + 1, col] - positions_local[row - 1, col]
                left_col = max(0, col - 1)
                right_col = min(lateral_count - 1, col + 1)
                lateral = positions_local[row, right_col] - positions_local[row, left_col]
                fwd_n = _mxt_nav_normalized(fwd, (0.0, 0.0, 1.0))
                normal_n = _mxt_nav_normalized(np.cross(fwd_n, lateral), (0.0, 1.0, 0.0))
                terrain, flags = _mxt_nav_terrain_and_flags(props, tx, ty, pos_world, triggers_by_segment.get(seg, []))
                if len(getattr(props, "next_segments", [])) > 1 or len(getattr(props, "prev_segments", [])) > 1:
                    flags |= MXT_NAV_NODE_FLAGS["branch"]
                node_id = len(nodes)
                checkpoint = min(cp_count - 1, int(math.floor(ty * cp_count)))
                node = {
                    "id": int(node_id),
                    "segment": int(seg_index[seg]),
                    "segment_name": seg.name,
                    "checkpoint": int(checkpoint),
                    "tx": round(tx, 6),
                    "ty": round(ty, 6),
                    "position": _mxt_nav_json_vec3(pos_world),
                    "normal": _mxt_nav_json_vec3(normal_n),
                    "forward": _mxt_nav_json_vec3(fwd_n),
                    "terrain": int(terrain),
                    "edge_clearance": round(_mxt_nav_edge_clearance_tx(props, tx, ty), 6),
                    "author_weight": 1.0,
                    "flags": int(flags),
                }
                nodes.append(node)
                row_nodes[row][col] = node_id

        for row in range(row_count):
            for col in range(lateral_count - 1):
                a = row_nodes[row][col]
                b = row_nodes[row][col + 1]
                if a is not None and b is not None:
                    pa = np.asarray(nodes[a]["position"], dtype=np.float64)
                    pb = np.asarray(nodes[b]["position"], dtype=np.float64)
                    cost = float(np.linalg.norm(pb - pa))
                    add_edge(a, b, cost, MXT_NAV_EDGE_FLAGS["lateral"])
                    add_edge(b, a, cost, MXT_NAV_EDGE_FLAGS["lateral"])

        for row in range(row_count - 1):
            for col in range(lateral_count):
                a = row_nodes[row][col]
                if a is None:
                    continue
                for next_col in (col - 1, col, col + 1):
                    if next_col < 0 or next_col >= lateral_count:
                        continue
                    b = row_nodes[row + 1][next_col]
                    if b is None:
                        continue
                    pa = np.asarray(nodes[a]["position"], dtype=np.float64)
                    pb = np.asarray(nodes[b]["position"], dtype=np.float64)
                    flags = MXT_NAV_EDGE_FLAGS["normal"]
                    if int(nodes[b]["terrain"]) & MXT_NAV_TERRAIN_BITS["jump"]:
                        flags |= MXT_NAV_EDGE_FLAGS["jump"]
                    add_edge(a, b, float(np.linalg.norm(pb - pa)), flags)

        segment_grids[seg] = {
            "rows": row_nodes,
            "tx": tx_1d,
            "ty": ty_1d,
            "row_count": row_count,
            "lateral_count": lateral_count,
        }
        segment_records.append({
            "id": int(seg_index[seg]),
            "name": seg.name,
            "node_start": int(min((n for row in row_nodes for n in row if n is not None), default=0)),
            "node_count": int(sum(1 for row in row_nodes for n in row if n is not None)),
            "rows": int(row_count),
            "lateral_samples": int(lateral_count),
        })

    transition_edges = 0
    glide_drop_edges = 0

    def connect_boundary_rows(from_seg, from_row, to_seg, to_row, max_distance, flags):
        nonlocal transition_edges
        from_grid = segment_grids.get(from_seg)
        to_grid = segment_grids.get(to_seg)
        if not from_grid or not to_grid:
            return
        from_nodes = from_grid["rows"][from_row]
        to_nodes = to_grid["rows"][to_row]
        for a in from_nodes:
            if a is None:
                continue
            pa = np.asarray(nodes[a]["position"], dtype=np.float64)
            for b in to_nodes:
                if b is None:
                    continue
                pb = np.asarray(nodes[b]["position"], dtype=np.float64)
                delta = pb - pa
                dist = float(np.linalg.norm(delta))
                if dist <= max_distance:
                    b_node = nodes[b]
                    edge_flags = flags
                    if int(b_node["terrain"]) & MXT_NAV_TERRAIN_BITS["jump"]:
                        edge_flags |= MXT_NAV_EDGE_FLAGS["jump"]
                    if add_edge(a, b, dist * 1.1, edge_flags):
                        transition_edges += 1

    def segment_has_prev(seg, prev_seg):
        props = seg.mxt_road_overall_props
        for ref in props.prev_segments:
            if ref.segment == prev_seg:
                return True
        return False

    def connect_glide_drop_rows(from_seg, to_seg, flags):
        nonlocal glide_drop_edges
        from_props = from_seg.mxt_road_overall_props
        to_props = to_seg.mxt_road_overall_props
        if not getattr(from_props, "cpu_nav_allow_glide_drops", True):
            return
        if not getattr(to_props, "cpu_nav_allow_glide_drops", True):
            return
        from_grid = segment_grids.get(from_seg)
        to_grid = segment_grids.get(to_seg)
        if not from_grid or not to_grid:
            return
        from_nodes = from_grid["rows"][-1]
        to_nodes = to_grid["rows"][0]
        for a in from_nodes:
            if a is None:
                continue
            pa = np.asarray(nodes[a]["position"], dtype=np.float64)
            for b in to_nodes:
                if b is None:
                    continue
                pb = np.asarray(nodes[b]["position"], dtype=np.float64)
                y_drop = float(pa[1] - pb[1])
                if y_drop <= 0.0:
                    continue
                dist = float(np.linalg.norm(pb - pa))
                edge_flags = flags | MXT_NAV_EDGE_FLAGS["jump"] | MXT_NAV_EDGE_FLAGS["glide_drop"]
                if add_edge(a, b, dist * 1.1, edge_flags):
                    glide_drop_edges += 1

    authored_pairs = set()
    for seg in seg_order:
        props = seg.mxt_road_overall_props
        next_refs = [ref.segment for ref in props.next_segments if ref.segment in segment_grids]
        for nxt in next_refs:
            authored_pairs.add((seg, nxt))
            flags = MXT_NAV_EDGE_FLAGS["transition"]
            if len(next_refs) > 1 or len(getattr(nxt.mxt_road_overall_props, "prev_segments", [])) > 1:
                flags |= MXT_NAV_EDGE_FLAGS["branch"]
            connect_boundary_rows(seg, -1, nxt, 0, transition_distance, flags)
            if segment_has_prev(nxt, seg):
                connect_glide_drop_rows(seg, nxt, flags)

    branch_edges = 0
    if branch_distance > 0.0:
        before = len(edges)
        segs_with_grids = list(segment_grids.keys())
        for seg_a in segs_with_grids:
            for seg_b in segs_with_grids:
                if seg_a == seg_b or (seg_a, seg_b) in authored_pairs:
                    continue
                connect_boundary_rows(seg_a, -1, seg_b, 0, branch_distance, MXT_NAV_EDGE_FLAGS["transition"] | MXT_NAV_EDGE_FLAGS["branch"])
        branch_edges = max(0, len(edges) - before)

    nav_segments = [seg for seg in seg_order if seg in segment_grids]
    first_seg = nav_segments[0] if nav_segments else None
    last_seg = nav_segments[-1] if nav_segments else None
    start_node = None
    finish_nodes = []
    if first_seg in segment_grids:
        first_row = segment_grids[first_seg]["rows"][0]
        candidates = [n for n in first_row if n is not None]
        if candidates:
            start_node = min(candidates, key=lambda n: abs(float(nodes[n]["tx"])))
    if last_seg in segment_grids:
        finish_nodes = [n for n in segment_grids[last_seg]["rows"][-1] if n is not None]

    route_kind_map = {
        "default": "default",
        "safe": "safe",
        "boost_dash": "dash",
        "dash": "dash",
        "recharge": "recharge",
    }
    nodes_by_id, route_outgoing = _mxt_nav_route_outgoing(nodes, edges)
    route_alternatives = {
        name: _mxt_nav_route_alternatives(
            nodes, edges, start_node, finish_nodes, kind, 4, nodes_by_id, route_outgoing
        )
        for name, kind in route_kind_map.items()
    }
    routes = {
        name: alternatives[0] if alternatives else []
        for name, alternatives in route_alternatives.items()
    }
    choice_points = _mxt_nav_choice_points(nodes, edges, route_alternatives, nodes_by_id, route_outgoing)
    diagnostic_routes = {
        "reachable": _mxt_nav_reachable_trace(nodes, edges, start_node, "default", nodes_by_id, route_outgoing),
    }
    route_lengths = {name: len(path) for name, path in routes.items()}
    route_alternative_counts = {name: len(paths) for name, paths in route_alternatives.items()}
    diagnostic_route_lengths = {name: len(path) for name, path in diagnostic_routes.items()}

    connected_nodes = set()
    for edge in edges:
        connected_nodes.add(int(edge["from"]))
        connected_nodes.add(int(edge["to"]))
    disconnected = len(nodes) - len(connected_nodes)

    nav = {
        "format": "mxt_nav",
        "version": MXT_NAV_FORMAT_VERSION,
        "source_track": filepath,
        "settings": {
            "lateral_samples": int(lateral_count),
            "row_spacing_meters": float(row_spacing_meters),
            "transition_distance": float(transition_distance),
            "branch_distance": float(branch_distance),
        },
        "terrain_bits": MXT_NAV_TERRAIN_BITS,
        "node_flags": MXT_NAV_NODE_FLAGS,
        "edge_flags": MXT_NAV_EDGE_FLAGS,
        "segments": segment_records,
        "nodes": nodes,
        "edges": edges,
        "routes": routes,
        "route_alternatives": route_alternatives,
        "choice_points": choice_points,
        "diagnostic_routes": diagnostic_routes,
        "diagnostics": {
            "node_count": int(len(nodes)),
            "edge_count": int(len(edges)),
            "transition_edges": int(transition_edges),
            "branch_edges": int(branch_edges),
            "glide_drop_edges": int(glide_drop_edges),
            "disconnected_nodes": int(disconnected),
            "start_node": -1 if start_node is None else int(start_node),
            "finish_node_count": int(len(finish_nodes)),
            "route_lengths": route_lengths,
            "route_alternative_counts": route_alternative_counts,
            "choice_point_count": int(len(choice_points)),
            "diagnostic_route_lengths": diagnostic_route_lengths,
            "skipped_segments": skipped_segments,
        },
    }

    global _MXT_NAV_DRAW_CACHE
    draw_edges = []
    for edge in edges:
        a = nodes[int(edge["from"])]["position"]
        b = nodes[int(edge["to"])]["position"]
        draw_edges.append(Vector((a[0], a[1], a[2])))
        draw_edges.append(Vector((b[0], b[1], b[2])))
    route_positions = {}
    for route_name, route_nodes in routes.items():
        if route_name == "dash":
            continue
        route_positions[route_name] = [
            Vector(tuple(nodes[int(node_id)]["position"]))
            for node_id in route_nodes
            if 0 <= int(node_id) < len(nodes)
        ]
    route_alternative_positions = []
    for route_name, alternatives in route_alternatives.items():
        if route_name == "dash":
            continue
        for alt_index, route_nodes in enumerate(alternatives):
            if alt_index == 0:
                continue
            positions = [
                Vector(tuple(nodes[int(node_id)]["position"]))
                for node_id in route_nodes
                if 0 <= int(node_id) < len(nodes)
            ]
            if len(positions) >= 2:
                route_alternative_positions.append({
                    "route": route_name,
                    "index": int(alt_index),
                    "positions": positions,
                })
    if not any(len(points) >= 2 for points in route_positions.values()):
        for route_name, route_nodes in diagnostic_routes.items():
            route_positions[route_name] = [
                Vector(tuple(nodes[int(node_id)]["position"]))
                for node_id in route_nodes
                if 0 <= int(node_id) < len(nodes)
            ]
    _MXT_NAV_DRAW_CACHE = {
        "node_positions": [Vector(tuple(node["position"])) for node in nodes],
        "edge_positions": draw_edges,
        "route_positions": route_positions,
        "route_alternative_positions": route_alternative_positions,
    }

    return nav


def _mxt_track_reachable_segment_order(first):
    segs = []
    visited = set()
    queue = [first]
    while queue:
        seg = queue.pop(0)
        if not seg or seg in visited:
            continue
        visited.add(seg)
        segs.append(seg)
        props = seg.mxt_road_overall_props
        for ref in props.next_segments:
            if ref.segment and ref.segment not in visited:
                queue.append(ref.segment)

    indeg = {s: 0 for s in visited}
    for seg in visited:
        props = seg.mxt_road_overall_props
        for ref in props.next_segments:
            nxt = ref.segment
            if nxt and nxt in indeg and nxt != first:
                indeg[nxt] += 1

    seg_order = []
    q = [s for s in segs if indeg[s] == 0]
    if first in q:
        q.remove(first)
        q.insert(0, first)

    seen = set()
    while q:
        seg = q.pop(0)
        if seg in seen:
            continue
        seen.add(seg)
        seg_order.append(seg)
        props = seg.mxt_road_overall_props
        for ref in props.next_segments:
            nxt = ref.segment
            if nxt and nxt in indeg and nxt != first:
                indeg[nxt] -= 1
                if indeg[nxt] == 0:
                    q.append(nxt)

    for seg in segs:
        if seg not in seen:
            seg_order.append(seg)

    return seg_order, visited


def _mxt_bake_cpu_nav_preview(context):
    ts = context.scene.mxt_track_settings
    first = ts.first_segment
    if not first:
        raise RuntimeError("First segment not set")
    seg_order, visited = _mxt_track_reachable_segment_order(first)
    with _mxt_profile_scope("nav_rebake_curvematrix_checkpoints"):
        for seg in seg_order:
            _bake_curve_matrix_direct(seg)
            _generate_checkpoints_for_segment(seg)
    for trig in ts.trigger_objects:
        if trig.segment in visited:
            _update_trigger_helper(trig)
    context.view_layer.update()
    seg_index = {s: i for i, s in enumerate(seg_order)}
    return _mxt_nav_generate(context, "", seg_order, seg_index)


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

    # Export preview meshes as OBJ
    obj_path = base_path + ".obj"
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
        if bpy.context.object and bpy.context.object.mode != 'OBJECT':
            prev_mode = bpy.context.object.mode
            bpy.ops.object.mode_set(mode='OBJECT')

        orig_active = bpy.context.view_layer.objects.active
        orig_sel = [obj for obj in bpy.context.selected_objects]
        bpy.ops.object.select_all(action='DESELECT')
        for obj in preview_meshes:
            obj.select_set(True)
        bpy.context.view_layer.objects.active = preview_meshes[0]
        # Blender 4.0 renamed the OBJ export operator. Try the new name first
        if hasattr(bpy.ops.wm, "obj_export"):
            # Attempt to include vertex colors, UVs and normals
            try:
                bpy.ops.wm.obj_export(filepath=obj_path,
                                       export_selected_objects=True,
                                       export_uv=True,
                                       export_normals=True,
                                       export_colors=True)
            except TypeError:
                bpy.ops.wm.obj_export(filepath=obj_path, export_selected_objects=True)
        elif hasattr(bpy.ops.export_scene, "obj"):
            # Legacy exporter; try vertex-color flag name if available
            ok = False
            try:
                bpy.ops.export_scene.obj(filepath=obj_path,
                                         use_selection=True,
                                         use_mesh_modifiers=True,
                                         use_uvs=True,
                                         use_normals=True,
                                         export_vertex_colors=True)
                ok = True
            except TypeError:
                pass
            if not ok:
                try:
                    bpy.ops.export_scene.obj(filepath=obj_path,
                                             use_selection=True,
                                             use_mesh_modifiers=True,
                                             use_uvs=True,
                                             use_normals=True)
                    ok = True
                except TypeError:
                    pass
            if not ok:
                raise RuntimeError("OBJ export failed: unsupported parameters")
        else:
            raise RuntimeError("OBJ export operator not found")
        bpy.ops.object.select_all(action='DESELECT')
        for obj in orig_sel:
            obj.select_set(True)
        bpy.context.view_layer.objects.active = orig_active
        if prev_mode:
            bpy.ops.object.mode_set(mode=prev_mode)

    import json

    if getattr(ts, "export_cpu_nav", True):
        nav_path = base_path + ".mxt_nav"
        with _mxt_profile_scope("export_cpu_nav"):
            nav = _mxt_nav_generate(context, filepath, seg_order, seg_index)
        with open(nav_path, "w", encoding="utf-8") as nf:
            json.dump(nav, nf, indent=2, separators=(",", ": "))
        metadata["cpu_nav"] = {
            "path": os.path.basename(nav_path),
            "version": MXT_NAV_FORMAT_VERSION,
            "nodes": int(nav["diagnostics"]["node_count"]),
            "edges": int(nav["diagnostics"]["edge_count"]),
            "routes": {name: len(path) for name, path in nav["routes"].items()},
            "route_alternatives": {name: len(paths) for name, paths in nav.get("route_alternatives", {}).items()},
            "choice_points": int(nav["diagnostics"].get("choice_point_count", 0)),
            "diagnostic_routes": {name: len(path) for name, path in nav.get("diagnostic_routes", {}).items()},
        }

    json_path = base_path + ".json"
    with open(json_path, "w", encoding="utf-8") as jf:
        json.dump(metadata, jf, indent=2)


class MXTRoad_OT_BakeCpuNav(Operator):
    """Bake the CPU navigation graph into the viewport overlay without writing track files"""

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
            context.scene.mxt_track_settings.draw_cpu_nav = True
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
classes_to_register = (
    MXTSegmentRef,
    MXTTriggerObject,
    MXTMeshCollisionProperties,
    MXTTrackEditorUIState,
    MXTTrackSettings,
    MXT_UL_SegmentRefs,
    MXTRoad_OT_AddPrevSegment,
    MXTRoad_OT_RemovePrevSegment,
    MXTRoad_OT_AddNextSegment,
    MXTRoad_OT_RemoveNextSegment,
    MXTModulation,
    MXTEmbed,
    MXT_UL_Embeds,
    MXTRoad_OT_AddEmbed,
    MXTRoad_OT_RemoveEmbed,
    MXT_UL_TriggerObjects,
    MXT_OT_AddTrigger,
    MXT_OT_RemoveTrigger,
    MXTCheckpoint,
    MXTRoad_LineHandleData,
    MXTRoad_ControlPointData,
    MXTRoad_RoadSegmentOverallProperties,
    MXT_UL_Modulations,
    MXTRoad_OT_AddModulation,
    MXTRoad_OT_RemoveModulation,
    MXTRoad_OT_SelectHelper,
    MXTRoad_OT_ConvertSegmentType,
    MXTRoad_OT_RespaceCPTimes,
    MXTRoad_OT_CreateRoadSegment,
    MXTRoad_OT_AddControlPoint,
    MXTRoad_OT_UpdatePathVisuals,
    MXT_OT_set_handle_length,
    MXT_GGT_CPHandleGizmos,
    MXTRoad_PT_MainPanel,
    MXTRoad_OT_GenerateCurveMatrix,
    MXTRoad_OT_GenerateMesh,
    MXTRoad_OT_GenerateCheckpoints,
    MXTRoad_OT_BakeCpuNav,
    MXTRoad_OT_ExportTrack,
)
def register():
    global mxt_roads_pending_visual_update, mxt_timer_is_active, _timer_live
    mxt_roads_pending_visual_update = set()
    mxt_timer_is_active = False
    _timer_live = False
    for cls in classes_to_register: bpy.utils.register_class(cls)
    bpy.types.Object.mxt_road_overall_props = PointerProperty(type=MXTRoad_RoadSegmentOverallProperties)
    bpy.types.Object.mxt_cp_data = PointerProperty(type=MXTRoad_ControlPointData)
    bpy.types.Object.mxt_line_handle_data = PointerProperty(type=MXTRoad_LineHandleData)
    bpy.types.Object.mxt_mesh_collision_props = PointerProperty(type=MXTMeshCollisionProperties)
    bpy.types.Scene.mxt_track_settings = PointerProperty(type=MXTTrackSettings)
    bpy.types.WindowManager.mxt_track_editor_ui_state = PointerProperty(type=MXTTrackEditorUIState)
    handlers = bpy.app.handlers.depsgraph_update_post
    if mxt_on_depsgraph_update not in handlers: handlers.append(mxt_on_depsgraph_update)
    global _mxt_draw_handle
    _mxt_draw_handle = bpy.types.SpaceView3D.draw_handler_add(
        mxt_draw_callback, (), 'WINDOW', 'POST_VIEW')
    print("MXT Road Creator (v0.1.0) Registered")
def unregister():
    global mxt_roads_pending_visual_update, mxt_timer_is_active
    global _mxt_draw_handle
    if _mxt_draw_handle is not None:
        bpy.types.SpaceView3D.draw_handler_remove(_mxt_draw_handle, 'WINDOW')
        _mxt_draw_handle = None
    if mxt_timer_is_active:
        try: bpy.app.timers.unregister(_process_pending_visual_updates)
        except ValueError: pass
        mxt_timer_is_active = False
    mxt_roads_pending_visual_update.clear()
    handlers = bpy.app.handlers.depsgraph_update_post
    global _timer_live
    if _timer_live:
        try: bpy.app.timers.unregister(_process_live_updates)
        except ValueError: pass
    _timer_live = False
    _cm_pending.clear(); _mesh_pending.clear()
    if mxt_on_depsgraph_update in handlers: handlers.remove(mxt_on_depsgraph_update)
    if hasattr(bpy.types.Object, "mxt_cp_data"): del bpy.types.Object.mxt_cp_data
    if hasattr(bpy.types.Object, "mxt_line_handle_data"): del bpy.types.Object.mxt_line_handle_data
    if hasattr(bpy.types.Object, "mxt_mesh_collision_props"): del bpy.types.Object.mxt_mesh_collision_props
    if hasattr(bpy.types.Object, "mxt_road_overall_props"): del bpy.types.Object.mxt_road_overall_props
    if hasattr(bpy.types.Scene, "mxt_track_settings"): del bpy.types.Scene.mxt_track_settings
    if hasattr(bpy.types.WindowManager, "mxt_track_editor_ui_state"): del bpy.types.WindowManager.mxt_track_editor_ui_state
    for cls in reversed(classes_to_register): bpy.utils.unregister_class(cls)
    print("MXT Road Creator (v0.1.0) Unregistered")
if __name__ == "__main__":
    try: unregister()
    except Exception: pass
    register()
