extends RefCounted
## Effects shared by the examples: hit sparks, explosions and their sounds,
## all played through the factory (BulletFactory2D.spawn_layer_effect and
## play_sound), so they pool, mix and pause like the bullets do.

const HIT_SPARK := preload("res://shared/effects/enemy_hit_spark_effect.tres")
const EXPLOSION := preload("res://shared/effects/explosion_effect.tres")
const HIT_SOUND := preload("res://shared/effects/hit_sound.tres")
const EXPLOSION_SOUND := preload("res://shared/effects/explosion_sound.tres")


static func hit_spark(factory: BulletFactory2D, at: Vector2) -> void:
	factory.spawn_layer_effect(HIT_SPARK, Transform2D(randf() * TAU, at))
	factory.play_sound(HIT_SOUND, at)


static func explosion(factory: BulletFactory2D, at: Vector2, size := 1.0) -> void:
	factory.spawn_layer_effect(EXPLOSION, Transform2D(randf() * TAU, Vector2(size, size), 0.0, at))
	factory.play_sound(EXPLOSION_SOUND, at)


## A boss going down: a chain of blasts over its body, then one big one.
static func boss_death(factory: BulletFactory2D, center: Vector2, radius := 60.0) -> void:
	for i in 7:
		if not is_instance_valid(factory) or not factory.is_inside_tree():
			return
		var offset := Vector2.from_angle(randf() * TAU) * randf_range(0.2, 1.0) * radius
		explosion(factory, center + offset, randf_range(0.7, 1.1))
		await factory.get_tree().create_timer(0.12).timeout
	if is_instance_valid(factory) and factory.is_inside_tree():
		explosion(factory, center, 2.2)
