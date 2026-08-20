class_name SessionMemoryTelemetry extends RefCounted

var game_manager
var log_file: FileAccess

func initialize(p_game_manager) -> void:
	game_manager = p_game_manager
	var log_dir := ProjectSettings.globalize_path("user://logs")
	if DirAccess.make_dir_recursive_absolute(log_dir) != OK:
		return
	var file_name := "memory-%s.csv" % str(Time.get_unix_time_from_system())
	log_file = FileAccess.open(log_dir.path_join(file_name), FileAccess.WRITE)
	if log_file == null:
		return
	log_file.store_csv_line(PackedStringArray([
		"time_msec", "event", "state", "role", "players",
		"static_bytes", "static_peak_bytes", "message_buffer_peak_bytes",
		"video_bytes", "texture_bytes", "buffer_bytes",
		"objects", "resources", "nodes", "orphan_nodes",
		"recording_frames", "recording_input_bytes", "playback_frames", "playback_source_bytes",
		"seek_checkpoints", "seek_checkpoint_bytes", "debug_recording_frames", "debug_playback_frames",
		"race_archetypes", "ghost_archetypes", "lobby_archetypes", "magnifier_archetypes", "lobby_rebuilds_total",
		"game_sim_started", "game_sim_native_bytes", "game_sim_level_bytes", "game_sim_state_bytes", "game_sim_rollback_bytes",
		"server_sim_started", "server_sim_native_bytes", "server_sim_level_bytes", "server_sim_state_bytes", "server_sim_rollback_bytes",
		"ghost_count", "ghost_sim_native_bytes", "ghost_sim_level_bytes", "ghost_sim_state_bytes", "ghost_sim_rollback_bytes",
	]))
	sample("session_start")

func sample(event_name: String) -> void:
	if log_file == null or game_manager == null:
		return
	var replay_stats: Dictionary = game_manager.replay_controller.get_memory_usage_stats()
	var game_sim_stats := _sim_stats(game_manager.game_sim)
	var server_sim_stats := _sim_stats(game_manager.server_game_sim)
	var ghost_stats := game_manager.time_attack_ghost_controller.memory_usage_stats() if game_manager.time_attack_ghost_controller != null else {}
	var role := "offline"
	if game_manager.network_manager.network_active:
		role = "listen" if game_manager.network_manager.is_server and game_manager.network_manager.listen_server else ("server" if game_manager.network_manager.is_server else "client")
	var state := "menu"
	if game_manager.replay_controller.replay_playback_active:
		state = "replay"
	elif game_manager.game_sim.sim_started:
		state = "race"
	elif game_manager.lobby_control.visible:
		state = "lobby"
	var row := PackedStringArray([
		str(Time.get_ticks_msec()), event_name, state, role, str(game_manager.network_manager.player_ids.size()),
		_monitor_int(Performance.MEMORY_STATIC), _monitor_int(Performance.MEMORY_STATIC_MAX), _monitor_int(Performance.MEMORY_MESSAGE_BUFFER_MAX),
		_monitor_int(Performance.RENDER_VIDEO_MEM_USED), _monitor_int(Performance.RENDER_TEXTURE_MEM_USED), _monitor_int(Performance.RENDER_BUFFER_MEM_USED),
		_monitor_int(Performance.OBJECT_COUNT), _monitor_int(Performance.OBJECT_RESOURCE_COUNT), _monitor_int(Performance.OBJECT_NODE_COUNT), _monitor_int(Performance.OBJECT_ORPHAN_NODE_COUNT),
		str(replay_stats.get("recording_frames", 0)), str(replay_stats.get("recording_input_bytes", 0)), str(replay_stats.get("playback_frames", 0)), str(replay_stats.get("playback_source_bytes", 0)),
		str(replay_stats.get("seek_checkpoint_count", 0)), str(replay_stats.get("seek_checkpoint_bytes", 0)), str(replay_stats.get("debug_recording_frames", 0)), str(replay_stats.get("debug_playback_frames", 0)),
		_archetype_count(game_manager.car_render_manager), str(ghost_stats.get("render_archetypes", 0)), _archetype_count(game_manager.lobby_chibi_controller.render_manager), _archetype_count(game_manager.lobby_chibi_controller.magnifier_render_manager), str(game_manager.lobby_chibi_controller.render_rebuild_count_total),
		str(game_sim_stats.get("sim_started", false)), str(game_sim_stats.get("tracked_native_bytes", 0)), str(game_sim_stats.get("level_heap_capacity_bytes", 0)), str(game_sim_stats.get("gamestate_heap_capacity_bytes", 0)), str(game_sim_stats.get("rollback_buffer_bytes", 0)),
		str(server_sim_stats.get("sim_started", false)), str(server_sim_stats.get("tracked_native_bytes", 0)), str(server_sim_stats.get("level_heap_capacity_bytes", 0)), str(server_sim_stats.get("gamestate_heap_capacity_bytes", 0)), str(server_sim_stats.get("rollback_buffer_bytes", 0)),
		str(ghost_stats.get("ghost_count", 0)), str(ghost_stats.get("aggregate_tracked_native_bytes", 0)), str(ghost_stats.get("aggregate_level_heap_bytes", 0)), str(ghost_stats.get("aggregate_gamestate_heap_bytes", 0)), str(ghost_stats.get("aggregate_rollback_bytes", 0)),
	])
	log_file.store_csv_line(row)
	log_file.flush()

func _sim_stats(sim: GameSim) -> Dictionary:
	if sim == null:
		return {}
	return sim.get_memory_usage_stats()

func _archetype_count(manager: CarRenderManager) -> String:
	return "0" if manager == null else str(manager.archetypes.size())

func _monitor_int(monitor: int) -> String:
	return str(int(Performance.get_monitor(monitor)))
