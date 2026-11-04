class_name BENCHMARK_GLOBALS

### The idea of this class is to have a bunch of things that can be accessed from any script
### The set up of everything that is held here should happen in the main script of the scene / meaning when everything is already loaded

# A bunch of bullet types that the player can choose from
enum BulletType{
	BulletVolley,
	GodotArea2D
	}

# Settings for whether the player and enemy bullets can interact with eachother and in what way
enum BulletOnBulletCollision{
	NO,
	DESTROY_PLAYER_BULLETS,
	DESTROY_PLAYER_AND_ENEMY_BULLETS
}

# The bullet factory that is used to spawn bullets
static var FACTORY:BulletFactory2D

# The current bullet type that the player should spawn
static var BULLET_TYPE_TO_SPAWN:BulletType

# The one and only player
static var PLAYER:Node2D

# The player data
static var PLAYER_DATA_NODE:PlayerData

# The health bar of the player
static var PLAYER_HEALTH_BAR:HealthBarComponent

# The user interface
static var UI:BenchmarkUI

# Contains all enemy spawners - the nodes that are used to spawn enemies periodically
static var ALL_ENEMY_SPAWNERS:Array[EnemySpawner]

# Contains all spawned area2d bullets
static var ALL_GODOT_AREA2D_BULLETS_CONTAINER:Node

# A stationary target used for homing bullets tests
static var STATIONARY_TARGET:Node2D

# First Moving target
static var MOVING_TARGET_ONE:Node2D

# Second moving target
static var MOVING_TARGET_TWO:Node2D

# Holds movement Path2Ds
static var MOVEMENT_PATH_HOLDER:Node

# Holds all attachment scenes
static var ATTACHMENT_SCENES:Dictionary[int, PackedScene]

# Effects every scene shares (sprite effect layers played by the plugin, sounds through its mixer)
const EXPLOSION_EFFECT:EffectLayerData2D = preload("res://shared/effects/explosion_effect.tres")
const EXPLOSION_SOUND:BulletSoundData2D = preload("res://shared/effects/explosion_sound.tres")
const PLAYER_HIT_SPARK:EffectLayerData2D = preload("res://shared/effects/player_hit_spark_effect.tres")
const ENEMY_HIT_SPARK:EffectLayerData2D = preload("res://shared/effects/enemy_hit_spark_effect.tres")
# A hit that kills the bullet fires On Destroy, never On Hit (EffectLayerData2D.trigger): these play the
# same spark on the killing hit, so every hit sparks once, whether the bullet survives it or not.
const PLAYER_KILL_SPARK:EffectLayerData2D = preload("res://shared/effects/player_kill_spark_effect.tres")
const ENEMY_KILL_SPARK:EffectLayerData2D = preload("res://shared/effects/enemy_kill_spark_effect.tres")
const MUZZLE_FLASH:EffectLayerData2D = preload("res://shared/effects/muzzle_flash_effect.tres")

# Plays the explosion one-shot (and its sound) at a position, through the factory.
static func explode_at(at:Vector2, size:float = 1.0)->void:
	if FACTORY == null or not is_instance_valid(FACTORY) or not FACTORY.is_inside_tree():
		return
	FACTORY.spawn_layer_effect(EXPLOSION_EFFECT, Transform2D(0.0, Vector2(size, size), 0.0, at))
	FACTORY.play_sound(EXPLOSION_SOUND, at)
