extends BlastTest
## Every volley callback is LIVE: bullet_homing_target_reached,
## sprite_animation_finished and attach_time_based_function callbacks run
## synchronously inside the factory tick (inside the physics frame), in the
## frame their event happens, never through call_deferred. Follow-up work
## (homing auto-pop) runs right after the handler and respects what the
## handler changed. Attach/detach of timers apply immediately from anywhere.

var events: Array = []


func _homing_data(n: int, per_bullet_pop := false, shared_pop := false) -> BulletVolleyData2D:
	var d := H.make_volley_data(n, 0.0, 30.0)
	var arr: Array = []
	for i in n:
		arr.append(Transform2D())
	d.transforms = arr
	d.homing_distance_before_reached = 400.0
	d.bullet_homing_auto_pop_after_target_reached = per_bullet_pop
	d.shared_homing_deque_auto_pop_after_target_reached = shared_pop
	return d


func _on_reached(volley: BulletVolley2D, idx: int, _target: Object, pos: Vector2) -> void:
	events.append({
		"index": idx,
		"pos": pos,
		"alive": volley.is_bullet_status_enabled(idx),
		"in_physics": Engine.is_in_physics_frame(),
		"frame": Engine.get_physics_frames(),
	})


func before_each() -> void:
	await super()
	events.clear()


func test_homing_reached_fires_live_once_per_bullet() -> void:
	var v: BulletVolley2D = factory.spawn_volley(_homing_data(2))
	v.bullet_homing_target_reached.connect(_on_reached)
	v.shared_homing_deque_push_back_global_position_target(Vector2(5, 0))
	await physics(6)
	assert_eq(events.size(), 2, "one reached signal per bullet for one target")
	for e in events:
		assert_true(e["alive"], "bullet alive in the handler")
		assert_true(e["in_physics"], "emitted live from the factory tick")
	assert_eq(events[0]["frame"], events[1]["frame"], "both in the frame they arrived")


func test_per_bullet_auto_pop_spares_a_target_pushed_back_by_the_handler() -> void:
	var v: BulletVolley2D = factory.spawn_volley(_homing_data(1, true))
	v.bullet_homing_push_back_global_position_target(0, Vector2(5, 0))
	v.bullet_homing_target_reached.connect(func(vol: BulletVolley2D, idx: int, _t, _p):
		if events.is_empty():
			events.append(idx)
			vol.bullet_homing_push_back_global_position_target(idx, Vector2(900, 0)))
	await physics(2)
	assert_eq(events.size(), 1, "reached once")
	assert_eq(v.bullet_homing_check_targets_amount(0), 1, "the reached front was auto-popped, the handler's push survived")
	assert_almost_eq(v.bullet_get_current_homing_target(0), Vector2(900, 0), Vector2(0.01, 0.01), "front is the target the handler pushed")


func test_per_bullet_auto_pop_never_eats_a_new_front_pushed_by_the_handler() -> void:
	var v: BulletVolley2D = factory.spawn_volley(_homing_data(1, true))
	v.bullet_homing_push_back_global_position_target(0, Vector2(5, 0))
	v.bullet_homing_target_reached.connect(func(vol: BulletVolley2D, idx: int, _t, _p):
		if events.is_empty():
			events.append(idx)
			vol.bullet_homing_push_front_global_position_target(idx, Vector2(900, 0)))
	await physics(2)
	assert_eq(events.size(), 1, "reached once")
	assert_almost_eq(v.bullet_get_current_homing_target(0), Vector2(900, 0), Vector2(0.01, 0.01), "a new front pushed by the handler is never auto-popped")


func test_shared_auto_pop_pops_once_per_tick() -> void:
	var v: BulletVolley2D = factory.spawn_volley(_homing_data(3, false, true))
	v.bullet_homing_target_reached.connect(_on_reached)
	for k in 3:
		v.shared_homing_deque_push_back_global_position_target(Vector2(5 + k, 0))
	await physics(1) # start of the first frame: no tick yet
	await physics(1) # one tick done
	assert_eq(events.size(), 3, "all three bullets report the first front")
	assert_eq(v.shared_homing_deque_check_homing_targets_amount(), 2, "one shared pop for the whole storm")
	await physics(1)
	assert_eq(events.size(), 6, "all three report the next front")
	assert_eq(v.shared_homing_deque_check_homing_targets_amount(), 1, "one more pop")


func test_spawner_forwards_reached_live_without_connecting_to_the_volley() -> void:
	var got: Array = []
	var sp := make_spawner(_homing_data(1), BulletSpawner2D.PATTERN_FROM_SELF, 1)
	sp.set_homing_enabled(true)
	sp.set_homing_target_source(BulletSpawner2D.HOMING_SOURCE_GLOBAL_POSITION)
	sp.set_homing_global_position(Vector2(5, 0))
	sp.volley_bullet_homing_target_reached.connect(func(vol: BulletVolley2D, idx: int, _t, _p): got.append([vol.is_bullet_status_enabled(idx), Engine.is_in_physics_frame()]))
	watch_signals(sp)
	assert_true(sp.shoot_once(), "fired")
	var vol: BulletVolley2D = get_signal_parameters(sp, "volley_fired", 0)[0]
	assert_eq(vol.get_signal_connection_list("bullet_homing_target_reached").size(), 0, "no per-volley forwarding connection")
	for i in 10:
		await physics()
		if not got.is_empty():
			break
	assert_eq(got, [[true, true]], "spawner forwards the reach live, bullet alive")


func test_a_reused_volley_drops_listeners_of_its_previous_life() -> void:
	var v: BulletVolley2D = factory.spawn_volley(_homing_data(1))
	v.bullet_homing_target_reached.connect(_on_reached)
	v.disable_bullet(0)
	await idle(1)
	var reused: BulletVolley2D = factory.spawn_volley(_homing_data(1))
	assert_same(reused, v, "pool reuse")
	reused.shared_homing_deque_push_back_global_position_target(Vector2(5, 0))
	await physics(4)
	assert_eq(events.size(), 0, "the previous owner's listener never hears the new life")


func _anim_data(loop: bool) -> BulletVolleyData2D:
	var d := H.make_still_data(1)
	var sf := SpriteFrames.new()
	var img := Image.create_empty(1, 1, false, Image.FORMAT_RGBA8)
	img.fill(Color.WHITE)
	var tex := ImageTexture.create_from_image(img)
	sf.add_frame("default", tex)
	sf.add_frame("default", tex)
	sf.set_animation_speed("default", 30.0)
	sf.set_animation_loop("default", loop)
	d.sprite_frames = sf
	return d


func test_sprite_animation_finished_fires_live_once() -> void:
	var v: BulletVolley2D = factory.spawn_volley(_anim_data(false))
	v.sprite_animation_finished.connect(func(_vol = null): events.append(Engine.is_in_physics_frame()))
	await physics(20)
	assert_eq(events, [true], "finished exactly once, live from the tick")
	assert_true(v.is_sprite_animation_finished(), "finished state readable")
	assert_true(v.restart_sprite_animation(), "restart")
	await physics(20)
	assert_eq(events, [true, true], "a restart re-arms the signal once")


func test_looping_animation_never_finishes() -> void:
	var v: BulletVolley2D = factory.spawn_volley(_anim_data(true))
	v.sprite_animation_finished.connect(func(_vol = null): events.append(1))
	await physics(30)
	assert_eq(events.size(), 0, "a looping animation never reports finished")
	assert_true(v.is_sprite_animation_playing(), "still playing")


func test_timer_fires_live_inside_the_physics_frame() -> void:
	var v: BulletVolley2D = factory.spawn_volley(H.make_still_data(1))
	v.attach_time_based_function(0.05, func(): events.append(Engine.is_in_physics_frame()))
	assert_eq(v.debug_get_timer_count(), 1, "attached immediately")
	await physics(10)
	assert_eq(events, [true], "one-shot fired once, live")
	assert_eq(v.debug_get_timer_count(), 0, "one-shot removed after firing")


func test_timer_detaching_a_sibling_due_the_same_tick_cancels_it() -> void:
	var v: BulletVolley2D = factory.spawn_volley(H.make_still_data(1))
	var second := func(): events.append("second")
	v.attach_time_based_function(0.05, func():
		events.append("first")
		v.detach_time_based_function(second))
	v.attach_time_based_function(0.05, second)
	await physics(10)
	assert_eq(events, ["first"], "a sibling detached by an earlier callback of the same tick never fires")


func test_timer_attach_inside_a_hit_handler_is_immediate() -> void:
	make_wall(Vector2(200, 0))
	await physics()
	var counts: Array = []
	factory.body_entered.connect(func(_b, vol: BulletVolley2D, _i):
		vol.attach_time_based_function(1.0, func(): pass)
		counts.append(vol.debug_get_timer_count()))
	var d := H.make_volley_data(1, 300.0, 8.0)
	d.transforms = [Transform2D()]
	d.monitorable = true
	d.set_collision_mask_from_array([3])
	d.collision_shape = H.make_circle_shape(6.0)
	d.bullet_max_collision_count = 0
	factory.spawn_volley(d)
	for i in 90:
		await physics()
		if not counts.is_empty():
			break
	assert_eq(counts, [1], "attach applies immediately inside the physics frame")
	expect_no_errors()


func test_timer_callback_freeing_its_volley_is_safe() -> void:
	var v: BulletVolley2D = factory.spawn_volley(H.make_still_data(1))
	var other: BulletVolley2D = factory.spawn_volley(H.make_still_data(1))
	v.attach_time_based_function(0.05, func():
		events.append("free")
		v.free())
	v.attach_time_based_function(0.05, func(): events.append("after"))
	await physics(10)
	assert_eq(events, ["free"], "the freed volley runs nothing more")
	assert_true(is_instance_valid(other), "other volleys untouched")
	assert_true(factory.debug_assert_no_dangling().get("ok", false), "factory consistent")
