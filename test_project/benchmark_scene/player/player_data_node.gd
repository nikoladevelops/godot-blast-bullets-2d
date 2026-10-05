class_name PlayerData # using classes makes it possible for these functions to be recognized by Godot instantly, allowing you to use auto complete when typing without any problem
extends Node

# The scene for the godot area2D bullets
@onready var godot_area2d_bullet_scn:PackedScene = preload("res://benchmark_scene/area_2d_bullet.tscn")

# Shaders
@onready var colorful_fragment_shader:Shader = preload("res://shared/shaders/colorful_fragment_shader.gdshader")
@onready var glitch_fragment_shader:Shader = preload("res://shared/shaders/glitch_fragment_shader.gdshader")
@onready var scale_vertex_shader:Shader = preload("res://shared/shaders/scale_vertex_shader.gdshader")
#

# These textures are used as animation frames for the bullets. 
# They are being iterated over again and again until the life time of the bullets is over.
var rocket_textures:Array[Texture2D] = [
	preload("res://shared/art/player_bullets/1.png"), 
	preload("res://shared/art/player_bullets/2.png"),
	preload("res://shared/art/player_bullets/3.png"),
	preload("res://shared/art/player_bullets/4.png"),
	preload("res://shared/art/player_bullets/5.png"),
	preload("res://shared/art/player_bullets/6.png"),
	preload("res://shared/art/player_bullets/7.png"),
	preload("res://shared/art/player_bullets/8.png"),
	preload("res://shared/art/player_bullets/9.png"),
	preload("res://shared/art/player_bullets/10.png")
	]
	
@onready var animatedSprite:AnimatedSprite2D = $BulletsAnimatedSprite
	
# The default texture that can be used, instead of having animations
var godot_texture:Texture2D = preload("res://icon.svg")

# Holds data that is needed for factory.spawn_volley
var volley_data:BulletVolleyData2D

# Holds the selected attachment id used for pooling attachments
var selected_attachment_id:int = 0

# Holds the selected attachment offset relative to the bullet texture's center
var selected_attachment_offset:Vector2 = Vector2(-60, 0)

# Holds data that is needed to set up the speed of the bullets
var bullet_speed_data:Array[BulletSpeedData2D]

var bullet_curves_data_1:BulletCurvesData2D = preload("res://shared/data/bullet_curves_data_1.tres")

# Caches the option index that the user picked for bullet speed (UI related)
var cache_bullet_speed_option_index:int = 0

# Holds data that is optional if we want to set up the rotation of the bullets
var bullet_rotation_data:Array[BulletRotationData2D]

# Caches the option index that the user picked for bullet rotation (UI related)
var cache_bullet_rotation_option_index:int = 0

# Holds the custom resource data to which we have access every single time a bullet hits something
var damage_data:DamageData

# The marker used to spawn a single bullet / the centered marker
var bullet_marker:Marker2D

# The amount of bullets to spawn
var bullets_amount:int

var cached_transforms:Array[Transform2D]

## Bullet Grid related
var rows_per_column:int = 10
var grid_alignment:BulletPatterns2D.Alignment = BulletPatterns2D.Alignment.CENTER_LEFT
var col_offset:float = 150
var row_offset:float = 150

var rotate_grid_with_marker:bool = true
var random_local_rotation:bool = false
##

# Sets up everything so that the PlayerDataNode can be used, basically acts like an additional constructor that has to be called
func set_up(new_bullet_marker:Marker2D) -> void:
	bullet_marker = new_bullet_marker
	bullets_amount = 1 # By default spawning a single bullet at the exact bullet_marker position
	
	# Set up default speed data that will be used when setting up the bullets
	bullet_speed_data = BulletSpeedData2D.generate_random_data(bullets_amount, 50,350,900,900,300,300)
	
	# Set up damage data that will be used when setting up the bullets
	# Create functions to track area_entered and body_entered signals of the factory and this data will be available there
	damage_data = DamageData.new()
	damage_data.base_damage=5 # the default damage set currently
	damage_data.is_player_owned=true
	
	volley_data = set_up_volley_data()

# Returns a partially set up BulletVolleyData2D, only thing left to do is set a new value to the .transforms property
func set_up_volley_data()->BulletVolleyData2D:
	var data:BulletVolleyData2D = BulletVolleyData2D.new()
	data.sprite_frames = animatedSprite.sprite_frames

	data.animation = "idk2"
	
	data.transforms=[Transform2D()]
	data.all_bullet_speed_data = bullet_speed_data # every bullet uses its own speed entry
	
	#data.collision_layer = BulletVolleyData2D.calculate_bitmask([2])
	#data.collision_mask = BulletVolleyData2D.calculate_bitmask([3]) # by default bullets interact only with enemy
	data.set_collision_layer_from_array([2])
	data.set_collision_mask_from_array([3])
	
	var circle:CircleShape2D = CircleShape2D.new()
	circle.radius = 32
	
	var rectangle:RectangleShape2D = RectangleShape2D.new()
	rectangle.size = Vector2(32,32)
	
	var capsule:CapsuleShape2D = CapsuleShape2D.new()
	capsule.height = 10
	capsule.radius = 10
	capsule.mid_height = 15
	
	data.collision_shape = circle
	
	data.texture_size = Vector2(140,140)
	data.collision_shape_offset=Vector2(0,0)
	data.max_life_time = 2
	data.all_bullet_rotation_data = bullet_rotation_data
	data.shared_bullets_custom_data = damage_data
	#data.is_life_time_over_signal_enabled = true # If you want to track when the life time is over and receive a signal inside BulletFactory2D
	data.shared_bullet_attachment_offset = selected_attachment_offset
	return data

# Determines which type of bullets to be spawned
func spawn_bullets(player_rotation:float)->void:
	## All of this logic here is because the shaders I use take advantage of instance uniforms, so each time I spawn a brand new instance I actually want a different value for those shader params
	if volley_data.material != null:
		var material:ShaderMaterial = volley_data.material as ShaderMaterial
		
		if material:
			## 	All my custom ShaderMaterial scripts have a "time_offset" instance uniform that needs to be set so that each spawned MultiMeshInstance differes slightly from the rest / different timing of the effects..
			volley_data.instance_shader_parameters.assign(
				{"time_offset" : randf()}
			)
	##
			
	
	match BENCHMARK_GLOBALS.BULLET_TYPE_TO_SPAWN:
		BENCHMARK_GLOBALS.BulletType.BulletVolley:
			spawn_volley_bullets()
		BENCHMARK_GLOBALS.BulletType.GodotArea2D:
			spawn_godot_area2d_bullets(player_rotation)

# Spawns BulletVolley bullets
func spawn_volley_bullets()->void:
	if bullets_amount < 10:
		volley_data.transforms = BulletPatterns2D.helper_generate_transforms_grid(bullets_amount, bullet_marker.get_global_transform(), bullets_amount, grid_alignment, col_offset, row_offset, rotate_grid_with_marker, random_local_rotation)
	else:
		volley_data.transforms = BulletPatterns2D.helper_generate_transforms_grid(bullets_amount, bullet_marker.get_global_transform(), rows_per_column, grid_alignment, col_offset, row_offset, rotate_grid_with_marker, random_local_rotation)
	
	#volley_data.max_life_time = 5
	#volley_data.is_life_time_over_signal_enabled = true
	#volley_data.bullet_max_collision_amount = 1
	var volley:BulletVolley2D = BENCHMARK_GLOBALS.FACTORY.spawn_volley(volley_data)
	
	volley.homing_smoothing = 0.0# Set from 0 to 20 or even bigger (but you might have issues with interpolation)
	volley.homing_update_interval = 0.00# Set an update timer - keep it low for smooth updates
	volley.homing_take_control_of_texture_rotation = true
	volley.homing_distance_before_reached = 50
	
	#volley.bullet_homing_auto_pop_after_target_reached = true
	volley.is_auto_pooling_enabled = true
	volley.bullet_max_collision_count = 1 # How many times the bullet can collide before getting disabled
	#
	# Test different behaviors here
	
	
func spawn_godot_area2d_bullets(player_rotation:float)->void:
	var transforms:Array[Transform2D]
	
	if bullets_amount < 10:
		transforms = BulletPatterns2D.helper_generate_transforms_grid(bullets_amount, bullet_marker.get_global_transform(), bullets_amount, grid_alignment, col_offset, row_offset, rotate_grid_with_marker, random_local_rotation)
	else:
		transforms = BulletPatterns2D.helper_generate_transforms_grid(bullets_amount, bullet_marker.get_global_transform(), rows_per_column, grid_alignment, col_offset, row_offset, rotate_grid_with_marker, random_local_rotation)
	
	var bullet_scale:Vector2 = Vector2(5,5)
	var bullet_direction:Vector2 = Vector2(1, 0).rotated(player_rotation)
	
	for transf in transforms:
		var area2d_bullet:Area2DBullet = godot_area2d_bullet_scn.instantiate()
		area2d_bullet.global_transform = transf
		area2d_bullet.damage = damage_data.base_damage
		area2d_bullet.direction = bullet_direction
		area2d_bullet.scale = bullet_scale
		BENCHMARK_GLOBALS.ALL_GODOT_AREA2D_BULLETS_CONTAINER.add_child(area2d_bullet)

# Whether monitorable should be enabled for the bullets
func set_monitorable_enabled(enable:bool)->void:
	volley_data.monitorable=enable

# Generates and sets new random rotation data, but only if bullet rotation is enabled
func generate_bullet_rotation_data(option_index:int)->void:
	match option_index:
		0:
			bullet_rotation_data = []
		1:
			bullet_rotation_data = BulletRotationData2D.generate_random_data(bullets_amount, 2.5, 8.5, 13, 23, 8, 12)
		2:
			bullet_rotation_data = BulletRotationData2D.generate_random_data(bullets_amount, 5.5, 10, 12, 15, 1, 2.5)
		3:
			bullet_rotation_data = BulletRotationData2D.generate_random_data(bullets_amount, -1, 2, -10, 14, -10, 12)
	
	cache_bullet_rotation_option_index = option_index
	volley_data.all_bullet_rotation_data = bullet_rotation_data
	
# Generates and sets new random bullet speed data
func generate_bullet_speed_data(option_index:int)->void:
	match option_index:
		0:
			bullet_speed_data = BulletSpeedData2D.generate_random_data(bullets_amount, 50,350,900,900,300,300)
		1:
			bullet_speed_data = BulletSpeedData2D.generate_random_data(bullets_amount, 500,700,700,1300,500,700)
		2:
			bullet_speed_data = BulletSpeedData2D.generate_random_data(bullets_amount, 1800,2500,3000,3500,1000,1100)
	
	cache_bullet_speed_option_index = option_index
	volley_data.all_bullet_speed_data = bullet_speed_data
#
## Switches the bullet texture currently being used
#func switch_bullet_texture(option_index:int)->void:
	#if option_index == 0:
		#var rocket_frames := make_sprite_frames(rocket_textures)
		#volley_data.sprite_frames = rocket_frames
	#elif option_index == 1:
		#var single_frames := make_sprite_frames([godot_texture])
		#volley_data.sprite_frames = single_frames

# Sets the is_texture_rotation_permanent property for the bullet that are going to be spawned -> Whether the texture should rotate depending on the direction or if it should stay the same
func set_bullet_is_texture_rotation_permanent(is_permanent:bool)->void:
	volley_data.is_texture_rotation_permanent = is_permanent

# Changes the amount of bullets that are going to be spawned at once when shooting
func set_new_bullets_spawn_amount(new_bullets_amount:int)->void:
	bullets_amount = new_bullets_amount
	
	# Need to refresh these since they depend on the bullets amount
	generate_bullet_rotation_data(cache_bullet_rotation_option_index) 
	generate_bullet_speed_data(cache_bullet_speed_option_index)

# Sets a brand new damage value for the bullets
func set_new_damage_value(new_damage:int)->void:
	damage_data.base_damage = new_damage

# Sets a brand new collision mask based on the integers passed
func set_bullet_collision_mask(arr:Array[int])->void:
	volley_data.collision_mask = BulletVolleyData2D.calculate_bitmask(arr)

# Changes the size of the collision shapes that the bullets have
func set_collision_shape_size(new_size:Vector2)->void:
	volley_data.collision_shape_size = new_size

# Changes the collision shape offset of the bullets
func set_collision_shape_offset(new_offset:Vector2)->void:
	volley_data.collision_shape_offset = new_offset

# Changes the bullet attachmnent's offset
func set_bullet_attachment_offset(new_offset:Vector2)->void:
	selected_attachment_offset = new_offset
	
	volley_data.shared_bullet_attachment_offset = new_offset

# Changes the bullet texture's rotation
func set_bullet_texture_rotation(degrees:int)->void:
	volley_data.texture_rotation_radians = deg_to_rad(degrees)

# Changes the bullet's lifetime
func set_bullet_lifetime(new_lifetime:float)->void:
	if new_lifetime <= 0:
		volley_data.is_life_time_infinite = true
	else:
		volley_data.is_life_time_infinite = false

	volley_data.max_life_time = new_lifetime

# Switches the attachment scene
func switch_attachment_scn(option_index:int)->void:
	selected_attachment_id = option_index
	
	var scene:PackedScene = BENCHMARK_GLOBALS.ATTACHMENT_SCENES[selected_attachment_id]
	
	volley_data.shared_bullet_attachment = scene
	

# Sets whether the physics shapes should also get rotated when rotation data is provided
func set_rotate_physics_shapes(should_rotate_physics_shapes:bool)->void:
	volley_data.rotate_only_textures = !should_rotate_physics_shapes

# Sets a new texture size for the bullets
func set_new_texture_size(new_size:Vector2)->void:
	volley_data.texture_size = new_size

# Sets a material for the bullets based on the option_index the user picked from the UI
func switch_material(option_index)->void:
	match option_index:
		0:
			volley_data.material = null
		1:
			var material:CanvasItemMaterial = CanvasItemMaterial.new()
			material.blend_mode = CanvasItemMaterial.BLEND_MODE_ADD
			
			volley_data.material = material
		2:
			var material:ShaderMaterial = ShaderMaterial.new()
			material.shader = colorful_fragment_shader
			
			volley_data.material = material 
		3:
			var material:ShaderMaterial = ShaderMaterial.new()
			material.shader = glitch_fragment_shader
			
			volley_data.material = material
		4:
			var material:ShaderMaterial = ShaderMaterial.new()
			material.shader = scale_vertex_shader
			
			volley_data.material = material
			

# Sets a different Z Index for the bullets
func set_bullets_z_index(new_z_index:int)->void:
	volley_data.z_index = new_z_index

## Grid Related
# Will make it so that the bullets' direction gets adjusted based on the rotation data that they have
func set_adjust_direction_based_on_rotation(should_adjust_direction:bool)->void:
	volley_data.adjust_direction_based_on_rotation = should_adjust_direction
	
func set_grid_rows_per_column(new_rows_per_column:int)->void:
	rows_per_column = new_rows_per_column

func set_grid_alignment(new_alignment:BulletPatterns2D.Alignment)->void:
	grid_alignment = new_alignment

func set_grid_column_offset(new_col_offset:float)->void:
	col_offset = new_col_offset

func set_grid_row_offset(new_row_offset:float)->void:
	row_offset = new_row_offset

func set_rotate_grid_with_marker(enable:bool)->void:
	rotate_grid_with_marker = enable
	
func set_grid_random_local_rotation(enable:bool)->void:
	random_local_rotation = enable
	
func set_stop_rotation_when_max_reached(enable:bool)->void:
	volley_data.stop_rotation_when_max_reached = enable
	
##
