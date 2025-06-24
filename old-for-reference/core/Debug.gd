extends Node

# Theming
var rtl_font:Font = null
var rtl_font_size := 12.0
var rtl_font_shadow_size := 4.0
var rtl_scale := Vector2( 1.0, 1.0 )
var margins := Vector4( 0.0, 3.0, 3.0, 0.0 ) # Top Left Bottom Right
var enabled := true
var log_enabled := false
var show_latest := true
var show_usage := true
var show_seconds := false
var show_low_and_high := false
var show_object_counts := true
var refresh_interval := 100

# Do Not Edit
const microseconds_to_seconds := 1.0 / 1_000_000.0
var debug_prints := PackedStringArray()
var timer_stack:Array[DT]
var next_refresh := 0
var total_time := 0.0
var file_log_path := "user://debug.log"
var debug_canvas_layer := CanvasLayer.new()
var debug_rich_text_label := RichTextLabel.new()
var fps := 0.0
var fps_easing := 0.2
var visible_green := Color( 0, 0.8, 0.2 )
var comparison_gradient := make_gradient( PackedColorArray([Color.DARK_SLATE_GRAY, Color.MEDIUM_PURPLE, Color.DEEP_SKY_BLUE, Color.LIME_GREEN]), PackedFloat32Array([0.0, 0.10, 0.50, 1.0]) )
var comparison_html_colors := html_color_lerp_cache( comparison_gradient )
var frame_time_html_colors := html_color_lerp_cache( make_gradient( PackedColorArray([Color(0.3,0.5,0.3), Color.ORANGE, Color.PURPLE, Color.RED]), PackedFloat32Array([0.0, 0.10, 0.20, 0.30]) ))
var performance_html_colors := html_color_lerp_cache( make_gradient( PackedColorArray([visible_green, Color.RED]), PackedFloat32Array([0.0, 1.0]) ))
var use_colors := false

class Average:
	var index := 0
	var sum := 0.0
	var average := 0.0
	var array := PackedFloat32Array()
	var limit := 0
	
	func _init( p_limit := 60 ) -> void:
		limit = p_limit
	
	func append( time:float ) -> float:
		if index >= array.size():
			array.append(0.0)
		sum -= array[index]
		sum += time
		array[index] = time
		average = sum / array.size()
		index = wrapi( index + 1, 0, limit )
		return average

class DT:
	var key:String
	var start := 0.0
	var time := 0.0
	var average_time := 0.0:
		get:
			return maxf( time / count, 0.0 )
	var high := 0.0
	var low := 99999999.0
	var count := 0.0
	var count_this_frame := 0
	var count_frames := 0
	var comparison_group := 0
	
	static var map := {}
	static var list:Array[DT] = []
	
	func _init( _key:String ) -> void:
		key = _key
		list.append(self)
		map[_key] = self
	
	static func find( _key:String ) -> DT:
		if not DT.map.has( _key ):
			DT.map[_key] = DT.new( _key )
		return DT.map[_key]
	
	func reset() -> void:
		start = 0
		time = 0
		high = 0
		low = 99999999
		count = 0
		count_this_frame = 0
		count_frames = 0

func start( key:String, comparison_group := -1 ) -> void:
	if not enabled: return
	var dt := DT.find(key)
	timer_stack.append(dt)
	dt.comparison_group = comparison_group
	dt.count += 1
	dt.count_this_frame += 1
	dt.start = Time.get_ticks_usec()

func stop() -> float:
	var stop_time := Time.get_ticks_usec()
	if not enabled: return 0
	var perform_time := 0.0
	if enabled and not timer_stack.is_empty():
		var dt := timer_stack[timer_stack.size()-1]
		timer_stack.pop_back()
		perform_time = stop_time - dt.start
		dt.time += perform_time
		total_time += perform_time
		dt.low =  minf( dt.low, perform_time )
		dt.high = maxf( dt.high, perform_time )
	return perform_time

var block_index := -1
func start_block( key:String, comparison_group := -1 ) -> void:
	if not enabled: return
	if block_index >= 0:
		stop()
	block_index += 1
	var dt := DT.find(key + str(block_index))
	timer_stack.append(dt)
	dt.start = Time.get_ticks_usec()
	dt.comparison_group = comparison_group
	dt.count += 1
	dt.count_this_frame += 1

func stop_block() -> float:
	if not enabled: return 0
	block_index = -1
	var perform_time := 0.0
	if enabled and not timer_stack.is_empty():
		var dt := timer_stack[timer_stack.size()-1]
		timer_stack.pop_back()
		perform_time = Time.get_ticks_usec() - dt.start
		dt.time += perform_time
		total_time += perform_time
		dt.low =  minf( dt.low, perform_time )
		dt.high = maxf( dt.high, perform_time )
	return perform_time

func record( key:String, time:int, comparison_group := -1, amount := 1 ) -> void:
	if not enabled: return
	var dt := DT.find(key)
	dt.count += amount
	dt.count_this_frame += amount
	dt.comparison_group = comparison_group
	dt.time += time
	total_time += time
	dt.low = minf( dt.low, time )
	dt.high = maxf( dt.high, time )

func benchmark( key:String, callback:Callable, amount := 1 ) -> void:
	if amount <= 0:
		start( key )
		callback.call()
		stop()
	else:
		start( key )
		for i in amount:
			callback.call()
		stop()

func count( key:String, amount:float=1.0 ) -> void:
	DT.find(key).count += amount

func print( whatever:Variant ) -> void:
	debug_prints.append( str(whatever) )

func start_logging() -> void:
	log_enabled = true

func stop_logging() -> void:
	log_enabled = false

func log( text:String ) -> void:
	if log_enabled:
		print_debug( text )

func reset() -> void:
	debug_prints.clear()
	timer_stack.clear()
	for dt:DT in DT.list:
		dt.reset()
	#reset_timer = 0

func safe_div( a:float, b:float, fallback:float ) -> float:
	if b == 0:
		return fallback
	return a / b

func get_prints() -> String:
	return "\n".join(debug_prints)

func clear_prints() -> void:
	debug_prints.clear()

func comparison( names:Array[String], callables:Array[Callable], repeats := 10, comparison_group := hash(names) ) -> void:
	var c := names.size()
	var indices := range(c)
	if DT.map.has(names[0]):
		indices.shuffle()
	var t := 0
	for i in repeats:
		for j:int in indices:
			var cc := callables[j]
			t = Time.get_ticks_usec()
			cc.call()
			t = Time.get_ticks_usec() - t
			record( names[j], t, comparison_group )

func timed_comparison( names:Array[String], callables:Array[Callable], repeats := 1, duration := 1.0/60.0, comparison_group := hash(names) ) -> void:
	var callback_count := names.size()
	var end_time := Time.get_ticks_usec() + duration * 1_000_000.0
	while Time.get_ticks_usec() < end_time:
		var adjusted_index := randi_range( 0, callback_count - 1 )
		start( names[adjusted_index], comparison_group )
		for i in repeats:
			callables[adjusted_index].call()
		stop()

var set_average := Average.new()
var cpu_average := Average.new()
var gpu_average := Average.new()

func results() -> String:
	var dt_list := DT.list
	var group_times := {}
	for dt:DT in dt_list:
		if dt.count < 1: continue
		if dt.count_frames < 1: continue
		if dt.comparison_group < 0: continue
		var time_per_call := dt.time / dt.count
		if (not group_times.has(dt.comparison_group) or group_times[dt.comparison_group] < time_per_call):
			group_times[dt.comparison_group] = time_per_call
	
	var text := ""
	var available_frame_time := 1.0 / 60.0
	for dt:DT in dt_list:
		if dt.count == 0:
			text += "[color=#633]" + dt.key + " (No info)[/color]\n"
			continue
		
		var performance_color := performance_html_colors[0]
		var seconds_per_call := (dt.time * microseconds_to_seconds) / dt.count
		var max_calls_per_frame := 0.0
		if seconds_per_call > 0.0:
			max_calls_per_frame = available_frame_time / seconds_per_call
			if max_calls_per_frame > 0.0:
				performance_color = performance_html_colors[floori(pow( 1.0 / maxf(max_calls_per_frame,1.0), 0.3 ) * (performance_html_colors.size()-1))]
		
		var comparison_formatted := ""
		if dt.comparison_group >= 0:
			var comparison_value := 0.0
			var group_max_average_time:float = group_times.get( dt.comparison_group, 0.0 )
			if group_max_average_time > 0:
				comparison_value = 1.0 / ((dt.time / dt.count) / group_max_average_time) - 1.0
			comparison_formatted = "[color=#%s]%.2f%% [/color]" % [comparison_html_colors[floori(clampf( comparison_value, 0.0, 1.0 ) * (comparison_html_colors.size()-1))],comparison_value * 100.0]
		
		var formatted_duration := ""
		if show_seconds:
			formatted_duration = "%.3fs" % (dt.time * microseconds_to_seconds)
		else:
			formatted_duration = str(int(dt.time)).lpad( 12, " ") + "us"
		
		var formatted_average_time := ""
		if dt.average_time < 10:
			formatted_average_time = "%.2f" % dt.average_time
		else:
			formatted_average_time = str(int(dt.average_time))
		
		var formatted_range := "[color=#" + performance_color + "]" + formatted_average_time.lpad( 5," ") + "u[/color]"
		if show_low_and_high:
			formatted_range = "[color=#999]" + str(int(dt.low)) + "-" + formatted_range + "-" + str(int(dt.high)) + "[/color]"
		
		text += "[color=#558][color=#08B]" + formatted_duration.lpad( 6," ") + "[/color] " \
		+ formatted_range + " " + comparison_formatted \
		+ "[color=#" + (frame_time_html_colors[floori(minf(dt.time / total_time, 1.0) * (frame_time_html_colors.size()-1))]) + "]" \
		+ ("%.1f%%" % (dt.time / total_time * 100.0)).lpad( 5," ") + "[/color] " \
		+ dt.key + "[/color] [color=#0AF]x" + str(dt.count) + "[/color]\n"
	return text

func _ready() -> void:
	debug_canvas_layer.layer = 10
	var margin := MarginContainer.new()
	margin.mouse_filter = Control.MOUSE_FILTER_IGNORE
	margin.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	if margins.length_squared() > 0:
		margin.add_theme_constant_override( "margin_up", int(margins.x) )
		margin.add_theme_constant_override( "margin_left", int(margins.y) )
		margin.add_theme_constant_override( "margin_bottom", int(margins.z) )
		margin.add_theme_constant_override( "margin_right", int(margins.w) )
	debug_rich_text_label.autowrap_mode = TextServer.AUTOWRAP_OFF
	debug_rich_text_label.clip_contents = false
	debug_rich_text_label.fit_content = true
	debug_rich_text_label.mouse_filter = Control.MOUSE_FILTER_IGNORE
	debug_rich_text_label.size_flags_vertical = Control.SIZE_SHRINK_END
	debug_rich_text_label.bbcode_enabled = true
	if rtl_font != null:
		debug_rich_text_label.add_theme_font_override( "normal_font", rtl_font )
	debug_rich_text_label.add_theme_font_size_override( "normal_font_size", int(rtl_font_size) )
	if rtl_font_shadow_size > 0:
		debug_rich_text_label.add_theme_color_override( "font_outline_color", Color.BLACK )
		debug_rich_text_label.add_theme_constant_override( "outline_size", int(rtl_font_shadow_size) )
	debug_canvas_layer.add_child(margin)
	margin.add_child(debug_rich_text_label)
	get_tree().root.add_child.call_deferred(debug_canvas_layer)
	RenderingServer.viewport_set_measure_render_time(get_viewport().get_viewport_rid(),true)

func _physics_process(_delta: float) -> void:
	if not enabled: return
	for dt:DT in DT.list:
		dt.count_this_frame = 0
		dt.count_frames += 1

func _process( delta: float ) -> void:
	if not enabled:
		debug_rich_text_label.text = ""
		return
	fps = lerpf( fps, 1.0 / maxf(delta,0.0001), fps_easing )
	if Time.get_ticks_msec() >= next_refresh:
		next_refresh = Time.get_ticks_msec() + refresh_interval
		var text := ""
		if enabled:
			text += "\n".join(debug_prints) + "\n" + results()
		if show_usage:
			var viewport_rid := get_viewport().get_viewport_rid()
			var last_cpu_time := RenderingServer.viewport_get_measured_render_time_cpu(viewport_rid) * 1000
			var last_set_time := RenderingServer.get_frame_setup_time_cpu() * 1000
			var last_gpu_time := RenderingServer.viewport_get_measured_render_time_gpu(viewport_rid) * 1000
			text += "[color=#558]FPS [color=#0AF]" + ("%.1f" % fps).lpad( 5," ") + "[/color]" \
			+ " SET [color=#0AF]" + str(int(set_average.append(last_set_time))).lpad( 4," ") + "u[/color]" \
			+ " CPU [color=#0AF]" + str(int(cpu_average.append(last_cpu_time))).lpad( 4," ") + "u[/color]" \
			+ " GPU [color=#0AF]" + str(int(gpu_average.append(last_gpu_time))).lpad( 4," ") + "u[/color]" \
			+ " MEM [color=#0AF]" + str(OS.get_static_memory_usage()) +"b[/color][/color]"
		
		if show_object_counts:
			text += "\n[color=#558]" \
			+ "OBJ [color=#0AF]" + str(Performance.get_monitor(Performance.OBJECT_COUNT)).lpad( 5," ") + "[/color]" \
			+ " RES [color=#0AF]" + str(Performance.get_monitor(Performance.OBJECT_RESOURCE_COUNT)).lpad( 5," ") + "[/color]" \
			+ " NOD [color=#0AF]" + str(Performance.get_monitor(Performance.OBJECT_NODE_COUNT)).lpad( 5," ") + "[/color]" \
			+ " ORP [color=#0AF]" + str(Performance.get_monitor(Performance.OBJECT_ORPHAN_NODE_COUNT)).lpad( 5," ") +"[/color]" \
			+ "[/color]"
		debug_rich_text_label.text = text
	clear_prints()

func make_gradient( colors:PackedColorArray, offsets:PackedFloat32Array ) -> Gradient:
	var gradient := Gradient.new()
	gradient.colors = colors
	gradient.offsets = offsets
	return gradient

func html_color_lerp_cache( gradient:Gradient, steps := 100 ) -> PackedStringArray:
	var strings := PackedStringArray()
	var step_delta := 1.0 / (steps - 1.0)
	for i in steps:
		strings.append( gradient.sample( i * step_delta ).to_html() )
	return strings
