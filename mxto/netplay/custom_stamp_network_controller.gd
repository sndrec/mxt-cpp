class_name CustomStampNetworkController extends Node

const CarLivery = preload("res://vehicle/customization/car_livery.gd")
const CarLiveryStore = preload("res://vehicle/customization/car_livery_store.gd")
const CustomStampBlob = preload("res://vehicle/customization/custom_stamp_blob.gd")
const CustomStampStore = preload("res://vehicle/customization/custom_stamp_store.gd")

@onready var network_manager: NetworkManager = get_parent() as NetworkManager

var custom_stamp_manifests := {}
var custom_stamp_blob_cache := {}
var custom_stamp_blob_waiters := {}
var revision := 0
const BLOB_SEND_BUDGET_BYTES_PER_FRAME := 24 * 1024
const MANIFEST_SNAPSHOT_MAX_BYTES := 2 * 1024 * 1024
var outgoing_blob_queue: Array[Dictionary] = []
var outgoing_blob_queue_head := 0
var outgoing_blob_queue_bytes := 0
var outgoing_blob_keys := {}
var log_manifest_in := 0
var log_manifest_out := 0
var log_manifest_bytes_in := 0
var log_manifest_bytes_out := 0
var log_manifest_accepted := 0
var log_manifest_deduped := 0
var log_blob_in := 0
var log_blob_out := 0
var log_blob_bytes_in := 0
var log_blob_bytes_out := 0
var log_blob_accepted := 0
var log_blob_deduped := 0

func _process(_delta: float) -> void:
	if network_manager == null or network_manager.race_active or !network_manager.has_network_peer():
		_clear_outgoing_blob_queue()
		return
	var budget := BLOB_SEND_BUDGET_BYTES_PER_FRAME
	while outgoing_blob_queue_head < outgoing_blob_queue.size():
		var entry: Dictionary = outgoing_blob_queue[outgoing_blob_queue_head]
		var encoded_bytes := int(entry.get("bytes", 0))
		if budget < BLOB_SEND_BUDGET_BYTES_PER_FRAME and encoded_bytes > budget:
			break
		outgoing_blob_queue_head += 1
		outgoing_blob_queue_bytes = maxi(0, outgoing_blob_queue_bytes - encoded_bytes)
		outgoing_blob_keys.erase(str(entry.get("key", "")))
		var peer_id := int(entry.get("peer_id", 0))
		if !network_manager._can_send_rpc_to_peer(peer_id):
			continue
		var blob_data: Dictionary = entry.get("data", {})
		if bool(entry.get("submit", false)):
			_submit_custom_stamp_blob.rpc_id(peer_id, blob_data)
		else:
			_receive_custom_stamp_blob.rpc_id(peer_id, blob_data)
		log_blob_out += 1
		log_blob_bytes_out += encoded_bytes
		budget -= encoded_bytes
	if outgoing_blob_queue_head >= outgoing_blob_queue.size():
		_clear_outgoing_blob_queue()
	elif outgoing_blob_queue_head >= 128 and outgoing_blob_queue_head * 2 >= outgoing_blob_queue.size():
		outgoing_blob_queue = outgoing_blob_queue.slice(outgoing_blob_queue_head)
		outgoing_blob_queue_head = 0

func _queue_custom_stamp_blob(peer_id: int, blob_data: Dictionary, submit_to_server: bool) -> void:
	if peer_id <= 0:
		return
	var stamp_hash := str(blob_data.get("stamp_hash", blob_data.get("hash", "")))
	var key := "%d:%d:%s" % [peer_id, int(submit_to_server), stamp_hash]
	if outgoing_blob_keys.has(key):
		return
	var encoded_bytes := var_to_bytes(blob_data).size()
	outgoing_blob_queue.append({
		"peer_id": peer_id,
		"data": blob_data,
		"bytes": encoded_bytes,
		"submit": submit_to_server,
		"key": key,
	})
	outgoing_blob_keys[key] = true
	outgoing_blob_queue_bytes += encoded_bytes

func _clear_outgoing_blob_queue() -> void:
	outgoing_blob_queue.clear()
	outgoing_blob_queue_head = 0
	outgoing_blob_queue_bytes = 0
	outgoing_blob_keys.clear()

func send_active_custom_stamp_manifest() -> void:
	if network_manager.race_active or !network_manager.has_network_peer():
		return
	var payload := _build_local_custom_stamp_payload()
	if !bool(payload.get("ok", false)):
		push_warning("Custom stamp manifest not sent: %s" % str(payload.get("error", "unknown error")))
		return
	var manifest: Array = payload.get("manifest", [])
	var manifest_bytes := var_to_bytes(manifest).size()
	var my_id := multiplayer.get_unique_id()
	_cache_custom_stamp_payload_blobs(payload)
	_accept_custom_stamp_manifest(my_id, manifest)
	if network_manager.is_server:
		_send_custom_stamp_manifest_snapshot({my_id: manifest})
	else:
		_submit_custom_stamp_manifest.rpc_id(1, manifest)
		log_manifest_out += 1
		log_manifest_bytes_out += manifest_bytes

func _build_local_custom_stamp_payload() -> Dictionary:
	var settings := network_manager._get_local_player_settings_snapshot()
	var vehicle_content_id := str(settings.get("vehicle_content_id", ""))
	if vehicle_content_id == "":
		return {"ok": true, "manifest": [], "blobs": []}
	var livery: CarLivery = null
	if settings.has("car_livery") and typeof(settings["car_livery"]) == TYPE_DICTIONARY and !(settings["car_livery"] as Dictionary).is_empty():
		livery = CarLivery.new()
		livery.from_dict(settings["car_livery"])
	else:
		livery = CarLiveryStore.load_for_car(vehicle_content_id)
	livery.vehicle_content_id = vehicle_content_id
	return CustomStampStore.build_livery_payload(livery)

func _cache_custom_stamp_payload_blobs(payload: Dictionary) -> void:
	for item in payload.get("blobs", []):
		var blob := item as CustomStampBlob
		if blob == null:
			continue
		custom_stamp_blob_cache[blob.stamp_hash] = blob
		CustomStampStore.save_blob(blob)

@rpc("any_peer", "call_remote", "reliable", 11)
func _submit_custom_stamp_manifest(manifest: Array) -> void:
	if !network_manager.is_server or network_manager.race_active:
		return
	var sender_id := multiplayer.get_remote_sender_id()
	log_manifest_in += 1
	log_manifest_bytes_in += var_to_bytes(manifest).size()
	if sender_id == 0:
		sender_id = multiplayer.get_unique_id()
	var was_duplicate: bool = custom_stamp_manifests.has(sender_id) and custom_stamp_manifests[sender_id] == manifest
	if !_accept_custom_stamp_manifest(sender_id, manifest):
		return
	if was_duplicate:
		_request_missing_custom_stamp_blobs_from_owner(sender_id, manifest)
		return
	_send_custom_stamp_manifest_snapshot({sender_id: manifest})
	_request_missing_custom_stamp_blobs_from_owner(sender_id, manifest)

func _apply_received_custom_stamp_manifest(player_id: int, manifest: Array) -> void:
	if !_accept_custom_stamp_manifest(player_id, manifest):
		return
	var missing := CustomStampStore.missing_hashes(manifest)
	if missing.is_empty():
		return
	if network_manager.is_server:
		_queue_custom_stamp_blob_waiters(player_id, missing, multiplayer.get_unique_id())
		_request_missing_custom_stamp_blobs_from_owner(player_id, manifest)
	else:
		_request_custom_stamp_blobs.rpc_id(1, player_id, Array(missing))

func _accept_custom_stamp_manifest(player_id: int, manifest: Array) -> bool:
	var validation_error := _validate_custom_stamp_manifest(manifest)
	if validation_error != "":
		push_warning("Rejected custom stamp manifest from %s: %s" % [str(player_id), validation_error])
		return false
	var existing = custom_stamp_manifests.get(player_id, null)
	if typeof(existing) == TYPE_ARRAY and existing == manifest:
		log_manifest_deduped += 1
		return true
	custom_stamp_manifests[player_id] = manifest.duplicate(true)
	revision += 1
	log_manifest_accepted += 1
	return true

@rpc("any_peer", "call_remote", "reliable", 11)
func _request_custom_stamp_blobs(owner_id: int, hashes: Array) -> void:
	if !network_manager.is_server or network_manager.race_active:
		return
	var requester_id := multiplayer.get_remote_sender_id()
	if requester_id == 0:
		requester_id = multiplayer.get_unique_id()
	var filtered := _filter_manifest_hashes(owner_id, hashes)
	if filtered.is_empty():
		return
	var missing_for_server: Array = []
	for hash_value in filtered:
		var stamp_hash := str(hash_value)
		var cached := get_custom_stamp_blob(stamp_hash)
		if cached != null:
			_queue_custom_stamp_blob(requester_id, cached.to_cache_dict(), false)
		else:
			_add_custom_stamp_blob_waiter(stamp_hash, requester_id)
			missing_for_server.append(stamp_hash)
	if !missing_for_server.is_empty():
		_request_custom_stamp_blobs_from_owner(owner_id, missing_for_server)

@rpc("any_peer", "call_remote", "reliable", 11)
func _provide_custom_stamp_blobs(hashes: Array) -> void:
	if network_manager.race_active:
		return
	var sender_id := multiplayer.get_remote_sender_id()
	if !network_manager.is_server and sender_id != 0 and sender_id != 1:
		return
	for hash_value in hashes:
		var stamp_hash := str(hash_value)
		var blob := get_custom_stamp_blob(stamp_hash)
		if blob == null:
			continue
		if network_manager.is_server:
			_accept_custom_stamp_blob(blob.to_cache_dict())
		else:
			_queue_custom_stamp_blob(1, blob.to_cache_dict(), true)

@rpc("any_peer", "call_remote", "reliable", 11)
func _submit_custom_stamp_blob(blob_data: Dictionary) -> void:
	if !network_manager.is_server or network_manager.race_active:
		return
	var sender_id := multiplayer.get_remote_sender_id()
	log_blob_in += 1
	log_blob_bytes_in += var_to_bytes(blob_data).size()
	if sender_id == 0:
		sender_id = multiplayer.get_unique_id()
	_accept_custom_stamp_blob(blob_data, sender_id)

@rpc("any_peer", "call_remote", "reliable", 11)
func _receive_custom_stamp_blob(blob_data: Dictionary) -> void:
	if network_manager.race_active:
		return
	var sender_id := multiplayer.get_remote_sender_id()
	if sender_id != 0:
		log_blob_in += 1
		log_blob_bytes_in += var_to_bytes(blob_data).size()
	if !network_manager.is_server and sender_id != 0 and sender_id != 1:
		return
	_accept_custom_stamp_blob(blob_data)

func _accept_custom_stamp_blob(blob_data: Dictionary, expected_owner_id := -1) -> bool:
	var blob := CustomStampBlob.new()
	blob.from_cache_dict(blob_data)
	var validation_error := blob.validate_blob()
	if validation_error != "":
		push_warning("Rejected custom stamp blob: %s" % validation_error)
		return false
	if !_custom_stamp_hash_is_manifested(blob.stamp_hash):
		push_warning("Rejected unmanifested custom stamp blob: %s" % blob.stamp_hash)
		return false
	if expected_owner_id >= 0 and !_custom_stamp_manifest_has_hash(expected_owner_id, blob.stamp_hash):
		push_warning("Rejected custom stamp blob from wrong owner: %s" % blob.stamp_hash)
		return false
	var already_cached := custom_stamp_blob_cache.has(blob.stamp_hash) or CustomStampStore.has_blob(blob.stamp_hash)
	custom_stamp_blob_cache[blob.stamp_hash] = blob
	if !already_cached:
		CustomStampStore.save_blob(blob, false)
		revision += 1
		log_blob_accepted += 1
	else:
		log_blob_deduped += 1
	if network_manager.is_server and custom_stamp_blob_waiters.has(blob.stamp_hash):
		var waiters: Array = custom_stamp_blob_waiters[blob.stamp_hash]
		for waiter in waiters:
			var waiter_id := int(waiter)
			if waiter_id == multiplayer.get_unique_id():
				continue
			_queue_custom_stamp_blob(waiter_id, blob.to_cache_dict(), false)
		custom_stamp_blob_waiters.erase(blob.stamp_hash)
	return true

func _validate_custom_stamp_manifest(manifest: Array) -> String:
	if manifest.size() > CarLivery.MAX_STAMPS:
		return "too many custom stamp entries"
	var total_compressed := 0
	var hashes := {}
	var custom_palette_count := 0
	var blob_sizes := {}
	for raw_entry in manifest:
		if typeof(raw_entry) != TYPE_DICTIONARY:
			return "manifest entry is not a dictionary"
		var entry: Dictionary = raw_entry
		if str(entry.get("source", "custom")) != "custom":
			return "manifest entry source is not custom"
		var stamp_hash := str(entry.get("hash", ""))
		if stamp_hash == "":
			return "manifest entry is missing hash"
		hashes[stamp_hash] = true
		var width := int(entry.get("width", 0))
		var height := int(entry.get("height", 0))
		var dimension_error := CustomStampBlob.validate_dimensions(width, height)
		if dimension_error != "":
			return dimension_error
		var bpp := int(entry.get("bits_per_pixel", CustomStampBlob.BPP_AUTHORED_PALETTE))
		if bpp != CustomStampBlob.BPP_CUSTOM_PALETTE and bpp != CustomStampBlob.BPP_AUTHORED_PALETTE:
			return "unsupported bits_per_pixel"
		var expected_size := CustomStampBlob.index_byte_size(width, height, bpp)
		if int(entry.get("uncompressed_size", -1)) != expected_size:
			return "manifest uncompressed size does not match dimensions"
		var compressed_size := int(entry.get("compressed_size", -1))
		if compressed_size <= 0 or compressed_size > CustomStampBlob.COMPRESSED_BYTE_CAP:
			return "manifest compressed size exceeds cap"
		if blob_sizes.has(stamp_hash):
			if int(blob_sizes[stamp_hash]) != compressed_size:
				return "manifest repeats hash with different compressed size"
		else:
			blob_sizes[stamp_hash] = compressed_size
			total_compressed += compressed_size
			if total_compressed > CustomStampBlob.COMPRESSED_BYTE_CAP:
				return "manifest exceeds per-player compressed byte cap"
		if int(entry.get("palette_id", 0)) < 0 or int(entry.get("palette_id", 0)) > 255:
			return "palette_id outside supported range"
		if bpp == CustomStampBlob.BPP_CUSTOM_PALETTE:
			custom_palette_count += 1
			if custom_palette_count > CarLivery.MAX_STAMPS:
				return "too many custom palettes"
			if !entry.has("custom_palette") or typeof(entry["custom_palette"]) != TYPE_ARRAY:
				return "custom palette entry is missing palette"
			if (entry["custom_palette"] as Array).size() > 16:
				return "custom palette exceeds 16 colours"
		if !_manifest_rect_is_valid(entry, width, height, bool(entry.get("rect_rotated", false))):
			return "custom stamp packed rect is invalid"
	if hashes.size() > CarLivery.MAX_STAMPS:
		return "too many unique custom stamp hashes"
	return ""

func _manifest_rect_is_valid(entry: Dictionary, width: int, height: int, rotated: bool) -> bool:
	if !entry.has("rect_pixels") or typeof(entry["rect_pixels"]) != TYPE_ARRAY:
		return false
	if !entry.has("region_size") or typeof(entry["region_size"]) != TYPE_ARRAY:
		return false
	var rect_values: Array = entry["rect_pixels"]
	var region_values: Array = entry["region_size"]
	if rect_values.size() < 4 or region_values.size() < 2:
		return false
	var rect := Rect2i(int(rect_values[0]), int(rect_values[1]), int(rect_values[2]), int(rect_values[3]))
	var region := Vector2i(int(region_values[0]), int(region_values[1]))
	if region != Vector2i(256, 128) and region != Vector2i(128, 256):
		return false
	if rect.position.x < 0 or rect.position.y < 0 or rect.size.x <= 0 or rect.size.y <= 0:
		return false
	var expected_size := Vector2i(height, width) if rotated else Vector2i(width, height)
	if rect.size != expected_size:
		return false
	return rect.position.x + rect.size.x <= region.x and rect.position.y + rect.size.y <= region.y

func _filter_manifest_hashes(owner_id: int, hashes: Array) -> Array:
	var out: Array = []
	if !custom_stamp_manifests.has(owner_id):
		return out
	var allowed := {}
	for entry in custom_stamp_manifests[owner_id]:
		if typeof(entry) == TYPE_DICTIONARY:
			allowed[str((entry as Dictionary).get("hash", ""))] = true
	for hash_value in hashes:
		var stamp_hash := str(hash_value)
		if stamp_hash != "" and allowed.has(stamp_hash) and !out.has(stamp_hash):
			out.append(stamp_hash)
	return out

func _request_missing_custom_stamp_blobs_from_owner(owner_id: int, manifest: Array) -> void:
	var missing := CustomStampStore.missing_hashes(manifest)
	if missing.is_empty():
		return
	_queue_custom_stamp_blob_waiters(owner_id, missing, multiplayer.get_unique_id())
	_request_custom_stamp_blobs_from_owner(owner_id, Array(missing))

func _request_custom_stamp_blobs_from_owner(owner_id: int, hashes: Array) -> void:
	if hashes.is_empty():
		return
	if owner_id == multiplayer.get_unique_id():
		_provide_custom_stamp_blobs(hashes)
	else:
		_provide_custom_stamp_blobs.rpc_id(owner_id, hashes)

func _queue_custom_stamp_blob_waiters(_owner_id: int, hashes: Array, waiter_id: int) -> void:
	for hash_value in hashes:
		_add_custom_stamp_blob_waiter(str(hash_value), waiter_id)

func _add_custom_stamp_blob_waiter(stamp_hash: String, waiter_id: int) -> void:
	if stamp_hash == "" or waiter_id <= 0:
		return
	var waiters: Array = custom_stamp_blob_waiters.get(stamp_hash, [])
	if !waiters.has(waiter_id):
		waiters.append(waiter_id)
	custom_stamp_blob_waiters[stamp_hash] = waiters

func _clear_custom_stamp_waiters_for_peer(peer_id: int) -> void:
	for stamp_hash in custom_stamp_blob_waiters.keys():
		var waiters: Array = custom_stamp_blob_waiters[stamp_hash]
		if waiters.has(peer_id):
			waiters.erase(peer_id)
		if waiters.is_empty():
			custom_stamp_blob_waiters.erase(stamp_hash)
		else:
			custom_stamp_blob_waiters[stamp_hash] = waiters

func get_custom_stamp_blob(stamp_hash: String) -> CustomStampBlob:
	if custom_stamp_blob_cache.has(stamp_hash):
		return custom_stamp_blob_cache[stamp_hash]
	var blob := CustomStampStore.load_blob(stamp_hash)
	if blob != null:
		custom_stamp_blob_cache[stamp_hash] = blob
	return blob

func get_custom_stamp_manifest(player_id: int) -> Array:
	return custom_stamp_manifests.get(player_id, []).duplicate(true)

func _custom_stamp_hash_is_manifested(stamp_hash: String) -> bool:
	for manifest in custom_stamp_manifests.values():
		for raw_entry in manifest:
			if typeof(raw_entry) == TYPE_DICTIONARY and str((raw_entry as Dictionary).get("hash", "")) == stamp_hash:
				return true
	return false

func _custom_stamp_manifest_has_hash(player_id: int, stamp_hash: String) -> bool:
	if !custom_stamp_manifests.has(player_id):
		return false
	for raw_entry in custom_stamp_manifests[player_id]:
		if typeof(raw_entry) == TYPE_DICTIONARY and str((raw_entry as Dictionary).get("hash", "")) == stamp_hash:
			return true
	return false

func clear() -> void:
	if !custom_stamp_manifests.is_empty() or !custom_stamp_blob_cache.is_empty():
		revision += 1
	custom_stamp_manifests.clear()
	custom_stamp_blob_cache.clear()
	custom_stamp_blob_waiters.clear()
	_clear_outgoing_blob_queue()

func remove_peer(peer_id: int) -> void:
	if custom_stamp_manifests.has(peer_id):
		revision += 1
	custom_stamp_manifests.erase(peer_id)
	_clear_custom_stamp_waiters_for_peer(peer_id)
	_drop_queued_blobs_for_peer(peer_id)

func _drop_queued_blobs_for_peer(peer_id: int) -> void:
	if outgoing_blob_queue_head >= outgoing_blob_queue.size():
		return
	var retained: Array[Dictionary] = []
	for i in range(outgoing_blob_queue_head, outgoing_blob_queue.size()):
		var entry: Dictionary = outgoing_blob_queue[i]
		if int(entry.get("peer_id", 0)) != peer_id:
			retained.append(entry)
	outgoing_blob_queue = retained
	outgoing_blob_queue_head = 0
	outgoing_blob_queue_bytes = 0
	outgoing_blob_keys.clear()
	for entry in outgoing_blob_queue:
		outgoing_blob_queue_bytes += int(entry.get("bytes", 0))
		outgoing_blob_keys[str(entry.get("key", ""))] = true

func send_manifests_to_peer(peer_id: int) -> void:
	if custom_stamp_manifests.is_empty() or !network_manager._can_send_rpc_to_peer(peer_id):
		return
	_send_custom_stamp_manifest_snapshot(custom_stamp_manifests, peer_id)

func _send_custom_stamp_manifest_snapshot(manifests: Dictionary, peer_id: int = 0) -> void:
	var raw := var_to_bytes(manifests)
	if raw.is_empty() or raw.size() > MANIFEST_SNAPSHOT_MAX_BYTES:
		push_warning("Custom stamp manifest snapshot rejected before send: %d bytes" % raw.size())
		return
	var payload := raw.compress(FileAccess.COMPRESSION_ZSTD)
	if payload.is_empty():
		payload = raw
	if peer_id > 0:
		_receive_custom_stamp_manifest_snapshot.rpc_id(peer_id, raw.size(), payload)
		log_manifest_out += 1
		log_manifest_bytes_out += payload.size()
	else:
		_receive_custom_stamp_manifest_snapshot.rpc(raw.size(), payload)
		var recipients := multiplayer.get_peers().size()
		log_manifest_out += recipients
		log_manifest_bytes_out += payload.size() * recipients

@rpc("authority", "call_remote", "reliable", 11)
func _receive_custom_stamp_manifest_snapshot(raw_size: int, payload: PackedByteArray) -> void:
	if network_manager.is_server or network_manager.race_active:
		return
	if raw_size <= 0 or raw_size > MANIFEST_SNAPSHOT_MAX_BYTES or payload.is_empty() or payload.size() > MANIFEST_SNAPSHOT_MAX_BYTES:
		return
	log_manifest_in += 1
	log_manifest_bytes_in += payload.size()
	var raw := payload.decompress(raw_size, FileAccess.COMPRESSION_ZSTD)
	if raw.is_empty() and payload.size() == raw_size:
		raw = payload
	if raw.size() != raw_size:
		return
	var decoded = bytes_to_var(raw)
	if typeof(decoded) != TYPE_DICTIONARY:
		return
	for raw_player_id in (decoded as Dictionary).keys():
		var manifest = (decoded as Dictionary)[raw_player_id]
		if typeof(manifest) != TYPE_ARRAY:
			continue
		_apply_received_custom_stamp_manifest(int(raw_player_id), manifest)

func consume_log_interval() -> Dictionary:
	var snapshot := {
		"manifest_in": log_manifest_in,
		"manifest_out": log_manifest_out,
		"manifest_bytes_in": log_manifest_bytes_in,
		"manifest_bytes_out": log_manifest_bytes_out,
		"manifest_accepted": log_manifest_accepted,
		"manifest_deduped": log_manifest_deduped,
		"blob_in": log_blob_in,
		"blob_out": log_blob_out,
		"blob_bytes_in": log_blob_bytes_in,
		"blob_bytes_out": log_blob_bytes_out,
		"blob_accepted": log_blob_accepted,
		"blob_deduped": log_blob_deduped,
		"blob_queue_messages": outgoing_blob_queue.size() - outgoing_blob_queue_head,
		"blob_queue_bytes": outgoing_blob_queue_bytes,
	}
	log_manifest_in = 0
	log_manifest_out = 0
	log_manifest_bytes_in = 0
	log_manifest_bytes_out = 0
	log_manifest_accepted = 0
	log_manifest_deduped = 0
	log_blob_in = 0
	log_blob_out = 0
	log_blob_bytes_in = 0
	log_blob_bytes_out = 0
	log_blob_accepted = 0
	log_blob_deduped = 0
	return snapshot
