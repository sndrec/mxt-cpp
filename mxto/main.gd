extends Node

@onready var game_sim: GameSim = $GameSim
@onready var start_button: Button = $Control/StartButton
@onready var track_selector: OptionButton = $Control/TrackSelector
@onready var car_node_container: CarNodeContainer = $GameWorld/CarNodeContainer
@onready var cam: Camera3D = $GameWorld/Camera3D2

var tracks: Array = []

func _ready() -> void:
       _load_tracks()

func _load_tracks() -> void:
       tracks.clear()
       track_selector.clear()
       _scan_dir("res://track")
       for t in tracks:
               track_selector.add_item(t["name"])
       if tracks.size() > 0:
               track_selector.selected = 0

func _scan_dir(path: String) -> void:
       var dir := DirAccess.open(path)
       if dir == null:
               return
       dir.list_dir_begin()
       var file := dir.get_next()
       while file != "":
               if dir.current_is_dir() and !file.begins_with("."):
                       _scan_dir(path + "/" + file)
               elif file.get_extension() == "json":
                       var json_path := path + "/" + file
                       var mxt_path := json_path.get_basename() + ".mxt_track"
                       if FileAccess.file_exists(mxt_path):
                               var json_data := FileAccess.get_file_as_string(json_path)
                               var parsed := JSON.parse_string(json_data)
                               if typeof(parsed) == TYPE_DICTIONARY and parsed.has("name"):
                                       tracks.append({"name": parsed["name"], "mxt": mxt_path})
               file = dir.get_next()
       dir.list_dir_end()

func _on_start_button_pressed() -> void:
       if track_selector.selected < 0 or track_selector.selected >= tracks.size():
               return
       var info : Dictionary = tracks[track_selector.selected]
       car_node_container.instantiate_cars()
       var level_buffer := StreamPeerBuffer.new()
       level_buffer.data_array = FileAccess.get_file_as_bytes(info["mxt"])
       game_sim.car_node_container = car_node_container
       game_sim.instantiate_gamesim(level_buffer)
       $Control.visible = false

func _physics_process(delta: float) -> void:
       DebugDraw3D.scoped_config().set_no_depth_test(true)
       if game_sim.sim_started:
               game_sim.tick_gamesim()
               game_sim.render_gamesim()

func _unhandled_input(event: InputEvent) -> void:
       if game_sim.sim_started and event.is_action_pressed("ui_cancel"):
               _return_to_menu()

func _return_to_menu() -> void:
       game_sim.destroy_gamesim()
       for child in car_node_container.get_children():
               child.queue_free()
       $Control.visible = true

func _process(delta: float) -> void:
	pass
