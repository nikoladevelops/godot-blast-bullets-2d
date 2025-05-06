extends BulletFactory2D

@onready var particle_scn:PackedScene = preload("res://shared/bullet_attachment_nodes/attached_particles.tscn")

# BlastBullets2D plugin created by https://x.com/realNikich / https://github.com/nikoladevelops
# For tutorials: https://www.youtube.com/@realnikich

# NOTE NEVER OVERRIDE THE _ready FUNC HERE IF YOU ARE PLANNING TO ATTACH A CUSTOM SCRIPT. YOUR GAME WILL CRASH!!
# NOTE Depending on the enemy type (whether the thing being hit by the bullets is an area or a body), you need to handle either the _on_area_entered or _on_body_entered of the BulletFactory2D
# NOTE Ensure the enemy is in the correct collision_layer and that the bullets are also in the same collision_mask
# NOTE Static bodies can be detected by the bullets, but only if you set their monitorable to true - this is at the cost of performance of course, so it's better to stick with Area2D or other types of bodies
# Example: if enemy is in collision_layer = 3, then the bullets you are spawning should always have the collision_mask = 3 as well, otherwise they won't interact with eachother

# This function is connected to the area_entered signal of the bullet factory. It is executed each time a bullet spawned from the factory hits an Area2D (and again in order for a thing to be hit, ensure the layers are correct!)
func _on_area_entered(enemy_area: Object, bullets_custom_data: Resource, _bullet_global_transform: Transform2D) -> void:
	if enemy_area is AbstractEnemy:
		var dmg_data:DamageData = bullets_custom_data as DamageData # We know for a fact that we have a DamageData inside our bullets, because that's how we've set them up before spawning them - we can replace it with some other custom resource instead and check for its type here too (we may spawn bullets with different custom data and have additional check logic)
		if dmg_data.is_player_owned == false: # If it wasn't the player who spawned the bullet, then that means an enemy is hitting another enemy - I want the bullet to dissapear without it damaging the enemy (No friendly fire :P)
			return
		enemy_area.take_damage(dmg_data.base_damage) # You can do way more complex damage logic with the rest of the properties inside bullets_custom_data, you can do anything..
		#print("Bullet just collided with an enemy area")
	#else:
		#print("Bullet just collided with an area")

# This function is connected to the body_entered signal of the bullet factory.  It is executed each time a bullet spawned from the factory hits a body (and again in order for a thing to be hit, ensure the layers are correct and you also have enabled the .monitorable property inside bullets data!)
func _on_body_entered(enemy_body: Object, bullets_custom_data: Resource, _bullet_global_transform: Transform2D) -> void:
	if enemy_body is Player:
		var dmg_data:DamageData = bullets_custom_data as DamageData
		enemy_body.take_damage(dmg_data.base_damage)
		
	#print("Bullet just collided with a body")

# This function will only be executed if the bullets_data has the is_life_time_over_signal_enabled property set to TRUE
func _on_life_time_over(bullets_custom_data: Resource, all_bullet_global_transforms: Array) -> void:
	pass
	# Just a small example that you can spawn particles or other things in the same exact position where the bullet got disabled (its life time got to 0)
	#for t in all_bullet_global_transforms:
		#var instance:Node2D = particle_scn.instantiate()
		#instance.global_transform = t
		#get_tree().root.add_child(instance)
