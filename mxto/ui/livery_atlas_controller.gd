class_name LiveryAtlasController
extends Node

signal atlas_texture_changed(texture: Texture2D)

const CustomStampAtlasBuilder = preload("res://vehicle/customization/custom_stamp_atlas_builder.gd")
const CustomStampBlob = preload("res://vehicle/customization/custom_stamp_blob.gd")
const CustomStampPacker = preload("res://vehicle/customization/custom_stamp_packer.gd")
const CustomStampStore = preload("res://vehicle/customization/custom_stamp_store.gd")

var budget_label: Label
var atlas_preview: TextureRect
var atlas_texture: Texture2D
var region_texture: Texture2D
var build_thread: Thread
var active_revision := 0
var latest_revision := 0
var queued_records: Array = []
var has_queued_build := false

func initialize(owner_ui: Control) -> void:
	budget_label = owner_ui.get_node("Container/SettingsTabs/Garage/CarPreviewSpace/CustomStampBudget")
	atlas_preview = owner_ui.get_node("Container/SettingsTabs/Garage/CarPreviewSpace/CustomStampAtlasPreview")

func refresh(livery: CarLivery) -> bool:
	atlas_texture = null
	region_texture = null
	latest_revision += 1
	var atlas_revision := latest_revision
	atlas_texture_changed.emit(atlas_texture)
	if livery == null:
		_update_budget_overlay()
		return true
	var payload := CustomStampStore.build_livery_payload(livery)
	if !bool(payload.get("ok", false)):
		push_warning("Custom stamps do not fit the local vehicle atlas: %s" % str(payload.get("error", "unknown error")))
		_update_budget_overlay(payload, false)
		return false
	var placements: Dictionary = payload.get("placements", {})
	_update_budget_overlay(payload, true)
	if placements.is_empty():
		return true
	CustomStampPacker.apply_placements_to_livery(livery, placements, Vector2i.ZERO, CustomStampAtlasBuilder.ATLAS_SIZE)
	_request_build([{
		"player_id": 0,
		"region_origin": Vector2i.ZERO,
		"placements": placements,
		"blobs": payload.get("blobs", []),
	}], atlas_revision)
	return true

func poll() -> void:
	_poll_build_thread()

func finish() -> void:
	has_queued_build = false
	queued_records = []
	_poll_build_thread(true)

func _request_build(player_records: Array, revision: int) -> void:
	if build_thread != null and build_thread.is_alive():
		queued_records = player_records.duplicate(true)
		has_queued_build = true
		return
	_start_build_thread(player_records, revision)

func _start_build_thread(player_records: Array, revision: int) -> void:
	if build_thread != null:
		_poll_build_thread(true)
	active_revision = revision
	build_thread = Thread.new()
	var err := build_thread.start(_build_atlas_image_thread.bind(player_records.duplicate(true), revision))
	if err != OK:
		build_thread = null
		push_warning("Failed to start custom stamp atlas thread: %s" % err)
		var atlas_build := CustomStampAtlasBuilder.build_atlas_image(player_records)
		_apply_build_result(atlas_build, revision)

func _build_atlas_image_thread(player_records: Array, revision: int) -> Dictionary:
	var result := CustomStampAtlasBuilder.build_atlas_image(player_records)
	result["revision"] = revision
	return result

func _poll_build_thread(force_wait := false) -> void:
	if build_thread == null:
		return
	if !force_wait and build_thread.is_alive():
		return
	var result = build_thread.wait_to_finish()
	build_thread = null
	if typeof(result) == TYPE_DICTIONARY:
		_apply_build_result(result, int(result.get("revision", active_revision)))
	if has_queued_build:
		var queued := queued_records
		queued_records = []
		has_queued_build = false
		_start_build_thread(queued, latest_revision)

func _apply_build_result(atlas_build: Dictionary, revision: int) -> void:
	if revision != latest_revision:
		return
	if !bool(atlas_build.get("ok", false)):
		push_warning("Failed to build garage custom stamp atlas: %s" % str(atlas_build.get("error", "unknown error")))
		return
	var image := atlas_build.get("image", null) as Image
	atlas_texture = CustomStampAtlasBuilder.texture_from_image(image)
	atlas_texture_changed.emit(atlas_texture)

func _update_budget_overlay(payload: Dictionary = {}, ok := true) -> void:
	if budget_label == null or atlas_preview == null:
		return
	var blobs: Array = payload.get("blobs", [])
	var pixel_count := 0
	var uncompressed_size := 0
	var compressed_size := 0
	for blob in blobs:
		var stamp_blob := blob as CustomStampBlob
		if stamp_blob == null:
			continue
		pixel_count += stamp_blob.width * stamp_blob.height
		uncompressed_size += stamp_blob.uncompressed_size
		compressed_size += stamp_blob.compressed_indices.size()
	var text := "Custom stamps"
	if !ok:
		text += " OVER BUDGET"
	text += "\nUncompressed: %.1f / %.1f KiB" % [float(uncompressed_size) / 1024.0, float(CustomStampBlob.PLAYER_INDEXED_PIXEL_BUDGET) / 1024.0]
	text += "\nPixels: %.1f / %.1f KiB" % [float(pixel_count) / 1024.0, float(CustomStampBlob.PLAYER_INDEXED_PIXEL_BUDGET) / 1024.0]
	text += "\nCompressed: %.1f / %.1f KiB" % [float(compressed_size) / 1024.0, float(CustomStampBlob.COMPRESSED_BYTE_CAP) / 1024.0]
	budget_label.text = text
	budget_label.modulate = Color(1.0, 0.35, 0.25, 1.0) if !ok else Color.WHITE
	region_texture = _build_region_preview_texture(payload)
	atlas_preview.texture = region_texture
	atlas_preview.visible = region_texture != null
	if region_texture != null:
		var texture_size := region_texture.get_size()
		atlas_preview.custom_minimum_size = texture_size
		atlas_preview.size = texture_size
		atlas_preview.offset_left = 10.0
		atlas_preview.offset_top = -texture_size.y - 10.0
		atlas_preview.offset_right = texture_size.x + 10.0
		atlas_preview.offset_bottom = -10.0

func _build_region_preview_texture(payload: Dictionary) -> Texture2D:
	var placements: Dictionary = payload.get("placements", {})
	if placements.is_empty():
		return null
	var region_size := Vector2i.ZERO
	for placement_value in placements.values():
		if typeof(placement_value) == TYPE_DICTIONARY:
			var placement: Dictionary = placement_value
			region_size = placement.get("region_size", Vector2i.ZERO)
			break
	if region_size == Vector2i.ZERO:
		return null
	var image := Image.create(region_size.x, region_size.y, false, Image.FORMAT_RGBA8)
	image.fill(Color(0.02, 0.02, 0.025, 0.72))
	for blob in payload.get("blobs", []):
		var stamp_blob := blob as CustomStampBlob
		if stamp_blob == null or !placements.has(stamp_blob.stamp_hash):
			continue
		var stamp_image := CustomStampStore.create_preview_image(stamp_blob)
		if stamp_image == null:
			continue
		var placement: Dictionary = placements[stamp_blob.stamp_hash]
		var rect: Rect2i = placement["rect"]
		var rotated := bool(placement.get("rotated", false))
		for y in range(stamp_blob.height):
			for x in range(stamp_blob.width):
				var colour := stamp_image.get_pixel(x, y)
				if colour.a <= 0.0:
					continue
				var dest := Vector2i(rect.position.x + x, rect.position.y + y)
				if rotated:
					dest = Vector2i(rect.position.x + y, rect.position.y + stamp_blob.width - 1 - x)
				if dest.x >= 0 and dest.y >= 0 and dest.x < region_size.x and dest.y < region_size.y:
					image.set_pixel(dest.x, dest.y, colour)
	return ImageTexture.create_from_image(image)
