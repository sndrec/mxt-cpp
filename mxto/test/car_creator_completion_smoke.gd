extends SceneTree

const CurveGraphClass = preload("res://ui/vehicle_editor_curve_graph.gd")
const ALL_ROUNDER_PATH := "res://vehicle/asset/allrounder/blue_falcon.mxt_car_props"
const TOP_SPEEDER_PATH := "res://vehicle/asset/topspeeder/fire_stingray.mxt_car_props"

var failures: Array[String] = []


func _initialize() -> void:
	_test_derived_authoring_and_drafts()
	_test_performance_grades()
	await _test_curve_transforms()
	if failures.is_empty():
		print("MXT_CAR_CREATOR_COMPLETION_OK")
		quit(0)
		return
	for failure in failures:
		push_error(failure)
	quit(1)


func _test_derived_authoring_and_drafts() -> void:
	var session := MxtCarAuthoringSession.new()
	var derived_stat := "drive_target_speed_multiplier"
	_expect(session.is_special_derived("manual_boost", derived_stat), "new sessions should derive manual-boost target speed")
	var derived_before: Array = session.get_curve("manual_boost", derived_stat)
	var turbo_gain: Array = session.get_curve("base", "manual_turbo_gain")
	(turbo_gain[0] as Dictionary)["value"] = float((turbo_gain[0] as Dictionary)["value"]) + 0.25
	_expect(bool(session.set_curve("base", "manual_turbo_gain", turbo_gain).get("valid", false)), "base turbo-gain edit should validate")
	var derived_after: Array = session.get_curve("manual_boost", derived_stat)
	_expect(derived_after != derived_before, "derived manual-boost target speed should follow its base source")
	_expect(bool(session.make_special_custom("manual_boost", derived_stat).get("valid", false)), "one derived pair should become custom")
	var custom_curve: Array = session.get_curve("manual_boost", derived_stat)
	var weight: Array = session.get_curve("base", "weight_kg")
	(weight[0] as Dictionary)["value"] = float((weight[0] as Dictionary)["value"]) + 1.0
	_expect(bool(session.set_curve("base", "weight_kg", weight).get("valid", false)), "unrelated base edit should validate")
	_expect(session.get_curve("manual_boost", derived_stat) == custom_curve, "unrelated edits must not overwrite a custom pair")

	var draft_id := "completion_smoke_%d" % Time.get_ticks_usec()
	var store := MxtCarDraftStore.new()
	var metadata := {
		"title": "Completion Smoke",
		"author_name": "Test Runner",
		"description": "First line\nSecond line",
		"workshop_published_file_id": 0,
		"preview_livery": {},
	}
	_expect(session.set_manual_boost_volume_db(-7.5), "boost volume should accept values within [-20, 20] dB")
	_expect(!session.set_manual_boost_volume_db(20.5), "boost volume should reject values above 20 dB")
	var saved: Dictionary = store.save_draft(draft_id, session, metadata)
	_expect(bool(saved.get("valid", false)), "multiline draft metadata should save atomically: %s" % [saved.get("errors", [])])
	var loaded_session := MxtCarAuthoringSession.new()
	var loaded: Dictionary = store.load_draft(draft_id, loaded_session)
	_expect(bool(loaded.get("valid", false)), "saved draft should reload: %s" % [loaded])
	_expect(String(loaded.get("description", "")) == metadata.description, "draft reload should preserve multiline description")
	_expect(loaded_session.get_authoring_intent() == session.get_authoring_intent(), "draft reload should preserve derived/custom intent")
	_expect(is_equal_approx(loaded_session.get_manual_boost_volume_db(), -7.5), "draft reload should preserve boost volume")
	_remove_tree(ProjectSettings.globalize_path("user://vehicle_drafts/" + draft_id))


func _test_performance_grades() -> void:
	var analyzer := MxtCarPerformanceAnalyzer.new()
	var first: Dictionary = analyzer.analyze_file(ALL_ROUNDER_PATH, 0.5)
	var second: Dictionary = analyzer.analyze_file(ALL_ROUNDER_PATH, 0.5)
	_expect(bool(first.get("valid", false)), "All Rounder performance analysis should succeed")
	_expect(first == second, "performance analysis should be deterministic and cache-stable")
	_expect(is_equal_approx(float(first.get("benchmark_machine_setting", -1.0)), 0.5), "performance grades should use a fixed 50% center")
	_expect(String(first.get("benchmark_reference", "")).contains("0%, 50%, and 100%"), "performance grades should disclose their fixed official extrema")
	var categories: Array = first.get("categories", [])
	_expect(categories.size() == 7, "performance analysis should expose exactly seven headline categories")
	for category_value in categories:
		var category: Dictionary = category_value
		_expect(String(category.get("grade", "")) == "C", "All Rounder should anchor %s at C" % String(category.get("name", "category")))
		for component_value in category.get("components", []):
			var component: Dictionary = component_value
			_expect(not String(component.get("explanation", "")).is_empty(), "%s benchmark should explain what it measures" % String(component.get("name", "benchmark")))
	var cornering: Dictionary = categories[2]
	var cornering_components: Array = cornering.get("components", [])
	_expect(String((cornering_components[0] as Dictionary).get("name", "")) == "Normal Steering", "normal steering benchmark should use a player-facing name")
	_expect(String((cornering_components[0] as Dictionary).get("unit", "")) == "degrees/second", "steering benchmark should use degrees per second")
	_expect(String((cornering_components[2] as Dictionary).get("name", "")) == "Drift Steering", "drift benchmark should not substitute Turbo Slide handling")
	_expect(String((cornering_components[2] as Dictionary).get("unit", "")) == "degrees/second", "drift steering should use degrees per second")
	var acceleration_components: Array = (categories[1] as Dictionary).get("components", [])
	_expect(String((acceleration_components[2] as Dictionary).get("unit", "")) == "meters", "first-five-second benchmark should be displayed as distance")
	var body_components: Array = (categories[5] as Dictionary).get("components", [])
	_expect(body_components.size() == 2, "Body should contain only max energy and damage resistance")
	_expect(String((body_components[0] as Dictionary).get("name", "")) == "Max Energy", "Body should expose base max energy directly")
	_expect(String((body_components[1] as Dictionary).get("name", "")) == "Damage Resistance", "Body should expose reciprocal damage resistance")

	var all_rounder_zero: Dictionary = analyzer.analyze_file(ALL_ROUNDER_PATH, 0.0)
	var zero_has_non_c := false
	for category_value in all_rounder_zero.get("categories", []):
		zero_has_non_c = zero_has_non_c or String((category_value as Dictionary).get("grade", "")) != "C"
	_expect(zero_has_non_c, "changing machine setting should move grades against the fixed 50% calibration")

	var top_speeder: Dictionary = analyzer.analyze_file(TOP_SPEEDER_PATH, 1.0)
	var top_booster: Array = ((top_speeder.get("categories", []) as Array)[4] as Dictionary).get("components", [])
	var top_terminal := float(top_speeder.get("terminal_speed_kmh", 0.0))
	_expect(float((top_booster[0] as Dictionary).get("value", 0.0)) > top_terminal, "one-second boost speed should be absolute speed above settled top speed")
	_expect(float((top_booster[1] as Dictionary).get("value", 0.0)) >= float((top_booster[0] as Dictionary).get("value", 0.0)), "peak boost speed should be an absolute peak reached after boosting from settled speed")
	for stat_value in first.get("advanced_stats", []):
		var stat_data: Dictionary = stat_value
		if String(stat_data.get("name", "")) == "drag":
			_expect(String(stat_data.get("friendly_name", "")) == "Drag", "raw drag should not be mislabeled as Rolling Drag")
			_expect(String(stat_data.get("explanation", "")).contains("air resistance"), "Drag help should distinguish its constant loss from automatic air resistance")

	var grip_session := MxtCarAuthoringSession.new()
	var grip_before: Dictionary = analyzer.analyze_session(grip_session, 0.5)
	var press_grip_curve: Array = grip_session.get_curve("base", "accel_press_grip_frames")
	for key_value in press_grip_curve:
		(key_value as Dictionary)["value"] = 250.0
	_expect(bool(grip_session.set_curve("base", "accel_press_grip_frames", press_grip_curve).get("valid", false)), "accelerator grip curve should validate")
	var grip_after: Dictionary = analyzer.analyze_session(grip_session, 0.5)
	var grip_before_components: Array = (((grip_before.get("categories", []) as Array)[3]) as Dictionary).get("components", [])
	var grip_after_components: Array = (((grip_after.get("categories", []) as Array)[3]) as Dictionary).get("components", [])
	for component_index in grip_before_components.size():
		_expect(is_equal_approx(
			float((grip_before_components[component_index] as Dictionary).get("value", 0.0)),
			float((grip_after_components[component_index] as Dictionary).get("value", 0.0))),
			"accelerator-induced grip must not affect Grip benchmark component %d" % component_index)

	for trait_value in first.get("traits", []):
		var trait_data: Dictionary = trait_value
		var shared_turbo_loss := String(trait_data.get("stat_name", "")) == "turbo_flat_loss_per_second" \
				and String(trait_data.get("context", "")) in [
					"While Manual Boosting", "While Dashplate Boosting", "While Stacking Boosts"]
		_expect(not shared_turbo_loss, "roster-baseline turbo loss must not appear as an All Rounder trait")

	var custom := MxtCarAuthoringSession.new()
	var stat_name := "turbo_flat_loss_per_second"
	_expect(bool(custom.make_special_custom("manual_boost", stat_name).get("valid", false)), "manual-boost turbo loss should become custom")
	var custom_curve: Array = custom.get_curve("manual_boost", stat_name)
	for key_value in custom_curve:
		(key_value as Dictionary)["value"] = 0.25
	_expect(bool(custom.set_curve("manual_boost", stat_name, custom_curve).get("valid", false)), "custom turbo-loss curve should validate")
	var custom_analysis: Dictionary = analyzer.analyze_session(custom, 0.5)
	var found_baseline_trait := false
	for trait_value in custom_analysis.get("traits", []):
		var trait_data: Dictionary = trait_value
		if String(trait_data.get("stat_name", "")) == stat_name \
				and String(trait_data.get("context", "")) == "While Manual Boosting":
			found_baseline_trait = true
			_expect(bool(trait_data.get("uses_roster_baseline", false)), "custom turbo-loss trait should identify its roster baseline")
			_expect(is_equal_approx(float(trait_data.get("baseline_adjustment", 0.0)), 0.5), "custom turbo-loss trait baseline should be 0.5x")
			_expect(is_equal_approx(float(trait_data.get("percent", 0.0)), -50.0), "custom turbo-loss trait should be -50% relative to the 0.5x baseline")
	_expect(found_baseline_trait, "a custom turbo-loss adjustment should appear when it differs from the roster baseline")

	var drive_target := "drive_target_speed_multiplier"
	_expect(bool(custom.make_special_custom("manual_boost", drive_target).get("valid", false)), "manual-boost drive target should become custom")
	var drive_curve: Array = custom.get_curve("manual_boost", drive_target)
	for key_value in drive_curve:
		(key_value as Dictionary)["value"] = 5.0
	_expect(bool(custom.set_curve("manual_boost", drive_target, drive_curve).get("valid", false)), "custom drive-target curve should validate")
	_expect(bool(custom.make_special_custom("s_boost", "acceleration").get("valid", false)), "S-Boost acceleration should become custom")
	_expect(custom.set_s_boost_value("acceleration", 5.0), "custom S-Boost acceleration should validate")
	_expect(bool(custom.make_special_custom("no_boost", "boost_energy_use_rate").get("valid", false)), "no-boost energy use should become custom")
	var irrelevant_energy_curve: Array = custom.get_curve("no_boost", "boost_energy_use_rate")
	for key_value in irrelevant_energy_curve:
		(key_value as Dictionary)["value"] = 5.0
	_expect(bool(custom.set_curve("no_boost", "boost_energy_use_rate", irrelevant_energy_curve).get("valid", false)), "custom no-boost energy-use curve should validate")
	_expect(bool(custom.make_special_custom("mts", "acceleration").get("valid", false)), "Turbo Slide acceleration should become custom")
	var mts_acceleration_curve: Array = custom.get_curve("mts", "acceleration")
	for key_value in mts_acceleration_curve:
		(key_value as Dictionary)["value"] = 0.4
	_expect(bool(custom.set_curve("mts", "acceleration", mts_acceleration_curve).get("valid", false)), "custom Turbo Slide acceleration should validate")
	var filtered_analysis: Dictionary = analyzer.analyze_session(custom, 0.5)
	var found_mts_acceleration := false
	for trait_value in filtered_analysis.get("traits", []):
		var trait_data: Dictionary = trait_value
		_expect(String(trait_data.get("context", "")) != "During S-Boost", "S-Boost overrides must not appear as traits")
		_expect(String(trait_data.get("stat_name", "")) != drive_target, "Drive Target Speed must not appear as a trait")
		var impossible_energy_use := String(trait_data.get("stat_name", "")) == "boost_energy_use_rate" \
				and String(trait_data.get("context", "")) == "Without Boost"
		_expect(not impossible_energy_use, "boost energy use without an active manual boost must not appear as a trait")
		if String(trait_data.get("stat_name", "")) == "acceleration" \
				and String(trait_data.get("context", "")) == "While Turbo Sliding":
			found_mts_acceleration = true
			_expect(String(trait_data.get("kind", "")) == "strength", "lower Turbo Slide acceleration should be classified as an advantage")
			_expect(String(trait_data.get("explanation", "")).contains("pull back down"), "Turbo Slide acceleration should explain its inverted interpretation")
	_expect(found_mts_acceleration, "custom Turbo Slide acceleration should produce a contextual trait")


func _test_curve_transforms() -> void:
	var graph := CurveGraphClass.new()
	root.add_child(graph)
	graph.size = Vector2(640.0, 320.0)
	graph.set_keys([
		{"time": 0.2, "value": 1.0, "tangent_in": 0.0, "tangent_out": 0.0},
		{"time": 0.5, "value": 2.0, "tangent_in": 0.1, "tangent_out": 0.2},
		{"time": 0.8, "value": 3.0, "tangent_in": 0.3, "tangent_out": 0.4},
	])
	await process_frame
	var original := graph.get_keys()
	var first_id := int((graph.keys[0] as Dictionary)["_editor_id"])
	graph.selected_ids = {first_id: true}
	graph.active_id = first_id
	graph.last_mouse_position = Vector2(160.0, 160.0)
	graph.call("_begin_keyboard_transform", 1)
	graph.call("_apply_grab", graph.interaction_mouse_start + Vector2(320.0, 35.0))
	graph.call("_cancel_local_interaction", false)
	_expect(graph.get_keys() == original, "cancelling a curve transform should restore exact keys and tangents")

	graph.selected_ids = {first_id: true}
	graph.active_id = first_id
	graph.last_mouse_position = Vector2(160.0, 160.0)
	graph.call("_begin_keyboard_transform", 1)
	graph.call("_apply_grab", graph.interaction_mouse_start + Vector2(430.0, 0.0))
	graph.call("_preview_interaction")
	graph.call("_commit_interaction")
	var moved := graph.get_keys()
	_expect(float((moved[0] as Dictionary)["time"]) == 0.5 and float((moved[1] as Dictionary)["time"]) == 0.8, "crossing a selected key should preserve unselected key positions")
	_expect(float((moved[2] as Dictionary)["value"]) == 1.0, "crossing a key should stably reorder the selected key")
	var old_span: float = graph.y_max - graph.y_min
	graph.call("_zoom_y", Vector2(320.0, 160.0), 0.5)
	_expect(graph.y_max - graph.y_min < old_span, "curve Y zoom should change only the visible Y span")
	graph.queue_free()
	await process_frame


func _expect(condition: bool, message: String) -> void:
	if !condition:
		failures.append(message)


func _remove_tree(path: String) -> void:
	var directory := DirAccess.open(path)
	if directory == null:
		return
	directory.list_dir_begin()
	var name := directory.get_next()
	while !name.is_empty():
		var child := path.path_join(name)
		if directory.current_is_dir():
			_remove_tree(child)
		else:
			DirAccess.remove_absolute(child)
		name = directory.get_next()
	directory.list_dir_end()
	DirAccess.remove_absolute(path)
