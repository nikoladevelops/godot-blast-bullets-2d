class_name BenchmarkUI
extends CanvasLayer

# The default health that the player begins with
@export var default_player_health:float
# The player health bar
@onready var player_health_bar:HealthBarComponent = $AlwaysVisibleView/HealthBar

## Bullet Settings Related
# Displays all object pool related settings
@onready var object_pool_settings_view:Control = $ObjectPoolSettingsView
# Displays all bullet related settings
@onready var bullet_settings_view:Control = $BulletSettingsView
# Displays more settings for the bullets
@onready var more_settings_view:Control = $MoreSettingsView
# Displays settings only related to godot area2ds'
@onready var godot_area2d_bullets_view:Control = $GodotArea2DBulletsView

@onready var bullet_settings_btn:Button = $AlwaysVisibleView/SettingsUIManagerVBoxContainer/BulletSettingsBtn
@onready var object_pool_settings_btn:Button = $AlwaysVisibleView/SettingsUIManagerVBoxContainer/ObjectPoolSettingsBtn
@onready var more_settings_btn:Button = $AlwaysVisibleView/SettingsUIManagerVBoxContainer/MoreSettingsBtn

# Saves the last view the user switched to
@onready var last_visible_ui_setting_view:Control = $BulletSettingsView
##

# The label that displays the current fps
@onready var current_fps_label:Label = $TopRightContainer/CurrentFPS
# The label that displays the lowest recorded fps
@onready var low_fps_label:Label = $TopRightContainer/LowFPS
# The button responsible for toggling whether the tracking of the fps is enabled
@onready var track_fps_btn:Button = $TopRightContainer/TrackFPSBtn
# If you check this checkbox then the monitorable property will be set to true, and all static bodies will now be detected by the bullets
@onready var enable_monitorable_checkbox:CheckBox = $BulletSettingsView/VBoxContainer3/EnableMonitorableCheckBox
# If you check this checkbox then the debugger will be enabled and you will be able to see the movement of the collision shapes of the bullets
@onready var enable_debugger_checkbox:CheckBox = $BulletSettingsView/VBoxContainer/VBoxContainer/EnableDebuggerCheckBox

## Color Picker
# Responsible for picking a color for the block bullets debugger
@onready var block_debugger_color_picker:ColorPicker = $BlockDebuggerColorPicker
# Responsible for picking a color for the directional bullets debugger
@onready var directional_debugger_color_picker:ColorPicker = $DirectionalDebuggerColorPicker
# Responsible for showing/hiding the block debugger color picker
@onready var show_block_debugger_color_picker_btn:Button = $BulletSettingsView/VBoxContainer/BlockDebuggerVBoxContainer2/ShowBlockDebuggerColorPickerBtn
# Responsible for showing/hiding the directional debugger color picker
@onready var show_directional_debugger_color_picker_btn:Button = $BulletSettingsView/VBoxContainer/DirectionalDebuggerVBoxContainer/ShowDirectionalDebuggerColorPickerBtn
# Responsible for closing the currently shown color picker
@onready var close_color_picker_btn:Button = $CloseCurrentColorPickerBtn
var last_selected_color_picker:ColorPicker = null
##

# If you check this checkbox then the bullets will always keep their texture rotation, no matter the direction they travel in
@onready var is_texture_rotation_permanent_checkbox:CheckBox = $BulletSettingsView/VBoxContainer3/IsTextureRotationPermanentCheckBox
# The view that holds all buttons responsible for setting the amount of bullets the player spawns when shooting
@onready var select_amount_bullets_view:SelectBtnView = $AlwaysVisibleView/SelectAmountBulletsBtnView
# The view that holds all buttons responsible for setting the amount of multimeshes to be freed / object pool settings related
@onready var select_amount_multi_meshes_view:SelectBtnView = $ObjectPoolSettingsView/SelectAmountMultiMeshesBtnView
# Whether the physics shapes get rotated when rotation data is provided
@onready var rotate_physics_shapes_checkbox:CheckBox = $BulletSettingsView/VBoxContainer/VBoxContainer/RotatePhysicsShapesCheckBox
# Will adjust the direction of the bullets based on their rotation data if they have any
@onready var adjust_direction_based_on_rotation_checkbox:CheckBox = $BulletSettingsView/VBoxContainer/VBoxContainer/AdjustDirectionBasedOnRotationCheckBox

# The save button used to save bullet data
@onready var save_btn:Button = $TopRightContainer/SaveBtn
# The load button used to load bullet data
@onready var load_btn:Button = $TopRightContainer/LoadBtn

## Bullet On Bullet Collision
@onready var dont_collide_btn:Button = $BulletSettingsView/BulletCollisionVboxContainer/DontCollideBtn
@onready var destroy_player_bullets_btn:Button = $BulletSettingsView/BulletCollisionVboxContainer/DestroyPlayerBulletsBtn
@onready var destroy_enemy_bullets_btn:Button = $BulletSettingsView/BulletCollisionVboxContainer/DestroyEnemyBulletsBtn
@onready var destroy_both_btn:Button = $BulletSettingsView/BulletCollisionVboxContainer/DestroyBothBtn
##

## Bullet Type
# Button responsible for switching the type of bullets that are being spawned
@onready var directional_bullets_btn:Button = $AlwaysVisibleView/BulletTypeContainer/DirectionalBtn
# Button responsible for switching the type of bullets that are being spawned
@onready var block_bullets_btn:Button = $AlwaysVisibleView/BulletTypeContainer/BlockBulletsBtn
# Button responsible for switching the type of bullets that are being spawned
@onready var godot_area2d_bullets_btn:Button = $AlwaysVisibleView/BulletTypeContainer/GodotArea2DBulletsBtn
##

# Switch buttons
@onready var switch_bullet_texture_btn:SwitchButton = $BulletSettingsView/VBoxContainer5/SwitchBulletTextureBtn
@onready var switch_attachment_scene_btn:SwitchButton = $BulletSettingsView/VBoxContainer5/SwitchAttachmentBtn
@onready var generate_speed_btn:SwitchButton = $BulletSettingsView/VBoxContainer4/GenerateSpeedBtn
@onready var generate_rotation_btn:SwitchButton = $BulletSettingsView/VBoxContainer4/GenerateRotationBtn
@onready var switch_material_btn:SwitchButton = $BulletSettingsView/VBoxContainer5/SwitchMaterialBtn

# Note when refactoring this button has dependencies in other scripts
@onready var disable_or_enable_factory_btn:SwitchButton = $BulletSettingsView/VBoxContainer2/DisableOrEnableFactoryBtn
#
@onready var pool_attachments_after_free_checkbox:CheckBox = $BulletSettingsView/VBoxContainer3/PoolAttachmentsAfterFreeCheckBox

# Attachment pooling related
@onready var switch_bullet_attachment_id_btn:SwitchButton = $ObjectPoolSettingsView/AttachmentPoolRelated/VBoxContainer/SwitchBulletAttachmentIdBtn
@onready var select_amount_attachments_btn_view:SelectBtnView =$ObjectPoolSettingsView/AttachmentPoolRelated/SelectAmountAttachmentsBtnView
#

# Change UI Settings View
@onready var change_settings_ui_select_btn_view:SelectBtnView = $AlwaysVisibleView/ChangeSettingsUISelectBtnView
#

## More Settings
# Responsible for setting physics interpolation ON/OFF (both the engine setting and the BulletFactory2D setting)
@onready var physics_interpolation_check_box:CheckBox = $MoreSettingsView/HBoxContainer/PhysicsInterpolationCheckBox
# Responsible for setting VSync ON/OFF
@onready var enable_v_sync_check_box:CheckBox = $MoreSettingsView/HBoxContainer/VSyncCheckBox
# Responsible for setting whether the grid should rotate with the marker
@onready var rotate_grid_with_marker_check_box:CheckBox = $MoreSettingsView/VBoxContainer2/RotateGridWithMarkerCheckBox
# Responsible for setting random local rotation when spawning bullets
@onready var random_local_rotation_check_box:CheckBox = $MoreSettingsView/VBoxContainer2/RandomLocalRotationCheckBox
# Responsible for setting random global rotation when spawning bullets
@onready var random_global_rotation_check_box:CheckBox = $MoreSettingsView/VBoxContainer2/RandomGlobalRotationCheckBox
##

# Whether the attachments should go to the object pool after freeing all active bullets in the factory
var should_pool_attachments_after_free_active_bullets:bool = false

# The theme used for disabled buttons
var disabled_btn_theme:Theme = preload("res://shared/ui/btn_disabled_theme.tres")
# The theme used for enabled buttons
var enabled_btn_theme:Theme = preload("res://shared/ui/btn_enabled_theme.tres")
# Boolean responsible for keeping track of whether the tracking of the fps is enabled/disabled
var is_tracking_fps:bool = false;

# Keeps track of the lowest ever recorded fps
var lowest_fps:int = 100000000

func _ready() -> void:
	player_health_bar.set_up(default_player_health);
	player_health_bar.health = default_player_health
	
	switch_bullet_texture_btn.switch_btn_pressed.connect(func(_option:String, option_index:int):
		BENCHMARK_GLOBALS.PLAYER_DATA_NODE.switch_bullet_texture(option_index)
	)
	
	switch_attachment_scene_btn.switch_btn_pressed.connect(func(_option:String, option_index:int):
		BENCHMARK_GLOBALS.PLAYER_DATA_NODE.switch_attachment_scn(option_index)
	)
	
	generate_speed_btn.switch_btn_pressed.connect(func(_option:String, option_index:int):
		BENCHMARK_GLOBALS.PLAYER_DATA_NODE.generate_bullet_speed_data(option_index)
	)
	
	generate_rotation_btn.switch_btn_pressed.connect(func(_option:String, option_index:int):
		BENCHMARK_GLOBALS.PLAYER_DATA_NODE.generate_bullet_rotation_data(option_index)
	)
	
	disable_or_enable_factory_btn.switch_btn_pressed.connect(func(_option:String, option_index:int):
		match option_index:
			0:
				BENCHMARK_GLOBALS.FACTORY.enable_bullet_processing()
			1:
				BENCHMARK_GLOBALS.FACTORY.disable_bullet_processing()
	)
	
	switch_material_btn.switch_btn_pressed.connect(func(_option:String, option_index:int):
		BENCHMARK_GLOBALS.PLAYER_DATA_NODE.switch_material(option_index)
	)
	

# The whole function is used to track the fps if tracking is enabled
func _process(_delta)->void:
	if is_tracking_fps:
		var currentFps:int = (int)(Engine.get_frames_per_second())
		if currentFps < lowest_fps:
			lowest_fps = currentFps
			low_fps_label.text = "Lowest: " + str(lowest_fps)
		current_fps_label.text = "FPS: " + str(currentFps)

# Starts/stops tracking of fps when button is pressed
func _on_track_fps_button_pressed()->void:
	if(is_tracking_fps):
		track_fps_btn.theme = disabled_btn_theme
		track_fps_btn.text = "Not Tracking FPS"
	else:
		track_fps_btn.theme = enabled_btn_theme
		track_fps_btn.text = "Tracking FPS..."
		
	is_tracking_fps = !is_tracking_fps
	lowest_fps=100000000

func _on_enable_debugger_check_box_pressed() -> void:
	BENCHMARK_GLOBALS.FACTORY.is_debugger_enabled = enable_debugger_checkbox.button_pressed

func _on_close_current_color_picker_btn_pressed() -> void:
	set_color_picker_visible(last_selected_color_picker, false)

# Changes a color picker's visibility by ensuring that it's the only one currently on screen if should_be_visible is set to true
func set_color_picker_visible(chosen_color_picker:ColorPicker, should_be_visible:bool)->void:
	if chosen_color_picker == null:
		return
	
	# Reset visibility
	directional_debugger_color_picker.visible = false
	block_debugger_color_picker.visible = false
	
	# Disable/enable show buttons based on whether the chosen color picker should be visible or not
	show_directional_debugger_color_picker_btn.disabled = should_be_visible
	show_block_debugger_color_picker_btn.disabled = should_be_visible
	
	# Change visibility of the chosen one
	chosen_color_picker.visible = should_be_visible
	
	# Hide / show the close button
	close_color_picker_btn.visible = should_be_visible
	
	
func _on_show_directional_debugger_color_picker_btn_pressed() -> void:
	set_color_picker_visible(directional_debugger_color_picker, true)
	last_selected_color_picker = directional_debugger_color_picker

func _on_show_block_debugger_color_picker_btn_pressed() -> void:
	set_color_picker_visible(block_debugger_color_picker, true)
	last_selected_color_picker = block_debugger_color_picker

func change_directional_debugger_btn_color(color:Color)->void:
	var stylebox:StyleBoxFlat = show_directional_debugger_color_picker_btn.get_theme_stylebox("normal")
	stylebox.bg_color = color
	
	show_directional_debugger_color_picker_btn.add_theme_stylebox_override("disabled", stylebox)
	

func change_block_debugger_btn_color(color:Color)->void:
	var stylebox:StyleBoxFlat = show_block_debugger_color_picker_btn.get_theme_stylebox("normal")
	stylebox.bg_color = color
	
	show_block_debugger_color_picker_btn.add_theme_stylebox_override("disabled", stylebox)
	


func _on_block_debugger_color_picker_color_changed(color: Color) -> void:
	change_block_debugger_btn_color(color)
	BENCHMARK_GLOBALS.FACTORY.block_bullets_debugger_color = color

func _on_directional_debugger_color_picker_color_changed(color: Color) -> void:
	change_directional_debugger_btn_color(color)
	BENCHMARK_GLOBALS.FACTORY.directional_bullets_debugger_color = color

func _on_enable_monitorable_check_box_pressed() -> void:
	BENCHMARK_GLOBALS.PLAYER_DATA_NODE.set_monitorable_enabled(enable_monitorable_checkbox.button_pressed)

func _on_is_texture_rotation_permanent_check_box_pressed() -> void:
	BENCHMARK_GLOBALS.PLAYER_DATA_NODE.set_bullet_is_texture_rotation_permanent(is_texture_rotation_permanent_checkbox.button_pressed)

func _on_fill_up_player_health_btn_pressed() -> void:
	BENCHMARK_GLOBALS.PLAYER_HEALTH_BAR.health = BENCHMARK_GLOBALS.PLAYER_HEALTH_BAR.max_value
	
func _on_select_amount_bullets_btn_view_new_btn_selected(new_selected_btn: Button) -> void:
	var new_bullets_amount:int = new_selected_btn.text.to_int()
	BENCHMARK_GLOBALS.PLAYER_DATA_NODE.set_new_bullets_spawn_amount(new_bullets_amount)

func _on_select_fire_timer_btn_view_new_btn_selected(new_selected_btn: Button) -> void:
	var new_fire_timer_wait_time:float = new_selected_btn.text.to_float()
	BENCHMARK_GLOBALS.PLAYER.change_fire_timer_cooldown(new_fire_timer_wait_time)

## Freeing object pool logic

func _on_free_multi_mesh_directional_pool_btn_pressed() -> void:
	var amount_bullets:int = select_amount_bullets_view.get_selected_btn.text.to_int()
	
	BENCHMARK_GLOBALS.FACTORY.free_bullets_pool(BulletFactory2D.DIRECTIONAL_BULLETS, amount_bullets)

func _on_free_multi_mesh_block_bullets_pool_btn_pressed() -> void:
	var amount_bullets:int = select_amount_bullets_view.get_selected_btn.text.to_int()
	
	BENCHMARK_GLOBALS.FACTORY.free_bullets_pool(BulletFactory2D.BLOCK_BULLETS, amount_bullets)

func _on_free_all_bullet_pools_btn_pressed() -> void:
	BENCHMARK_GLOBALS.FACTORY.free_bullets_pool(BulletFactory2D.DIRECTIONAL_BULLETS)
	BENCHMARK_GLOBALS.FACTORY.free_bullets_pool(BulletFactory2D.BLOCK_BULLETS)

##

## Populate object pool logic

func _on_populate_multi_mesh_directional_pool_btn_pressed() -> void:
	var amount_multi_meshes:int = select_amount_multi_meshes_view.get_selected_btn.text.to_int()
	var amount_bullets:int = select_amount_bullets_view.get_selected_btn.text.to_int()
	
	BENCHMARK_GLOBALS.FACTORY.populate_bullets_pool(BulletFactory2D.DIRECTIONAL_BULLETS,amount_multi_meshes,amount_bullets)
	

func _on_populate_multi_mesh_block_pool_btn_pressed() -> void:
	var amount_multi_meshes:int = select_amount_multi_meshes_view.get_selected_btn.text.to_int()
	var amount_bullets:int = select_amount_bullets_view.get_selected_btn.text.to_int()
	
	BENCHMARK_GLOBALS.FACTORY.populate_bullets_pool(BulletFactory2D.BLOCK_BULLETS,amount_multi_meshes,amount_bullets)
	

func _on_select_bullet_damage_view_new_btn_selected(new_selected_btn: Button) -> void:
	var new_damage:int = new_selected_btn.text.to_int()
	BENCHMARK_GLOBALS.PLAYER_DATA_NODE.set_new_damage_value(new_damage)


func _on_select_bullet_collision_view_new_btn_selected(new_selected_btn: Button) -> void:
	var new_player_bullets_collision_mask:Array[int]
	var new_player_bullets_monitorable:bool
	
	var new_enemy_bullets_collision_mask:Array[int]
	
	match new_selected_btn:
		dont_collide_btn:
			new_player_bullets_collision_mask = [3]
			new_player_bullets_monitorable=false
			
			new_enemy_bullets_collision_mask = [1, 3]
		destroy_player_bullets_btn:
			new_player_bullets_collision_mask = [3, 4]
			new_player_bullets_monitorable=false
			
			new_enemy_bullets_collision_mask = [1, 3]
		destroy_enemy_bullets_btn:
			new_player_bullets_collision_mask = [3]
			new_player_bullets_monitorable=true
			
			new_enemy_bullets_collision_mask = [1, 2, 3]
		destroy_both_btn:
			new_player_bullets_collision_mask = [3, 4]
			new_player_bullets_monitorable=true
			
			new_enemy_bullets_collision_mask = [1, 2, 3]

	BENCHMARK_GLOBALS.PLAYER_DATA_NODE.set_bullet_collision_mask(new_player_bullets_collision_mask)
	BENCHMARK_GLOBALS.PLAYER_DATA_NODE.set_monitorable_enabled(new_player_bullets_monitorable)
	enable_monitorable_checkbox.button_pressed = new_player_bullets_monitorable # UI should express the change as well
	
	for spawner in BENCHMARK_GLOBALS.ALL_ENEMY_SPAWNERS:
		spawner.set_enemy_default_bullet_collision_mask(new_enemy_bullets_collision_mask)


func _on_select_bullet_type_view_new_btn_selected(new_selected_btn: Button) -> void:
	match new_selected_btn:
		directional_bullets_btn:
			BENCHMARK_GLOBALS.BULLET_TYPE_TO_SPAWN = BENCHMARK_GLOBALS.BulletType.MultiMeshDirectional
			
			set_blast_bullets2d_ui_settings_visible(last_visible_ui_setting_view)
		block_bullets_btn:
			BENCHMARK_GLOBALS.BULLET_TYPE_TO_SPAWN = BENCHMARK_GLOBALS.BulletType.MultiMeshBlock
			
			set_blast_bullets2d_ui_settings_visible(last_visible_ui_setting_view)
		godot_area2d_bullets_btn:
			BENCHMARK_GLOBALS.BULLET_TYPE_TO_SPAWN = BENCHMARK_GLOBALS.BulletType.GodotArea2D
			
			set_blast_bullets2d_ui_settings_visible(null)

## If you pass null, it will hide all blast bullets 2d related settings
## If you pass a settings view then it will make that view visible while hiding the rest
func set_blast_bullets2d_ui_settings_visible(view_to_open:Control)->void:
	## Hide all setting views
	bullet_settings_view.visible = false
	object_pool_settings_view.visible = false
	more_settings_view.visible = false
	godot_area2d_bullets_view.visible = false
	
	## Hide last selected color picker just in case
	set_color_picker_visible(last_selected_color_picker, false)
	
	## If the view_to_open is null it means the user wants to hide ALL bullet settings from the UI
	if view_to_open == null:
		## So hide all buttons that are meant for switching between settings
		bullet_settings_btn.visible = false
		object_pool_settings_btn.visible = false
		more_settings_btn.visible = false
		
		# Show the godot area2d bullets view instead to display a messsage to the user
		godot_area2d_bullets_view.visible = true
		return
	
	## Otherwise ensure the buttons that are meant for switching between settings are actually visible
	bullet_settings_btn.visible = true
	object_pool_settings_btn.visible = true
	more_settings_btn.visible = true
	godot_area2d_bullets_view.visible = false # Always hide this
	
	## Show the view you want to switch to
	view_to_open.visible = true

func _on_select_collision_shape_size_view_new_btn_selected(new_selected_btn: Button) -> void:
	var split_str:PackedStringArray = new_selected_btn.text.split(';');
	
	var new_vec_size:Vector2 = Vector2(split_str[0].to_int(), split_str[1].to_int())
	
	BENCHMARK_GLOBALS.PLAYER_DATA_NODE.set_collision_shape_size(new_vec_size)


func _on_select_collision_shape_offset_view_new_btn_selected(new_selected_btn: Button) -> void:
	var split_str:PackedStringArray = new_selected_btn.text.split(';');
	
	var new_vec_offset:Vector2 = Vector2(split_str[0].to_int(), split_str[1].to_int())
	
	BENCHMARK_GLOBALS.PLAYER_DATA_NODE.set_collision_shape_offset(new_vec_offset)


func _on_select_bullet_attachment_offset_view_new_btn_selected(new_selected_btn: Button) -> void:
	var split_str:PackedStringArray = new_selected_btn.text.split(';');
	
	var new_vec_offset:Vector2 = Vector2(split_str[0].to_int(), split_str[1].to_int())
	
	BENCHMARK_GLOBALS.PLAYER_DATA_NODE.set_bullet_attachment_offset(new_vec_offset)


func _on_select_texture_rotation_view_new_btn_selected(new_selected_btn: Button) -> void:
	var degrees:int = new_selected_btn.text.to_int()
	
	BENCHMARK_GLOBALS.PLAYER_DATA_NODE.set_bullet_texture_rotation(degrees)
	
	


func _on_select_bullet_lifetime_view_new_btn_selected(new_selected_btn: Button) -> void:
	var new_life_time:float = new_selected_btn.text.to_float()

	BENCHMARK_GLOBALS.PLAYER_DATA_NODE.set_bullet_lifetime(new_life_time)


func _on_reset_factory_btn_pressed() -> void:
	BENCHMARK_GLOBALS.FACTORY.reset()
	
	disable_or_enable_factory_btn.switch_to_option_index(0)
	

func _on_free_active_bullets_btn_pressed() -> void:
	BENCHMARK_GLOBALS.FACTORY.free_active_bullets(should_pool_attachments_after_free_active_bullets)
	
	# Also free all Godot Area2D bullets
	for child in BENCHMARK_GLOBALS.ALL_GODOT_AREA2D_BULLETS_CONTAINER.get_children():
		child.queue_free()
	
	disable_or_enable_factory_btn.switch_to_option_index(0)


func _on_pool_attachments_after_free_check_box_pressed() -> void:
	should_pool_attachments_after_free_active_bullets = pool_attachments_after_free_checkbox.button_pressed


func _on_free_specific_attachment_pool_btn_pressed() -> void:
	var attachment_id:int = switch_bullet_attachment_id_btn.current_selected_option_index+1 # because id 1 is the first attachment and id 2 is the second attachment but ordering of the options starts from 0 so all indexes are behind with -1
	BENCHMARK_GLOBALS.FACTORY.free_attachments_pool(attachment_id)
	
	disable_or_enable_factory_btn.switch_to_option_index(0)

func _on_free_all_attachment_pools_btn_pressed() -> void:
	BENCHMARK_GLOBALS.FACTORY.free_attachments_pool()
	disable_or_enable_factory_btn.switch_to_option_index(0)

func _on_populate_attachments_pool_btn_pressed() -> void:
	var attachment_id:int = switch_bullet_attachment_id_btn.current_selected_option_index+1 # because id 1 is the first attachment and id 2 is the second attachment but ordering of the options starts from 0 so all indexes are behind with -1
	var amount_attachments_to_pool:int = select_amount_attachments_btn_view.get_selected_btn.text.to_int()
	
	var attachment_packed_scn:PackedScene = BENCHMARK_GLOBALS.PLAYER_DATA_NODE.get_attachment_scn_based_on_attachment_id(attachment_id)
	
	BENCHMARK_GLOBALS.FACTORY.populate_attachments_pool(attachment_packed_scn, amount_attachments_to_pool)
	

func _on_rotate_physics_shapes_check_box_pressed() -> void:
	BENCHMARK_GLOBALS.PLAYER_DATA_NODE.set_rotate_physics_shapes(rotate_physics_shapes_checkbox.button_pressed)


func _on_select_texture_size_btn_view_new_btn_selected(new_selected_btn: Button) -> void:
	var split_str:PackedStringArray = new_selected_btn.text.split(';');
	
	var new_vec_texture_size:Vector2 = Vector2(split_str[0].to_int(), split_str[1].to_int())
	
	BENCHMARK_GLOBALS.PLAYER_DATA_NODE.set_new_texture_size(new_vec_texture_size)


func _on_select_z_index_btn_view_new_btn_selected(new_selected_btn: Button) -> void:
	var new_z_index:int = new_selected_btn.text.to_int()
	BENCHMARK_GLOBALS.PLAYER_DATA_NODE.set_bullets_z_index(new_z_index)


func _on_adjust_direction_based_on_rotation_check_box_pressed() -> void:
	var should_adjust_direction:bool = adjust_direction_based_on_rotation_checkbox.button_pressed
	BENCHMARK_GLOBALS.PLAYER_DATA_NODE.set_adjust_direction_based_on_rotation(should_adjust_direction)


func _on_change_settings_ui_select_btn_view_new_btn_selected(_new_selected_btn: Button) -> void:
	if last_selected_color_picker != null:
		set_color_picker_visible(last_selected_color_picker, false)
	
	match change_settings_ui_select_btn_view.selected_btn_index:
		0:
			bullet_settings_view.visible = true
			object_pool_settings_view.visible = false
			more_settings_view.visible = false
			
			last_visible_ui_setting_view = bullet_settings_view
		1:
			bullet_settings_view.visible = false
			object_pool_settings_view.visible = true
			more_settings_view.visible = false
			
			last_visible_ui_setting_view = object_pool_settings_view
		2:
			bullet_settings_view.visible = false
			object_pool_settings_view.visible = false
			more_settings_view.visible = true
			
			last_visible_ui_setting_view = more_settings_view


## MORE SETTINGS VIEW RELATED

func _on_select_physics_ticks_btn_view_new_btn_selected(new_selected_btn: Button) -> void:
	var ticks_per_second:int = new_selected_btn.text.to_int()
	Engine.physics_ticks_per_second = ticks_per_second


func _on_physics_interpolation_check_box_pressed() -> void:
	if physics_interpolation_check_box.button_pressed:
		get_tree().physics_interpolation = true # This is the engine's own implementation
		Engine.physics_jitter_fix = 0
		BENCHMARK_GLOBALS.FACTORY.set_use_physics_interpolation_runtime(true) # This is for the bullets
	else:
		get_tree().physics_interpolation = false
		Engine.physics_jitter_fix = 0.5
		BENCHMARK_GLOBALS.FACTORY.set_use_physics_interpolation_runtime(false)


func _on_v_sync_check_box_pressed() -> void:
	if enable_v_sync_check_box.button_pressed:
		DisplayServer.window_set_vsync_mode(DisplayServer.VSYNC_ENABLED)
	else:
		DisplayServer.window_set_vsync_mode(DisplayServer.VSYNC_DISABLED)


func _on_select_rows_per_column_btn_view_new_btn_selected(new_selected_btn: Button) -> void:
	var new_rows_per_column:int = new_selected_btn.text.to_int()
	BENCHMARK_GLOBALS.PLAYER_DATA_NODE.set_grid_rows_per_column(new_rows_per_column)


func _on_select_grid_alignment_btn_view_new_btn_selected(new_selected_btn: Button) -> void:
	var new_alignment:BulletFactory2D.Alignment
	match new_selected_btn.text:
		"CenterLeft":
			new_alignment = BulletFactory2D.Alignment.CENTER_LEFT
		"Topleft":
			new_alignment = BulletFactory2D.Alignment.TOP_LEFT
		"BottomLeft":
			new_alignment = BulletFactory2D.Alignment.BOTTOM_LEFT
			
	BENCHMARK_GLOBALS.PLAYER_DATA_NODE.set_grid_alignment(new_alignment)


func _on_select_column_offset_btn_view_new_btn_selected(new_selected_btn: Button) -> void:
	var new_col_offset:float = new_selected_btn.text.to_float()
	BENCHMARK_GLOBALS.PLAYER_DATA_NODE.set_grid_column_offset(new_col_offset)


func _on_select_row_offset_btn_view_new_btn_selected(new_selected_btn: Button) -> void:
	var new_row_offset:float = new_selected_btn.text.to_float()
	BENCHMARK_GLOBALS.PLAYER_DATA_NODE.set_grid_row_offset(new_row_offset)


func _on_rotate_grid_with_marker_check_box_pressed() -> void:
	var new_rotate_grid_with_marker:bool = rotate_grid_with_marker_check_box.button_pressed
	BENCHMARK_GLOBALS.PLAYER_DATA_NODE.set_rotate_grid_with_marker(new_rotate_grid_with_marker)


func _on_random_local_rotation_check_box_pressed() -> void:
	var should_use_random_local_rotation:bool = random_local_rotation_check_box.button_pressed
	BENCHMARK_GLOBALS.PLAYER_DATA_NODE.set_grid_random_local_rotation(should_use_random_local_rotation)


func _on_random_global_rotation_check_box_pressed() -> void:
	var should_use_random_global_rotation:bool = random_global_rotation_check_box.button_pressed
	BENCHMARK_GLOBALS.PLAYER_DATA_NODE.set_grid_random_global_rotation(should_use_random_global_rotation)
