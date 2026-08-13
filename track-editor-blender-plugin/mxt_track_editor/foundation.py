"""Property models, segment editing, and live-update ownership."""

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
    "rail": 0x100,
    "hole": 0x200,
    "fall": 0x400,
    "kill": 0x800,
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
    "mesh": 0x800,
}
MXT_NAV_EDGE_FLAGS = {
    "normal": 0x1,
    "transition": 0x2,
    "branch": 0x4,
    "jump": 0x8,
    "manual": 0x10,
    "lateral": 0x20,
    "glide_drop": 0x40,
    "lookahead": 0x80,
}
MXT_NAV_MESH_SURFACE_TERRAIN = {
    'TRACK': MXT_NAV_TERRAIN_BITS["normal"],
    'RAIL': MXT_NAV_TERRAIN_BITS["rail"],
    'RECHARGE': MXT_NAV_TERRAIN_BITS["recharge"],
    'DIRT': MXT_NAV_TERRAIN_BITS["dirt"],
    'ICE': MXT_NAV_TERRAIN_BITS["ice"],
    'LAVA': MXT_NAV_TERRAIN_BITS["lava"],
    'HOLE': MXT_NAV_TERRAIN_BITS["hole"],
    'FALL': MXT_NAV_TERRAIN_BITS["fall"],
    'KILL': MXT_NAV_TERRAIN_BITS["kill"],
    'DASH': MXT_NAV_TERRAIN_BITS["dash"],
    'JUMP': MXT_NAV_TERRAIN_BITS["jump"],
}
MXT_NAV_MESH_DRIVABLE_SURFACES = {'TRACK', 'RECHARGE', 'DIRT', 'ICE', 'LAVA', 'DASH', 'JUMP'}
MXT_NAV_MESH_BLOCKER_SURFACES = {'RAIL', 'KILL'}

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

MXT_SONG_ITEMS = [
    ('res://content/base/sound/mus_aeropolis.tres', "Aeropolis", "res://content/base/sound/mus_aeropolis.tres"),
    ('res://content/base/sound/mus_big_blue.tres', "Big Blue", "res://content/base/sound/mus_big_blue.tres"),
    ('res://content/base/sound/mus_casino.tres', "Casino", "res://content/base/sound/mus_casino.tres"),
    ('res://content/base/sound/mus_chance.tres', "Chance", "res://content/base/sound/mus_chance.tres"),
    ('res://content/base/sound/mus_dream_chaser.tres', "Dream Chaser", "res://content/base/sound/mus_dream_chaser.tres"),
    ('res://content/base/sound/mus_green_plant.tres', "Green Plant", "res://content/base/sound/mus_green_plant.tres"),
    ('res://content/base/sound/mus_lightning.tres', "Lightning", "res://content/base/sound/mus_lightning.tres"),
    ('res://content/base/sound/mus_mutecity.tres', "Mute City", "res://content/base/sound/mus_mutecity.tres"),
    ('res://content/base/sound/mus_porttown.tres', "Port Town", "res://content/base/sound/mus_porttown.tres"),
    ('res://content/base/sound/mus_ptown.tres', "P Town", "res://content/base/sound/mus_ptown.tres"),
    ('res://content/base/sound/mus_rainbow.tres', "Rainbow", "res://content/base/sound/mus_rainbow.tres"),
    ('res://content/base/sound/mus_sandocean.tres', "Sand Ocean", "res://content/base/sound/mus_sandocean.tres"),
    ('res://content/base/sound/mus_scream.tres', "Scream", "res://content/base/sound/mus_scream.tres"),
    ('res://content/base/sound/mus_virtualreality.tres', "Virtual Reality", "res://content/base/sound/mus_virtualreality.tres"),
    ('CUSTOM', "Custom", "Use a track-local or explicit MXMusic resource path"),
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
        description="Include this mesh in the exported glTF visual without using it as track collision",
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

    song: EnumProperty(
        name="Song",
        description="Music resource used by this track",
        items=MXT_SONG_ITEMS,
        default='res://content/base/sound/mus_mutecity.tres'
    )

    song_custom_resource: StringProperty(
        name="Custom Song Resource",
        description="Track-local MXMusic resource filename/path, e.g. music.tres. Relative paths resolve from the exported track JSON folder",
        default="music.tres"
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
        default=False
    )
    draw_cpu_nav: BoolProperty(
        name="Draw CPU Nav",
        description="Draw the most recently baked CPU navigation graph in the viewport",
        default=False
    )
    draw_cpu_nav_routes: BoolProperty(
        name="Draw CPU Nav Routes",
        description="Draw pathfinder routes from the most recently baked CPU navigation graph",
        default=False
    )
    draw_cpu_nav_route_default: BoolProperty(name="Default Route", default=False)
    draw_cpu_nav_route_safe: BoolProperty(name="Safe Route", default=False)
    draw_cpu_nav_route_aggressive: BoolProperty(name="Aggressive Route", default=False)
    draw_cpu_nav_route_dash: BoolProperty(name="Dash Route", default=False)
    draw_cpu_nav_route_recharge: BoolProperty(name="Recharge Route", default=False)
    draw_cpu_nav_route_reachable: BoolProperty(name="Reachable Trace", default=False)
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
    cpu_nav_mesh_sample_spacing_meters: FloatProperty(
        name="Mesh Nav Spacing",
        description="Approximate meters between CPU nav samples on drivable mesh collision geometry",
        default=35.0,
        min=2.0,
        soft_max=250.0
    )
    draw_mesh_collision_nav: BoolProperty(
        name="Draw Mesh Collision Nav",
        description="Draw sampled mesh collision nav points and blocking mesh collision outlines after baking CPU nav",
        default=False
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
    cpu_nav_lateral_samples: IntProperty(
        name="CPU Nav Lateral Samples",
        description="Per-segment nav lanes across this road; 0 uses the global track setting",
        default=0,
        min=0,
        max=65
    )
    cpu_nav_row_spacing_meters: FloatProperty(
        name="CPU Nav Row Spacing",
        description="Per-segment meters between nav rows; 0 uses the global track setting",
        default=0.0,
        min=0.0,
        soft_max=250.0
    )
    cpu_nav_transition_distance: FloatProperty(
        name="CPU Nav Transition Distance",
        description="Outgoing authored transition connection distance for this segment; -1 uses the global track setting",
        default=-1.0,
        min=-1.0,
        soft_max=500.0
    )
    cpu_nav_branch_distance: FloatProperty(
        name="CPU Nav Branch Distance",
        description="Physically touching branch connection distance from this segment; -1 uses the global track setting",
        default=-1.0,
        min=-1.0,
        soft_max=500.0
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

# Imported after foundational definitions to keep registration direct.
from .curve_matrix import (
    MXTRoad_OT_GenerateCurveMatrix,
)

from .shapes import (
    RoadShapeCylinder,
    RoadShapeCylinderOpen,
    RoadShapeFlat,
    RoadShapePipe,
    RoadShapePipeOpen,
    RoadShapeRoundedSquare,
    RoadShapeRoundedSquareOpen,
    _sample_curve_matrix,
    mxt_draw_callback,
)

from .mesh import (
    MXTRoad_OT_GenerateCheckpoints,
    MXTRoad_OT_GenerateMesh,
)

from .export import (
    MXTRoad_OT_BakeCpuNav,
    MXTRoad_OT_ExportTrack,
    MXTRoad_OT_PreviewCpuNavGraph,
)

from .ui import (
    MXTRoad_PT_MainPanel,
)

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
    MXTRoad_OT_PreviewCpuNavGraph,
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
