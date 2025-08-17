class_name EnemySpawner
extends Node2D

# The wait time before a new enemy gets spawned
@export var spawn_timer_wait_time:float = 2.0
# The max amount of enemies that are alive, before the spawner stops working - you will need to kill at least a single enemy when reaching this limit in order for the spawner to begin working again
@export var max_enemies_amount:int=15
# Spawn timer used to count down the seconds before a new enemy is instantiated and added to the scene
@onready var spawn_timer:Timer = $Timer
# The enemy scene being instantiated each time the spawn timer times out. TO WORK CORRECTLY NEEDS TO BE AN ABSTRACT ENEMY WHEN INITIALIZED
@export var enemy_scn:PackedScene
# Holds all spawned enemies by the spawner
@onready var enemy_container:Node = $EnemyContainer
# The rect that defines the area in which enemies are being spawned at random positions
@onready var spawner_area_rect:ColorRect = $SpawnerAreaRect

# The amount of enemies that are currently alive
var current_enemies_amount:int = 0

# All enemies being spawned will have this collision mask for their bullets
var default_collision_mask_for_enemy_bullets_being_spawned:Array[int] = [1,3] # by default enemy bullets collide with both the player and other enemies

func _ready() -> void:
	spawn_timer.wait_time = spawn_timer_wait_time
	spawn_timer.autostart = true
	spawn_timer.start()

# When the timer runs out, spawn a new enemy
func _on_timer_timeout() -> void:
	spawn_enemy()

# Adds a brand new enemy instance to the scene tree, but only if the max_enemies_amount is not reached
func spawn_enemy()->void:
	if current_enemies_amount == max_enemies_amount:
		return
	
	var enemy_instance:AbstractEnemy = enemy_scn.instantiate()
	
	var rand_x:float = randf_range(spawner_area_rect.global_position.x, spawner_area_rect.global_position.x + spawner_area_rect.size.x)
	var rand_y:float = randf_range(spawner_area_rect.global_position.y, spawner_area_rect.global_position.y + spawner_area_rect.size.y)
	
	enemy_instance.global_position = Vector2(rand_x, rand_y)
	enemy_instance.z_index = 1
	
	current_enemies_amount+=1
	enemy_instance.died.connect(func(): current_enemies_amount-=1)
	
	enemy_instance.default_bullet_collision_mask = default_collision_mask_for_enemy_bullets_being_spawned # sets the default collision mask for the bullets that the enemy is spawned with
	enemy_container.add_child(enemy_instance)

# Changes the bullet collision mask of all spawned enemies and also changes the default collision mask that enemies are spawned with
func set_enemy_default_bullet_collision_mask(new_collision_mask:Array[int])->void:
	default_collision_mask_for_enemy_bullets_being_spawned = new_collision_mask
	call_deferred("change_bullet_collision_mask_for_existing_enemies", new_collision_mask)
	
# Changes the collision mask for all existing enemies spawned by this particular spawner
func change_bullet_collision_mask_for_existing_enemies(new_collision_mask:Array[int])->void:
	for child in enemy_container.get_children():
		var enemy:AbstractEnemy = child as AbstractEnemy
		enemy.set_bullet_collision_mask(new_collision_mask)
