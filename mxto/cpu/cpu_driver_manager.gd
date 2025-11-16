class_name CpuDriverManager
extends Node

const CpuDriver := preload("res://cpu/cpu_driver.gd")
const PlayerInputClass := preload("res://player/player_input.gd")

var drivers: Dictionary = {}
var pending_inputs: Dictionary = {}
var last_generated_tick: Dictionary = {}

func configure_drivers(ids: Array) -> void:
	var id_set := ids.duplicate()
	for id in drivers.keys():
		if !id_set.has(id):
			var node: Node = drivers[id]
			node.queue_free()
			drivers.erase(id)
			pending_inputs.erase(id)
			last_generated_tick.erase(id)
	for id in id_set:
		if !drivers.has(id):
			var driver := CpuDriver.new()
			add_child(driver)
			drivers[id] = driver
			pending_inputs[id] = PlayerInputClass.new().serialize()
			last_generated_tick[id] = -1

func submit_observation(id: int, tick: int, observation: PackedByteArray) -> void:
	if !drivers.has(id):
		return
	var driver: CpuDriver = drivers[id]
	driver.process_observation(tick, observation)
	var input_bytes := driver.generate_input()
	if input_bytes.is_empty():
		var neutral := PlayerInputClass.new()
		input_bytes = neutral.serialize()
	pending_inputs[id] = input_bytes
	last_generated_tick[id] = tick

func fetch_input_for_tick(id: int, expected_tick: int) -> PackedByteArray:
	if !drivers.has(id):
		var neutral := PlayerInputClass.new()
		return neutral.serialize()
	if !pending_inputs.has(id):
		var neutral := PlayerInputClass.new()
		pending_inputs[id] = neutral.serialize()
	var last_tick := int(last_generated_tick.get(id, -1))
	if last_tick < expected_tick - 1:
		# No fresh observation yet; reuse previous input.
		return pending_inputs[id]
	return pending_inputs[id]
