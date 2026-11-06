extends RefCounted
## Effects shared by the examples: hit sparks, explosions and their sounds,
## all played through the factory (BulletFactory2D.spawn_layer_effect and
## play_sound), so they pool, mix and pause like the bullets do.

const HIT_SPARK := preload("res://shared/effects/enemy_hit_spark_effect.tres")
const EXPLOSION := preload("res://shared/effects/explosion_effect.tres")
## spawn_layer_effect takes the position and rotation of its pose, never its
## scale: a bigger blast is a copy of the layer with a larger scale.
static var _sized := {}
const HIT_SOUND := preload("res://shared/effects/hit_sound.tres")
const EXPLOSION_SOUND := preload("res://shared/effects/explosion_sound.tres")


static func hit_spark(factory: BulletFactory2D, at: Vector2) -> void:
	factory.spawn_layer_effect(HIT_SPARK, Transform2D(randf() * TAU, at))
	factory.play_sound(HIT_SOUND, at)


static func explosion(factory: BulletFactory2D, at: Vector2, size := 1.0) -> void:
	factory.spawn_layer_effect(_explosion_layer(size), Transform2D(0.0, at)) # the layer turns it randomly
	factory.play_sound(EXPLOSION_SOUND, at)


static func _explosion_layer(size: float) -> EffectLayerData2D:
	if is_equal_approx(size, 1.0):
		return EXPLOSION
	if not _sized.has(size):
		var layer: EffectLayerData2D = EXPLOSION.duplicate()
		layer.scale = Vector2(size, size)
		_sized[size] = layer
	return _sized[size]


## A boss going down: a chain of blasts over its body, then one big one.
static func boss_death(factory: BulletFactory2D, center: Vector2, radius := 60.0) -> void:
	for i in 7:
		if not is_instance_valid(factory) or not factory.is_inside_tree():
			return
		var offset := Vector2.from_angle(randf() * TAU) * randf_range(0.2, 1.0) * radius
		explosion(factory, center + offset) # the layer varies its own size (0.85-1.15)
		await factory.get_tree().create_timer(0.12).timeout
	if is_instance_valid(factory) and factory.is_inside_tree():
		explosion(factory, center, 2.2)
