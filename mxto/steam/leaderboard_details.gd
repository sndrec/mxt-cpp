extends RefCounted

const MAGIC := 0x3154584d
const FORMAT_REVISION := 3
const PREVIOUS_FORMAT_REVISION := 2
const PREVIOUS_WORD_COUNT := 29
const WORD_COUNT := 30


static func decode(details_value) -> Dictionary:
	if typeof(details_value) != TYPE_ARRAY:
		return {}
	var values: Array = details_value
	if values.size() < PREVIOUS_WORD_COUNT or (int(values[0]) & 0xffffffff) != MAGIC:
		return {}
	var format_revision := int(values[1])
	if format_revision != FORMAT_REVISION and format_revision != PREVIOUS_FORMAT_REVISION:
		return {}
	if format_revision == FORMAT_REVISION and values.size() < WORD_COUNT:
		return {}
	var replay_sha256 := _words_to_digest(values, 5)
	var machine_setting_percent := -1
	if format_revision == FORMAT_REVISION:
		machine_setting_percent = int(values[29])
		if machine_setting_percent < 0 or machine_setting_percent > 100:
			return {}
	var packed_version := int(values[4]) & 0xffffffff
	return {
		"format_revision": format_revision,
		"game_version": {
			"major": (packed_version >> 24) & 0xff,
			"compatibility": (packed_version >> 16) & 0xff,
			"patch": packed_version & 0xffff,
		},
		"ruleset_revision": int(values[2]),
		"replay_schema_version": int(values[3]),
		"replay_sha256": replay_sha256,
		"track_gameplay_digest": _words_to_digest(values, 13),
		"vehicle_gameplay_digest": _words_to_digest(values, 21),
		"machine_setting_percent": machine_setting_percent,
	}


static func _words_to_digest(values: Array, start: int) -> String:
	var chunks: Array[String] = []
	for index in range(start, start + 8):
		chunks.append("%08x" % (int(values[index]) & 0xffffffff))
	return "sha256:" + "".join(chunks)
