class_name CpuDriver
extends Node

const PlayerInputClass := preload("res://player/player_input.gd")

var last_input: PlayerInput = PlayerInputClass.new()

func reset_state() -> void:
	last_input = PlayerInputClass.new()

func process_observation(_tick: int, _observation: PackedByteArray) -> void:
	pass

func generate_input() -> PackedByteArray:
	# Default implementation: neutral input.
	last_input.apply_quantization()
	return last_input.serialize()
