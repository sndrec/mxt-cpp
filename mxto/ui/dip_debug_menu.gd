extends Node

@export var game_sim_path: NodePath

const WINDOW_TITLE := "GameSim DIP Switches"
const DIP_BRANCH_CENTERLINE := 0x20

var _game_sim: GameSim
var _menu_open := false
var _dip_entries: Array = []
var _imgui_connected := false

func _ready() -> void:
	_resolve_game_sim()
	set_process_input(true)
	_connect_imgui()

func _input(event: InputEvent) -> void:
	if event is InputEventKey and event.pressed and not event.echo and event.keycode == Key.KEY_F2:
		_menu_open = not _menu_open
		if not _menu_open:
			return
		_connect_imgui()
		if _game_sim == null:
			_resolve_game_sim()

func _on_imgui_layout() -> void:
	_render_lap_distance_window()
	if not _menu_open:
		return
	if _game_sim == null:
		_resolve_game_sim()

	ImGui.SetNextWindowSize(Vector2(320, 0), ImGui.Cond_Once)
	var open_state := [_menu_open]
	var window_visible := ImGui.Begin(WINDOW_TITLE, open_state)
	if open_state[0] != _menu_open:
		_menu_open = open_state[0]
	if window_visible:
		_render_menu_contents()
	ImGui.End()

func _render_lap_distance_window() -> void:
	if _game_sim == null:
		_resolve_game_sim()
	if _game_sim == null:
		return
	if not _game_sim.is_dip_switch_enabled(DIP_BRANCH_CENTERLINE):
		return

	ImGui.SetNextWindowSize(Vector2(220, 0), ImGui.Cond_Once)
	var window_open := ImGui.Begin("Lap Distance")
	if window_open:
		var lap_distance := _game_sim.get_first_lap_distance()
		ImGui.Text("First player lap distance: %.2f m" % lap_distance)
	ImGui.End()

func _render_menu_contents() -> void:
	if _game_sim == null:
		ImGui.Text("GameSim node not found.")
		return

	_sync_dip_entries()
	if _dip_entries.is_empty():
		ImGui.Text("No DIP switches exposed.")
		return

	for entry in _dip_entries:
		var value_container: Array = entry["value"]
		var previous_value: bool = value_container[0]
		var display_label: String = entry["label"]
		var checkbox_label := "%s##dip_%d" % [display_label, entry["flag"]]
		ImGui.Checkbox(checkbox_label, value_container)
		var new_value: bool = value_container[0]
		if new_value != previous_value:
			_game_sim.set_dip_switch_enabled(entry["flag"], new_value)

func _sync_dip_entries() -> void:
	var entries_by_flag := {}
	for entry in _dip_entries:
		entries_by_flag[entry["flag"]] = entry

	var refreshed: Array = []
	var switch_data: Array = []
	if _game_sim != null:
		switch_data = _game_sim.get_dip_switches()

	for item in switch_data:
		if not item is Dictionary:
			continue
		var flag := int(item.get("flag", 0))
		var label := String(item.get("label", item.get("key", "")))
		var enabled := bool(item.get("enabled", false))
		var entry = entries_by_flag.get(flag)
		if entry == null:
			entry = {
				"flag": flag,
				"key": String(item.get("key", label)),
				"label": label,
				"value": [enabled],
			}
		else:
			entry["label"] = label
			var value_container: Array = entry.get("value", [])
			if value_container.is_empty():
				entry["value"] = [enabled]
			else:
				value_container[0] = enabled
		refreshed.append(entry)

	_dip_entries = refreshed

func _resolve_game_sim() -> void:
	var node: Node = null
	if game_sim_path != NodePath():
		node = get_node_or_null(game_sim_path)
	if node == null:
		node = get_tree().get_root().find_child("GameSim", true, false)
	if node is GameSim:
		_game_sim = node
	else:
		_game_sim = null

func _connect_imgui() -> void:
	if _imgui_connected:
		return
	if not Engine.has_singleton("ImGuiGD"):
		call_deferred("_connect_imgui")
		return
	ImGuiGD.Connect(Callable(self, "_on_imgui_layout"))
	_imgui_connected = true
