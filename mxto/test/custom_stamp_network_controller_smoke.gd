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
	var raw_indices := PackedByteArray()
	raw_indices.resize(32)
	var blob := CustomStampBlob.from_index_bytes(8, 8, CustomStampBlob.BPP_CUSTOM_PALETTE, 0, raw_indices, PackedColorArray([Color.TRANSPARENT])) as CustomStampBlob
	var manifest_entry := blob.to_manifest(true)
	manifest_entry["source"] = "custom"
	manifest_entry["rect_pixels"] = [0, 0, 8, 8]
	manifest_entry["region_size"] = [256, 128]
	manifest_entry["rect_rotated"] = false
	var manifest_wire := owner.wire_codec.encode_manifest([manifest_entry])
	var decoded_manifest := owner.wire_codec.decode_manifest(manifest_wire)
	if decoded_manifest.size() != 1 or str((decoded_manifest[0] as Dictionary).get("hash", "")) != blob.stamp_hash:
		_fail("manifest wire roundtrip failed")
		return
	var blob_wire := owner.wire_codec.encode_blob(blob.to_wire_dict())
	var decoded_blob_data := owner.wire_codec.decode_blob(blob_wire)
	var decoded_blob := CustomStampBlob.new()
	decoded_blob.from_wire_dict(decoded_blob_data)
	if decoded_blob.validate_blob() != "" or decoded_blob.stamp_hash != blob.stamp_hash:
		_fail("blob wire roundtrip failed")
		return
	var hash_wire := owner.wire_codec.encode_hashes([blob.stamp_hash])
	if owner.wire_codec.decode_hashes(hash_wire) != [blob.stamp_hash]:
		_fail("hash wire roundtrip failed")
		return
	owner._queue_custom_stamp_blob(7, blob, false)
	owner._queue_custom_stamp_blob(7, blob, false)
	if owner.outgoing_blob_queue.size() != 1 or owner.outgoing_blob_queue_bytes <= 0:
		_fail("outgoing blob queue did not dedupe a peer/hash pair")
		return
	owner.remove_peer(7)
	if owner.custom_stamp_manifests.has(7) or owner.custom_stamp_blob_waiters["probe"].has(7) or !owner.outgoing_blob_queue.is_empty():
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
