class_name BENCHMARK_GLOBALS

### The idea of this class is to have a bunch of things that can be accessed from any script
### The set up of everything that is held here should happen in the main script of the scene / meaning when everything is already loaded

# A bunch of bullet types that the player can choose from
enum BulletType{
	MultiMeshDirectional,
	MultiMeshBlock,
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
