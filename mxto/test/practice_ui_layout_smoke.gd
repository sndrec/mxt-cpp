extends SceneTree

func _init() -> void:
	call_deferred("_run")


func _run() -> void:
	root.size = Vector2i(1280, 720)
	var backdrop := ColorRect.new()
	backdrop.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	backdrop.color = Color(0.12, 0.14, 0.18, 1.0)
	root.add_child(backdrop)
	var hud := (load("res://practice/practice_hud.tscn") as PackedScene).instantiate() as CanvasLayer
	var editor := (load("res://practice/practice_input_editor.tscn") as PackedScene).instantiate() as CanvasLayer
	var options := (load("res://ui/options_menu.tscn") as PackedScene).instantiate() as Control
	root.add_child(hud)
	root.add_child(editor)
	root.add_child(options)
	options.visible = true
	(options.get_node("Shade/Center/Panel/Margin/Root/Tabs") as TabContainer).current_tab = 3
	hud.visible = true
	editor.visible = true
	var status := hud.get_node("Root/Margin/Panel/Inner/Status") as Label
	status.text = "PRACTICE  ·  0.00x  ·  Manual input\nSlot 1 — Saved  ·  Rewind 44/45  ·  Timeline 334\nTick 333  ·  Lap 1/5  ·  287.5 km/h  ·  Yaw +0.0°/s\nGripped  ·  Energy 110/110  ·  Turbo 0.0  ·  Boost Off\nVelocity F/L/V  +285.2  -0.0  -0.0 km/h  ·  Angular P/Y/R  +0.0  +0.0  +0.3°/s\nCorners gripped  ·  Grounded  ·  Surface Road  ·  Height 19.17\nCheckpoint 0 + 0.0852  ·  Progress 0.0002  ·  Ground CP 0  ·  Collision CP 0\nInput SX/SY/L/R  127/127/0/0  ·  A1 B0 Boost0 Spin0 Side0"
	await process_frame
	await process_frame
	var hud_panel := hud.get_node("Root/Margin/Panel") as PanelContainer
	var editor_panel := editor.get_node("Panel") as PanelContainer
	if absf(hud_panel.size.x - 720.0) > 1.0 or absf(hud_panel.global_position.x - 20.0) > 1.0 \
			or absf(hud_panel.global_position.y + hud_panel.size.y - 700.0) > 1.0:
		_fail("telemetry panel is not fixed-width and bottom-left aligned: pos=%s size=%s" % [hud_panel.global_position, hud_panel.size])
		return
	if absf(editor_panel.size.x - 540.0) > 1.0 or absf(editor_panel.position.x - 720.0) > 1.0 \
			or absf(editor_panel.position.y - 100.0) > 1.0 or editor_panel.size.y > 430.0:
		_fail("exact-input panel is not compact and top-right aligned: pos=%s size=%s" % [editor_panel.position, editor_panel.size])
		return
	var controls := options.get_node("Shade/Center/Panel/Margin/Root/Tabs/Controls/ControllerSettings") as Control
	var expected_actions := [
		"Pause", "CameraUp", "CameraDown", "LookBack",
		"StickerSlot1", "StickerSlot2", "StickerSlot3", "StickerSlot4",
		"PracticeSlotPrevious", "PracticeSlotNext", "PracticeSlotSave", "PracticeSlotLoad",
		"PracticeRewind", "PracticeStep",
	]
	var collected: Dictionary = controls.call("_collect_bindings")
	var collected_bindings: Dictionary = collected.get("bindings", {})
	for action_name in expected_actions:
		if !InputMap.has_action(action_name) or InputMap.action_get_events(action_name).is_empty() \
				or !collected_bindings.has(action_name):
			_fail("Options does not expose and persist action %s" % action_name)
			return
	var binding_scroll := controls.get_node("HBoxContainer/BindingScroll") as ScrollContainer
	var bindings_list := binding_scroll.get_node("VBoxContainer") as VBoxContainer
	var last_binding := bindings_list.get_node("HBoxContainer24") as HBoxContainer
	if binding_scroll.size.y > controls.size.y + 1.0 or last_binding.position.y + last_binding.size.y <= binding_scroll.size.y:
		_fail("Options binding list is not contained by a useful scroll area: scroll_height=%.1f list_bottom=%.1f tab_height=%.1f" % [binding_scroll.size.y, last_binding.position.y + last_binding.size.y, controls.size.y])
		return
	var options_panel := options.get_node("Shade/Center/Panel") as PanelContainer
	if options_panel.global_position.y < -1.0 or options_panel.global_position.y + options_panel.size.y > 721.0:
		_fail("Options panel no longer fits a 720p viewport: pos=%s size=%s" % [options_panel.global_position, options_panel.size])
		return
	print("MXT_PRACTICE_UI_LAYOUT_SMOKE_OK hud=%s editor=%s" % [hud_panel.size, editor_panel.size])
	hud.free()
	editor.free()
	options.free()
	backdrop.free()
	quit(0)


func _fail(message: String) -> void:
	push_error("MXT_PRACTICE_UI_LAYOUT_SMOKE_FAIL: " + message)
	quit(1)
