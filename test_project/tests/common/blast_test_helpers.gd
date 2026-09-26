class_name BlastTestHelpers
extends RefCounted
## Shared builders for the BlastBullets2D headless suites.
## Every suite is a SceneTree script; include this via preload and call the
## static makers so spawn data stays identical across suites (same speeds,
## layers, texture_size) and failures mean real regressions, not drift.
## NOTE: sprite_frames is intentionally left null (error path is covered by
## the engine); texture_size keeps volleys visible-sized for the debugger.

static func make_directional_data(n: int = 4, speed: float = 200.0, lifetime: float = 5.0) -> DirectionalBulletsData2D:
	var data := DirectionalBulletsData2D.new()
	var arr: Array = []
	for i in n:
		arr.append(Transform2D(0.0, Vector2(24.0 * i, 0.0)))
	data.transforms = arr
	# Strict indexing: one speed entry per bullet (entry i drives bullet i).
	var speeds: Array = []
	for i in n:
		var sp := BulletSpeedData2D.new()
		sp.speed = speed
		sp.max_speed = 3000.0
		sp.acceleration = 0.0
		speeds.append(sp)
	data.all_bullet_speed_data = speeds
	data.max_life_time = lifetime
	data.texture_size = Vector2(16, 16)
	data.set_collision_layer_from_array([2])
	data.set_collision_mask_from_array([4])
	return data

static func make_block_data(n: int = 4, speed: float = 200.0, lifetime: float = 5.0) -> BlockBulletsData2D:
	var data := BlockBulletsData2D.new()
	var arr: Array = []
	for i in n:
		arr.append(Transform2D(0.0, Vector2(24.0 * i, 0.0)))
	data.transforms = arr
	var sp := BulletSpeedData2D.new()
	sp.speed = speed
	sp.max_speed = 3000.0
	sp.acceleration = 0.0
	data.block_speed = sp
	data.block_rotation_radians = 0.0
	data.max_life_time = lifetime
	data.texture_size = Vector2(16, 16)
	data.set_collision_layer_from_array([2])
	data.set_collision_mask_from_array([4])
	return data

static func finite_volley(v: Array) -> bool:
	for t in v:
		if not (t as Transform2D).is_finite():
			return false
	return true

static func make_circle_shape(radius: float = 8.0) -> CircleShape2D:
	var c := CircleShape2D.new()
	c.radius = radius
	return c
