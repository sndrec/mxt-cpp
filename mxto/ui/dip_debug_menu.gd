extends Node

@export var game_sim_path: NodePath

const WINDOW_TITLE := "GameSim DIP Switches"
const DIP_BRANCH_CENTERLINE := 0x20
const DIP_DEBUG_MENU_TEMP_DISABLED := true

var _game_sim: GameSim
var _menu_open := false
var _dip_entries: Array = []
var _imgui_connected := false

func _ready() -> void:
	# TEMP: DIP debug menu uses ImGui; disabled while diagnosing exported-build crash.
	return

func _input(event: InputEvent) -> void:
	# TEMP: DIP debug menu uses ImGui; disabled while diagnosing exported-build crash.
	return

func _on_imgui_layout() -> void:
	# TEMP: ImGui layout disabled.
	return

func _render_lap_distance_window() -> void:
	# TEMP: ImGui layout disabled.
	return

func _render_menu_contents() -> void:
	# TEMP: ImGui layout disabled.
	return

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
	# TEMP: ImGui connection disabled.
	return
