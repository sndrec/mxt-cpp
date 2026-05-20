extends SceneTree

const HUD_SCENE := "res://ui/race_hud.tscn"

func _init() -> void:
	var hud: RaceHud = load(HUD_SCENE).instantiate()
	root.add_child(hud)
	await process_frame
	if hud.sticker_pool.size() != hud.STICKER_POOL_SIZE:
		push_error("race HUD sticker pool size mismatch: %d" % hud.sticker_pool.size())
		quit(1)
		return
	for slot in hud.sticker_pool.size():
		var sticker_node := hud.sticker_pool[slot]
		if sticker_node == null or !is_instance_valid(sticker_node):
			push_error("race HUD sticker pool slot %d is invalid" % slot)
			quit(1)
			return
		var xray := sticker_node.get_node_or_null("XRay") as Sprite3D
		var normal := sticker_node.get_node_or_null("Normal") as Sprite3D
		if xray == null or normal == null:
			push_error("race HUD sticker pool slot %d is missing billboard sprites" % slot)
			quit(1)
			return
		if !xray.no_depth_test or normal.no_depth_test:
			push_error("race HUD sticker depth modes are incorrect in slot %d" % slot)
			quit(1)
			return
	if hud.stickers == null or hud.stickers.stickers.is_empty():
		push_error("race HUD sticker smoke has no sticker textures")
		quit(1)
		return
	var expected_axis_size := hud.STICKER_BASE_PIXEL_SIZE * hud.STICKER_BASE_TEXTURE_PIXELS
	for i in range(hud.stickers.stickers.size()):
		var texture := hud.stickers.stickers[i] as Texture2D
		if texture == null:
			continue
		var texture_size := texture.get_size()
		var max_axis := maxf(texture_size.x, texture_size.y)
		var pixel_size := float(hud.call("_sticker_pixel_size_for_texture", texture, 1.0))
		var axis_size: float = pixel_size * max_axis
		if absf(axis_size - expected_axis_size) > 0.0001:
			push_error("sticker %d physical size mismatch: %f expected %f" % [i, axis_size, expected_axis_size])
			quit(1)
			return
	print("MXT_RACE_HUD_STICKER_POOL_SMOKE slots=", hud.sticker_pool.size())
	quit(0)
