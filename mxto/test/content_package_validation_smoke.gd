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
	_remove_tree(vehicle_root)
	_prepare_vehicle_package(vehicle_root)
	var vehicle_result: Dictionary = validator.validate_package_directory(vehicle_root)
	_expect(bool(vehicle_result.get("valid", false)), "representative vehicle package should validate: %s" % [vehicle_result.get("errors", [])])
	_add_vehicle_texture_payloads(vehicle_root)
	var textured_vehicle_result: Dictionary = validator.validate_package_directory(vehicle_root)
	_expect(bool(textured_vehicle_result.get("valid", false)), "vehicle package with standalone material PNGs should validate: %s" % [textured_vehicle_result.get("errors", [])])

	var package_io := MxtContentPackageIO.new()
	var archive_path := ProjectSettings.globalize_path("user://" + ROOT_NAME + ".mxtpkg")
	var export_result: Dictionary = package_io.export_mxtpkg(root, archive_path)
	_expect(bool(export_result.get("valid", false)), "validated directory should export as .mxtpkg: %s" % [export_result.get("errors", [])])
	var inspect_result: Dictionary = package_io.inspect_mxtpkg(archive_path)
	_expect(bool(inspect_result.get("valid", false)), "exported .mxtpkg should pass archive preflight: %s" % [inspect_result.get("errors", [])])
	var library_root := ProjectSettings.globalize_path("user://" + ROOT_NAME + "_library")
	var import_result: Dictionary = package_io.import_mxtpkg(archive_path, library_root)
	_expect(bool(import_result.get("valid", false)), "exported .mxtpkg should import: %s" % [import_result.get("errors", [])])
	if bool(import_result.get("valid", false)):
		_expect(import_result.get("package_digest", "") == export_result.get("package_digest", ""), "archive round trip must preserve package digest")
		var installed: Dictionary = validator.validate_package_directory(import_result["package_path"])
		_expect(bool(installed.get("valid", false)), "installed package should pass the directory validator")
		var repeated_import: Dictionary = package_io.import_mxtpkg(archive_path, library_root)
		_expect(bool(repeated_import.get("valid", false)) and bool(repeated_import.get("reused_existing", false)), "reimport should reuse the content-addressed installation")
		var catalog := MxtContentCatalog.new()
		var scan_result: Dictionary = catalog.scan_local_library(library_root)
		_expect(bool(scan_result.get("valid", false)), "content-addressed local library should scan without diagnostics")
		var local_id := "mxt:track:package:" + String(import_result["package_digest"]).trim_prefix("sha256:")
		var local_record: Dictionary = catalog.resolve_content(local_id)
		_expect(!local_record.is_empty(), "local package must resolve by stable content ID")
		_expect(local_record.get("gameplay_digest", "") == import_result.get("gameplay_digest", ""), "catalog record must preserve gameplay identity")
		_expect(catalog.find_gameplay("track", import_result["gameplay_digest"]).size() >= 1, "catalog must resolve exact gameplay digests")
		var workshop_add: Dictionary = catalog.add_workshop_package(import_result["package_path"], 123456)
		_expect(bool(workshop_add.get("valid", false)), "validated package should register as a Workshop record")
		var workshop_record: Dictionary = catalog.resolve_content("mxt:track:workshop:123456")
		_expect(workshop_record.get("package_digest", "") == import_result.get("package_digest", ""), "Workshop stable ID must retain its exact package digest")

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
	var changed_archive := FileAccess.open(archive_path, FileAccess.READ_WRITE)
	if changed_archive != null:
		changed_archive.seek_end()
		changed_archive.store_8(0)
		changed_archive.close()
		var trailing_archive_data: Dictionary = package_io.inspect_mxtpkg(archive_path)
		_expect(!bool(trailing_archive_data.get("valid", true)), "archives with hidden trailing data must be rejected")

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
	var authoring_session := MxtCarAuthoringSession.new()
	var loaded: Dictionary = authoring_session.load_file("res://vehicle/asset/allrounder/blue_falcon.mxt_car_props")
	_expect(bool(loaded.get("valid", false)), "official vehicle properties must load")
	var intent: Dictionary = authoring_session.get_authoring_intent()
	var applied: Dictionary = authoring_session.set_authoring_intent(intent)
	_expect(bool(applied.get("valid", false)), "inferred vehicle authoring intent must materialize")
	var saved: Dictionary = authoring_session.save_file(root.path_join("vehicle/properties.mxt_car_props"))
	_expect(bool(saved.get("valid", false)), "vehicle properties fixture must save")
	_copy(ProjectSettings.globalize_path("res://asset/CAUTION.png"), root.path_join("preview.png"))
	var visual := FileAccess.open(root.path_join("vehicle/visual.json"), FileAccess.WRITE)
	visual.store_string(JSON.stringify({
		"format_revision": 1,
		"model_transform": {
			"translation": [0.0, 0.0, 0.0],
			"rotation_degrees": [0.0, 0.0, 0.0],
			"scale": [1.0, 1.0, 1.0],
		},
		"body_surfaces": [0],
		"material_inputs": {
			"albedo_surface": -1,
			"normal_surface": -1,
			"paint_mask_surface": -1,
		},
		"thrusters": [],
	}, "  ", true))
	visual.close()
	var authoring := FileAccess.open(root.path_join("vehicle/authoring.json"), FileAccess.WRITE)
	authoring.store_string(JSON.stringify(intent, "  ", true))
	authoring.close()
	var manifest := {
		"format_revision": 1,
		"content_type": "vehicle",
		"title": "Vehicle Validation Fixture",
		"description": "",
		"author_name": "MaxX Throttle",
		"payload": {
			"model": "vehicle/model.glb",
			"properties": "vehicle/properties.mxt_car_props",
			"visual_metadata": "vehicle/visual.json",
			"authoring": "vehicle/authoring.json",
		},
		"payload_sha256": {
			"vehicle/model.glb": FileAccess.get_sha256(root.path_join("vehicle/model.glb")),
			"vehicle/properties.mxt_car_props": FileAccess.get_sha256(root.path_join("vehicle/properties.mxt_car_props")),
			"vehicle/visual.json": FileAccess.get_sha256(root.path_join("vehicle/visual.json")),
			"vehicle/authoring.json": FileAccess.get_sha256(root.path_join("vehicle/authoring.json")),
			"preview.png": FileAccess.get_sha256(root.path_join("preview.png")),
		},
	}
	var file := FileAccess.open(root.path_join("manifest.json"), FileAccess.WRITE)
	file.store_string(JSON.stringify(manifest, "  ", true))
	file.close()


func _add_vehicle_texture_payloads(root: String) -> void:
	var source := ProjectSettings.globalize_path("res://asset/CAUTION.png")
	for name in ["albedo.png", "normal.png", "paint_mask.png"]:
		_copy(source, root.path_join("vehicle/" + name))
	var manifest: Dictionary = JSON.parse_string(FileAccess.get_file_as_string(root.path_join("manifest.json")))
	var payload: Dictionary = manifest["payload"]
	payload["albedo_texture"] = "vehicle/albedo.png"
	payload["normal_texture"] = "vehicle/normal.png"
	payload["paint_mask_texture"] = "vehicle/paint_mask.png"
	var hashes: Dictionary = manifest["payload_sha256"]
	for name in ["albedo.png", "normal.png", "paint_mask.png"]:
		hashes["vehicle/" + name] = FileAccess.get_sha256(root.path_join("vehicle/" + name))
	var visual_path := root.path_join("vehicle/visual.json")
	var visual: Dictionary = JSON.parse_string(FileAccess.get_file_as_string(visual_path))
	(visual["material_inputs"] as Dictionary)["use_mesh_normals"] = true
	var visual_file := FileAccess.open(visual_path, FileAccess.WRITE)
	visual_file.store_string(JSON.stringify(visual, "  ", true))
	visual_file.close()
	hashes["vehicle/visual.json"] = FileAccess.get_sha256(visual_path)
	var manifest_file := FileAccess.open(root.path_join("manifest.json"), FileAccess.WRITE)
	manifest_file.store_string(JSON.stringify(manifest, "  ", true))
	manifest_file.close()


func _copy(source: String, destination: String) -> void:
	var error := DirAccess.copy_absolute(source, destination)
	_expect(error == OK, "copy failed (%d): %s -> %s" % [error, source, destination])


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
