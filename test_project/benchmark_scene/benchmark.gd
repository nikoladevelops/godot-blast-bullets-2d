extends Node2D

@onready var enemy_spawners_container:Node = $EnemySpawnersContainer

## Prints one line per spawner hit (off by default: per-hit printing distorts
## the measured FPS).
@export var log_spawner_hits := false

@onready var attachment_scenes:Dictionary[int, PackedScene] = {
	0 : null,
	1 : preload("res://shared/bullet_attachment_nodes/attached_particles.tscn"),
	2 : preload("res://shared/bullet_attachment_nodes/attached_particles2.tscn"),
	3: preload("res://shared/bullet_attachment_nodes/light_attachment.tscn")
}

func _ready() -> void:
	# This code here is the same as going into menu Debug->Visible Collision Shapes and setting it to true
	# Basically makes all collision shapes visible
	if(get_tree().is_debugging_collisions_hint() == false):
		get_tree().set_debug_collisions_hint(true) 
	
	BENCHMARK_GLOBALS.ATTACHMENT_SCENES = attachment_scenes
	BENCHMARK_GLOBALS.FACTORY = $BulletFactory2D
	BENCHMARK_GLOBALS.BULLET_TYPE_TO_SPAWN = BENCHMARK_GLOBALS.BulletType.BulletVolley # set the default current bullet type that needs to be spawned
	BENCHMARK_GLOBALS.PLAYER = $Player
	BENCHMARK_GLOBALS.PLAYER_DATA_NODE = $Player/PlayerDataNode
	BENCHMARK_GLOBALS.UI = $UI
	BENCHMARK_GLOBALS.PLAYER_HEALTH_BAR = $UI/AlwaysVisibleView/HealthBar
	BENCHMARK_GLOBALS.ALL_GODOT_AREA2D_BULLETS_CONTAINER = $AllGodotArea2DBulletsContainer
	BENCHMARK_GLOBALS.STATIONARY_TARGET = $BulletHellRelated/StationaryTarget
	BENCHMARK_GLOBALS.MOVING_TARGET_ONE = $BulletHellRelated/MovingTargetOne
	BENCHMARK_GLOBALS.MOVING_TARGET_TWO = $BulletHellRelated/MovingTargetTwo
	BENCHMARK_GLOBALS.MOVEMENT_PATH_HOLDER = $Paths
	
	# Make sure to set the actual debugger colors to the UI buttons
	var initial_debugger_color:Color = BENCHMARK_GLOBALS.FACTORY.debugger_color
	
	BENCHMARK_GLOBALS.UI.change_debugger_btn_color(initial_debugger_color)
	BENCHMARK_GLOBALS.UI.debugger_color_picker.color = initial_debugger_color
	
	BENCHMARK_GLOBALS.UI.enable_debugger_checkbox.button_pressed = BENCHMARK_GLOBALS.FACTORY.is_debugger_enabled
	
	# Add all spawners so they are available globally
	for child in enemy_spawners_container.get_children():
		var spawner:EnemySpawner = child as EnemySpawner
		BENCHMARK_GLOBALS.ALL_ENEMY_SPAWNERS.push_back(spawner)
	


func _on_bullet_spawner_2d_area_entered(_hit_target_area: Object, _volley: BulletVolley2D, _bullet_index: int) -> void:
	if log_spawner_hits:
		print("Spawner hit an area!")


func _on_bullet_spawner_2d_body_entered(_hit_target_body: Object, _volley: BulletVolley2D, _bullet_index: int) -> void:
	if log_spawner_hits:
		print("Spawner hit a body!")


func _on_bullet_spawner_2d_bullet_grazed(target: Node2D, volley: BulletVolley2D, bullet_index: int, zone: BulletGrazeZone2D, ring_index: int) -> void:
	print("Bullet volley" + str(volley) + " at index " + str(bullet_index) + " grazed " + str(ring_index))
