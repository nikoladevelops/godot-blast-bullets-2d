class_name ShootingEnemy
extends AbstractEnemy

@export var max_health:float = 100
# The gun that rotates to look at the player before shooting
@onready var turret_gun:Sprite2D = $TurretGun
# This marker determines the position where the bullets get spawned
@onready var bullet_marker:Marker2D = $TurretGun/BulletMarker
# Detection area used to detect whether the player is in range so that he gets shot at
@onready var detect_area:Area2D = $DetectArea2D
# Timer responsible for shoot cooldown time
@onready var shoot_timer:Timer = $ShootTimer

# Whether the turret can shoot
var can_shoot:bool

# Whether the turret can rotate and look at the player / track him
var can_look_at:bool

# The bullets data of the enemy
var bullets_data:DirectionalBulletsData2D

func _ready() -> void:
	health_bar = get_node("HealthBar")
	health_bar.set_up(max_health)
	health_bar.visible = false # Set it by default to be invisible
	
	
	bullets_data = DirectionalBulletsData2D.new()
	bullets_data.textures = [
		preload("res://shared/art/enemy_bullets/enemy_bullet1.png"),
		preload("res://shared/art/enemy_bullets/enemy_bullet2.png"),
		preload("res://shared/art/enemy_bullets/enemy_bullet3.png"),
		preload("res://shared/art/enemy_bullets/enemy_bullet4.png")
	]
	
	bullets_data.texture_size = Vector2(142,142)
	bullets_data.collision_shape_size = Vector2(34,34)
	bullets_data.default_change_texture_time = 0.09
	
	bullets_data.max_life_time = 2
	
	var sp_data:BulletSpeedData2D = BulletSpeedData2D.new()
	sp_data.speed = 200
	sp_data.max_speed = 3000
	sp_data.acceleration = 1500
	
	bullets_data.all_bullet_speed_data = [sp_data]
	
	var dmg_data:DamageData = DamageData.new()
	dmg_data.base_damage = 7
	
	bullets_data.bullets_custom_data = dmg_data
	
	bullets_data.collision_layer = DirectionalBulletsData2D.calculate_bitmask([4])
	bullets_data.collision_mask = DirectionalBulletsData2D.calculate_bitmask(default_bullet_collision_mask)
	bullets_data.monitorable=true # Monitorable allows for these bullets to be detected by other areas
	
func _physics_process(_delta: float) -> void:
	if can_look_at:
		turret_gun.look_at(BENCHMARK_GLOBALS.PLAYER.global_position)
		turret_gun.rotation += deg_to_rad(-90) # since the texture is rotated
		
	if can_shoot:
		shoot()
		can_shoot=false


func shoot()->void:
	var original_transform:Transform2D = bullet_marker.global_transform
	# Create a new transform with the same rotation and position, but scale of 1. This is because I've scaled the marker by x amount of times since it's a child of the TurretGun that is scaled (it scales all children) and I want to reverse that so that only the texture_size is taken into account
	var transf = Transform2D(original_transform.get_rotation(), original_transform.origin)
	bullets_data.transforms = [transf]
	var bullets_multi:DirectionalBullets2D = BENCHMARK_GLOBALS.FACTORY.spawn_controllable_directional_bullets(bullets_data)
	bullets_multi.all_bullets_set_attachment(BENCHMARK_GLOBALS.ATTACHMENT_SCENES[1], 1, Vector2(-30, 0))

func _on_shoot_timer_timeout() -> void:
	can_shoot=true

func _on_detect_area_2d_body_entered(body: Node2D) -> void:
	if body is Player:
		can_look_at=true
		can_shoot=true
		shoot_timer.start()

func _on_detect_area_2d_body_exited(body: Node2D) -> void:
	if body is Player:
		can_look_at=false
		can_shoot=false
		shoot_timer.stop()

func set_bullet_collision_mask(arr:Array[int])->void:
	bullets_data.collision_mask = MultiMeshBulletsData2D.calculate_bitmask(arr)
