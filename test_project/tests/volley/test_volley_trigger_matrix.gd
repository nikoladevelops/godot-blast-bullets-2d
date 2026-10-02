extends BlastTest
## Every effect trigger against the path that must fire it. On Spawn fires on
## every spawn (fresh AND pooled reuse); On Hit on counted non-killing hits;
## On Destroy on collision-kill only (never timeout); On Bounce on ricochet;
## On Lifetime Over on expiry; On Clear on manual clear only. Spawner-owned
## hits route to spawner signals, never the factory's.
## (free_active_bullets/reset are silent teardown: test_factory_clear_triggers.)

const SPAWN := 1
const HIT := 2
const DESTROY := 3
const BOUNCE := 4
const LIFETIME := 5
const CLEAR := 6


func _data(layers: Array, max_collisions: int = 0, lifetime: float = 30.0, bounce: bool = false) -> DirectionalBulletsData2D:
	var d := H.make_directional_data(1, 900.0, lifetime)
	d.transforms = [Transform2D()]
	d.monitorable = true
	d.collision_shape = H.make_circle_shape(6.0)
	d.bullet_max_collision_count = max_collisions
	if bounce:
		d.set_bounce_mask_from_array([4])
	d.effect_layers = layers
	return d


func _layer(trigger: int) -> BulletEffectLayerData2D:
	return H.make_effect_layer(trigger, 4)


func _wall() -> void:
	make_wall(Vector2(200, 0), Vector2(20, 400), 8, 2)
	await physics()


func _wait(cond: Callable, frames := 40) -> bool:
	for i in frames:
		await physics()
		if cond.call():
			return true
	return false


func test_on_spawn_factory_spawner_and_reuse() -> void:
	factory.spawn_controllable_directional_bullets(_data([_layer(SPAWN)]))
	assert_gte(factory.get_active_effect_count(), 1, "factory spawn flashes")
	factory.clear_sprite_effects()
	factory.free_active_bullets()
	var spawner := make_spawner(_data([_layer(SPAWN)]), 1)
	watch_signals(spawner)
	assert_true(spawner.shoot_once(), "spawner shoot_once fires")
	assert_signal_emitted(spawner, "volley_fired", "volley_fired observed")
	assert_gte(factory.get_active_effect_count(), 1, "spawner shot flashes")
	var vol: DirectionalBullets2D = get_signal_parameters(spawner, "volley_fired", 0)[0]
	factory.debug_reset_pool_stats()
	vol.clear_all_bullets()
	factory.clear_sprite_effects()
	assert_true(spawner.shoot_once(), "second spawner shot fires")
	assert_gte(int(factory.debug_get_pool_hit_stats().get("directional_hits", 0)), 1, "second shot reused the pool")
	assert_gte(factory.get_active_effect_count(), 1, "pooled reuse flashes again")


func test_on_hit_counted_hit() -> void:
	await _wall()
	var v: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(_data([_layer(HIT)], 0))
	factory.clear_sprite_effects()
	assert_true(await _wait(func(): return v.get_bullet_collision_count(0) >= 1), "bullet registered the hit")
	assert_gte(factory.get_active_effect_count(), 1, "On Hit sparked")


func test_on_destroy_kill_not_timeout() -> void:
	await _wall()
	var v: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(_data([_layer(DESTROY)], 1))
	factory.clear_sprite_effects()
	await _wait(func(): return not v.is_bullet_status_enabled(0))
	assert_false(v.is_bullet_status_enabled(0), "killing blow disabled the bullet")
	assert_gte(factory.get_active_effect_count(), 1, "On Destroy detonated")


func test_on_destroy_never_on_timeout() -> void:
	var v: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(_data([_layer(DESTROY)], 0, 0.3))
	factory.clear_sprite_effects()
	await _wait(func(): return not v.is_bullet_status_enabled(0))
	assert_false(v.is_bullet_status_enabled(0), "timeout disabled the bullet")
	assert_eq(factory.get_active_effect_count(), 0, "timeout never detonates")


func test_on_lifetime_over() -> void:
	factory.spawn_controllable_directional_bullets(_data([_layer(LIFETIME)], 0, 0.3))
	factory.clear_sprite_effects()
	assert_true(await _wait(func(): return factory.get_active_effect_count() >= 1, 60), "On Lifetime Over fizzled on expiry")


func test_on_clear() -> void:
	var v: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(_data([_layer(CLEAR)], 0, 60.0))
	factory.clear_sprite_effects()
	assert_true(v.clear_bullet(0), "clear_bullet returns true")
	assert_gte(factory.get_active_effect_count(), 1, "On Clear fired")


func test_on_bounce() -> void:
	await _wall()
	var v: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(_data([_layer(BOUNCE)], 0, 60.0, true))
	factory.clear_sprite_effects()
	assert_true(await _wait(func(): return v.bullet_get_bounce_count(0) >= 1, 60), "bullet bounced")
	assert_gte(factory.get_active_effect_count(), 1, "On Bounce sparked")


func test_spawner_hits_route_to_spawner_only() -> void:
	await _wall()
	var spawner := make_spawner(_data([], 0, 60.0), 1)
	watch_signals(factory)
	watch_signals(spawner)
	assert_true(spawner.shoot_once(), "spawner shot fires")
	await _wait(func(): return get_signal_emit_count(spawner, "body_entered") >= 1)
	assert_signal_emitted(spawner, "body_entered", "spawner body_entered fired")
	assert_signal_not_emitted(factory, "directional_body_entered", "factory signal silent for a spawner volley")
