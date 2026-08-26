class_name SpectatorRoster
extends CanvasLayer

const UPDATE_INTERVAL_SECONDS := 0.05
const ENERGY_METER_WIDTH := 96.0

@onready var root: Control = $Root
@onready var rows_container: VBoxContainer = $Root/Panel/Margin/Box/PlayerScroll/Rows

var network_manager: NetworkManager
var game_sim: GameSim
var spectator_controller: SpectatorController
var stable_player_ids: Array[int] = []
var rows := {}
var update_accumulator := 0.0

func initialize(
	in_network_manager: NetworkManager,
	in_game_sim: GameSim,
	in_spectator_controller: SpectatorController
) -> void:
	network_manager = in_network_manager
	game_sim = in_game_sim
	spectator_controller = in_spectator_controller
	spectator_controller.race_configured.connect(_capture_race_roster)
	spectator_controller.race_reset.connect(_clear_roster)
	root.visible = false

func _process(delta: float) -> void:
	if network_manager == null or game_sim == null or spectator_controller == null:
		root.visible = false
		return
	var should_show := network_manager.network_active \
		and game_sim.sim_started \
		and spectator_controller.can_live_spectate() \
		and !stable_player_ids.is_empty()
	root.visible = should_show
	if !should_show:
		return
	update_accumulator += delta
	if update_accumulator < UPDATE_INTERVAL_SECONDS:
		return
	update_accumulator = fmod(update_accumulator, UPDATE_INTERVAL_SECONDS)
	_update_rows()

func _capture_race_roster() -> void:
	_clear_roster()
	var human_ids := network_manager.race_player_ids
	if human_ids.is_empty():
		human_ids = network_manager.player_ids
	var cpu_ids := network_manager.lobby_settings.get_cpu_roster()
	for id_value in human_ids:
		var player_id := int(id_value)
		if cpu_ids.has(player_id) or network_manager.spectator_ids.has(player_id):
			continue
		stable_player_ids.append(player_id)
		_add_player_row(player_id)
	_update_rows()

func _clear_roster() -> void:
	stable_player_ids.clear()
	rows.clear()
	update_accumulator = 0.0
	if rows_container != null:
		for child in rows_container.get_children():
			child.queue_free()
	if root != null:
		root.visible = false

func _add_player_row(player_id: int) -> void:
	var row := HBoxContainer.new()
	row.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	row.add_theme_constant_override("separation", 6)
	rows_container.add_child(row)

	var place_label := Label.new()
	place_label.custom_minimum_size = Vector2(34.0, 0.0)
	place_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_RIGHT
	place_label.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
	place_label.text = "-"
	row.add_child(place_label)

	var name_button := Button.new()
	name_button.text = _player_display_name(player_id)
	name_button.alignment = HORIZONTAL_ALIGNMENT_LEFT
	name_button.flat = true
	name_button.focus_mode = Control.FOCUS_NONE
	name_button.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	name_button.pressed.connect(_on_player_pressed.bind(player_id))
	row.add_child(name_button)

	var energy_meter := ProgressBar.new()
	energy_meter.custom_minimum_size = Vector2(ENERGY_METER_WIDTH, 12.0)
	energy_meter.min_value = 0.0
	energy_meter.max_value = 1.0
	energy_meter.value = 1.0
	energy_meter.show_percentage = false
	energy_meter.mouse_filter = Control.MOUSE_FILTER_IGNORE
	var depleted_style := StyleBoxFlat.new()
	depleted_style.bg_color = Color(0.0, 0.0, 0.0, 0.65)
	depleted_style.border_width_left = 1
	depleted_style.border_width_top = 1
	depleted_style.border_width_right = 1
	depleted_style.border_width_bottom = 1
	depleted_style.border_color = Color(0.7, 0.7, 0.7, 0.8)
	var energy_style := StyleBoxFlat.new()
	energy_style.bg_color = Color(0.25, 0.95, 0.45, 1.0)
	energy_style.border_width_left = 1
	energy_style.border_width_top = 1
	energy_style.border_width_right = 1
	energy_style.border_width_bottom = 1
	energy_style.border_color = Color(0.9, 1.0, 0.92, 0.9)
	energy_meter.add_theme_stylebox_override("background", depleted_style)
	energy_meter.add_theme_stylebox_override("fill", energy_style)
	row.add_child(energy_meter)

	rows[player_id] = {
		"place": place_label,
		"name": name_button,
		"energy": energy_meter,
	}

func _update_rows() -> void:
	for player_id in stable_player_ids:
		var row: Dictionary = rows.get(player_id, {})
		if row.is_empty():
			continue
		var place := int(network_manager.race_results.player_finish_placements.get(
			player_id,
			game_sim.get_player_race_place(player_id)))
		var place_label := row["place"] as Label
		place_label.text = str(place) if place > 0 else "-"

		var energy_meter := row["energy"] as ProgressBar
		energy_meter.value = game_sim.get_player_energy_fraction(player_id)

		var name_button := row["name"] as Button
		var is_focused := spectator_controller.live_focus_id == player_id
		name_button.add_theme_color_override("font_color", Color(0.35, 0.85, 1.0) if is_focused else Color(0.9, 0.9, 0.9))
		name_button.disabled = network_manager._disconnected_during_race.has(player_id)

func _on_player_pressed(player_id: int) -> void:
	if spectator_controller.focus_player(player_id):
		_update_rows()

func _player_display_name(player_id: int) -> String:
	var settings = network_manager.lobby_settings.player_settings.get(player_id, null)
	if typeof(settings) == TYPE_DICTIONARY and settings.has("username"):
		return str(settings["username"])
	return str(player_id)
