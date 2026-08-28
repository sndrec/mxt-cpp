extends SceneTree

const RULESET_REVISION := 2

func _argument(args: PackedStringArray, name: String) -> String:
	var index := args.find(name)
	if index < 0 or index + 1 >= args.size():
		return ""
	return args[index + 1]

func _fail(message: String) -> void:
	printerr("MXT_CURATE_TRACK_FAIL ", message)
	quit(1)

func _initialize() -> void:
	var args := OS.get_cmdline_user_args()
	var package_path := _argument(args, "--package")
	var workshop_id_text := _argument(args, "--workshop-id")
	var slug := _argument(args, "--slug").to_lower()
	var output_path := _argument(args, "--output")
	if package_path.is_empty() or workshop_id_text.is_empty() or slug.is_empty() or output_path.is_empty():
		_fail("required arguments: --package, --workshop-id, --slug, --output")
		return
	var slug_pattern := RegEx.new()
	if slug_pattern.compile("^[a-z0-9][a-z0-9-]{0,47}$") != OK or slug_pattern.search(slug) == null:
		_fail("slug must contain only lowercase letters, digits, and hyphens")
		return
	var workshop_id := int(workshop_id_text)
	if workshop_id <= 0:
		_fail("Workshop ID must be a positive integer")
		return
	var catalog := MxtContentCatalog.new()
	var package_result: Dictionary = catalog.add_workshop_package(package_path, workshop_id)
	if !bool(package_result.get("valid", false)):
		_fail("package validation failed: %s" % str(package_result.get("errors", [])))
		return
	var record := package_result.get("record") as MxtContentRecord
	if record == null or record.content_type != MxtContentRecord.CONTENT_TRACK \
			or record.source != MxtContentRecord.SOURCE_WORKSHOP:
		_fail("validated package is not a Workshop track")
		return
	var gameplay_digest := record.gameplay_digest
	var digest_hex := gameplay_digest.trim_prefix("sha256:")
	if digest_hex.length() != 64:
		_fail("validated package returned an invalid gameplay digest")
		return
	var manifest_value = JSON.parse_string(FileAccess.get_file_as_string("res://steam/leaderboards.json"))
	if typeof(manifest_value) != TYPE_DICTIONARY:
		_fail("checked-in leaderboard manifest could not be parsed")
		return
	var manifest: Dictionary = (manifest_value as Dictionary).duplicate(true)
	if int(manifest.get("format_revision", -1)) != 1 \
		or int(manifest.get("time_attack_ruleset_revision", -1)) != RULESET_REVISION \
		or typeof(manifest.get("boards", [])) != TYPE_ARRAY:
		_fail("checked-in leaderboard manifest is incompatible")
		return
	var boards: Array = manifest.get("boards", [])
	for value in boards:
		if typeof(value) != TYPE_DICTIONARY:
			continue
		var existing: Dictionary = value
		if String(existing.get("track_gameplay_digest", "")) == gameplay_digest:
			_fail("this exact gameplay digest is already curated")
			return
	var board_name := "mxt_ta_curated_%s_%s_r%d" % [slug, digest_hex.left(8), RULESET_REVISION]
	boards.append({
		"track_slug": slug,
		"track_title": record.title if !record.title.is_empty() else slug.replace("-", " ").capitalize(),
		"track_source": "curated_workshop",
		"published_file_id": workshop_id_text,
		"track_gameplay_digest": gameplay_digest,
		"steam_name": board_name,
	})
	boards.sort_custom(func(a: Dictionary, b: Dictionary):
		var a_source := String(a.get("track_source", "official"))
		var b_source := String(b.get("track_source", "official"))
		if a_source != b_source:
			return a_source == "official"
		return String(a.get("track_slug", "")) < String(b.get("track_slug", ""))
	)
	manifest["boards"] = boards
	var output := FileAccess.open(output_path, FileAccess.WRITE)
	if output == null:
		_fail("could not open output manifest: %s" % error_string(FileAccess.get_open_error()))
		return
	output.store_string(JSON.stringify(manifest, "  ") + "\n")
	output.close()
	print("MXT_CURATE_TRACK_OK ", JSON.stringify({
		"output": output_path,
		"steam_name": board_name,
		"published_file_id": workshop_id_text,
		"track_gameplay_digest": gameplay_digest,
		"package_digest": record.package_digest,
	}))
	quit()
