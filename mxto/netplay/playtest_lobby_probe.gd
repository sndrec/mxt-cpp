class_name PlaytestLobbyProbe
extends Node

signal availability_changed(available: bool)

@export var server_address := "99.248.53.136"
@export_range(1, 65535, 1) var server_port := 27016
@export_range(0.25, 10.0, 0.05) var probe_interval_seconds := 1.0
@export_range(0.1, 5.0, 0.05) var probe_timeout_seconds := 0.8
@export_range(0.25, 10.0, 0.05) var availability_hold_seconds := 2.25

var _enabled := false
var _available := false
var _probe_peer: ENetMultiplayerPeer
var _next_probe_msec := 0
var _probe_deadline_msec := 0
var _last_success_msec := -1

func _ready() -> void:
	set_process(false)

func set_enabled(enabled: bool) -> void:
	if _enabled == enabled:
		return
	_enabled = enabled
	if !_enabled:
		_close_probe()
		_last_success_msec = -1
		_set_available(false)
		set_process(false)
		return
	_next_probe_msec = 0
	set_process(true)

func _process(_delta: float) -> void:
	var now_msec := Time.get_ticks_msec()
	if _probe_peer != null:
		_probe_peer.poll()
		var status := _probe_peer.get_connection_status()
		if status == MultiplayerPeer.CONNECTION_DISCONNECTED:
			_close_probe()
		elif status == MultiplayerPeer.CONNECTION_CONNECTED:
			_last_success_msec = now_msec
			_set_available(true)
			_close_probe()
		elif now_msec >= _probe_deadline_msec:
			_close_probe()

	if _probe_peer == null and now_msec >= _next_probe_msec:
		_start_probe(now_msec)

	if _available and (_last_success_msec < 0 or now_msec - _last_success_msec > roundi(availability_hold_seconds * 1000.0)):
		_set_available(false)

func _start_probe(now_msec: int) -> void:
	var peer := ENetMultiplayerPeer.new()
	var error := peer.create_client(server_address, server_port)
	_next_probe_msec = now_msec + roundi(probe_interval_seconds * 1000.0)
	if error != OK:
		peer.close()
		return
	_probe_peer = peer
	_probe_deadline_msec = now_msec + roundi(probe_timeout_seconds * 1000.0)

func _close_probe() -> void:
	if _probe_peer == null:
		return
	_probe_peer.close()
	_probe_peer = null

func _set_available(available: bool) -> void:
	if _available == available:
		return
	_available = available
	availability_changed.emit(_available)

func _exit_tree() -> void:
	_close_probe()
