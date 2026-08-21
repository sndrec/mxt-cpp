class_name PracticeController
extends Node

signal session_started(options: Dictionary)
signal session_ended

const SESSION_KIND := "practice"

var game_manager: GameManager
var session_active := false
var session_serial := 0
var session_options: Dictionary = {}
var session_completed := false


func initialize(in_game_manager: GameManager) -> void:
	game_manager = in_game_manager


func begin_session(options: Dictionary) -> bool:
	if String(options.get("session_kind", "")) != SESSION_KIND:
		return false
	if session_active:
		end_session()
	session_serial += 1
	session_options = options.duplicate(true)
	session_active = true
	session_completed = false
	session_started.emit(session_options.duplicate(true))
	return true


func end_session() -> void:
	if !session_active and session_options.is_empty():
		return
	session_active = false
	session_completed = false
	session_options.clear()
	session_ended.emit()


func mark_completed() -> void:
	if session_active:
		session_completed = true


func is_infinite() -> bool:
	return session_active and int(session_options.get("lap_count", 3)) == 0


func retry_options() -> Dictionary:
	return session_options.duplicate(true) if session_active else {}
