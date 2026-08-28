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
	print("MXT_RACE_CONFIGURATION_WIRE_SMOKE_OK config_bytes=", source.encode_wire().size(),
		" track_bytes=", track_source.encode_wire().size())
	quit(0)
