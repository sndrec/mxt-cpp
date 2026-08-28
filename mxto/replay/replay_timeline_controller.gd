class_name ReplayTimelineController
extends Node

const INTERFACE_CANVAS_LAYER := 90
const INPUT_DISPLAY_SCRIPT := preload("res://replay/replay_input_display.gd")
const DEATH_KO_TEXTURE: Texture2D = preload("res://asset/tex/ui/replay_death_ko.png")
const DEATH_FALL_TEXTURE: Texture2D = preload("res://asset/tex/ui/replay_death_fall.png")
const DEATH_EXPLOSION_TEXTURE: Texture2D = preload("res://asset/tex/ui/replay_death_explosion.png")
const DEATH_ICON_SIZE := Vector2(24.0, 24.0)
const DEATH_FALLOUT := 2

@onready var game_manager: GameManager = get_parent() as GameManager
@onready var playback: ReplayController = get_node("../ReplayController") as ReplayController
@onready var replay_camera_controller: ReplayCameraController = get_node("../ReplayCameraController") as ReplayCameraController
@onready var race_presentation_controller: RacePresentationController = get_node("../RacePresentationController") as RacePresentationController
@onready var debug_runtime_controller: DebugRuntimeController = get_node("../DebugRuntimeController") as DebugRuntimeController

var interface_layer: CanvasLayer
var replay_timeline_root: Control
var replay_timeline_panel: PanelContainer
var replay_timeline_track: ColorRect
var replay_timeline_fill: ColorRect
var replay_timeline_playhead: ColorRect
var replay_timeline_marker_layer: Control
var replay_timeline_time_label: Label
var replay_timeline_rate_label: Label
var replay_timeline_play_button: Button
var replay_timeline_camera_mode_button: Button
var replay_timeline_save_local_button: Button
var replay_timeline_focus_prev_button: Button
var replay_timeline_focus_next_button: Button
var replay_timeline_resume_practice_button: Button
var replay_timeline_resume_reason_label: Label
var replay_resume_dialog: ConfirmationDialog
var replay_resume_keep_original_checkbox: CheckBox
var replay_input_display_panel: PanelContainer
var replay_input_display: Control
var replay_input_display_checkbox: CheckBox
var replay_input_display_enabled := false
var replay_hide_hud_checkbox: CheckBox
var replay_hide_hud_enabled := false
var replay_input_display_frame_inputs: Dictionary = {}
var replay_timeline_markers: Dictionary = {}
var replay_marker_last_laps: Dictionary = {}
var replay_marker_last_places: Dictionary = {}
var replay_marker_last_death_states: Dictionary = {}
var collecting_markers := false
var replay_timeline_markers_dirty := true
var replay_timeline_marker_last_focus := -999999
var replay_timeline_marker_last_size := Vector2(-1.0, -1.0)


func _input(event: InputEvent) -> void:
	if !playback.replay_playback_active or !(event is InputEventKey):
		return
	var key := event as InputEventKey
	if !key.pressed or key.echo:
		return
	if key.keycode == KEY_LEFT:
		playback._step_replay_by_ticks(-1)
		get_viewport().set_input_as_handled()
	elif key.keycode == KEY_RIGHT:
		playback._step_replay_by_ticks(1)
		get_viewport().set_input_as_handled()


func _ensure_interface_layer() -> CanvasLayer:
	if interface_layer != null and is_instance_valid(interface_layer):
		return interface_layer
	interface_layer = CanvasLayer.new()
	interface_layer.name = "ReplayInterfaceLayer"
	interface_layer.layer = INTERFACE_CANVAS_LAYER
	add_child(interface_layer)
	return interface_layer


func initialize() -> void:
	if replay_timeline_root != null and is_instance_valid(replay_timeline_root):
		return
	replay_timeline_root = Control.new()
	replay_timeline_root.name = "ReplayTimeline"
	replay_timeline_root.visible = false
	replay_timeline_root.mouse_filter = Control.MOUSE_FILTER_IGNORE
	replay_timeline_root.set_anchors_preset(Control.PRESET_FULL_RECT)
	_ensure_interface_layer().add_child(replay_timeline_root)
	replay_timeline_panel = PanelContainer.new()
	replay_timeline_panel.mouse_filter = Control.MOUSE_FILTER_STOP
	replay_timeline_panel.anchor_left = 0.08
	replay_timeline_panel.anchor_right = 0.92
	replay_timeline_panel.anchor_top = 1.0
	replay_timeline_panel.anchor_bottom = 1.0
	replay_timeline_panel.offset_top = -210.0
	replay_timeline_panel.offset_bottom = -18.0
	replay_timeline_root.add_child(replay_timeline_panel)
	replay_input_display_panel = PanelContainer.new()
	replay_input_display_panel.name = "ReplayInputDisplayPanel"
	replay_input_display_panel.mouse_filter = Control.MOUSE_FILTER_IGNORE
	replay_input_display_panel.anchor_left = 0.08
	replay_input_display_panel.anchor_right = 0.08
	replay_input_display_panel.anchor_top = 1.0
	replay_input_display_panel.anchor_bottom = 1.0
	replay_input_display_panel.offset_left = 0.0
	replay_input_display_panel.offset_right = 176.0
	replay_input_display_panel.offset_top = -260.0
	replay_input_display_panel.offset_bottom = -140.0
	replay_input_display_panel.visible = false
	_ensure_interface_layer().add_child(replay_input_display_panel)
	var input_display_margin := MarginContainer.new()
	input_display_margin.add_theme_constant_override("margin_left", 6)
	input_display_margin.add_theme_constant_override("margin_right", 6)
	input_display_margin.add_theme_constant_override("margin_top", 4)
	input_display_margin.add_theme_constant_override("margin_bottom", 4)
	replay_input_display_panel.add_child(input_display_margin)
	replay_input_display = INPUT_DISPLAY_SCRIPT.new() as Control
	input_display_margin.add_child(replay_input_display)
	var margin := MarginContainer.new()
	margin.add_theme_constant_override("margin_left", 16)
	margin.add_theme_constant_override("margin_right", 16)
	margin.add_theme_constant_override("margin_top", 10)
	margin.add_theme_constant_override("margin_bottom", 10)
	replay_timeline_panel.add_child(margin)
	var rows := VBoxContainer.new()
	rows.add_theme_constant_override("separation", 8)
	margin.add_child(rows)
	var focus_controls := HBoxContainer.new()
	focus_controls.add_theme_constant_override("separation", 10)
	rows.add_child(focus_controls)
	replay_timeline_focus_prev_button = Button.new()
	replay_timeline_focus_prev_button.text = "<"
	replay_timeline_focus_prev_button.focus_mode = Control.FOCUS_NONE
	replay_timeline_focus_prev_button.custom_minimum_size = Vector2(180.0, 28.0)
	replay_timeline_focus_prev_button.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	replay_timeline_focus_prev_button.pressed.connect(replay_camera_controller.change_focus.bind(-1))
	focus_controls.add_child(replay_timeline_focus_prev_button)
	replay_timeline_focus_next_button = Button.new()
	replay_timeline_focus_next_button.text = ">"
	replay_timeline_focus_next_button.focus_mode = Control.FOCUS_NONE
	replay_timeline_focus_next_button.custom_minimum_size = Vector2(180.0, 28.0)
	replay_timeline_focus_next_button.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	replay_timeline_focus_next_button.pressed.connect(replay_camera_controller.change_focus.bind(1))
	focus_controls.add_child(replay_timeline_focus_next_button)
	replay_timeline_track = ColorRect.new()
	replay_timeline_track.color = Color(0.08, 0.09, 0.1, 0.92)
	replay_timeline_track.custom_minimum_size = Vector2(0.0, 18.0)
	replay_timeline_track.mouse_filter = Control.MOUSE_FILTER_STOP
	replay_timeline_track.gui_input.connect(_on_replay_timeline_track_input)
	rows.add_child(replay_timeline_track)
	replay_timeline_fill = ColorRect.new()
	replay_timeline_fill.color = Color(0.95, 0.76, 0.26, 1.0)
	replay_timeline_fill.mouse_filter = Control.MOUSE_FILTER_IGNORE
	replay_timeline_track.add_child(replay_timeline_fill)
	replay_timeline_playhead = ColorRect.new()
	replay_timeline_playhead.color = Color(1.0, 1.0, 1.0, 1.0)
	replay_timeline_playhead.mouse_filter = Control.MOUSE_FILTER_IGNORE
	replay_timeline_track.add_child(replay_timeline_playhead)
	replay_timeline_marker_layer = Control.new()
	replay_timeline_marker_layer.name = "MarkerLayer"
	replay_timeline_marker_layer.mouse_filter = Control.MOUSE_FILTER_IGNORE
	replay_timeline_marker_layer.set_anchors_preset(Control.PRESET_FULL_RECT)
	replay_timeline_track.add_child(replay_timeline_marker_layer)
	replay_timeline_track.move_child(replay_timeline_playhead, replay_timeline_track.get_child_count() - 1)
	var controls := HBoxContainer.new()
	controls.add_theme_constant_override("separation", 10)
	rows.add_child(controls)
	replay_timeline_play_button = Button.new()
	replay_timeline_play_button.text = "Pause"
	replay_timeline_play_button.focus_mode = Control.FOCUS_NONE
	replay_timeline_play_button.pressed.connect(_on_replay_timeline_play_pressed)
	controls.add_child(replay_timeline_play_button)
	replay_timeline_camera_mode_button = Button.new()
	replay_timeline_camera_mode_button.text = "Camera: Game"
	replay_timeline_camera_mode_button.focus_mode = Control.FOCUS_NONE
	replay_timeline_camera_mode_button.pressed.connect(replay_camera_controller.cycle_mode)
	controls.add_child(replay_timeline_camera_mode_button)
	var slower := Button.new()
	slower.text = "-"
	slower.focus_mode = Control.FOCUS_NONE
	slower.pressed.connect(_on_replay_timeline_slower_pressed)
	controls.add_child(slower)
	replay_timeline_rate_label = Label.new()
	replay_timeline_rate_label.custom_minimum_size.x = 70.0
	replay_timeline_rate_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	controls.add_child(replay_timeline_rate_label)
	var faster := Button.new()
	faster.text = "+"
	faster.focus_mode = Control.FOCUS_NONE
	faster.pressed.connect(_on_replay_timeline_faster_pressed)
	controls.add_child(faster)
	replay_input_display_checkbox = CheckBox.new()
	replay_input_display_checkbox.text = "Inputs"
	replay_input_display_checkbox.focus_mode = Control.FOCUS_NONE
	replay_input_display_checkbox.button_pressed = replay_input_display_enabled
	replay_input_display_checkbox.toggled.connect(_on_replay_input_display_toggled)
	controls.add_child(replay_input_display_checkbox)
	replay_hide_hud_checkbox = CheckBox.new()
	replay_hide_hud_checkbox.text = "Hide HUD"
	replay_hide_hud_checkbox.focus_mode = Control.FOCUS_NONE
	replay_hide_hud_checkbox.button_pressed = replay_hide_hud_enabled
	replay_hide_hud_checkbox.toggled.connect(_on_replay_hide_hud_toggled)
	controls.add_child(replay_hide_hud_checkbox)
	replay_timeline_save_local_button = Button.new()
	replay_timeline_save_local_button.text = "Save Replay Locally"
	replay_timeline_save_local_button.focus_mode = Control.FOCUS_NONE
	replay_timeline_save_local_button.visible = false
	replay_timeline_save_local_button.pressed.connect(_on_replay_timeline_save_local_pressed)
	controls.add_child(replay_timeline_save_local_button)
	replay_timeline_resume_practice_button = Button.new()
	replay_timeline_resume_practice_button.text = "Resume in Practice"
	replay_timeline_resume_practice_button.focus_mode = Control.FOCUS_NONE
	replay_timeline_resume_practice_button.pressed.connect(_on_replay_resume_practice_pressed)
	controls.add_child(replay_timeline_resume_practice_button)
	replay_timeline_time_label = Label.new()
	replay_timeline_time_label.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	replay_timeline_time_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_RIGHT
	controls.add_child(replay_timeline_time_label)
	replay_camera_controller.build_controls(rows)
	replay_timeline_resume_reason_label = Label.new()
	replay_timeline_resume_reason_label.modulate = Color(0.78, 0.78, 0.78, 1.0)
	replay_timeline_resume_reason_label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	rows.add_child(replay_timeline_resume_reason_label)



func _on_replay_resume_practice_pressed() -> void:
	var eligibility := playback._replay_resume_eligibility()
	if !bool(eligibility.get("eligible", false)):
		race_presentation_controller.show_notification(String(eligibility.get("reason", "Replay cannot resume.")), 4000)
		return
	if replay_resume_dialog == null or !is_instance_valid(replay_resume_dialog):
		replay_resume_dialog = ConfirmationDialog.new()
		replay_resume_dialog.name = "ReplayResumePracticeDialog"
		replay_resume_dialog.title = "Resume in Practice"
		replay_resume_dialog.dialog_text = "Continue from the current replay frame?\nThe focused racer becomes yours; every other racer continues as a CPU."
		replay_resume_dialog.ok_button_text = "Resume"
		replay_resume_dialog.cancel_button_text = "Cancel"
		replay_resume_dialog.exclusive = true
		replay_resume_dialog.confirmed.connect(_on_replay_resume_practice_confirmed)
		replay_resume_keep_original_checkbox = CheckBox.new()
		replay_resume_keep_original_checkbox.text = "Keep Original as Ghost"
		replay_resume_keep_original_checkbox.custom_minimum_size = Vector2(0.0, 30.0)
		replay_resume_dialog.add_child(replay_resume_keep_original_checkbox)
		add_child(replay_resume_dialog)
	var has_future := game_manager._singleplayer_tick < playback._playback_frame_count()
	replay_resume_keep_original_checkbox.button_pressed = has_future
	replay_resume_keep_original_checkbox.disabled = !has_future
	replay_resume_dialog.popup_centered(Vector2i(650, 210))


func _on_replay_resume_practice_confirmed() -> void:
	var keep_original := replay_resume_keep_original_checkbox != null \
		and !replay_resume_keep_original_checkbox.disabled \
		and replay_resume_keep_original_checkbox.button_pressed
	var payload := playback._capture_replay_resume_payload(keep_original)
	if payload.is_empty():
		race_presentation_controller.show_notification("The current replay frame could not be captured.", 4000)
		return
	game_manager.call_deferred("resume_replay_in_practice", payload)



func format_time(tick_value: int) -> String:
	var total_msec := int(round(float(maxi(tick_value, 0)) * 1000.0 / 60.0))
	var minutes := int(total_msec / 60000)
	var seconds := int(total_msec / 1000) % 60
	var milliseconds := total_msec % 1000
	return "%d:%02d.%03d" % [minutes, seconds, milliseconds]

func refresh_save_local_button() -> void:
	if replay_timeline_save_local_button == null:
		return
	var local_path := game_manager.replay_recorder.staged_local_path(playback.replay_playback_loaded_path)
	replay_timeline_save_local_button.visible = !local_path.is_empty()
	var already_saved := !local_path.is_empty() and FileAccess.file_exists(local_path)
	replay_timeline_save_local_button.disabled = already_saved
	replay_timeline_save_local_button.text = "Saved Locally" if already_saved else "Save Replay Locally"

func _on_replay_timeline_save_local_pressed() -> void:
	var saved_path := game_manager.replay_recorder.save_staged_locally(playback.replay_playback_loaded_path)
	if saved_path.is_empty():
		race_presentation_controller.show_notification("Replay could not be saved", 3000)
		return
	race_presentation_controller.show_notification("Replay Saved", 2200)
	refresh_save_local_button()

func _replay_marker_bucket(player_id: int) -> Dictionary:
	if !replay_timeline_markers.has(player_id):
		replay_timeline_markers[player_id] = {
			"death_ko": [],
			"death_fall": [],
			"death_explosion": [],
			"kos": [],
			"laps": [],
			"finishes": [],
			"first_overtakes": [],
			"place_up": [],
			"place_down": [],
		}
	return replay_timeline_markers[player_id]

func _add_replay_timeline_marker(player_id: int, marker_type: String, tick_value: int) -> void:
	tick_value = clampi(tick_value, 0, maxi(playback._playback_frame_count(), 1))
	var bucket := _replay_marker_bucket(player_id)
	var key := marker_type
	if !bucket.has(key):
		bucket[key] = []
	var markers: Array = bucket[key]
	if !markers.has(tick_value):
		markers.append(tick_value)
		replay_timeline_markers_dirty = true

func initialize_markers() -> void:
	replay_timeline_markers.clear()
	replay_marker_last_laps.clear()
	replay_marker_last_places.clear()
	replay_marker_last_death_states.clear()
	replay_timeline_markers_dirty = true
	for id_value in playback.replay_playback_racer_ids:
		var id := int(id_value)
		_replay_marker_bucket(id)
		var finish_tick := playback._lookup_replay_tick_for_id(playback.replay_saved_finish_times, id)
		if finish_tick >= 0:
			_add_replay_timeline_marker(id, "finishes", finish_tick)
		if playback.replay_skip_seek_bake_requested:
			var death_tick := playback._lookup_replay_tick_for_id(playback.replay_saved_eliminations, id)
			if death_tick >= 0:
				_add_replay_timeline_marker(id, "death_explosion", death_tick)

func record_timeline_event(event: Dictionary) -> void:
	if int(event.get("type", 0)) != 1:
		return
	var tick_value := int(event.get("tick", game_manager._singleplayer_tick))
	var attacker_id := int(event.get("actor_id", -1))
	var target_id := int(event.get("target_id", -1))
	if attacker_id >= 0:
		_add_replay_timeline_marker(attacker_id, "kos", tick_value)
	if target_id >= 0:
		_add_replay_timeline_marker(target_id, "death_ko", tick_value)

func _replay_bucket_has_death_at_tick(bucket: Dictionary, tick_value: int) -> bool:
	return (bucket.get("death_ko", []) as Array).has(tick_value) \
		or (bucket.get("death_fall", []) as Array).has(tick_value) \
		or (bucket.get("death_explosion", []) as Array).has(tick_value)

func _update_replay_death_timeline_markers() -> void:
	if game_manager.game_sim == null or !game_manager.game_sim.has_method("get_vehicle_death_states"):
		return
	var states: PackedInt32Array = game_manager.game_sim.get_vehicle_death_states()
	for index in range(0, states.size() - 1, 2):
		var id := int(states[index])
		var death_state := int(states[index + 1])
		var previous_state := int(replay_marker_last_death_states.get(id, 0))
		if death_state != 0 and previous_state == 0:
			var tick_value := game_manager._singleplayer_tick
			var bucket := _replay_marker_bucket(id)
			if !_replay_bucket_has_death_at_tick(bucket, tick_value):
				var marker_type := "death_fall" if (death_state & DEATH_FALLOUT) != 0 else "death_explosion"
				_add_replay_timeline_marker(id, marker_type, tick_value)
		replay_marker_last_death_states[id] = death_state

func _update_replay_lap_timeline_markers() -> void:
	if game_manager.game_sim == null or !game_manager.game_sim.has_method("get_player_lap"):
		return
	for id_value in playback.replay_playback_racer_ids:
		var id := int(id_value)
		var lap := int(game_manager.game_sim.get_player_lap(id))
		if !replay_marker_last_laps.has(id):
			replay_marker_last_laps[id] = lap
			continue
		var previous_lap := int(replay_marker_last_laps[id])
		if lap > previous_lap:
			for crossed_lap in range(previous_lap + 1, lap + 1):
				if crossed_lap > 0:
					_add_replay_timeline_marker(id, "laps", game_manager._singleplayer_tick)
		replay_marker_last_laps[id] = lap

func _update_replay_placement_timeline_markers() -> void:
	if game_manager.game_sim == null or !game_manager.game_sim.has_method("get_player_race_place"):
		return
	for id_value in playback.replay_playback_racer_ids:
		var id := int(id_value)
		var place := int(game_manager.game_sim.get_player_race_place(id))
		if place <= 0:
			continue
		if !replay_marker_last_places.has(id):
			replay_marker_last_places[id] = place
			continue
		var previous_place := int(replay_marker_last_places[id])
		if previous_place <= 0:
			replay_marker_last_places[id] = place
			continue
		if place < previous_place:
			_add_replay_timeline_marker(id, "place_up", game_manager._singleplayer_tick)
			if place == 1:
				_add_replay_timeline_marker(id, "first_overtakes", game_manager._singleplayer_tick)
		elif place > previous_place:
			_add_replay_timeline_marker(id, "place_down", game_manager._singleplayer_tick)
		replay_marker_last_places[id] = place


func capture_simulation_markers() -> void:
	_update_replay_lap_timeline_markers()
	_update_replay_placement_timeline_markers()
	_update_replay_death_timeline_markers()

func _clear_replay_timeline_marker_nodes() -> void:
	if replay_timeline_marker_layer == null:
		return
	for child in replay_timeline_marker_layer.get_children():
		replay_timeline_marker_layer.remove_child(child)
		child.queue_free()
	replay_timeline_marker_last_focus = -999999
	replay_timeline_marker_last_size = Vector2(-1.0, -1.0)

func reset() -> void:
	replay_timeline_markers.clear()
	replay_marker_last_laps.clear()
	replay_marker_last_places.clear()
	replay_marker_last_death_states.clear()
	collecting_markers = false
	replay_timeline_markers_dirty = true
	_clear_replay_timeline_marker_nodes()
	replay_input_display_frame_inputs = {}
	refresh_input_display()
	if replay_timeline_root != null:
		replay_timeline_root.visible = false
	apply_hud_visibility()

func _timeline_marker_x(tick_value: int) -> float:
	var total_ticks := maxf(float(maxi(playback._playback_frame_count(), 1)), 1.0)
	return replay_timeline_track.size.x * clampf(float(tick_value) / total_ticks, 0.0, 1.0)

func _add_timeline_line_marker(x: float, width: float, height: float, bottom: float, color: Color) -> void:
	var rect := ColorRect.new()
	rect.color = color
	rect.mouse_filter = Control.MOUSE_FILTER_IGNORE
	rect.size = Vector2(width, height)
	rect.position = Vector2(x - width * 0.5, bottom - height)
	replay_timeline_marker_layer.add_child(rect)

func _add_timeline_circle_marker(x: float, radius: float, color: Color) -> void:
	var circle := Polygon2D.new()
	circle.color = color
	var points := PackedVector2Array()
	for i in range(20):
		var angle := TAU * float(i) / 20.0
		points.append(Vector2(cos(angle), sin(angle)) * radius)
	circle.polygon = points
	circle.position = Vector2(x, replay_timeline_track.size.y * 0.5)
	replay_timeline_marker_layer.add_child(circle)

func _add_timeline_death_marker(x: float, texture: Texture2D) -> void:
	var icon := TextureRect.new()
	icon.texture = texture
	icon.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
	icon.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_CENTERED
	icon.mouse_filter = Control.MOUSE_FILTER_IGNORE
	icon.size = DEATH_ICON_SIZE
	var half_size := DEATH_ICON_SIZE * 0.5
	var center_x := clampf(x, half_size.x, replay_timeline_track.size.x - half_size.x)
	icon.position = Vector2(center_x - half_size.x, replay_timeline_track.size.y * 0.5 - half_size.y)
	replay_timeline_marker_layer.add_child(icon)

func _add_timeline_flag_marker(x: float, color: Color) -> void:
	var bar_h := replay_timeline_track.size.y
	var line_h := bar_h + 16.0
	_add_timeline_line_marker(x, 3.0, line_h, bar_h, color)
	var flag := Polygon2D.new()
	flag.color = color
	flag.polygon = PackedVector2Array([
		Vector2(0.0, 0.0),
		Vector2(14.0, 5.0),
		Vector2(0.0, 10.0),
	])
	flag.position = Vector2(x + 1.5, bar_h - line_h)
	replay_timeline_marker_layer.add_child(flag)

func _redraw_replay_timeline_markers() -> void:
	if replay_timeline_track == null or replay_timeline_marker_layer == null:
		return
	_clear_replay_timeline_marker_nodes()
	var bucket := _replay_marker_bucket(replay_camera_controller.focused_player_id())
	var bar_h := replay_timeline_track.size.y
	var circle_radius := maxf(2.0, (bar_h + 2.0) * 0.5)
	for tick_value in bucket.get("place_down", []):
		_add_timeline_line_marker(_timeline_marker_x(int(tick_value)), 1.0, bar_h, bar_h, Color(1.0, 0.48, 0.48, 1.0))
	for tick_value in bucket.get("place_up", []):
		_add_timeline_line_marker(_timeline_marker_x(int(tick_value)), 1.0, bar_h, bar_h, Color(0.56, 1.0, 0.62, 1.0))
	for tick_value in bucket.get("laps", []):
		_add_timeline_flag_marker(_timeline_marker_x(int(tick_value)), Color(0.2, 1.0, 0.28, 1.0))
	for tick_value in bucket.get("finishes", []):
		_add_timeline_flag_marker(_timeline_marker_x(int(tick_value)), Color.WHITE)
	for tick_value in bucket.get("first_overtakes", []):
		_add_timeline_circle_marker(_timeline_marker_x(int(tick_value)), circle_radius, Color(1.0, 0.78, 0.12, 1.0))
	for tick_value in bucket.get("kos", []):
		_add_timeline_circle_marker(_timeline_marker_x(int(tick_value)), circle_radius, Color(1.0, 0.08, 0.05, 1.0))
	for tick_value in bucket.get("death_explosion", []):
		_add_timeline_death_marker(_timeline_marker_x(int(tick_value)), DEATH_EXPLOSION_TEXTURE)
	for tick_value in bucket.get("death_fall", []):
		_add_timeline_death_marker(_timeline_marker_x(int(tick_value)), DEATH_FALL_TEXTURE)
	for tick_value in bucket.get("death_ko", []):
		_add_timeline_death_marker(_timeline_marker_x(int(tick_value)), DEATH_KO_TEXTURE)
	replay_timeline_markers_dirty = false
	replay_timeline_marker_last_focus = replay_camera_controller.focused_player_id()
	replay_timeline_marker_last_size = replay_timeline_track.size

func _update_replay_timeline_marker_nodes() -> void:
	if replay_timeline_track == null or replay_timeline_marker_layer == null:
		return
	var focus_id := replay_camera_controller.focused_player_id()
	if replay_timeline_markers_dirty or focus_id != replay_timeline_marker_last_focus or replay_timeline_track.size != replay_timeline_marker_last_size:
		_redraw_replay_timeline_markers()


func _on_replay_timeline_play_pressed() -> void:
	playback.replay_playback_paused = !playback.replay_playback_paused
	playback._apply_replay_playback_clock()
	update()

func _on_replay_timeline_slower_pressed() -> void:
	playback._set_replay_playback_rate(playback.replay_playback_rate * 0.5)
	update()

func _on_replay_timeline_faster_pressed() -> void:
	playback._set_replay_playback_rate(playback.replay_playback_rate * 2.0)
	update()

func _on_replay_input_display_toggled(enabled: bool) -> void:
	replay_input_display_enabled = enabled
	refresh_input_display()

func _on_replay_hide_hud_toggled(enabled: bool) -> void:
	replay_hide_hud_enabled = enabled
	apply_hud_visibility()

func apply_hud_visibility() -> void:
	var hidden := playback.replay_playback_active and replay_hide_hud_enabled
	debug_runtime_controller.set_replay_hud_hidden(hidden)
	var race_hud := race_presentation_controller.local_race_hud() as RaceHud
	if race_hud == null:
		return
	if hidden or debug_runtime_controller.disable_hud or debug_runtime_controller.hide_hud_only:
		race_hud.visible = false
	else:
		race_hud.visible = true

func refresh_input_display() -> void:
	if replay_input_display_panel == null or replay_input_display == null:
		return
	var should_show := playback.replay_playback_active and replay_input_display_enabled and !game_manager.headless_mode
	replay_input_display_panel.visible = should_show
	if !should_show:
		return
	var focus_id := replay_camera_controller.focused_player_id()
	if replay_input_display_frame_inputs.has(focus_id):
		replay_input_display.call("set_input_bytes", replay_input_display_frame_inputs[focus_id] as PackedByteArray)
	else:
		replay_input_display.call("clear_input")


func _on_replay_timeline_track_input(event: InputEvent) -> void:
	if !playback.replay_playback_active:
		return
	var mouse_event := event as InputEventMouse
	if mouse_event == null:
		return
	if event is InputEventMouseButton:
		var button := event as InputEventMouseButton
		if button.button_index != MOUSE_BUTTON_LEFT or !button.pressed:
			return
	elif !(event is InputEventMouseMotion and Input.is_mouse_button_pressed(MOUSE_BUTTON_LEFT)):
		return
	var width := maxf(replay_timeline_track.size.x, 1.0)
	var ratio := clampf(mouse_event.position.x / width, 0.0, 1.0)
	playback._seek_replay_to_tick(roundi(ratio * float(maxi(playback._playback_frame_count(), 1))))
	update()
	get_viewport().set_input_as_handled()


func update() -> void:
	if replay_timeline_root == null:
		return
	var should_show := false
	if playback.replay_playback_active:
		var mouse_y := get_viewport().get_mouse_position().y
		var viewport_h := get_viewport().get_visible_rect().size.y
		should_show = mouse_y >= viewport_h - 240.0
		if replay_timeline_panel != null:
			should_show = should_show or replay_timeline_panel.get_global_rect().has_point(get_viewport().get_mouse_position())
	replay_timeline_root.visible = should_show
	var total_ticks := maxi(playback._playback_frame_count(), 1)
	var current_tick := clampi(game_manager._singleplayer_tick, 0, total_ticks)
	var ratio := float(current_tick) / float(total_ticks)
	if replay_timeline_fill != null and replay_timeline_track != null:
		replay_timeline_fill.position = Vector2.ZERO
		replay_timeline_fill.size = Vector2(replay_timeline_track.size.x * ratio, replay_timeline_track.size.y)
	if replay_timeline_playhead != null and replay_timeline_track != null:
		replay_timeline_playhead.size = Vector2(4.0, replay_timeline_track.size.y + 8.0)
		replay_timeline_playhead.position = Vector2(replay_timeline_track.size.x * ratio - 2.0, -4.0)
	_update_replay_timeline_marker_nodes()
	if replay_timeline_time_label != null:
		replay_timeline_time_label.text = "%s / %s    tick %d / %d" % [
			format_time(current_tick),
			format_time(total_ticks),
			current_tick,
			total_ticks
		]
	if replay_timeline_rate_label != null:
		replay_timeline_rate_label.text = playback._format_replay_playback_rate()
	if replay_timeline_play_button != null:
		replay_timeline_play_button.text = "Play" if playback.replay_playback_paused else "Pause"
	if replay_timeline_camera_mode_button != null:
		replay_timeline_camera_mode_button.text = "Camera: %s" % replay_camera_controller.mode_name()
	replay_camera_controller.update_control_visibility()
	if replay_timeline_resume_practice_button != null:
		var eligibility := playback._replay_resume_eligibility()
		var eligible := bool(eligibility.get("eligible", false))
		var reason := String(eligibility.get("reason", ""))
		replay_timeline_resume_practice_button.disabled = !eligible
		replay_timeline_resume_practice_button.tooltip_text = reason
		if replay_timeline_resume_reason_label != null:
			replay_timeline_resume_reason_label.text = reason
