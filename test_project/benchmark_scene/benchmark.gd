extends Node2D

@onready var enemy_spawners_container:Node = $EnemySpawnersContainer

# The save path at which the save file is being saved/loaded from
var save_path:String = OS.get_user_data_dir() + "/test.tres"; # could be either .tres or .res

# Whether the save/load button are still doing work -- used to prevent the user from spamming those buttons which would lead to crashes
var is_curently_saving_or_loading:bool = false

func _ready() -> void:
	# This code here is the same as going into menu Debug->Visible Collision Shapes and setting it to true
	# Basically makes all collision shapes visible
	if(get_tree().is_debugging_collisions_hint() == false):
		get_tree().set_debug_collisions_hint(true) 

	BENCHMARK_GLOBALS.FACTORY = $BulletFactory2D
	BENCHMARK_GLOBALS.BULLET_TYPE_TO_SPAWN = BENCHMARK_GLOBALS.BulletType.MultiMeshDirectional # set the default current bullet type that needs to be spawned
	BENCHMARK_GLOBALS.PLAYER = $Player
	BENCHMARK_GLOBALS.PLAYER_DATA_NODE = $Player/PlayerDataNode
	BENCHMARK_GLOBALS.UI = $UI
	BENCHMARK_GLOBALS.PLAYER_HEALTH_BAR = $UI/AlwaysVisibleView/HealthBar
	BENCHMARK_GLOBALS.ALL_GODOT_AREA2D_BULLETS_CONTAINER = $AllGodotArea2DBulletsContainer
	
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
	
	
	
	BENCHMARK_GLOBALS.FACTORY.save_finished.connect(_on_factory_save_finished)
	
	BENCHMARK_GLOBALS.UI.save_btn.pressed.connect(_on_save_btn_pressed)
		
	BENCHMARK_GLOBALS.UI.load_btn.pressed.connect(_on_load_btn_pressed)
	
func _on_save_btn_pressed():
	BENCHMARK_GLOBALS.FACTORY.save();
	
	BENCHMARK_GLOBALS.UI.disable_or_enable_factory_btn.switch_to_option_index(0)

func _on_load_btn_pressed():
	var factory_data:SaveDataBulletFactory2D = ResourceLoader.load(save_path)
	if factory_data == null:
		print("Bullets data from file was invalid, loading failed.")
	else:
		BENCHMARK_GLOBALS.FACTORY.load(factory_data)
		
		BENCHMARK_GLOBALS.UI.disable_or_enable_factory_btn.switch_to_option_index(0)
	
	
func _on_factory_save_finished(factory_data:SaveDataBulletFactory2D):
	if factory_data == null:
		print("Bullets data saving failed.")
	else:
		ResourceSaver.save(factory_data, save_path)
