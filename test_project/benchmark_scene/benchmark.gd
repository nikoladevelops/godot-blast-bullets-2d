extends Node2D

@onready var enemy_spawners_container:Node = $EnemySpawnersContainer

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
	BENCHMARK_GLOBALS.BULLET_TYPE_TO_SPAWN = BENCHMARK_GLOBALS.BulletType.MultiMeshDirectional # set the default current bullet type that needs to be spawned
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
	var initial_block_debugger_color:Color = BENCHMARK_GLOBALS.FACTORY.block_bullets_debugger_color
	var initial_directional_debugger_color:Color = BENCHMARK_GLOBALS.FACTORY.directional_bullets_debugger_color
	
	BENCHMARK_GLOBALS.UI.change_block_debugger_btn_color(initial_block_debugger_color)
	BENCHMARK_GLOBALS.UI.block_debugger_color_picker.color = initial_block_debugger_color
	
	BENCHMARK_GLOBALS.UI.change_directional_debugger_btn_color(initial_directional_debugger_color)
	BENCHMARK_GLOBALS.UI.directional_debugger_color_picker.color = initial_directional_debugger_color
	
	BENCHMARK_GLOBALS.UI.enable_debugger_checkbox.button_pressed = BENCHMARK_GLOBALS.FACTORY.is_debugger_enabled
	
	# Add all spawners so they are available globally
	for child in enemy_spawners_container.get_children():
		var spawner:EnemySpawner = child as EnemySpawner
		BENCHMARK_GLOBALS.ALL_ENEMY_SPAWNERS.push_back(spawner)
	
