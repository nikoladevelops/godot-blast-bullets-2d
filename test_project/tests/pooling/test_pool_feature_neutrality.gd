extends BlastTest
## Flocking across the pool: a pooled volley releases its flock (weakref
## proof) and the factory's armed count; reused for plain data it flies
## exactly like a cold volley and reports no flock; a PARKED volley (auto
## pooling off) keeps its flock and a wake resumes flocking.
##
## Graze across the pool: a pooled volley releases its zone resources
## (weakref proof) and its per-bullet state; reused for plain data it is
## unarmed and reports nothing; a PARKED volley (auto pooling off) keeps
## its zones and a wake resumes grazing with a fresh visit; graze events
## still queued when the volley is pooled never reach the next life.

const DT := 1.0 / 60.0


func _data(lifetime := 1000.0) -> BulletVolleyData2D:
	return H.make_flight_data([Transform2D(0.0, Vector2(0, 0)), Transform2D(0.5, Vector2(0, 30)), Transform2D(-0.5, Vector2(20, 10))], 100.0, lifetime)


func test_a_pooled_volley_releases_its_flock() -> void:
	var v: BulletVolley2D = factory.spawn_volley(_data())
	var f := H.make_flock(1.0, 1.0, 1.0)
	var ref: WeakRef = weakref(f)
	v.flock_join(f)
	f = null
	step_factory()
	assert_not_null(ref.get_ref(), "the armed volley holds the flock")
	for i in 3:
		v.disable_bullet(i)
	assert_eq(v.debug_get_life_state(), "pooled", "pooled")
	assert_null(ref.get_ref(), "the pooled volley released it")
	assert_eq(factory.debug_get_flock_stats()["armed_volleys"], 0, "disarmed")


func test_reuse_for_plain_data_flies_like_a_cold_volley() -> void:
	var v: BulletVolley2D = factory.spawn_volley(_data())
	v.flock_join(H.make_flock(3.0, 2.0, 2.0))
	step_factory(10)
	for i in 3:
		v.disable_bullet(i)
	var reused: BulletVolley2D = factory.spawn_volley(_data())
	assert_eq(reused, v, "the pool handed it back")
	var cold: BulletVolley2D = factory.spawn_volley(_data())
	assert_ne(cold, reused, "a fresh volley")
	assert_null(reused.flock_get(), "no flock")
	assert_eq(reused.flock_get_scope(), -1, "no scope")
	step_factory(30)
	for i in 3:
		assert_eq(reused.get_bullet_transform(i), cold.get_bullet_transform(i), "bullet %d flies like the cold volley" % i)
		assert_eq(reused.debug_get_bullet_flock_neighbors(i), 0, "bullet %d reads no flock" % i)


func test_a_parked_volley_keeps_its_flock_and_a_wake_resumes_flocking() -> void:
	var v: BulletVolley2D = factory.spawn_volley(_data())
	var f := H.make_flock(0.0, 0.0, 1.0)
	v.flock_join(f)
	v.set_is_auto_pooling_enabled(false)
	step_factory()
	for i in 3:
		v.disable_bullet(i)
	assert_eq(v.debug_get_life_state(), "parked", "parked, still owned")
	assert_eq(v.flock_get(), f, "a parked volley keeps its flock")
	assert_eq(factory.debug_get_flock_stats()["armed_volleys"], 1, "still armed")
	step_factory(3)
	assert_eq(factory.debug_get_flock_stats()["grid_members"], 0, "nothing gathered while parked")
	for i in 3:
		v.enable_bullet(i)
	step_factory()
	assert_eq(v.debug_get_bullet_flock_neighbors(0), 2, "the wake flocks again")
	assert_ne(v.debug_get_bullet_flock_steering(0), Vector2(), "with a pull")


func test_a_pooled_volley_releases_its_zones() -> void:
	var v := graze_volley(H.transforms_at([Vector2.ZERO]), 0.0)
	var zone := H.make_graze_zone([20.0])
	var ref: WeakRef = weakref(zone)
	v.graze_set_zones([zone], &"graze_targets")
	zone = null
	assert_not_null(ref.get_ref(), "the armed volley holds the zone")
	v.disable_bullet(0)
	assert_eq(v.debug_get_life_state(), "pooled", "pooled")
	assert_null(ref.get_ref(), "the pooled volley released it")
	var info: Dictionary = v.debug_get_graze_info()
	assert_false(info["armed"], "disarmed")
	assert_eq(info["state_bytes"], 0, "no per-bullet state kept")


func test_reuse_for_plain_data_reports_nothing() -> void:
	make_graze_target(Vector2(10, 0))
	var log_entries := H.record_graze(factory)
	var v := graze_volley(H.transforms_at([Vector2.ZERO]), 0.0)
	v.graze_set_zones([H.make_graze_zone([20.0])], &"graze_targets")
	step_factory()
	assert_eq(log_entries.size(), 1, "the first life grazed")
	v.disable_bullet(0)
	var reused := graze_volley(H.transforms_at([Vector2.ZERO]), 0.0)
	assert_eq(reused, v, "the pool handed it back")
	step_factory(3)
	assert_eq(log_entries.size(), 1, "the plain reuse grazes nothing")
	assert_false(reused.is_graze_armed(), "unarmed")


func test_a_parked_volley_keeps_its_zones_and_a_wake_resumes_grazing() -> void:
	var t := make_graze_target(Vector2(10, 0))
	var log_entries := H.record_graze(factory)
	var v := graze_volley(H.transforms_at([Vector2.ZERO]), 0.0)
	var zone := H.make_graze_zone([20.0])
	zone.regraze = GrazeZone2D.REGRAZE_AFTER_EXIT
	v.graze_set_zones([zone], &"graze_targets")
	v.set_is_auto_pooling_enabled(false)
	step_factory()
	v.disable_bullet(0)
	assert_eq(v.debug_get_life_state(), "parked", "parked, still owned")
	assert_eq(v.get_graze_zones(), [zone], "a parked volley keeps its zones")
	step_factory(3)
	assert_eq(log_entries.size(), 1, "nothing while parked")
	v.enable_bullet(0)
	step_factory()
	assert_eq(H.graze_kinds(log_entries), ["enter:0:0", "enter:0:0"], "the wake starts a fresh visit (After Exit grazes again)")
	assert_eq(log_entries[1][1], t, "same target")


func test_queued_events_never_reach_the_next_life() -> void:
	var t := make_graze_target(Vector2(3, 0))
	var log_entries := H.record_graze(factory)
	var v := graze_volley(H.transforms_at([Vector2.ZERO]), 0.0)
	v.graze_set_zones([H.make_graze_zone([20.0])], &"graze_targets")
	v.homing_take_control_of_texture_rotation = true
	v.shared_homing_deque_push_back_node2d_target(t)
	v.bullet_homing_target_reached.connect(func(_v: BulletVolley2D, _i: int, _t: Node2D, _p: Vector2) -> void:
		factory.is_factory_processing_bullets = false)
	step_factory()
	assert_eq(v.debug_get_graze_info()["pending_events"], 1, "a graze is queued behind the pause")
	v.disable_bullet(0)
	assert_eq(v.debug_get_life_state(), "pooled", "pooled while the graze waited")
	factory.is_factory_processing_bullets = true
	var reused := graze_volley(H.transforms_at([Vector2(500, 500)]), 0.0)
	assert_eq(reused, v, "reused")
	assert_eq(reused.debug_get_graze_info()["pending_events"], 0, "the new life starts with no queued events")
	step_factory(3)
	assert_eq(log_entries, [], "the old life's graze never fires")


# ---- A life armed with EVERY feature, then a plain respawn ----


var _old_timer_fired := 0


func _on_old_timer() -> void:
	_old_timer_fired += 1


## The rich life: the data carries wobble / gravity / bounce / curves /
## rotation / a movement pattern / custom data / a material, and the volley is
## additionally armed at runtime with a timer, an emitter, a split chain, sound
## effects, homing targets, an orbit and an attachment.
func _armed_data() -> BulletVolleyData2D:
	var d := H.make_volley_data(3, 120.0, 1000.0)
	var transforms: Array = []
	for i in 3:
		transforms.append(Transform2D(0.0, Vector2(24.0 * i, 0.0)))
	d.transforms = transforms
	d.collision_shape = H.make_circle_shape(6.0)
	var wobble := BulletWobbleData2D.new()
	wobble.enabled = true
	wobble.amplitude = 30.0
	d.shared_bullet_wobble_data = wobble
	d.gravity = Vector2(0, 90)
	d.bounce_mask = 8
	d.bounce_strength = 0.8
	d.shared_bullet_rotation_data = H.make_rotation(4.0, 10.0)
	d.shared_bullets_custom_data = Resource.new()
	d.material = CanvasItemMaterial.new()
	return d


## The plain twin: SAME pool key (amount + circle shape + no colors), nothing
## else armed.
func _plain_twin() -> BulletVolleyData2D:
	var d := H.make_volley_data(3, 120.0, 1000.0)
	var transforms: Array = []
	for i in 3:
		transforms.append(Transform2D(0.0, Vector2(24.0 * i, 0.0)))
	d.transforms = transforms
	d.collision_shape = H.make_circle_shape(6.0)
	return d


## Everything a bullet reads back, per bullet.
func _readout(v: BulletVolley2D, i: int) -> Dictionary:
	return {
		"bullet": v.debug_get_bullet_info(i),
		"wobble": v.debug_get_wobble_info(i),
		"gravity": v.debug_get_gravity_info(i),
		"bounce": v.debug_get_bounce_info(i),
		"orbit": v.debug_get_orbiting_info(i),
		"curves": v.debug_get_curves_info(i),
		"pattern": v.debug_get_pattern_info(i),
		"attachment": v.debug_get_attachment_info(i),
		"custom": v.bullet_get_custom_data(i),
	}


## One life with EVERY feature armed (timer, emitter, split chain, sound,
## homing, orbit, bounce, wobble, gravity, movement pattern, attachment,
## material, custom data, groups, metadata) leaves NOTHING in the pooled
## instance: the plain respawn reads exactly like a cold plain volley, and every
## resource of the armed life is released (weakref proof).
func test_an_armed_life_leaves_nothing_in_the_pooled_instance() -> void:
	var template := BulletSpawner2D.new()
	template.set_shooting_enabled(false)
	template.set_spawn_data(H.make_volley_data(1, 0.0, 30.0))
	template.helper_bullets_amount = 1
	add(template)
	template.set_bullet_factory(factory)
	var emitting := BulletEmitterData2D.new()
	emitting.interval_sec = 0.5
	emitting.max_shots = 1
	var chain := BulletSplitData2D.new()
	chain.stages = [BulletSplitStage2D.new()]
	var sound := H.make_sound(BulletSoundData2D.SOUND_ON_HIT)
	var scene := make_probe_scene()
	var flock := H.make_flock(1.0, 1.0, 1.0)
	var zone := H.make_graze_zone([200.0])
	var curve := Curve2D.new()
	curve.add_point(Vector2.ZERO)
	curve.add_point(Vector2(200, 50))
	var v: BulletVolley2D = factory.spawn_volley(_armed_data())
	v.attach_time_based_function(1.0, _on_old_timer, true)
	assert_true(v.emitter_set(emitting, template), "emitter armed")
	assert_true(v.split_set_data(chain, _plain_twin()), "split chain armed")
	v.sound_set_effects([sound], &"listeners")
	v.bullet_homing_push_back_global_position_target(0, Vector2(400, 400))
	v.bullet_enable_orbiting(1, 48.0, BulletVolley2D.OrbitLeft)
	v.bullet_set_attachment(2, scene, Vector2.ZERO, true)
	v.graze_set_zones([zone], &"graze_targets")
	v.flock_join(flock)
	v.set_shared_movement_pattern_curve(curve)
	v.add_to_group(&"armed_life")
	v.set_meta(&"armed_life", 1)
	# The attachment SCENE is deliberately not watched here: the factory keeps
	# the scenes it validated for its error wording (bounded, see
	# pooling/test_pool_attachments.gd::test_dead_in_memory_scenes_are_forgotten),
	# so its weakref would outlive the volley for a reason that is not the pool.
	var refs := {
		"emitting": weakref(emitting), "chain": weakref(chain), "sound": weakref(sound),
		"flock": weakref(flock), "zone": weakref(zone),
		"curve": weakref(curve), "material": weakref(v.material),
	}
	emitting = null
	chain = null
	sound = null
	scene = null
	flock = null
	zone = null
	curve = null
	expect_no_errors("every feature arms cleanly")
	await idle(1)
	assert_eq(v.debug_get_timer_count(), 1, "a timer")
	assert_true(v.debug_get_attachment_info(2).get("has_attachment", false), "an attachment")
	assert_gt(v.sound_get_effects().size(), 0, "sound effects")
	assert_true(v.is_graze_armed(), "graze zones")
	assert_true(v.flock_get() != null, "a flock")

	var pooled_attachments := int(factory.debug_get_attachments_pool_amount())
	for i in 3:
		v.disable_bullet(i)
	await idle(1)
	assert_true(v.is_pooled(), "pooled")
	assert_eq(int(factory.debug_get_attachments_pool_amount()), pooled_attachments + 1, "the armed life's attachment returned to the attachment pool")
	for k in refs:
		assert_null((refs[k] as WeakRef).get_ref(), "the pool released %s" % k)

	var hits_before := int(factory.debug_get_pool_hit_stats()["hits"])
	var reused: BulletVolley2D = factory.spawn_volley(_plain_twin())
	assert_same(reused, v, "the pooled instance is reused")
	assert_eq(int(factory.debug_get_pool_hit_stats()["hits"]), hits_before + 1, "counted as a pool hit")
	var cold: BulletVolley2D = factory.spawn_volley(_plain_twin())
	assert_ne(cold, reused, "the second spawn allocates a fresh volley")
	assert_eq(_old_timer_fired, 0, "the previous life's timer never fires")

	assert_eq(reused.debug_get_timer_count(), 0, "no timer")
	assert_eq(reused.bullet_get_emitter_next_shot_sec(0), -1.0, "no emitter")
	assert_eq(reused.bullet_get_split_count(0), 0, "no split count")
	assert_null(reused.split_get_data(), "no split chain")
	assert_eq(reused.sound_get_effects(), [], "no sound effects")
	assert_null(reused.flock_get(), "no flock")
	assert_eq(reused.debug_get_graze_info()["armed"], false, "no graze zones")
	assert_false(reused.has_shared_movement_pattern(), "no movement pattern")
	assert_null(reused.material, "no material")
	assert_false(reused.is_in_group(&"armed_life"), "no group")
	assert_false(reused.has_meta(&"armed_life"), "no metadata")
	for i in 3:
		assert_false(reused.debug_get_attachment_info(i).get("has_attachment", false), "bullet %d carries no attachment" % i)
		assert_eq(reused.debug_get_bullet_info(i)["has_homing_targets"], false, "bullet %d has no homing target" % i)
		assert_eq(reused.debug_get_bullet_info(i)["orbiting_enabled"], false, "bullet %d does not orbit" % i)
		assert_eq(_readout(reused, i), _readout(cold, i), "bullet %d reads exactly like a cold plain volley" % i)
	assert_eq(reused.get_shared_bullets_custom_data(), null, "no shared custom data")
	assert_eq(reused.shared_homing_deque_check_homing_targets_amount(), 0, "no shared homing targets")
	assert_eq(factory.debug_get_flock_stats()["armed_volleys"], 0, "the factory has no armed volley left")
	assert_true(factory.debug_assert_no_dangling().get("ok", false), "books ok")
