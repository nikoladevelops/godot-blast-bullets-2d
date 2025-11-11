class_name PlayerData # using classes makes it possible for these functions to be recognized by Godot instantly, allowing you to use auto complete when typing without any problem
extends Node

# Attachment scenes
@onready var cpu_particles_scn:PackedScene = preload("res://shared/bullet_attachment_nodes/attached_particles.tscn")
@onready var gpu_particles_scn:PackedScene = preload("res://shared/bullet_attachment_nodes/attached_particles2.tscn")
@onready var light_attachment_scn:PackedScene = preload("res://shared/bullet_attachment_nodes/light_attachment.tscn")

# The scene for the godot area2D bullets
@onready var godot_area2d_bullet_scn:PackedScene = preload("res://benchmark_scene/area_2d_bullet.tscn")

# Shaders
@onready var colorful_fragment_shader:Shader = preload("res://shared/shaders/colorful_fragment_shader.gdshader")
@onready var glitch_fragment_shader:Shader = preload("res://shared/shaders/glitch_fragment_shader.gdshader")
@onready var scale_vertex_shader:Shader = preload("res://shared/shaders/scale_vertex_shader.gdshader")
#

# These textures are used as animation frames for the bullets. 
# They are being iterated over again and again until the life time of the bullets is over.
var rocket_tectures:Array[Texture2D] = [
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
	
# The default texture that can be used, instead of having animations
var godot_texture:Texture2D = preload("res://icon.svg")

# Holds data that is needed for factory.spawn_directional_bullets
var directional_bullets_data:DirectionalBulletsData2D

# Holds data that is needed for factory.spawn_block_bullets
var block_bullets_data:BlockBulletsData2D

# Holds data that is needed to set up the speed of both directional and block bullets
var bullet_speed_data:Array[BulletSpeedData2D]

var bullet_curves_data_1:BulletCurvesData2D = preload("res://shared/data/bullet_curves_data_1.tres")

# Caches the option index that the user picked for bullet speed (UI related)
var cache_bullet_speed_option_index:int = 0

# Holds data that is optional if we want to set up the rotation of both directional and block bullets
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
var grid_alignment:BulletFactory2D.Alignment = BulletFactory2D.Alignment.CENTER_LEFT
var col_offset:float = 150
var row_offset:float = 150

var rotate_grid_with_marker:bool = true
var random_local_rotation:bool = false
##

# Sets up everything so that the PlayerDataNode can be used, basically acts like an additional constructor that has to be called
func set_up(new_bullet_marker:Marker2D) -> void:
	bullet_marker = new_bullet_marker
	bullets_amount = 1 # By default spawning a single bullet at the exact bullet_marker position
	
	# Set up default speed data that will be used when setting up block and directional bullets
	bullet_speed_data = BulletSpeedData2D.generate_random_data(bullets_amount, 50,350,900,900,300,300)
	
	# Set up damage data that will be used when setting up block and directional bullets
	# Create functions to track area_entered and body_entered signals of the factory and this data will be available there
	damage_data = DamageData.new()
	damage_data.base_damage=5 # the default damage set currently
	damage_data.is_player_owned=true
	
	block_bullets_data = set_up_block_bullets_data()
	directional_bullets_data = set_up_directional_bullets_data()

# Returns a partially set up BlockBulletsData2D, only thing left to do is set a new value to the .transforms and .block_rotation properties
func set_up_block_bullets_data()->BlockBulletsData2D:
	var data:BlockBulletsData2D = BlockBulletsData2D.new();
	data.textures = rocket_tectures
	data.block_speed = bullet_speed_data[0] # for the block of bullets use only the first bullet_speed_data as the block_speed
	
	#data.collision_layer = BlockBulletsData2D.calculate_bitmask([2])
	#data.collision_mask = BlockBulletsData2D.calculate_bitmask([3]) # by default bullets interact only with enemy
	data.set_collision_layer_from_array([2])
	data.set_collision_mask_from_array([3])
	
	data.texture_size = Vector2(140,140)
	data.collision_shape_size=Vector2(32,32)
	data.collision_shape_offset=Vector2(0,0)
	data.default_change_texture_time=0.09
	data.max_life_time = 2
	data.all_bullet_rotation_data = bullet_rotation_data
	data.bullet_attachment_offset = Vector2(-60,0)
	data.bullets_custom_data = damage_data
	
	return data

# Returns a partially set up DirectionalBulletsData2D, only thing left to do is set a new value to the .transforms property
func set_up_directional_bullets_data()->DirectionalBulletsData2D:
	var data:DirectionalBulletsData2D = DirectionalBulletsData2D.new()
	data.textures = rocket_tectures
	
	# You can also define wait time for each texture like so as long as the amount of textures matches the amount of values in this array.
	# Otherwise stick to using default_change_texture_time
	#data.change_texture_times = [
		#0.05,
		#0.03,
		#0.01,
		#0.02,
		#0.01,
		#0.01,
		#0.01,
		#0.08,
		#0.01,
		#0.03
	#]
	
	
	data.all_bullet_speed_data = bullet_speed_data # for the directional bullets use every single bullet speed
	
	#data.collision_layer = DirectionalBulletsData2D.calculate_bitmask([2])
	#data.collision_mask = DirectionalBulletsData2D.calculate_bitmask([3]) # by default bullets interact only with enemy
	data.set_collision_layer_from_array([2])
	data.set_collision_mask_from_array([3])

	data.texture_size = Vector2(140,140)
	data.collision_shape_size=Vector2(32,32)
	data.collision_shape_offset=Vector2(0,0)
	data.default_change_texture_time=0.09
	data.max_life_time = 2
	data.all_bullet_rotation_data = bullet_rotation_data
	data.bullet_attachment_offset = Vector2(-60,0)
	data.bullets_custom_data = damage_data
	#data.is_life_time_over_signal_enabled = true # If you want to track when the life time is over and receive a signal inside BulletFactory2D
	
	return data

# This is no longer used and is instead moved to C++ inside BulletFactory2D, I'm leaving the code commented as GDScript because it may be helpful to someone
#enum Alignment {
	#TOP_LEFT,
	#TOP_CENTER,
	#TOP_RIGHT,
	#CENTER_LEFT,
	#CENTER,
	#CENTER_RIGHT,
	#BOTTOM_LEFT,
	#BOTTOM_CENTER,
	#BOTTOM_RIGHT
#}

#func helper_generate_transforms_grid(marker_transf:Transform2D, rows_per_column:int = 10, alignment: Alignment = Alignment.CENTER_LEFT, column_offset: float = 150, row_offset: float = 150, rotate_grid_with_marker:bool = true, random_local_rotation=false, random_global_rotation: bool = false) -> Array[Transform2D]:
	#var generated_transforms: Array[Transform2D] = []
	#generated_transforms.resize(bullets_amount)
#
	#var columns_amount: int = ceil(bullets_amount / float(rows_per_column))
#
	#var total_width: float = (columns_amount - 1) * column_offset
	#var total_height: float = (rows_per_column - 1) * row_offset
#
	## Adjust for even/odd bullet counts to center properly
	#var x_start: float = -total_width / 2.0 if columns_amount % 2 == 1 else -total_width / 2.0 + column_offset / 2.0
	#var y_start: float = -total_height / 2.0 if rows_per_column % 2 == 1 else -total_height / 2.0 + row_offset / 2.0
#
	#match alignment:
		#Alignment.TOP_LEFT:
			#x_start = 0.0
			#y_start = 0.0
		#Alignment.TOP_CENTER:
			#x_start = -total_width / 2.0 if columns_amount % 2 == 1 else -total_width / 2.0 + column_offset / 2.0
			#y_start = 0.0
		#Alignment.TOP_RIGHT:
			#x_start = -total_width
			#y_start = 0.0
		#Alignment.CENTER_LEFT:
			#x_start = 0.0
		#Alignment.CENTER:
			#pass  # x_start and y_start are already centered
		#Alignment.CENTER_RIGHT:
			#x_start = -total_width
		#Alignment.BOTTOM_LEFT:
			#x_start = 0.0
			#y_start = -total_height
		#Alignment.BOTTOM_CENTER:
			#x_start = -total_width / 2.0 if columns_amount % 2 == 1 else -total_width / 2.0 + column_offset / 2.0
			#y_start = -total_height
		#Alignment.BOTTOM_RIGHT:
			#x_start = -total_width
			#y_start = -total_height
#
	#var count_spawned: int = 0
#
	#for column in range(columns_amount):
		#for row in range(rows_per_column):
			#if count_spawned >= bullets_amount:
				#break
#
			#var x: float = x_start + column * column_offset
			#var y: float = y_start + row * row_offset
			#var local_offset: Vector2 = Vector2(x, y)
#
			#var new_transform: Transform2D
			#if rotate_grid_with_marker:
				#var rotated_offset = marker_transf.basis_xform(local_offset)
				#new_transform = Transform2D(marker_transf.get_rotation(), marker_transf.origin + rotated_offset)
			#else:
				#var new_origin: Vector2 = marker_transf.origin + local_offset
				#new_transform = Transform2D(marker_transf.get_rotation(), new_origin)
			#
			#if random_local_rotation:
				#var random_angle = randf() * TAU
				#new_transform = new_transform.rotated_local(random_angle)
			#
			#if random_global_rotation:
				#var random_angle = randf() * TAU
				#new_transform = new_transform.rotated(random_angle)
#
			#generated_transforms[count_spawned] = new_transform
			#count_spawned += 1
#
	#return generated_transforms

# Determines which type of bullets to be spawned
func spawn_bullets(player_rotation:float)->void:
	## All of this logic here is because the shaders I use take advantage of instance uniforms, so each time I spawn a brand new instance I actually want a different value for those shader params
	if directional_bullets_data.material != null:
		var material:ShaderMaterial = directional_bullets_data.material as ShaderMaterial
		
		if material:
			## 	All my custom ShaderMaterial scripts have a "time_offset" instance uniform that needs to be set so that each spawned MultiMeshInstance differes slightly from the rest / different timing of the effects..
			directional_bullets_data.instance_shader_parameters.assign(
				{"time_offset" : randf()}
			)
	##
			
	
	match BENCHMARK_GLOBALS.BULLET_TYPE_TO_SPAWN:
		BENCHMARK_GLOBALS.BulletType.MultiMeshDirectional:
			spawn_multi_mesh_directional_bullets()
		BENCHMARK_GLOBALS.BulletType.MultiMeshBlock:
			spawn_multi_mesh_block_bullets(player_rotation)
		BENCHMARK_GLOBALS.BulletType.GodotArea2D:
			spawn_godot_area2d_bullets(player_rotation)

# Spawns MultiMeshDirectional bullets
func spawn_multi_mesh_directional_bullets()->void:
	if bullets_amount < 10:
		directional_bullets_data.transforms = BulletFactory2D.helper_generate_transforms_grid(bullets_amount, bullet_marker.get_global_transform(), bullets_amount, grid_alignment, col_offset, row_offset, rotate_grid_with_marker, random_local_rotation)
	else:
		directional_bullets_data.transforms = BulletFactory2D.helper_generate_transforms_grid(bullets_amount, bullet_marker.get_global_transform(), rows_per_column, grid_alignment, col_offset, row_offset, rotate_grid_with_marker, random_local_rotation)
	
	#directional_bullets_data.max_life_time = 5
	#directional_bullets_data.is_life_time_over_signal_enabled = true
	#directional_bullets_data.bullet_max_collision_amount = 1
	var dir_bullets:DirectionalBullets2D = BENCHMARK_GLOBALS.FACTORY.spawn_controllable_directional_bullets(directional_bullets_data)
	dir_bullets.homing_smoothing = 20.0# Set from 0 to 20 or even bigger (but you might have issues with interpolation)
	dir_bullets.homing_update_interval = 0.02# Set an update timer - keep it low for smooth updates
	dir_bullets.homing_take_control_of_texture_rotation = true
	dir_bullets.distance_from_target_before_considering_as_reached = 50
	
	dir_bullets.bullet_homing_auto_pop_after_target_reached = true
	dir_bullets.is_multimesh_auto_pooling_enabled = true
	dir_bullets.bullet_max_collision_count = 2
	
	dir_bullets.bullet_curves_data = bullet_curves_data_1
	
	#dir_bullets.all_bullets_set_attachment(gpu_particles_scn, 0, Vector2(-60,0), true)
	dir_bullets.shared_homing_deque_push_back_node2d_target(BENCHMARK_GLOBALS.STATIONARY_TARGET)
	
	#
	
	get_tree().create_timer(1).timeout.connect(func():
		dir_bullets.teleport_bullet(0, Vector2(0,0))
		)
	#dir_bullets.multimesh_attach_time_based_function(1, func():
		#dir_bullets.teleport_bullet(0, Vector2(0,0))
		##dir_bullets.set_textures([godot_texture], [0.05], 0)
		##dir_bullets.shared_homing_deque_pop_back_target()
		#, false, true)
	
	#dir_bullets.multimesh_attach_time_based_function(4, func():
		#dir_bullets.set_textures(rocket_tectures, [0.03], 0)
		##dir_bullets.shared_homing_deque_pop_back_target()
		#, false, true)
		
	#for i in dir_bullets.get_amount_bullets():
		#dir_bullets.bullet_set_attachment(i, gpu_particles_scn, 5, Vector2(-60,0), true)
	
	#dir_bullets.multimesh_attach_time_based_function(1, func(): 
		#print("Keeps executing over and over EVEN IF ALL BULLETS WERE DISABLED")
		#, true, false)
	#
	#dir_bullets.multimesh_attach_time_based_function(1, func(): 
		#print("Keeps executing over and over but only if the multimesh is active (at least one bullet left active)")
		#
		#, true, true)
	
		##
	#dir_bullets.multimesh_attach_time_based_function(2, func(): 
		#dir_bullets.activate_bullet(0)
		#, false, true)
	
	
# Spawns MultiMeshBlock bullets
func spawn_multi_mesh_block_bullets(player_rotation:float)->void:
	if bullets_amount < 10:
		block_bullets_data.transforms = BulletFactory2D.helper_generate_transforms_grid(bullets_amount, bullet_marker.get_global_transform(), bullets_amount, grid_alignment, col_offset, row_offset, rotate_grid_with_marker, random_local_rotation)
	else:
		block_bullets_data.transforms = BulletFactory2D.helper_generate_transforms_grid(bullets_amount, bullet_marker.get_global_transform(), rows_per_column, grid_alignment, col_offset, row_offset, rotate_grid_with_marker, random_local_rotation)
	
	
	block_bullets_data.block_rotation_radians=player_rotation # I want the block of bullets to be rotated the same way that the player is rotated
		
	BENCHMARK_GLOBALS.FACTORY.spawn_block_bullets(block_bullets_data)

func spawn_godot_area2d_bullets(player_rotation:float)->void:
	var transforms:Array[Transform2D]
	
	if bullets_amount < 10:
		transforms = BulletFactory2D.helper_generate_transforms_grid(bullets_amount, bullet_marker.get_global_transform(), bullets_amount, grid_alignment, col_offset, row_offset, rotate_grid_with_marker, random_local_rotation)
	else:
		transforms = BulletFactory2D.helper_generate_transforms_grid(bullets_amount, bullet_marker.get_global_transform(), rows_per_column, grid_alignment, col_offset, row_offset, rotate_grid_with_marker, random_local_rotation)
	
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
	block_bullets_data.monitorable=enable
	directional_bullets_data.monitorable=enable

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
	block_bullets_data.all_bullet_rotation_data = bullet_rotation_data
	directional_bullets_data.all_bullet_rotation_data = bullet_rotation_data
	
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
	block_bullets_data.block_speed = bullet_speed_data[0] # only use the first speed data for the whole block of bullets
	directional_bullets_data.all_bullet_speed_data = bullet_speed_data

# Switches the bullet texture currently being used
func switch_bullet_texture(option_index:int)->void:
	if option_index == 0:
		directional_bullets_data.textures = rocket_tectures;
		block_bullets_data.textures = rocket_tectures;
	elif option_index == 1:
		
		# Always reset these to an empty array otherwise the default_texture property won't be used since the .textures array will be still populated
		directional_bullets_data.textures = [];
		block_bullets_data.textures = [];
		
		directional_bullets_data.default_texture = godot_texture
		block_bullets_data.default_texture = godot_texture

# Sets the is_texture_rotation_permanent property for the bullet that are going to be spawned -> Whether the texture should rotate depending on the direction or if it should stay the same
func set_bullet_is_texture_rotation_permanent(is_permanent:bool)->void:
	directional_bullets_data.is_texture_rotation_permanent = is_permanent
	block_bullets_data.is_texture_rotation_permanent = is_permanent

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
	block_bullets_data.collision_mask = MultiMeshBulletsData2D.calculate_bitmask(arr)
	directional_bullets_data.collision_mask = MultiMeshBulletsData2D.calculate_bitmask(arr)

# Changes the size of the collision shapes that the bullets have
func set_collision_shape_size(new_size:Vector2)->void:
	block_bullets_data.collision_shape_size = new_size
	directional_bullets_data.collision_shape_size = new_size

# Changes the collision shape offset of the bullets
func set_collision_shape_offset(new_offset:Vector2)->void:
	block_bullets_data.collision_shape_offset = new_offset
	directional_bullets_data.collision_shape_offset = new_offset

# Changes the bullet attachmnent's offset
func set_bullet_attachment_offset(new_offset:Vector2)->void:
	block_bullets_data.bullet_attachment_offset = new_offset
	directional_bullets_data.bullet_attachment_offset = new_offset

# Changes the bullet texture's rotation
func set_bullet_texture_rotation(degrees:int)->void:
	block_bullets_data.texture_rotation_radians = deg_to_rad(degrees)
	directional_bullets_data.texture_rotation_radians = deg_to_rad(degrees)

# Changes the bullet's lifetime
func set_bullet_lifetime(new_lifetime:float)->void:
	if new_lifetime <= 0:
		block_bullets_data.is_life_time_infinite = true
		directional_bullets_data.is_life_time_infinite = true
	else:
		block_bullets_data.is_life_time_infinite = false
		directional_bullets_data.is_life_time_infinite = false

	block_bullets_data.max_life_time = new_lifetime
	directional_bullets_data.max_life_time = new_lifetime

# Switches the attachment scene
func switch_attachment_scn(option_index:int)->void:
	var new_attachment:PackedScene = null
	match option_index:
		0:
			new_attachment = null
		1:
			new_attachment = cpu_particles_scn
		2:
			new_attachment = gpu_particles_scn
		3:
			new_attachment = light_attachment_scn
	
	block_bullets_data.attachment_scenes = new_attachment
	directional_bullets_data.attachment_scenes = new_attachment

# Gets the attachment based on attachment_id (Note the attachment_id should match the one in the packed scene)
func get_attachment_scn_based_on_attachment_id(chosen_attachment_id)->PackedScene:
	var attachment_to_return:PackedScene = null
	match chosen_attachment_id:
		1:
			attachment_to_return = cpu_particles_scn
		2:
			attachment_to_return = gpu_particles_scn
		3:
			attachment_to_return = light_attachment_scn
			
	return attachment_to_return

# Sets whether the physics shapes should also get rotated when rotation data is provided
func set_rotate_physics_shapes(should_rotate_physics_shapes:bool)->void:
	block_bullets_data.rotate_only_textures = !should_rotate_physics_shapes
	directional_bullets_data.rotate_only_textures = !should_rotate_physics_shapes

# Sets a new texture size for the bullets
func set_new_texture_size(new_size:Vector2)->void:
	block_bullets_data.texture_size = new_size
	directional_bullets_data.texture_size = new_size

# Sets a material for the bullets based on the option_index the user picked from the UI
func switch_material(option_index)->void:
	match option_index:
		0:
			block_bullets_data.material = null
			directional_bullets_data.material = null
		1:
			var material:CanvasItemMaterial = CanvasItemMaterial.new()
			material.blend_mode = CanvasItemMaterial.BLEND_MODE_ADD
			
			block_bullets_data.material = material
			directional_bullets_data.material = material
		2:
			var material:ShaderMaterial = ShaderMaterial.new()
			material.shader = colorful_fragment_shader
			
			block_bullets_data.material = material
			directional_bullets_data.material = material 
		3:
			var material:ShaderMaterial = ShaderMaterial.new()
			material.shader = glitch_fragment_shader
			
			block_bullets_data.material = material
			directional_bullets_data.material = material
		4:
			var material:ShaderMaterial = ShaderMaterial.new()
			material.shader = scale_vertex_shader
			
			block_bullets_data.material = material
			directional_bullets_data.material = material
			

# Sets a different Z Index for the bullets
func set_bullets_z_index(new_z_index:int)->void:
	block_bullets_data.z_index = new_z_index
	directional_bullets_data.z_index = new_z_index

## Grid Related
# Will make it so that directional bullets' direction gets adjusted based on the rotation data that they have
func set_adjust_direction_based_on_rotation(should_adjust_direction:bool)->void:
	directional_bullets_data.adjust_direction_based_on_rotation = should_adjust_direction
	
func set_grid_rows_per_column(new_rows_per_column:int)->void:
	rows_per_column = new_rows_per_column

func set_grid_alignment(new_alignment:BulletFactory2D.Alignment)->void:
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
	block_bullets_data.stop_rotation_when_max_reached = enable
	directional_bullets_data.stop_rotation_when_max_reached = enable
	
##
