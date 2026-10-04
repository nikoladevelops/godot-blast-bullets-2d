extends BlastTest
## One pooled volley, two owners. Spawner A fires a volley, it dies and goes
## back to the pool; spawner B's next shot (same bucket) gets the SAME
## instance. From then on the volley belongs to B only: every signal kind
## (hits, bounces, lifetime, homing reached) reaches B and never A or the
## factory, A's retarget never touches it, A's stored life id no longer
## matches, and nothing A configured (listeners, timers, custom data,
## attachments) survives into B's life.

var a: BulletSpawner2D
var b: BulletSpawner2D
var a_events: Array = []
var b_events: Array = []
var factory_events: Array = []


func _data(lifetime := 8.0) -> BulletVolleyData2D:
	var d := H.make_volley_data(1, 300.0, lifetime)
	d.monitorable = true
	d.set_collision_mask_from_array([3, 4])
	d.collision_shape = H.make_circle_shape(6.0)
	d.bullet_max_collision_count = 0
	return d


func _wire(sp: BulletSpawner2D, bucket: Array) -> void:
	for sig in ["area_entered", "body_entered", "bounce_area_entered", "bounce_body_entered"]:
		sp.connect(sig, func(_t, v, i): bucket.append([sig, v, i]))
	sp.life_time_over.connect(func(v, idx): bucket.append(["life_time_over", v, idx]))
	sp.volley_bullet_homing_target_reached.connect(func(v, i, _t, _p): bucket.append(["reached", v, i]))


func before_each() -> void:
	await super()
	a_events.clear()
	b_events.clear()
	factory_events.clear()
	a = make_spawner(_data(), BulletSpawner2D.PATTERN_FROM_SELF, 1)
	b = make_spawner(_data(), BulletSpawner2D.PATTERN_FROM_SELF, 1)
	_wire(a, a_events)
	_wire(b, b_events)
	for sig in ["area_entered", "body_entered", "bounce_area_entered", "bounce_body_entered"]:
		factory.connect(sig, func(_t, v, i): factory_events.append([sig, v, i]))
	await idle(1)


func _shoot(sp: BulletSpawner2D) -> BulletVolley2D:
	watch_signals(sp)
	var before: int = get_signal_emit_count(sp, "volley_fired")
	assert_true(sp.shoot_once(), "%s fired" % sp.name)
	return get_signal_parameters(sp, "volley_fired", before)[0]


func _handover() -> BulletVolley2D:
	var v := _shoot(a)
	var a_life := v.get_life_id()
	v.disable_bullet(0)
	assert_true(v.is_pooled(), "A's dead volley is pooled")
	var w := _shoot(b)
	assert_same(w, v, "B's shot reuses A's pooled instance")
	assert_ne(w.get_life_id(), a_life, "A's stored life id is stale")
	assert_eq(int(w.debug_get_volley_info().get("owner_spawner_id", 0)), b.get_instance_id(), "owned by B")
	return w


func test_hits_reach_the_new_owner_only() -> void:
	var w := await _handover()
	make_wall(Vector2(200, 0))
	for i in 90:
		await physics()
		if not b_events.is_empty():
			break
	assert_eq(b_events.size(), 1, "B hears the hit")
	assert_eq(b_events[0][0], "body_entered", "as a body hit")
	assert_same(b_events[0][1], w, "for its volley")
	assert_eq(a_events, [], "A hears nothing")
	assert_eq(factory_events, [], "the factory hears nothing")


func test_bounces_reach_the_new_owner_only() -> void:
	var d := _data()
	d.set_bounce_mask_from_array([4])
	a.set_spawn_data(d)
	b.set_spawn_data(d.duplicate())
	await _handover()
	make_wall(Vector2(200, 0), Vector2(20, 400), 8, 2)
	for i in 90:
		await physics()
		if not b_events.is_empty():
			break
	assert_eq(b_events.size(), 1, "B hears the bounce")
	assert_eq(b_events[0][0], "bounce_body_entered", "as a bounce")
	assert_eq(a_events, [], "A hears nothing")


func test_lifetime_reaches_the_new_owner_only() -> void:
	var d := _data(0.2)
	d.is_life_time_over_signal_enabled = true
	a.set_spawn_data(d)
	b.set_spawn_data(d.duplicate())
	await _handover()
	for i in 60:
		await physics()
		if not b_events.is_empty():
			break
	assert_eq(b_events.size(), 1, "B hears the expiry")
	assert_eq(b_events[0][0], "life_time_over", "as life_time_over")
	assert_eq(a_events, [], "A hears nothing")


func test_homing_reached_reaches_the_new_owner_only() -> void:
	var w := await _handover()
	w.set_homing_distance_before_reached(400.0)
	w.shared_homing_deque_push_back_global_position_target(Vector2(40, 0))
	for i in 10:
		await physics()
		if not b_events.is_empty():
			break
	assert_eq(b_events.size(), 1, "B hears the reach")
	assert_eq(b_events[0][0], "reached", "forwarded reach")
	assert_eq(a_events, [], "A hears nothing")


func test_nothing_the_first_owner_attached_survives() -> void:
	var v := _shoot(a)
	var fired: Array = []
	v.attach_time_based_function(0.05, func(): fired.append(1), true)
	v.sprite_animation_finished.connect(func(_x = null): fired.append(2))
	v.bullet_set_custom_data(0, Resource.new())
	v.bullet_set_attachment(0, make_probe_scene(), Vector2.ZERO)
	v.disable_bullet(0)
	var w := _shoot(b)
	assert_same(w, v, "reused")
	assert_eq(w.debug_get_timer_count(), 0, "no timers")
	assert_eq(w.get_signal_connection_list("sprite_animation_finished").size(), 0, "no listeners")
	assert_null(w.bullet_get_custom_data(0), "no custom data")
	assert_null(w.bullet_get_attachment(0), "no attachment")
	await physics(10)
	assert_eq(fired, [], "A's timer never fires in B's life")


func test_first_owner_retarget_never_touches_the_reused_volley() -> void:
	a.set_homing_enabled(true)
	a.set_homing_target_source(BulletSpawner2D.HOMING_SOURCE_GLOBAL_POSITION)
	a.set_homing_global_position(Vector2(0, -500))
	var w := await _handover()
	assert_eq(a.retarget_live_volleys(), 0, "A has nothing to retarget")
	assert_eq(w.shared_homing_deque_check_homing_targets_amount(), 0, "B's volley untouched")


func test_two_spawners_firing_the_same_frame_never_share_an_instance() -> void:
	var v := _shoot(a)
	v.disable_bullet(0)
	assert_true(v.is_pooled(), "one pooled volley in the bucket")
	var x := _shoot(a)
	var y := _shoot(b)
	assert_not_same(x, y, "two live volleys, two instances")
	assert_true(x == v or y == v, "one of them is the pooled instance")
	assert_true(factory.debug_assert_no_dangling().get("ok", false), "factory consistent")
