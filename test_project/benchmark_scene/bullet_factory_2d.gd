extends BulletFactory2D

@onready var particle_scn:PackedScene = preload("res://shared/bullet_attachment_nodes/attached_particles.tscn")

# BlastBullets2D plugin created by https://x.com/realNikich / https://github.com/nikoladevelops
# For tutorials: https://www.youtube.com/@realnikich

# NOTE NEVER OVERRIDE THE _ready FUNC HERE IF YOU ARE PLANNING TO ATTACH A CUSTOM SCRIPT. YOUR GAME WILL CRASH!!
# NOTE Depending on the enemy type (whether the thing being hit by the bullets is an area or a body), you need to handle either the directional_area_entered / directional_body_entered (or the block_ variants) of the BulletFactory2D
# NOTE Ensure the enemy is in the correct collision_layer and that the bullets are also in the same collision_mask
# NOTE Static bodies can be hit by the bullets, but only if you set the data of the bullets's monitorable property to true - this is at the cost of performance of course, so it's better to stick with Area2D or other types of bodies
# Example: if enemy is in collision_layer = 3, then the bullets you are spawning should always have the collision_mask = 3 as well, otherwise they won't interact with eachother
# NOTE Handlers run synchronously in the physics tick with live instance state (no casts, no call_deferred needed for game logic).
# Only structural factory calls (reset/free_*/populate_*) must be deferred - the error message tells you when that happens.

# This function is connected to the directional_area_entered signal of the bullet factory. It is executed each time a directional bullet spawned from the factory hits an Area2D (and again in order for a thing to be hit, ensure the layers are correct!)
func _on_directional_area_entered(hit_target_area: Object, directional_bullets: DirectionalBullets2D, bullet_index: int) -> void:
	if hit_target_area is AbstractEnemy:
		var dmg_data: DamageData = directional_bullets.shared_bullets_custom_data as DamageData # We know for a fact that we have a DamageData inside our bullets, because that's how we've set them up before spawning them - we can replace it with some other custom resource instead and check for its type here too (we may spawn bullets with different custom data and have additional check logic)
		if dmg_data.is_player_owned == false: # If it wasn't the player who spawned the bullet, then that means an enemy is hitting another enemy - I want the bullet to dissapear without it damaging the enemy (No friendly fire :P)
			return
		hit_target_area.take_damage(dmg_data.base_damage) # You can do way more complex damage logic with the rest of the properties inside the custom data, you can do anything..
		#print("Bullet just collided with an enemy area")
	#else:
		#print("Bullet just collided with an area")

# This function is connected to the directional_body_entered signal of the bullet factory.  It is executed each time a directional bullet spawned from the factory hits a body (and again in order for a thing to be hit, ensure the layers are correct and you also have enabled the .monitorable property inside bullets data!)
func _on_directional_body_entered(hit_target_body: Object, directional_bullets: DirectionalBullets2D, bullet_index: int) -> void:
	if hit_target_body is Player:
		var dmg_data: DamageData = directional_bullets.shared_bullets_custom_data as DamageData
		hit_target_body.take_damage(dmg_data.base_damage)

	#print("Bullet just collided with a body")

func _on_directional_life_time_over(_directional_bullets: DirectionalBullets2D, _bullet_indexes: Array[int]) -> void:
	pass
	# Only if is_life_time_over_signal_enabled is set inside the bullets multimesh data
	#print(directional_bullets)
	#print(bullet_indexes)
	# Just a small example that you can spawn particles or other things in the same exact position where the bullet got disabled (its life time got to 0). Transforms are one instance call away via get_bullet_global_transform().
	#for i in _bullet_indexes:
		#var instance:Node2D = particle_scn.instantiate()
		#instance.global_transform = _directional_bullets.get_bullet_global_transform(i)
		#get_tree().root.add_child(instance)

# Same handlers for block bullets - same slim signature, typed BlockBullets2D instance, no casts.
func _on_block_area_entered(hit_target_area: Object, block_bullets: BlockBullets2D, bullet_index: int) -> void:
	if hit_target_area is AbstractEnemy:
		var dmg_data: DamageData = block_bullets.shared_bullets_custom_data as DamageData
		if dmg_data.is_player_owned == false:
			return
		hit_target_area.take_damage(dmg_data.base_damage)

func _on_block_body_entered(hit_target_body: Object, block_bullets: BlockBullets2D, bullet_index: int) -> void:
	if hit_target_body is Player:
		var dmg_data: DamageData = block_bullets.shared_bullets_custom_data as DamageData
		hit_target_body.take_damage(dmg_data.base_damage)

func _on_block_life_time_over(_block_bullets: BlockBullets2D, _bullet_indexes: Array[int]) -> void:
	pass
