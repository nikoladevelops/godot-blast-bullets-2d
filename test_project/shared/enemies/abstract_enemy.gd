class_name AbstractEnemy
extends Area2D

# The default bullet collision mask that the enemy is spawned with
var default_bullet_collision_mask:Array[int] = [1, 3] # interacts only with player and enemy by default

# Displays the current health of the enemy. Each enemy needs a health bar.
var health_bar:HealthBarComponent

# Emitted when the enemy gets destroyed (health becomes 0)
signal died

# Where a bullet fired from `muzzle` spawns: its whole hitbox ahead of the muzzle (the art faces +X),
# so it never starts overlapping the enemy that fired it. Enemy bullets also hit enemies (collision
# layer 3), and a bullet spawned inside its own shooter would hit it at once and die on the spot.
static func ahead_of_muzzle(muzzle:Transform2D, data:BulletVolleyData2D, clearance:float = 6.0)->Transform2D:
	var half_length := 0.0
	var box := data.collision_shape as RectangleShape2D
	if box != null:
		half_length = box.size.x * 0.5
	var forward := Vector2.RIGHT.rotated(muzzle.get_rotation())
	return Transform2D(muzzle.get_rotation(), muzzle.origin + forward * (half_length + clearance))

# Called whenever the enemy needs to be freed from the scene
func die()->void:
	BENCHMARK_GLOBALS.explode_at(global_position)
	emit_signal("died")
	queue_free()

# Call whenever you want to damage the enemy
func take_damage(dmg_amount:int)->void:
	health_bar.take_damage(dmg_amount)
	
	if health_bar.health == 0:
		die()

# Sets a brand new collision mask based on the integers passed / Should be overriden in child to provide correct implementation based on bullet type
func set_bullet_collision_mask(_arr:Array[int])->void:
	pass
