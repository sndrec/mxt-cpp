class_name WorkshopBrowser extends VBoxContainer

@onready var status_label: Label = $Status
@onready var item_list: ItemList = $Items
@onready var details: RichTextLabel = $Details
@onready var refresh_button: Button = $Actions/Refresh
@onready var download_button: Button = $Actions/Download
@onready var unsubscribe_button: Button = $Actions/Unsubscribe
@onready var open_button: Button = $Actions/OpenPage

var game_manager: GameManager
var items: Array = []

func _ready() -> void:
	var ancestor := get_parent()
	while ancestor != null and !(ancestor is GameManager):
		ancestor = ancestor.get_parent()
	game_manager = ancestor as GameManager
	refresh_button.pressed.connect(_refresh)
	download_button.pressed.connect(_download_selected)
	unsubscribe_button.pressed.connect(_unsubscribe_selected)
	open_button.pressed.connect(_open_selected)
	item_list.item_selected.connect(func(_index): _refresh_details())
	if game_manager != null:
		game_manager.workshop_content_changed.connect(_show_items)
	call_deferred("_refresh")

func _refresh() -> void:
	if game_manager == null or game_manager.steam_service == null:
		status_label.text = "Steam service is unavailable"
		return
	if !game_manager.steam_service.is_initialized():
		status_label.text = game_manager.steam_service.get_status_message()
		_show_items([])
		return
	status_label.text = "Refreshing subscribed Workshop items..."
	game_manager.refresh_workshop_content()

func _show_items(new_items: Array) -> void:
	items = new_items.duplicate(true)
	item_list.clear()
	for value in items:
		var item: Dictionary = value
		var record: Dictionary = item.get("record", {})
		var title := String(record.get("title", "Workshop item %d" % int(item.get("published_file_id", 0))))
		item_list.add_item("%s  [%s]" % [title, String(item.get("status", "unknown")).replace("_", " ")])
	status_label.text = "%d subscribed item%s" % [items.size(), "" if items.size() == 1 else "s"]
	if !items.is_empty():
		item_list.select(0)
	_refresh_details()

func _selected_item() -> Dictionary:
	var selected := item_list.get_selected_items()
	if selected.is_empty() or selected[0] < 0 or selected[0] >= items.size():
		return {}
	return items[selected[0]]

func _refresh_details() -> void:
	var item := _selected_item()
	if item.is_empty():
		details.text = "No subscribed Workshop content."
		download_button.disabled = true
		unsubscribe_button.disabled = true
		open_button.disabled = true
		return
	var record: Dictionary = item.get("record", {})
	var lines: Array[String] = [
		"[b]Published File ID:[/b] %d" % int(item.get("published_file_id", 0)),
		"[b]Status:[/b] %s" % String(item.get("status", "unknown")).replace("_", " ").capitalize(),
	]
	if !record.is_empty():
		lines.append("[b]Type:[/b] %s" % String(record.get("content_type", "")).capitalize())
		lines.append("[b]Package:[/b] %s" % String(record.get("package_digest", "")))
		lines.append("[b]Gameplay:[/b] %s" % String(record.get("gameplay_digest", "")))
	if item.has("install_path"):
		lines.append("[b]Install path:[/b] %s" % String(item["install_path"]))
	for error in item.get("errors", []):
		lines.append("[color=#ff6961]ERROR: %s[/color]" % error)
	details.text = "\n".join(lines)
	download_button.disabled = String(item.get("status", "")) == "ready"
	unsubscribe_button.disabled = int(item.get("published_file_id", 0)) <= 0
	open_button.disabled = int(item.get("published_file_id", 0)) <= 0

func _download_selected() -> void:
	var item := _selected_item()
	if !item.is_empty() and game_manager.steam_service.download_workshop_item(int(item.get("published_file_id", 0)), true):
		status_label.text = "Workshop download requested..."

func _unsubscribe_selected() -> void:
	var item := _selected_item()
	if !item.is_empty() and game_manager.steam_service.unsubscribe_workshop_item(int(item.get("published_file_id", 0))):
		status_label.text = "Workshop unsubscribe requested..."

func _open_selected() -> void:
	var item := _selected_item()
	if !item.is_empty():
		game_manager.steam_service.open_workshop_item_page(int(item.get("published_file_id", 0)))
