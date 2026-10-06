extends BlastTest
## A volley that lived with EVERY feature switched on, expired into the pool
## and was reused for plain bullets must be indistinguishable from a volley
## that was never used: homing queues, orbit, bounce, wobble, gravity,
## curves, movement patterns, trails, timers, attachments, custom data, tint,
## spin and graze are all reset, and the reused bullets sit and fly exactly where a
## cold volley's bullets do. Guards the single reset path of the merged
## volley class (reset_transient_volley_state + reset_motion_feature_state).

const N := 4
const LOADED_LIFETIME := 0.25 # seconds: short enough to expire into the pool quickly

var _timer_fired := 0


func _on_timer() -> void:
	_timer_fired += 1


func _plain() -> BulletVolleyData2D:
	return H.make_volley_data(N, 120.0, 5.0)


func _loaded() -> BulletVolleyData2D:
	var d := H.make_volley_data(N, 120.0, LOADED_LIFETIME)
	var curves := BulletCurvesData2D.new()
	curves.movement_speed_curve = H.make_flat_curve(300.0)
	d.shared_bullet_curves_data = curves
	d.shared_bullet_rotation_data = H.make_rotation(4.0, 10.0)
	var wobble := BulletWobbleData2D.new()
	wobble.enabled = true
	wobble.amplitude = 30.0
	d.shared_bullet_wobble_data = wobble
	d.gravity = Vector2(0, 90)
	d.bounce_mask = 8
	d.bounce_strength = 0.8
	d.effect_layers = [H.make_effect_layer(BulletEffectLayerData2D.EFFECT_TRAIL_FOLLOW, 2)]
	d.shared_bullets_custom_data = Resource.new()
	d.self_modulate = Color(1, 0.2, 0.2, 1)
	d.shared_bullet_attachment = make_probe_scene()
	return d


func _pattern_curve() -> Curve2D:
	var c := Curve2D.new()
	c.add_point(Vector2.ZERO)
	c.add_point(Vector2(200, 50))
	return c


## Fields a fresh life must reproduce. Instance-specific values (generation,
## ids) are left out on purpose.
func _volley_state(v: BulletVolley2D) -> Dictionary:
	var info: Dictionary = v.debug_get_volley_info()
	var fx: Dictionary = v.debug_get_effect_layers_info()
	return {
		"amount": info.get("amount_bullets"),
		"active": info.get("active_bullets"),
		"owner": info.get("owner_spawner_id"),
		"tint": info.get("self_modulate"),
		"shared_homing": v.shared_homing_deque_check_homing_targets_amount(),
		"timers": v.debug_get_timer_count(),
		"trail_bakes": fx.get("trail_bake_count"),
		"shared_custom_data": v.get_shared_bullets_custom_data(),
		"shared_pattern": v.has_shared_movement_pattern(),
		"graze": _graze_state(v),
	}


func _graze_state(v: BulletVolley2D) -> Dictionary:
	var g: Dictionary = v.debug_get_graze_info()
	g.erase("generation") # bumped per arming/release: instance history, not state
	return g


func _bullet_state(v: BulletVolley2D, i: int) -> Dictionary:
	return {
		"bullet": v.debug_get_bullet_info(i),
		"curves": v.debug_get_curves_info(i),
		"pattern": v.debug_get_pattern_info(i),
		"wobble": v.debug_get_wobble_info(i),
		"gravity": v.debug_get_gravity_info(i),
		"bounce": v.debug_get_bounce_info(i),
		"orbit": v.debug_get_orbiting_info(i),
		"attachment": v.debug_get_attachment_info(i),
		"custom_data": v.bullet_get_custom_data(i),
		"rotation": v.get_bullet_rotation_data(i).rotation_speed,
	}


func test_reused_volley_matches_a_cold_volley() -> void:
	var v: BulletVolley2D = factory.spawn_volley(_loaded())
	assert_not_null(v, "loaded volley spawned")
	await idle(1)
	# Runtime-only features on top of the data.
	v.shared_homing_deque_push_back_global_position_target(Vector2(400, 400))
	v.bullet_homing_push_back_global_position_target(1, Vector2(-300, 0))
	v.bullet_enable_orbiting(0, 48.0, BulletVolley2D.OrbitLeft)
	v.set_shared_movement_pattern_curve(_pattern_curve())
	v.attach_time_based_function(1.0, _on_timer, true)
	v.bullet_set_custom_data(2, Resource.new())
	make_graze_target(Vector2(0, 0))
	v.graze_set_zones([H.make_graze_zone([400.0])], &"graze_targets")
	factory.bullet_grazed.connect(func(_t: Node2D, _v: BulletVolley2D, _i: int, _z: BulletGrazeZone2D, _r: int) -> void: pass)
	expect_no_errors("every feature switches on cleanly")
	for i in 120: # early break; the lifetime is 15 ticks
		await physics()
		if not bool(v.debug_get_volley_info().get("is_active", true)):
			break
	await idle(2) # the lifetime hold flushes on the idle frame
	assert_true(bool(v.debug_get_volley_info().get("is_pooled", false)), "expired into the pool")
	assert_gt(int(factory.debug_get_graze_stats()["events_total"]), 0, "the loaded life really grazed")

	var hits_before := int(factory.debug_get_pool_hit_stats()["hits"])
	var reused: BulletVolley2D = factory.spawn_volley(_plain())
	assert_eq(reused, v, "the pooled instance is reused")
	assert_eq(int(factory.debug_get_pool_hit_stats()["hits"]), hits_before + 1, "counted as a pool hit")
	var cold: BulletVolley2D = factory.spawn_volley(_plain())
	assert_ne(cold, reused, "the second spawn allocates a fresh volley")

	assert_eq(_volley_state(reused), _volley_state(cold), "volley-wide state matches a cold volley")
	for i in N:
		assert_eq(_bullet_state(reused, i), _bullet_state(cold, i), "bullet %d state matches a cold volley" % i)
		assert_eq(reused.get_bullet_transform(i), cold.get_bullet_transform(i), "bullet %d starts at the cold pose" % i)
		assert_eq(reused.get_bullet_velocity(i), cold.get_bullet_velocity(i), "bullet %d starts with the cold velocity" % i)

	await physics(30)
	for i in N:
		var a: Vector2 = reused.get_bullet_transform(i).origin
		var b: Vector2 = cold.get_bullet_transform(i).origin
		assert_almost_eq(a.distance_to(b), 0.0, 0.001, "bullet %d flies like the cold volley" % i)
	assert_eq(_timer_fired, 0, "the previous life's timer never fires")
