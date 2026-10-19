extends BlastTest
## A volley that lived with EVERY feature switched on, expired into the pool
## and was reused for plain bullets must be indistinguishable from a volley
## that was never used: homing queues, orbit, bounce, wobble, gravity,
## curves, movement patterns, trails, timers, attachments, custom data, tint,
## spin, graze and flocking are all reset, and the reused bullets sit and fly exactly where a
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
		"flock": [v.flock_get(), v.flock_get_scope(), v.debug_get_bullet_flock_neighbors(0), v.debug_get_bullet_flock_steering(0)],
		"sound": [v.sound_get_effects(), v.sound_get_listener_group()],
		"split": [v.split_get_data(), v.split_get_depth()],
		"pickup": v.is_pickup_volley(),
		"collision": [v.get_collision_layer(), v.get_collision_mask(), v.get_bullet_max_collision_count()],
		"material": [v.material, v.texture, v.z_index, v.light_mask, v.visibility_layer],
		"groups": v.get_groups().filter(func(g): return not str(g).begins_with("_")),
		"meta": v.get_meta_list(),
	}


## The keys whose values differ: {key: [a, b]} (empty = equal).
func _diff(a: Dictionary, b: Dictionary) -> Dictionary:
	var out := {}
	for k in a:
		if not b.has(k) or str(a[k]) != str(b[k]):
			out[k] = [a[k], b.get(k)]
	return out


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
	v.flock_join(H.make_flock(1.5, 1.0, 1.0))
	v.sound_set_effects([H.make_sound(BulletSoundData2D.SOUND_ON_HIT)], &"listeners")
	var stage := BulletSplitStage2D.new()
	stage.split_on_lifetime_over = false # the expiry pools it plain, no children
	var chain := BulletSplitData2D.new()
	chain.stages = [stage]
	v.split_set_data(chain, _plain())
	v.add_to_group(&"loaded_life")
	v.set_meta(&"loaded_life", 1)
	expect_no_errors("every feature switches on cleanly")
	for i in 120: # early break; the lifetime is 15 ticks
		await physics()
		if not bool(v.debug_get_volley_info().get("is_active", true)):
			break
	await idle(2) # the lifetime hold flushes on the idle frame
	assert_true(bool(v.debug_get_volley_info().get("is_pooled", false)), "expired into the pool")
	assert_gt(int(factory.debug_get_graze_stats()["events_total"]), 0, "the loaded life really grazed")
	assert_eq(int(factory.debug_get_flock_stats()["armed_volleys"]), 0, "the pooled life left its flock")

	var hits_before := int(factory.debug_get_pool_hit_stats()["hits"])
	var plain := _plain() # one data for both: its texture is compared too
	var reused: BulletVolley2D = factory.spawn_volley(plain)
	assert_eq(reused, v, "the pooled instance is reused")
	assert_eq(int(factory.debug_get_pool_hit_stats()["hits"]), hits_before + 1, "counted as a pool hit")
	var cold: BulletVolley2D = factory.spawn_volley(plain)
	assert_ne(cold, reused, "the second spawn allocates a fresh volley")

	assert_eq(_diff(_volley_state(reused), _volley_state(cold)), {}, "volley-wide state matches a cold volley")
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



## Spawn data that leaves a mark everywhere a cold spawn of other data must
## not see: a pre-populated volley is seeded once from it and must come out
## of the pool exactly like a cold volley of the data it is spawned with.
func _populate_data() -> BulletVolleyData2D:
	var d := _loaded()
	d.shared_bullet_attachment = null # populate never attaches
	d.material = CanvasItemMaterial.new()
	d.z_index = 7
	d.light_mask = 3
	return d


func test_a_populated_volley_matches_a_cold_volley() -> void:
	var key := VolleyPoolKey2D.make(N, PhysicsServer2D.SHAPE_CIRCLE)
	var data := _populate_data()
	data.collision_shape = H.make_circle_shape(6.0)
	factory.populate_bullets_pool(key, data, 1)
	var plain := _plain()
	plain.collision_shape = H.make_circle_shape(6.0)
	var reused: BulletVolley2D = factory.spawn_volley(plain)
	assert_eq(int(factory.debug_get_pool_hit_stats()["hits"]), 1, "the populated volley was reused")
	var cold: BulletVolley2D = factory.spawn_volley(plain)
	assert_eq(_diff(_volley_state(reused), _volley_state(cold)), {}, "volley-wide state matches a cold volley")
	for i in N:
		assert_eq(_bullet_state(reused, i), _bullet_state(cold, i), "bullet %d state matches a cold volley" % i)


## A pre-populated volley waits in the pool as a released one: it keeps none
## of the populate data's resources alive (custom data, curves, material...).
func test_a_populated_volley_keeps_no_resource_of_its_data() -> void:
	var key := VolleyPoolKey2D.make(N, PhysicsServer2D.SHAPE_CIRCLE)
	var data := _populate_data()
	data.collision_shape = H.make_circle_shape(6.0)
	var refs := [weakref(data.shared_bullets_custom_data), weakref(data.shared_bullet_curves_data), weakref(data.shared_bullet_wobble_data), weakref(data.material), weakref(data.sprite_frames)]
	factory.populate_bullets_pool(key, data, 2)
	data = null
	var alive: Array = []
	for r in refs:
		alive.append(r.get_ref() != null)
	assert_eq(alive, [false, false, false, false, false], "the pool holds none of them")


## A volley that went back to the pool keeps none of its life's resources
## either: the material and its shader parameter values go with the life.
func test_a_pooled_volley_keeps_no_material() -> void:
	var data := _plain()
	var shader := Shader.new()
	shader.code = "shader_type canvas_item; instance uniform vec4 tint : source_color = vec4(1.0);"
	var mat := ShaderMaterial.new()
	mat.shader = shader
	data.material = mat
	data.instance_shader_parameters = {"tint": Color(1, 0, 0, 1)}
	var v: BulletVolley2D = factory.spawn_volley(data)
	var ref: WeakRef = weakref(mat)
	await idle(1)
	factory.clear_active_bullets()
	await idle(1)
	assert_true(v.is_pooled(), "pooled")
	assert_true(v.has_method("debug_check_invariants"), "bound")
	assert_eq(v.debug_check_invariants(), "", "a pooled volley passes every invariant")
	assert_null(v.material, "no material while pooled")
	data.material = null
	mat = null
	assert_null(ref.get_ref(), "the pool keeps the material alive no more")
	var reused: BulletVolley2D = factory.spawn_volley(_plain())
	assert_eq(reused, v, "reused")
	assert_null(reused.get_instance_shader_parameter(&"tint"), "the old life's parameter is gone")
