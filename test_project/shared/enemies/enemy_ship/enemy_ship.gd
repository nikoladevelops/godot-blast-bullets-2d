class_name NormalEnemy # having a class gives you auto completion when instantiating this from a packed scene - allows you to see all methods, so always use this approach
extends AbstractEnemy

# NOTE We've set up the ReceiveDamageArea2D to be in collision_layer = 3, this means that the bullets would need to have a collision_mask of layer 3 when being spawned in order to detect this enemy area
# NOTE You are also free to use bodies, but one thing you need to remember is that if you use STATIC BODIES for the enemy, you have to enable MONITORABLE property of the bullets, otherwise they won't be able to detect static bodies - also this is at a cost of performance, so using Area2D is always better!
# NOTE Depending on the enemy type (whether the thing being hit by the bullets is an area or a body), you need to handle either the _on_area_entered or _on_body_entered signal of the BulletFactory2D - this is done in an outside script

# The speed at which the enemy moves
@export var speed:float = 100

# The max amount of health the enemy starts with
@export var max_health:float = 100

# The amount of time needed before the ship can shoot again
@export var shoot_cooldown:float = 1

# The radius at which the enemy can detect the target (if the target is in this range then the enemy starts following it)
@export var follow_radius:float = 640

# The time needed for the enemy to update its direction
@export var reaction_time:float = 0.5

# At what distance should the enemy be from the player so that it stops moving (in other words at what distance is the player considered reached by the enemy)
@export var distance_tolerance: float = 415.0

# The collision shape that determines the actual radius / detection area in which the target will get followed
@onready var follow_radius_collision_shape:CollisionShape2D = $FollowTargetRadiusArea2D/CollisionShape2D

# Controls sprite animations
@onready var animated_sprite:AnimatedSprite2D = $AnimatedSprite2D

# Timer that controls shooting cooldown
@onready var shoot_timer:Timer = $ShootTimer

# Markers where the bullets are spawned at
@onready var left_marker:Marker2D = $AnimatedSprite2D/LeftMarker2D
@onready var right_marker:Marker2D = $AnimatedSprite2D/RightMarker2D
@onready var center_marker:Marker2D = $AnimatedSprite2D/CenterMarker2D

# The bullet data used to spawn bullets from
var bullets_data:DirectionalBulletsData2D

# Whether the ship should shoot two bullets at a time or not
var is_shooting_two_at_a_time:bool

# Whether the ship can shoot or not / used as a cooldown flag
var can_shoot:bool=true

# The target being followed by the enemy (usually a player reference is passed here)
@export var follow_target:Node2D

# The cached direction in which the enemy is moving
var direction:Vector2

# Whether the enemy is moving towards the target or not
var is_moving:bool

# Whether the target is in range - if it is the enemy starts following it
var is_target_in_range:bool

func _ready() -> void:
	health_bar = get_node("HealthBar")
	health_bar.set_up(max_health)
	health_bar.visible = false # Set it by default to be invisible
	
	shoot_timer.wait_time = shoot_cooldown
	
	var circle_shape = follow_radius_collision_shape.shape as CircleShape2D
	circle_shape.radius = follow_radius
	
	if follow_target == null:
		follow_target = BENCHMARK_GLOBALS.PLAYER
	
	calculate_new_direction()
	
	# Set up bullets data
	bullets_data = DirectionalBulletsData2D.new()
	bullets_data.textures = [
		preload("res://shared/art/enemy_bullets/enemy_bullet1.png"),
		preload("res://shared/art/enemy_bullets/enemy_bullet2.png"),
		preload("res://shared/art/enemy_bullets/enemy_bullet3.png"),
		preload("res://shared/art/enemy_bullets/enemy_bullet4.png")
	]
	
	bullets_data.texture_size = Vector2(100,100)
	bullets_data.collision_shape_size = Vector2(32,32)
	bullets_data.default_change_texture_time = 0.09
	
	bullets_data.max_life_time = 5
	
	var sp_data:BulletSpeedData2D = BulletSpeedData2D.new()
	sp_data.speed = 200
	sp_data.max_speed = 300
	sp_data.acceleration = 5
	
	var dmg_data:DamageData = DamageData.new()
	dmg_data.base_damage = 1
	
	bullets_data.bullets_custom_data = dmg_data
	
	bullets_data.collision_layer = DirectionalBulletsData2D.calculate_bitmask([4])
	bullets_data.collision_mask = DirectionalBulletsData2D.calculate_bitmask(default_bullet_collision_mask)
	bullets_data.monitorable=true # Monitorable allows for these bullets to be detected by other areas

func _physics_process(delta: float) -> void:
	calculate_new_direction()
	if is_moving:
		move_towards_target(delta)
	
	if is_target_in_range:
		shoot()

# Shoots either a single bullet or two bullets at a time
func shoot()->void:
	if can_shoot:
		bullets_data.transforms = get_marker_transforms()
		bullets_data.all_bullet_speed_data = get_speed_data()
		#bullets_data.block_rotation_radians = animated_sprite.transform.get_rotation() # Since we are using block bullets, their direction is determined by this property instead of automatically by the transforms
		var bullets_multi:DirectionalBullets2D = BENCHMARK_GLOBALS.FACTORY.spawn_controllable_directional_bullets(bullets_data)
		bullets_multi.all_bullets_set_attachment(BENCHMARK_GLOBALS.ATTACHMENT_SCENES[1], 1, Vector2(-30, 0))
		
		can_shoot=false
		shoot_timer.start()

# Moving towards the target gradually
func move_towards_target(delta:float)->void:
	global_position += direction * speed * delta

# Calculates the direction in which the enemy moves and in case the target was reached, blocks any further movement
func calculate_new_direction()->void:
	if is_target_in_range == false:
		is_moving = false
		return
	
	animated_sprite.look_at(follow_target.global_position)
	animated_sprite.rotate(deg_to_rad(90))
	direction = (follow_target.global_position - global_position).normalized()
	
	# Stop moving when reaching the distination - I want the enemy to stop moving at a given distance away from the player (I basically don't want the enemy to go exactly at the center of the player that's all)
	if global_position.distance_to(follow_target.global_position) <= distance_tolerance:
		is_moving = false
		animated_sprite.play("idle")
	else:
		animated_sprite.play("moving")
		is_moving = true

# Returns an array of marker transforms
func get_marker_transforms()->Array[Transform2D]:
	if is_shooting_two_at_a_time:
		# Only reason I have all of this is because the markers are a child of a parent that was scaled, and I don't want this scaling to affect my 'texture_size' so I'll just instead create brand new transforms
		var transf1:Transform2D = Transform2D(left_marker.global_transform.get_rotation() , left_marker.global_transform.origin)
		var transf2:Transform2D = Transform2D(right_marker.global_transform.get_rotation(), right_marker.global_transform.origin)
		
		return [transf1, transf2]
	
	var center_transf = Transform2D(center_marker.global_transform.get_rotation(), center_marker.global_transform.origin)
	return [center_transf]

func get_speed_data()->Array[BulletSpeedData2D]:
	var sp_data:BulletSpeedData2D = BulletSpeedData2D.new()
	sp_data.speed = 200
	sp_data.max_speed = 300
	sp_data.acceleration = 5
	
	if is_shooting_two_at_a_time:
		return [sp_data,sp_data]
	else:
		return [sp_data]

# When the player enters the detection radius
func _on_follow_target_radius_area_2d_body_entered(body: Node2D) -> void:
	if body is Player:
		is_target_in_range = true
		animated_sprite.play("moving")
		
# When the player exits detection radius
func _on_follow_target_radius_area_2d_body_exited(body: Node2D) -> void:
	if body is Player:
		is_target_in_range = false
		animated_sprite.play("idle")

# Allows shooting after cooldown
func _on_shoot_timer_timeout() -> void:
	can_shoot=true
	is_shooting_two_at_a_time = !is_shooting_two_at_a_time # next time with different shoot behavior

func set_bullet_collision_mask(arr:Array[int])->void:
	bullets_data.collision_mask = MultiMeshBulletsData2D.calculate_bitmask(arr)
