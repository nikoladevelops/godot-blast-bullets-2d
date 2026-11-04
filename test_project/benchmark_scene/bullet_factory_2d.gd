extends BulletFactory2D

@onready var particle_scn:PackedScene = preload("res://shared/bullet_attachment_nodes/attached_particles.tscn")

## Prints one line per hit. Off by default: printing on every hit (thousands
## per second in a bullet hell) dominates frame time and distorts the FPS this
## benchmark exists to measure.
@export var log_hits := false

# BlastBullets2D plugin created by https://x.com/realNikich / https://github.com/nikoladevelops
# For tutorials: https://www.youtube.com/@realnikich

# NOTE A script may define its own _ready / _physics_process / _process: the factory's own work runs next to them.
# NOTE Depending on the enemy type (whether the thing being hit by the bullets is an area or a body), you need to handle either the area_entered / body_entered signals of the BulletFactory2D
# NOTE Ensure the enemy is in the correct collision_layer and that the bullets are also in the same collision_mask
# NOTE Static bodies can be hit by the bullets, but only if you set the data of the bullets's monitorable property to true - this is at the cost of performance of course, so it's better to stick with Area2D or other types of bodies
# Example: if enemy is in collision_layer = 3, then the bullets you are spawning should always have the collision_mask = 3 as well, otherwise they won't interact with eachother
# NOTE Handlers run synchronously in the physics tick with live instance state (no casts, no call_deferred needed for game logic).
# Only structural factory calls (reset/free_*/populate_*) must be deferred - the error message tells you when that happens.

# This function is connected to the area_entered signal of the bullet factory. It is executed each time a bullet spawned from the factory hits an Area2D (and again in order for a thing to be hit, ensure the layers are correct!)
func _on_area_entered(hit_target_area: Object, volley: BulletVolley2D, _bullet_index: int) -> void:
	if log_hits:
		print("BulletFactory2D hit an area!")
	_damage_enemy(hit_target_area, volley)

# This function is connected to the body_entered signal of the bullet factory.  It is executed each time a bullet spawned from the factory hits a body (and again in order for a thing to be hit, ensure the layers are correct and you also have enabled the .monitorable property inside bullets data!)
func _on_body_entered(hit_target_body: Object, volley: BulletVolley2D, _bullet_index: int) -> void:
	if log_hits:
		print("BulletFactory2D hit a body!")
	if hit_target_body is Player:
		var dmg_data: DamageData = volley.shared_bullets_custom_data as DamageData
		hit_target_body.take_damage(dmg_data.base_damage)
		return
	# An enemy can be hit through one of its child bodies too (the turret has a StaticBody2D next to its
	# Area2D): with monitorable on, a bullet's single hit may land on that body first.
	_damage_enemy(hit_target_body, volley)

	#print("Bullet just collided with a body")

# The enemy that owns `hit` (the hit node itself, or its nearest AbstractEnemy ancestor), damaged by a
# player bullet. Enemy bullets never hurt enemies (no friendly fire).
func _damage_enemy(hit: Object, volley: BulletVolley2D) -> void:
	var node := hit as Node
	while node != null and not node is AbstractEnemy:
		node = node.get_parent()
	if node == null:
		return
	var dmg_data: DamageData = volley.shared_bullets_custom_data as DamageData # We know for a fact that we have a DamageData inside our bullets, because that's how we've set them up before spawning them
	if dmg_data == null or dmg_data.is_player_owned == false: # an enemy hitting another enemy: the bullet disappears without damage (no friendly fire)
		return
	(node as AbstractEnemy).take_damage(dmg_data.base_damage)


func _on_life_time_over(_volley: BulletVolley2D, _bullet_indexes: Array[int]) -> void:
	pass
	# Only if is_life_time_over_signal_enabled is set inside the bullets multimesh data
	#print(volley)
	#print(bullet_indexes)
	# Just a small example that you can spawn particles or other things in the same exact position where the bullet got disabled (its life time got to 0). Transforms are one instance call away via get_bullet_transform().
	#for i in _bullet_indexes:
		#var instance:Node2D = particle_scn.instantiate()
		#instance.global_transform = _volley.get_bullet_transform(i)
		#get_tree().root.add_child(instance)
