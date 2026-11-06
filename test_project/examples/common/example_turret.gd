extends Node2D
## The examples' enemy turret: the barrel and the spawner turn toward the
## player (so aimed fans really aim), and your shots spark on its hurtbox.
## The barrel art and the spawner's fan point down at rest (rotation 0).

@export var turn_speed_degrees := 220.0
const REST_AIM := PI / 2 # down: where the barrel and the fan point at rest

var _aim := 0.0
@onready var _barrel: Node2D = $EnemyTurret
@onready var _spawner: Node2D = $Spawner
@onready var _barrel_rest: float = _barrel.rotation
@onready var _spawner_rest: float = _spawner.rotation


func _ready() -> void:
	add_to_group(&"enemy") # the player's homing preset hunts this group
	var hurtbox := Area2D.new()
	hurtbox.name = "Hurtbox"
	hurtbox.collision_layer = 4 # the layer the player's bullets hit
	hurtbox.collision_mask = 0
	var shape := CollisionShape2D.new()
	var circle := CircleShape2D.new()
	circle.radius = 34.0
	shape.shape = circle
	hurtbox.add_child(shape)
	add_child(hurtbox)
	var factory := _factory()
	if factory != null:
		factory.area_entered.connect(_on_bullet_hit)


func _physics_process(delta: float) -> void:
	var player := get_tree().get_first_node_in_group(&"player") as Node2D
	if player == null:
		return
	var wanted := (player.global_position - global_position).angle() - REST_AIM
	_aim = rotate_toward(_aim, wanted, deg_to_rad(turn_speed_degrees) * delta)
	_barrel.rotation = _barrel_rest + _aim
	_spawner.rotation = _spawner_rest + _aim


## The player's shots reach the factory (the gun has no handler of its own).
func _on_bullet_hit(target: Object, volley: BulletVolley2D, bullet_index: int) -> void:
	if target != get_node_or_null("Hurtbox"):
		return
	_barrel.modulate = Color(2.5, 1.6, 1.2)
	create_tween().tween_property(_barrel, "modulate", Color.WHITE, 0.15)
	preload("res://examples/common/example_fx.gd").hit_spark(_factory(), volley.get_bullet_transform(bullet_index).origin)


func _factory() -> BulletFactory2D:
	return get_parent().get_node_or_null("BulletFactory2D") as BulletFactory2D
