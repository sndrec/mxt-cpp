extends SceneTree

func _fail(message: String) -> void:
	push_error("MXT_CUSTOM_STAMP_NETWORK_SMOKE_FAIL " + message)
	quit(1)

func _init() -> void:
	call_deferred("_run")

func _run() -> void:
	var packed := load("res://main.tscn") as PackedScene
	if packed == null:
		_fail("could not load main scene")
		return
	var game_manager := packed.instantiate() as GameManager
	root.add_child(game_manager)
	var owner := game_manager.network_manager.custom_stamp_network as CustomStampNetworkController
	if owner == null or owner.network_manager != game_manager.network_manager:
		_fail("owner was not attached to NetworkManager")
		return
	if !owner._validate_custom_stamp_manifest([]).is_empty():
		_fail("empty manifest should be valid")
		return
	owner.custom_stamp_manifests[7] = []
	var manifest := owner.get_custom_stamp_manifest(7)
	manifest.append({"hash": "mutation-probe"})
	if !owner.get_custom_stamp_manifest(7).is_empty():
		_fail("manifest reads did not preserve owner isolation")
		return
	owner.custom_stamp_blob_waiters["probe"] = [7, 8]
	owner.remove_peer(7)
	if owner.custom_stamp_manifests.has(7) or owner.custom_stamp_blob_waiters["probe"].has(7):
		_fail("peer removal did not clear manifest and waiter state")
		return
	owner.clear()
	if !owner.custom_stamp_manifests.is_empty() or !owner.custom_stamp_blob_waiters.is_empty():
		_fail("owner clear did not reset network state")
		return
	print("MXT_CUSTOM_STAMP_NETWORK_SMOKE_OK")
	game_manager.queue_free()
	await process_frame
	quit(0)
