class_name RaceContentAcquisitionController
extends Node

const DOWNLOAD_TIMEOUT_MSEC := 25000

@onready var network_manager: NetworkManager = get_node("../NetworkManager") as NetworkManager
@onready var track_content_controller: TrackContentController = get_node("../TrackContentController") as TrackContentController
@onready var vehicle_content_controller: VehicleContentController = get_node("../VehicleContentController") as VehicleContentController

var steam_service: MxtSteamService
var temporary_content_tracker: LobbyVehicleContentTracker


func initialize(
		in_steam_service: MxtSteamService,
		in_temporary_content_tracker: LobbyVehicleContentTracker) -> void:
	steam_service = in_steam_service
	temporary_content_tracker = in_temporary_content_tracker


func acquire(
		track_id: String,
		roster: MxtRaceRoster,
		track_evidence: MxtTrackContentEvidence) -> RaceContentReadiness:
	var readiness := _evaluate(track_id, roster, track_evidence)
	if readiness.ready:
		return readiness
	if !readiness.downloadable or steam_service == null or !steam_service.is_initialized():
		return readiness
	var tracked_any := false
	for workshop_id in readiness.workshop_ids:
		tracked_any = temporary_content_tracker.track_temporary_item(workshop_id) or tracked_any
	if tracked_any:
		steam_service.refresh_workshop_items()
		readiness = _evaluate(track_id, roster, track_evidence)
		if readiness.ready:
			return readiness
	var workshop_id_strings := PackedStringArray()
	for workshop_id in readiness.workshop_ids:
		workshop_id_strings.append(str(workshop_id))
	network_manager.race_admission.report(
		network_manager.race_admission.LOADING,
		"acquiring Workshop package%s %s" % [
			"" if readiness.workshop_ids.size() == 1 else "s",
			", ".join(workshop_id_strings),
		])
	var request_started := false
	for workshop_id in readiness.workshop_ids:
		request_started = steam_service.download_workshop_item(workshop_id, true) or request_started
	if !request_started:
		return readiness
	steam_service.refresh_workshop_items()
	var deadline := Time.get_ticks_msec() + DOWNLOAD_TIMEOUT_MSEC
	var next_refresh_msec := Time.get_ticks_msec() + 1000
	while network_manager.race_active and Time.get_ticks_msec() < deadline:
		await get_tree().create_timer(0.1).timeout
		readiness = _evaluate(track_id, roster, track_evidence)
		if readiness.ready:
			return readiness
		var now := Time.get_ticks_msec()
		if now >= next_refresh_msec:
			steam_service.refresh_workshop_items()
			next_refresh_msec = now + 1000
	readiness.detail = "%s after Workshop download timeout" % readiness.detail
	return readiness


func _evaluate(
		track_id: String,
		roster: MxtRaceRoster,
		track_evidence: MxtTrackContentEvidence) -> RaceContentReadiness:
	var result := RaceContentReadiness.new()
	var problems := PackedStringArray()
	var irrecoverable_problems := PackedStringArray()
	if track_evidence == null or track_evidence.count() == 0 or track_evidence.find_content_id(track_id) < 0:
		var problem := "race track content records are malformed"
		problems.append(problem)
		irrecoverable_problems.append(problem)
	else:
		for i in range(track_evidence.count()):
			if track_content_controller.track_content_evidence_matches(
					track_evidence.get_content_id(i),
					track_evidence.get_gameplay_digest(i),
					track_evidence.get_package_digest(i),
					track_evidence.get_workshop_id(i)):
				continue
			var workshop_id := track_evidence.get_workshop_id(i)
			var problem := "missing exact track %s" % track_evidence.get_content_id(i)
			if !_valid_workshop_id(workshop_id):
				problem = "non-Workshop track %s does not match the host build" % track_evidence.get_content_id(i)
				irrecoverable_problems.append(problem)
			problems.append(problem)
			_append_workshop_id(result.workshop_ids, workshop_id)
	for roster_index in roster.count():
		var player_settings := PlayerSettings.new()
		player_settings.from_dict(roster.get_settings_dictionary(roster_index))
		if player_settings.spectator or vehicle_content_controller.evidence_matches(player_settings):
			continue
		var problem := "missing exact vehicle %s" % player_settings.vehicle_content_id
		if !_valid_workshop_id(player_settings.vehicle_workshop_id):
			problem = "non-Workshop vehicle %s does not match the host build" % player_settings.vehicle_content_id
			irrecoverable_problems.append(problem)
		problems.append(problem)
		_append_workshop_id(result.workshop_ids, player_settings.vehicle_workshop_id)
	result.ready = problems.is_empty()
	result.downloadable = irrecoverable_problems.is_empty() and !result.workshop_ids.is_empty()
	result.detail = "; ".join(problems)
	return result


func _valid_workshop_id(value: String) -> bool:
	return value.is_valid_int() and value.to_int() > 0


func _append_workshop_id(ids: Array[int], value: String) -> void:
	if !_valid_workshop_id(value):
		return
	var workshop_id := value.to_int()
	if !ids.has(workshop_id):
		ids.append(workshop_id)
