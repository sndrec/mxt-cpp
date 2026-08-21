class_name GameVersion extends RefCounted

const MAJOR := 0
const COMPATIBILITY := 2
const PATCH := 08

static func number_string() -> String:
	return "%d.%d.%d" % [MAJOR, COMPATIBILITY, PATCH]

static func display_string() -> String:
	return "MaxX Throttle v%s" % number_string()

static func metadata() -> Dictionary:
	return {
		"major": MAJOR,
		"compatibility": COMPATIBILITY,
		"patch": PATCH,
	}
