class_name CustomStampPainterController
extends Node

signal stamp_saved(previous_hash: String, blob: CustomStampBlob)

const CustomStampBlob = preload("res://vehicle/customization/custom_stamp_blob.gd")
const CustomStampStore = preload("res://vehicle/customization/custom_stamp_store.gd")

var painter_popup: PopupPanel
var size_option: OptionButton
var canvas: TextureRect
var palette_grid: GridContainer
var colour_picker: ColorPickerButton
var clear_button: Button
var save_button: Button
var cancel_button: Button

var custom_painter_size := Vector2i(32, 32)
var custom_painter_indices := PackedByteArray()
var custom_painter_palette := PackedColorArray()
var custom_painter_texture: ImageTexture
var custom_painter_colour_index := 1
var custom_painter_drawing := false
var editing_stamp_hash := ""

func initialize(owner_ui: Control) -> void:
	painter_popup = owner_ui.get_node("CustomStampPainter")
	size_option = owner_ui.get_node("CustomStampPainter/PainterRoot/SizeRow/SizeOption")
	canvas = owner_ui.get_node("CustomStampPainter/PainterRoot/Canvas")
	palette_grid = owner_ui.get_node("CustomStampPainter/PainterRoot/PaletteGrid")
	colour_picker = owner_ui.get_node("CustomStampPainter/PainterRoot/ColourPicker")
	clear_button = owner_ui.get_node("CustomStampPainter/PainterRoot/ButtonRow/Clear")
	save_button = owner_ui.get_node("CustomStampPainter/PainterRoot/ButtonRow/Save")
	cancel_button = owner_ui.get_node("CustomStampPainter/PainterRoot/ButtonRow/Cancel")
	size_option.clear()
	for size in [8, 16, 32, 64, 128]:
		size_option.add_item("%dx%d" % [size, size], size)
	size_option.add_item("256x128", 256128)
	size_option.add_item("128x256", 128256)
	size_option.select(2)
	size_option.item_selected.connect(_on_custom_painter_size_selected)
	canvas.gui_input.connect(_on_custom_painter_canvas_input)
	colour_picker.color_changed.connect(_on_custom_painter_colour_changed)
	clear_button.pressed.connect(_on_custom_painter_clear_pressed)
	save_button.pressed.connect(_on_custom_painter_save_pressed)
	cancel_button.pressed.connect(_on_custom_painter_cancel_pressed)
	_custom_painter_reset(Vector2i(32, 32))

func open_new() -> void:
	editing_stamp_hash = ""
	_custom_painter_reset(custom_painter_size)
	painter_popup.popup_centered(Vector2i(580, 660))

func open_blob(stamp_hash: String, blob: CustomStampBlob) -> void:
	if blob == null or blob.bits_per_pixel != CustomStampBlob.BPP_CUSTOM_PALETTE:
		return
	editing_stamp_hash = stamp_hash
	_custom_painter_load_blob(blob)
	painter_popup.popup_centered(Vector2i(580, 660))

func cancel_if_editing(stamp_hash: String) -> void:
	if editing_stamp_hash == stamp_hash:
		_on_custom_painter_cancel_pressed()

func set_locked(locked: bool) -> void:
	if save_button != null:
		save_button.disabled = locked

func _on_custom_painter_size_selected(index: int) -> void:
	var id := size_option.get_item_id(index)
	match id:
		256128:
			_custom_painter_reset(Vector2i(256, 128))
		128256:
			_custom_painter_reset(Vector2i(128, 256))
		_:
			_custom_painter_reset(Vector2i(id, id))

func _custom_painter_reset(size: Vector2i) -> void:
	custom_painter_size = size
	_select_custom_painter_size_option(size)
	custom_painter_indices.resize(size.x * size.y)
	custom_painter_indices.fill(0)
	custom_painter_palette = _default_custom_painter_palette()
	custom_painter_colour_index = 1
	colour_picker.color = custom_painter_palette[custom_painter_colour_index]
	_rebuild_custom_painter_palette_buttons()
	_refresh_custom_painter_texture()

func _custom_painter_load_blob(blob: CustomStampBlob) -> void:
	if blob == null or blob.bits_per_pixel != CustomStampBlob.BPP_CUSTOM_PALETTE:
		return
	custom_painter_size = Vector2i(blob.width, blob.height)
	_select_custom_painter_size_option(custom_painter_size)
	custom_painter_indices = _custom_painter_unpack_indices(blob.decompress_indices(), blob.width * blob.height)
	custom_painter_palette = _normalized_custom_painter_palette(blob.custom_palette)
	custom_painter_colour_index = 1
	colour_picker.color = custom_painter_palette[custom_painter_colour_index]
	_rebuild_custom_painter_palette_buttons()
	_refresh_custom_painter_texture()

func _select_custom_painter_size_option(size: Vector2i) -> void:
	var target_id := size.x if size.x == size.y else int("%d%d" % [size.x, size.y])
	for i in range(size_option.item_count):
		if size_option.get_item_id(i) == target_id:
			size_option.select(i)
			return

func _custom_painter_unpack_indices(raw: PackedByteArray, pixel_count: int) -> PackedByteArray:
	var indices := PackedByteArray()
	indices.resize(pixel_count)
	for pixel_index in range(pixel_count):
		var packed := int(raw[int(pixel_index / 2)])
		if (pixel_index & 1) == 0:
			indices[pixel_index] = packed & 0x0f
		else:
			indices[pixel_index] = (packed >> 4) & 0x0f
	return indices

func _normalized_custom_painter_palette(source: PackedColorArray) -> PackedColorArray:
	var palette := PackedColorArray()
	palette.append(Color(1.0, 1.0, 1.0, 0.0))
	var start := 1 if source.size() > 0 and source[0].a <= 0.0 else 0
	for i in range(start, mini(source.size(), start + 15)):
		palette.append(source[i])
	while palette.size() < 16:
		palette.append(Color.WHITE)
	return palette

func _default_custom_painter_palette() -> PackedColorArray:
	var palette := PackedColorArray()
	palette.append(Color(1.0, 1.0, 1.0, 0.0))
	for colour in [
		Color.WHITE,
		Color.BLACK,
		Color(0.88, 0.08, 0.12, 1.0),
		Color(1.0, 0.78, 0.12, 1.0),
		Color(0.1, 0.75, 0.25, 1.0),
		Color(0.1, 0.55, 1.0, 1.0),
		Color(0.55, 0.25, 1.0, 1.0),
		Color(1.0, 0.25, 0.7, 1.0),
		Color(0.0, 0.85, 0.85, 1.0),
		Color(0.95, 0.45, 0.12, 1.0),
		Color(0.45, 0.25, 0.12, 1.0),
		Color(0.45, 0.45, 0.45, 1.0),
		Color(0.72, 0.72, 0.72, 1.0),
		Color(0.25, 0.35, 0.45, 1.0),
		Color(0.02, 0.02, 0.04, 1.0),
	]:
		palette.append(colour)
	return palette

func _rebuild_custom_painter_palette_buttons() -> void:
	for child in palette_grid.get_children():
		child.queue_free()
	for index in range(16):
		var button := Button.new()
		button.custom_minimum_size = Vector2(52.0, 28.0)
		button.text = "T" if index == 0 else str(index)
		button.modulate = Color(1.0, 1.0, 1.0, 1.0) if index == 0 else custom_painter_palette[index]
		button.disabled = false
		button.pressed.connect(_on_custom_painter_palette_pressed.bind(index))
		palette_grid.add_child(button)

func _on_custom_painter_palette_pressed(index: int) -> void:
	custom_painter_colour_index = clampi(index, 0, 15)
	if custom_painter_colour_index > 0:
		colour_picker.color = custom_painter_palette[custom_painter_colour_index]

func _on_custom_painter_colour_changed(colour: Color) -> void:
	if custom_painter_colour_index <= 0:
		return
	custom_painter_palette[custom_painter_colour_index] = Color(colour.r, colour.g, colour.b, 1.0)
	_rebuild_custom_painter_palette_buttons()
	_refresh_custom_painter_texture()

func _on_custom_painter_canvas_input(event: InputEvent) -> void:
	var mouse_button := event as InputEventMouseButton
	if mouse_button != null and mouse_button.button_index == MOUSE_BUTTON_LEFT:
		custom_painter_drawing = mouse_button.pressed
		if mouse_button.pressed:
			_custom_painter_draw_at(mouse_button.position)
			canvas.accept_event()
		return
	var motion := event as InputEventMouseMotion
	if motion != null and custom_painter_drawing:
		_custom_painter_draw_at(motion.position)
		canvas.accept_event()

func _custom_painter_draw_at(position: Vector2) -> void:
	if canvas.size.x <= 0.0 or canvas.size.y <= 0.0:
		return
	var x := clampi(int(floorf(position.x / canvas.size.x * float(custom_painter_size.x))), 0, custom_painter_size.x - 1)
	var y := clampi(int(floorf(position.y / canvas.size.y * float(custom_painter_size.y))), 0, custom_painter_size.y - 1)
	custom_painter_indices[y * custom_painter_size.x + x] = custom_painter_colour_index
	_refresh_custom_painter_texture()

func _refresh_custom_painter_texture() -> void:
	var image := Image.create(custom_painter_size.x, custom_painter_size.y, false, Image.FORMAT_RGBA8)
	for y in range(custom_painter_size.y):
		for x in range(custom_painter_size.x):
			var index := int(custom_painter_indices[y * custom_painter_size.x + x])
			image.set_pixel(x, y, custom_painter_palette[index] if index > 0 else Color(1.0, 1.0, 1.0, 0.0))
	if custom_painter_texture == null:
		custom_painter_texture = ImageTexture.create_from_image(image)
	else:
		custom_painter_texture.set_image(image)
	canvas.texture = custom_painter_texture

func _on_custom_painter_clear_pressed() -> void:
	custom_painter_indices.fill(0)
	_refresh_custom_painter_texture()

func _custom_painter_pack_indices() -> PackedByteArray:
	var raw := PackedByteArray()
	raw.resize(CustomStampBlob.index_byte_size(custom_painter_size.x, custom_painter_size.y, CustomStampBlob.BPP_CUSTOM_PALETTE))
	for pixel_index in range(custom_painter_indices.size()):
		var index := int(custom_painter_indices[pixel_index]) & 0x0f
		var byte_index := int(pixel_index / 2)
		if (pixel_index & 1) == 0:
			raw[byte_index] = (raw[byte_index] & 0xf0) | index
		else:
			raw[byte_index] = (raw[byte_index] & 0x0f) | (index << 4)
	return raw

func _on_custom_painter_save_pressed() -> void:
	var raw := _custom_painter_pack_indices()
	var blob: CustomStampBlob = CustomStampBlob.from_index_bytes(
		custom_painter_size.x,
		custom_painter_size.y,
		CustomStampBlob.BPP_CUSTOM_PALETTE,
		0,
		raw,
		custom_painter_palette)
	var validation_error := blob.validate_blob()
	if validation_error != "":
		push_warning("Painted custom stamp is invalid: %s" % validation_error)
		return
	var err := CustomStampStore.save_blob(blob)
	if err != OK:
		push_warning("Failed to save painted custom stamp: %s" % err)
		return
	var previous_hash := editing_stamp_hash
	stamp_saved.emit(previous_hash, blob)
	if !previous_hash.is_empty() and previous_hash != blob.stamp_hash:
		CustomStampStore.delete_blob(previous_hash)
	editing_stamp_hash = ""
	painter_popup.hide()

func _on_custom_painter_cancel_pressed() -> void:
	painter_popup.hide()
	editing_stamp_hash = ""

