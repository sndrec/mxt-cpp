@tool

class_name BoostPad extends MeshInstance3D


var current_boost_intensity : float = 1.0
var last_boost_tick : int = 0
var boost_buffer := StreamPeerBuffer.new()
var boost_time : float = 0.0
var boost_curve := preload("res://core/stage/dashplate_intensity_curve.tres")

@export var boost_curvature : float = 0.0

func _post_stage_loaded() -> void:
	pass

func _ready() -> void:
	if Engine.is_editor_hint():
		return
	var boost_shader := get_active_material(0) as ShaderMaterial
	set_blend_shape_value(0, boost_curvature)
	set_surface_override_material(0, boost_shader.duplicate(true))

func execute_boost() -> void:
	var time_since_last_boost := MXGlobal.tick_delta * (MXGlobal.currentStageOverseer.localTick - last_boost_tick)
	var curve_ratio := minf(1.0, time_since_last_boost * 0.0625 )
	current_boost_intensity = clampf(current_boost_intensity + boost_curve.sample_baked(curve_ratio), 1.0, 5.0)
	last_boost_tick = MXGlobal.currentStageOverseer.localTick

func _process( delta:float ) -> void:
	if Engine.is_editor_hint():
		#print(get_blend_shape_count())
		set_blend_shape_value(0, boost_curvature)
		return
	if !MXGlobal.currentStageOverseer:
		return
	var boost_shader := get_active_material(0) as ShaderMaterial
	var time_since_last_boost := MXGlobal.tick_delta * (MXGlobal.currentStageOverseer.localTick - last_boost_tick)
	var curve_ratio := minf(1.0, time_since_last_boost * 0.0625 )
	var boost_intensity := clampf(current_boost_intensity + boost_curve.sample_baked(curve_ratio), 1.0, 5.0)
	boost_shader.set_shader_parameter("booster_intensity", boost_intensity)
	boost_time = boost_time + delta * boost_intensity
	boost_shader.set_shader_parameter("boost_time", boost_time)

func save_state() -> PackedByteArray:
	boost_buffer.data_array = []
	boost_buffer.resize(64)
	boost_buffer.put_float(current_boost_intensity)
	boost_buffer.put_u32(last_boost_tick)
	
	boost_buffer.resize(boost_buffer.get_position())
	
	return boost_buffer.data_array

func load_state(inData : PackedByteArray) -> void:
	boost_buffer.data_array = inData
	current_boost_intensity = boost_buffer.get_float()
	last_boost_tick = boost_buffer.get_u32()
