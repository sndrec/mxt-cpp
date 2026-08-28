extends SceneTree

func _init() -> void:
	var source := MxtRaceConfiguration.new()
	source.session_kind = MxtRaceConfiguration.SESSION_PRACTICE
	source.game_mode = 1
	source.cpu_count = 999
	source.lap_count = 0
	source.time_attack_ruleset_revision = 42
	source.practice_local_player_id = 0x123456789
	source.vehicle_restore = false
	source.bumpers = true
	source.s_boost = false
	source.allow_workshop_vehicles = false
	source.boost_unlocked_from_start = true
	source.leaderboard_eligible = true
	source.resumed_from_replay = true
	source.custom_content = true
	source.leaderboard_ineligible_reason = "wire smoke"
	source.cpu_vehicle_content_ids = PackedStringArray(["official:all-rounder", "workshop:123"])

	var decoded := MxtRaceConfiguration.new()
	if !decoded.decode_wire(source.encode_wire()) \
			or decoded.session_kind != source.session_kind \
			or decoded.game_mode != source.game_mode \
			or decoded.cpu_count != source.cpu_count \
			or decoded.lap_count != source.lap_count \
			or decoded.time_attack_ruleset_revision != source.time_attack_ruleset_revision \
			or decoded.practice_local_player_id != source.practice_local_player_id \
			or decoded.vehicle_restore != source.vehicle_restore \
			or decoded.bumpers != source.bumpers \
			or decoded.s_boost != source.s_boost \
			or decoded.allow_workshop_vehicles != source.allow_workshop_vehicles \
			or decoded.boost_unlocked_from_start != source.boost_unlocked_from_start \
			or decoded.leaderboard_eligible != source.leaderboard_eligible \
			or decoded.resumed_from_replay != source.resumed_from_replay \
			or decoded.custom_content != source.custom_content \
			or decoded.leaderboard_ineligible_reason != source.leaderboard_ineligible_reason \
			or decoded.cpu_vehicle_content_ids != source.cpu_vehicle_content_ids:
		push_error("MXT_RACE_CONFIGURATION_WIRE_SMOKE_FAIL")
		quit(1)
		return
	var track_source := MxtTrackContentEvidence.new()
	track_source.append("official:track-a", "sha256:gameplay", "sha256:package", "")
	track_source.append("workshop:track-b", "sha256:gameplay-b", "sha256:package-b", "123456")
	var track_decoded := MxtTrackContentEvidence.new()
	if !track_decoded.decode_wire(track_source.encode_wire()) \
			or track_decoded.count() != 2 \
			or track_decoded.get_content_id(0) != "official:track-a" \
			or track_decoded.get_workshop_id(1) != "123456":
		push_error("MXT_TRACK_CONTENT_EVIDENCE_WIRE_SMOKE_FAIL")
		quit(1)
		return
	var roster_source := MxtRaceRoster.new()
	var roster_settings := {
		"username": "Wire Racer",
		"vehicle_content_id": "mxt:vehicle:workshop:123",
		"vehicle_gameplay_digest": "sha256:gameplay",
		"vehicle_package_digest": "sha256:package",
		"vehicle_workshop_id": "123",
		"accel_setting": 0.375,
		"spectator": false,
		"sticker_1": 7,
		"sticker_2": 8,
		"sticker_3": 9,
		"sticker_4": 10,
		"car_livery": {
			"version": 1,
			"vehicle_content_id": "mxt:vehicle:workshop:123",
			"primary_colour": "ff0000ff",
			"secondary_colour": "00ff00ff",
			"accent_colour": "0000ffff",
			"outline_colour": "ffffffff",
			"trail_colour": "808080ff",
			"outline_colour_customized": true,
			"trail_colour_customized": true,
			"stamps": [{
				"stamp_id": "smoke",
				"source": "custom",
				"hash": "sha256:stamp",
				"palette_id": 2,
				"rect": [1.0, 2.0, 3.0, 4.0],
				"rect_rotated": true,
				"enabled": true,
				"layer": 5,
				"local_origin": [6.0, 7.0, 8.0],
				"local_basis": [[1.0, 0.0, 0.0], [0.0, 1.0, 0.0], [0.0, 0.0, 1.0]],
				"rotation": 0.25,
				"size": [0.5, 0.75],
				"flip_horizontal": true,
				"flip_vertical": false,
				"mirror_local_x": true,
				"projection_depth": 0.4,
				"colour": "ff8040ff",
				"opacity": 0.625,
			}],
		},
	}
	if !roster_source.append_settings(123456789, 123456789, true, false, false, roster_settings):
		push_error("MXT_RACE_ROSTER_BUILD_FAIL: " + roster_source.get_last_error())
		quit(1)
		return
	var roster_decoded := MxtRaceRoster.new()
	if !roster_decoded.decode_wire(roster_source.encode_wire()) \
			or roster_decoded.count() != 1 \
			or roster_decoded.get_player_id(0) != 123456789 \
			or !roster_decoded.is_cpu(0):
		push_error("MXT_RACE_ROSTER_WIRE_SMOKE_FAIL: " + roster_decoded.get_last_error())
		quit(1)
		return
	var roster_roundtrip := roster_decoded.get_settings_dictionary(0)
	var roundtrip_livery: Dictionary = roster_roundtrip.get("car_livery", {})
	var roundtrip_stamps: Array = roundtrip_livery.get("stamps", [])
	if roster_roundtrip.get("username", "") != "Wire Racer" \
			or !is_equal_approx(float(roster_roundtrip.get("accel_setting", 0.0)), 0.375) \
			or roundtrip_livery.get("primary_colour", "") != "ff0000ff" \
			or roundtrip_stamps.size() != 1 \
			or (roundtrip_stamps[0] as Dictionary).get("hash", "") != "sha256:stamp":
		push_error("MXT_RACE_ROSTER_CONTENT_SMOKE_FAIL")
		quit(1)
		return
	print("MXT_RACE_CONFIGURATION_WIRE_SMOKE_OK config_bytes=", source.encode_wire().size(),
		" track_bytes=", track_source.encode_wire().size(), " roster_bytes=", roster_source.encode_wire().size())
	quit(0)
