class_name AbstractEnemy
extends Area2D

# The default bullet collision mask that the enemy is spawned with
var default_bullet_collision_mask:Array[int] = [1, 3] # interacts only with player and enemy by default

# Displays the current health of the enemy. Each enemy needs a health bar.
var health_bar:HealthBarComponent

# Emitted when the enemy gets destroyed (health becomes 0)
signal died

# Called whenever the enemy needs to be freed from the scene
func die()->void:
	emit_signal("died")
	queue_free()

# Call whenever you want to damage the enemy
func take_damage(dmg_amount:int)->void:
	health_bar.take_damage(dmg_amount)
	
	if health_bar.health == 0:
		die()

# Sets a brand new collision mask based on the integers passed / Should be overriden in child to provide correct implementation based on bullet type
func set_bullet_collision_mask(arr:Array[int])->void:
	pass
