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
const ANY_HIT := 9


func _data(layers: Array, max_collisions: int = 0, lifetime: float = 30.0, bounce: bool = false) -> BulletVolleyData2D:
	var d := H.make_volley_data(1, 900.0, lifetime)
	d.transforms = [Transform2D()]
	d.monitorable = true
	d.collision_shape = H.make_circle_shape(6.0)
	d.bullet_max_collision_count = max_collisions
	if bounce:
		d.set_bounce_mask_from_array([4])
	d.effect_layers = layers
	return d


func _layer(trigger: int) -> EffectLayerData2D:
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
	factory.spawn_volley(_data([_layer(SPAWN)]))
	assert_eq(factory.get_active_effect_count(), 1, "factory spawn flashes")
	factory.clear_sprite_effects()
	factory.free_active_bullets()
	var spawner := make_spawner(_data([_layer(SPAWN)]), 1)
	watch_signals(spawner)
	assert_true(spawner.shoot_once(), "spawner shoot_once fires")
	assert_signal_emitted(spawner, "volley_fired", "volley_fired observed")
	assert_eq(factory.get_active_effect_count(), 1, "spawner shot flashes")
	var vol: BulletVolley2D = get_signal_parameters(spawner, "volley_fired", 0)[0]
	factory.debug_reset_pool_stats()
	vol.clear_all_bullets()
	factory.clear_sprite_effects()
	assert_true(spawner.shoot_once(), "second spawner shot fires")
	assert_eq(int(factory.debug_get_pool_hit_stats().get("hits", 0)), 1, "second shot reused the pool")
	assert_eq(factory.get_active_effect_count(), 1, "pooled reuse flashes again")


func test_on_hit_counted_hit() -> void:
	handle_events(factory) # the hits / grazes / expiries are handled; this test pins something else
	await _wall()
	var v: BulletVolley2D = factory.spawn_volley(_data([_layer(HIT)], 0))
	factory.clear_sprite_effects()
	assert_true(await _wait(func(): return v.get_bullet_collision_count(0) >= 1), "bullet registered the hit")
	assert_eq(factory.get_active_effect_count(), 1, "On Hit sparked")


func test_on_destroy_kill_not_timeout() -> void:
	handle_events(factory) # the hits / grazes / expiries are handled; this test pins something else
	await _wall()
	var v: BulletVolley2D = factory.spawn_volley(_data([_layer(DESTROY)], 1))
	factory.clear_sprite_effects()
	await _wait(func(): return not v.is_bullet_status_enabled(0))
	assert_false(v.is_bullet_status_enabled(0), "killing blow disabled the bullet")
	assert_eq(factory.get_active_effect_count(), 1, "On Destroy detonated")


func test_on_destroy_never_on_timeout() -> void:
	var v: BulletVolley2D = factory.spawn_volley(_data([_layer(DESTROY)], 0, 0.3))
	factory.clear_sprite_effects()
	await _wait(func(): return not v.is_bullet_status_enabled(0))
	assert_false(v.is_bullet_status_enabled(0), "timeout disabled the bullet")
	assert_eq(factory.get_active_effect_count(), 0, "timeout never detonates")


func test_on_lifetime_over() -> void:
	factory.spawn_volley(_data([_layer(LIFETIME)], 0, 0.3))
	factory.clear_sprite_effects()
	assert_true(await _wait(func(): return factory.get_active_effect_count() >= 1, 60), "On Lifetime Over fizzled on expiry")


func test_on_clear() -> void:
	var v: BulletVolley2D = factory.spawn_volley(_data([_layer(CLEAR)], 0, 60.0))
	factory.clear_sprite_effects()
	assert_true(v.clear_bullet(0), "clear_bullet returns true")
	assert_eq(factory.get_active_effect_count(), 1, "On Clear fired")


func test_on_bounce() -> void:
	handle_events(factory) # the hits / grazes / expiries are handled; this test pins something else
	await _wall()
	var v: BulletVolley2D = factory.spawn_volley(_data([_layer(BOUNCE)], 0, 60.0, true))
	factory.clear_sprite_effects()
	assert_true(await _wait(func(): return v.bullet_get_bounce_count(0) >= 1, 60), "bullet bounced")
	assert_eq(factory.get_active_effect_count(), 1, "On Bounce sparked")


func test_spawner_hits_route_to_spawner_only() -> void:
	await _wall()
	var spawner := make_spawner(_data([], 0, 60.0), 1)
	watch_signals(factory)
	watch_signals(spawner)
	assert_true(spawner.shoot_once(), "spawner shot fires")
	await _wait(func(): return get_signal_emit_count(spawner, "body_entered") >= 1)
	assert_signal_emitted(spawner, "body_entered", "spawner body_entered fired")
	assert_signal_not_emitted(factory, "body_entered", "factory signal silent for a spawner volley")


func test_on_any_hit_is_appended_and_visible_in_the_trigger_hint() -> void:
	assert_eq(EffectLayerData2D.EFFECT_ON_ANY_HIT, 9, "appended after On Split (serialized ids)")
	var hint := ""
	for p in EffectLayerData2D.new().get_property_list():
		if p.get("name", "") == "trigger":
			hint = String(p.get("hint_string", ""))
	assert_true(hint.ends_with(",On Any Hit"), "the inspector lists On Any Hit last")
	@warning_ignore("int_as_enum_without_match") # deliberate invalid value: the API must reject it
	var l := H.make_effect_layer(10)
	expect_error_sequence(["EffectLayerData2D: trigger must be 0 (Trail Follow), 1 (On Spawn), 2 (On Hit), 3 (On Destroy), 4 (On Bounce), 5 (On Lifetime Over), 6 (On Clear), 7 (On Despawn), 8 (On Split) or 9 (On Any Hit), keeping the old value."])
	assert_eq(int(l.trigger), int(EffectLayerData2D.EFFECT_TRAIL_FOLLOW), "a rejected trigger keeps the old value")


func test_on_any_hit_fires_once_on_the_killing_blow() -> void:
	handle_events(factory) # the hit is handled; this test pins the effect
	await _wall()
	factory.debug_set_effect_log_enabled(true)
	factory.debug_clear_effect_log()
	var v: BulletVolley2D = factory.spawn_volley(_data([_layer(ANY_HIT)], 1))
	factory.clear_sprite_effects()
	await _wait(func(): return not v.is_bullet_status_enabled(0))
	assert_false(v.is_bullet_status_enabled(0), "the killing blow disabled the bullet")
	assert_eq(factory.get_active_effect_count(), 1, "exactly one effect: On Any Hit never doubles with itself")
	var rows: Array = []
	for e in factory.debug_get_effect_log():
		if int(e.get("trigger", -1)) == ANY_HIT:
			rows.append(e)
	assert_eq(rows.size(), 1, "the killing hit registered one On Any Hit fire")
	# The effect fired at the pose the physics server tested (the bullet's
	# cached transform: disable freezes it), never at a spawn or zero pose.
	var pos: Vector2 = rows[0].get("position", Vector2(-9999, -9999))
	var impact: Vector2 = v.get_bullet_transform(0).origin
	assert_almost_eq(pos.x, impact.x, 0.01, "impact pose x")
	assert_almost_eq(pos.y, impact.y, 0.01, "impact pose y")
	assert_gt(pos.x, 180.0, "the impact pose is at the wall, not the spawn point")


func test_on_any_hit_fires_once_per_survived_hit() -> void:
	handle_events(factory) # the hits are handled; this test pins the effect
	await _wall()
	make_wall(Vector2(400, 0), Vector2(20, 400), 8, 2) # a second wall in line
	await physics()
	var v: BulletVolley2D = factory.spawn_volley(_data([_layer(ANY_HIT)], 0))
	factory.clear_sprite_effects()
	assert_true(await _wait(func(): return v.get_bullet_collision_count(0) >= 2, 60), "the bullet registered both walls")
	assert_eq(factory.get_active_effect_count(), 2, "one On Any Hit per counted hit")


func test_on_any_hit_fires_on_an_area_hit() -> void:
	handle_events(factory) # the hit is handled; this test pins the effect
	make_area(Vector2(200, 0), Vector2(20, 400), 8)
	await physics()
	var v: BulletVolley2D = factory.spawn_volley(_data([_layer(ANY_HIT)], 0))
	factory.clear_sprite_effects()
	assert_true(await _wait(func(): return v.get_bullet_collision_count(0) >= 1), "the area hit counted")
	assert_eq(factory.get_active_effect_count(), 1, "On Any Hit fired on the area hit too")


func test_on_any_hit_never_fires_without_a_hit_or_from_pool_reuse() -> void:
	var d := _data([_layer(ANY_HIT)], 0, 0.2) # no wall: nothing to hit
	var v: BulletVolley2D = factory.spawn_volley(d)
	factory.clear_sprite_effects()
	assert_true(await _wait(func(): return not v.is_bullet_status_enabled(0), 60), "the expiry is not a hit")
	assert_eq(factory.get_active_effect_count(), 0, "an expiry never fires On Any Hit")
	factory.debug_reset_pool_stats()
	factory.debug_set_effect_log_enabled(true)
	factory.debug_clear_effect_log()
	var again: BulletVolley2D = factory.spawn_volley(d)
	assert_eq(again, v, "the pooled volley is reused")
	assert_eq(int(factory.debug_get_pool_hit_stats().get("hits", 0)), 1, "the second spawn came from the pool")
	factory.clear_sprite_effects()
	assert_true(await _wait(func(): return not again.is_bullet_status_enabled(0), 60), "the second life expired too")
	assert_eq(factory.get_active_effect_count(), 0, "pooled reuse alone never fires On Any Hit")
	for e in factory.debug_get_effect_log():
		assert_ne(int(e.get("trigger", -1)), ANY_HIT, "no On Any Hit row for a life that never hit")
