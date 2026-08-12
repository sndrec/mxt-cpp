extends SceneTree

const ROOT_NAME := "mxt_content_validation_smoke"

var failures: Array[String] = []


func _initialize() -> void:
	var root := ProjectSettings.globalize_path("user://" + ROOT_NAME)
	_prepare_track_package(root, 2000.0)
	var validator := MxtContentValidator.new()
	var first: Dictionary = validator.validate_package_directory(root)
	_expect(bool(first.get("valid", false)), "representative track package should validate: %s" % [first.get("errors", [])])
	if bool(first.get("valid", false)):
		var second: Dictionary = validator.validate_package_directory(root)
		_expect(first["package_digest"] == second.get("package_digest", ""), "package digest must be deterministic")
		_expect(first["gameplay_digest"] == second.get("gameplay_digest", ""), "gameplay digest must be deterministic")
		_expect(String(first["package_digest"]).begins_with("sha256:"), "package digest must be domain-labelled SHA-256")
		_expect(String(first["gameplay_digest"]).begins_with("sha256:"), "gameplay digest must be domain-labelled SHA-256")
		_prepare_track_package(root, 2500.0)
		var presentation_edit: Dictionary = validator.validate_package_directory(root)
		_expect(bool(presentation_edit.get("valid", false)), "presentation-only edit should remain valid")
		_expect(first["gameplay_digest"] == presentation_edit.get("gameplay_digest", ""), "presentation-only edits must not change gameplay identity")
		_expect(first["package_digest"] != presentation_edit.get("package_digest", ""), "presentation-only edits must change package identity")

	var vehicle_root := ProjectSettings.globalize_path("user://" + ROOT_NAME + "_vehicle")
	_prepare_vehicle_package(vehicle_root)
	var vehicle_result: Dictionary = validator.validate_package_directory(vehicle_root)
	_expect(bool(vehicle_result.get("valid", false)), "representative vehicle package should validate: %s" % [vehicle_result.get("errors", [])])

	var extra_path := root.path_join("undeclared.txt")
	var extra := FileAccess.open(extra_path, FileAccess.WRITE)
	extra.store_string("not declared")
	extra.close()
	var with_extra: Dictionary = validator.validate_package_directory(root)
	_expect(!bool(with_extra.get("valid", true)), "undeclared files must be rejected")
	DirAccess.remove_absolute(extra_path)
	var changed_preview := FileAccess.open(root.path_join("preview.png"), FileAccess.READ_WRITE)
	changed_preview.seek_end()
	changed_preview.store_8(0)
	changed_preview.close()
	var hash_mismatch: Dictionary = validator.validate_package_directory(root)
	_expect(!bool(hash_mismatch.get("valid", true)), "payload hash mismatches must be rejected")

	var duplicate_manifest := (
		'{"format_revision":1,"format_revision":1,"content_type":"vehicle",' +
		'"title":"Duplicate","description":"","author_name":"Tester",' +
		'"payload":{"model":"vehicle/model.glb","properties":"vehicle/properties.mxt_car_props"},' +
		'"payload_sha256":{"vehicle/model.glb":"%s","vehicle/properties.mxt_car_props":"%s","preview.png":"%s"}}'
	) % ["0".repeat(64), "0".repeat(64), "0".repeat(64)]
	var duplicate_result: Dictionary = validator.validate_manifest_bytes(duplicate_manifest.to_utf8_buffer())
	_expect(!bool(duplicate_result.get("valid", true)), "duplicate JSON members must be rejected")

	if failures.is_empty():
		print("MXT_CONTENT_PACKAGE_VALIDATION_OK")
		quit(0)
		return
	for failure in failures:
		push_error(failure)
	quit(1)


func _prepare_track_package(root: String, fog_distance: float) -> void:
	DirAccess.make_dir_recursive_absolute(root.path_join("track"))
	_copy(
		ProjectSettings.globalize_path("res://../tmp/fzgx_drift_highway_latest/fzgx_course14.mxt_track"),
		root.path_join("track/track.mxt_track")
	)
	_copy(ProjectSettings.globalize_path("res://asset/test_track_2.glb"), root.path_join("track/visual.glb"))
	_copy(ProjectSettings.globalize_path("res://asset/CAUTION.png"), root.path_join("preview.png"))
	var metadata := FileAccess.open(root.path_join("track/metadata.json"), FileAccess.WRITE)
	metadata.store_string(JSON.stringify({
		"difficulty": 1,
		"fog_distance": fog_distance,
		"sky_top_color": [0.1, 0.1, 0.2],
		"sky_horizon_color": [0.5, 0.5, 0.6],
		"sky_ground_color": [0.05, 0.05, 0.1],
		"ground_color": [0.1, 0.1, 0.1],
		"ground_height": -500.0,
		"cloud_color": [0.8, 0.8, 0.8],
		"cloud_height": 800.0,
		"light_color": [1.0, 0.95, 0.9],
		"light_intensity": 1.0,
		"ambient_intensity": 0.1,
		"ambient_color": [0.15, 0.15, 0.18],
		"light_direction": [0.3, -1.0, 0.4],
	}, "  ", true))
	metadata.close()
	var hashes := {
		"track/track.mxt_track": FileAccess.get_sha256(root.path_join("track/track.mxt_track")),
		"track/visual.glb": FileAccess.get_sha256(root.path_join("track/visual.glb")),
		"track/metadata.json": FileAccess.get_sha256(root.path_join("track/metadata.json")),
		"preview.png": FileAccess.get_sha256(root.path_join("preview.png")),
	}
	var manifest := {
		"format_revision": 1,
		"content_type": "track",
		"title": "Azure Validation Fixture",
		"description": "",
		"author_name": "MaxX Throttle",
		"payload": {
			"track": "track/track.mxt_track",
			"visual": "track/visual.glb",
			"metadata": "track/metadata.json",
		},
		"payload_sha256": hashes,
	}
	var file := FileAccess.open(root.path_join("manifest.json"), FileAccess.WRITE)
	file.store_string(JSON.stringify(manifest, "  ", true))
	file.close()


func _prepare_vehicle_package(root: String) -> void:
	DirAccess.make_dir_recursive_absolute(root.path_join("vehicle"))
	_copy(ProjectSettings.globalize_path("res://asset/test_track_2.glb"), root.path_join("vehicle/model.glb"))
	_copy(
		ProjectSettings.globalize_path("res://vehicle/asset/allrounder/blue_falcon.mxt_car_props"),
		root.path_join("vehicle/properties.mxt_car_props")
	)
	_copy(ProjectSettings.globalize_path("res://asset/CAUTION.png"), root.path_join("preview.png"))
	var manifest := {
		"format_revision": 1,
		"content_type": "vehicle",
		"title": "Vehicle Validation Fixture",
		"description": "",
		"author_name": "MaxX Throttle",
		"payload": {
			"model": "vehicle/model.glb",
			"properties": "vehicle/properties.mxt_car_props",
		},
		"payload_sha256": {
			"vehicle/model.glb": FileAccess.get_sha256(root.path_join("vehicle/model.glb")),
			"vehicle/properties.mxt_car_props": FileAccess.get_sha256(root.path_join("vehicle/properties.mxt_car_props")),
			"preview.png": FileAccess.get_sha256(root.path_join("preview.png")),
		},
	}
	var file := FileAccess.open(root.path_join("manifest.json"), FileAccess.WRITE)
	file.store_string(JSON.stringify(manifest, "  ", true))
	file.close()


func _copy(source: String, destination: String) -> void:
	var error := DirAccess.copy_absolute(source, destination)
	_expect(error == OK, "copy failed (%d): %s -> %s" % [error, source, destination])


func _expect(condition: bool, message: String) -> void:
	if !condition:
		failures.append(message)
