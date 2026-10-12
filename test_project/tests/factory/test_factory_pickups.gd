extends BlastTest
## Turning bullets into pickups (coins, gems): one call converts a volley's
## bullets, every volley of a spawner, every volley on screen or the ones in
## a circle/rect into one new pickup volley that bursts out, fades, is pulled
## to a target and is collected on arrival (BulletFactory2D.pickup_collected).
## Sources leave their volleys; bad input converts nothing; pickups are never
## converted again; nothing leaks into pooled volleys.

var collected: Array = []
var converted: Array = []


func _on_collected(target: Variant, position: Vector2, volley: BulletVolley2D, index: int, value: Variant) -> void:
	collected.append({ "target": target, "position": position, "volley": volley, "index": index, "value": value })


func _on_converted(count: int, volleys: Array) -> void:
	converted.append({ "count": count, "volleys": volleys })


func after_all() -> void:
	# collect_sound plays a real stream: same headless-audio drain as the sound suites.
	OS.delay_msec(500)
	await super()


func after_each() -> void:
	# The pattern limit is process-wide: never leak a test's value.
	BulletPatterns2D.set_max_bullets_per_pattern(20000)
	await super()


func before_each() -> void:
	await super()
	collected.clear()
	converted.clear()
	factory.pickup_collected.connect(_on_collected)
	factory.bullets_converted.connect(_on_converted)


func _coin() -> BulletPickupData2D:
	var p := BulletPickupData2D.new()
	p.magnet_delay_sec = 0.1
	p.magnet_acceleration = 3000.0
	p.magnet_max_speed = 1500.0
	p.drag = 2.0
	p.collect_radius = 20.0
	p.lifetime_sec = 20.0
	p.value = 5
	return p


func _target(pos: Vector2, group := "") -> Node2D:
	var n := Node2D.new()
	n.position = pos
	if group != "":
		n.add_to_group(group)
	add(n)
	return n


## A still volley with one bullet per position.
func _bullets(points: Array, speed := 0.0) -> BulletVolley2D:
	var data := H.make_volley_data(points.size(), speed, 60.0)
	var arr: Array = []
	for p in points:
		arr.append(Transform2D(0.0, p))
	data.transforms = arr
	return factory.spawn_volley(data)


func _wait_collected(n: int, frames := 300) -> void:
	for i in frames:
		await idle(1)
		if collected.size() >= n:
			return


func _alive(v: BulletVolley2D) -> Array:
	var out: Array = []
	for i in v.get_amount_bullets():
		if v.is_bullet_status_enabled(i):
			out.append(i)
	return out


# --- surface -----------------------------------------------------------------------------

func test_bound_surface() -> void:
	for m in ["convert_all_bullets_to_pickups", "convert_all_bullets_to_pickups_deferred", "convert_bullets_in_circle_to_pickups", "convert_bullets_in_rect_to_pickups"]:
		assert_true(factory.has_method(m), "factory has " + m)
	assert_true(ClassDB.class_has_method("BulletVolley2D", "convert_to_pickups"), "volley.convert_to_pickups")
	assert_true(ClassDB.class_has_method("BulletVolley2D", "is_pickup_volley"), "volley.is_pickup_volley")
	assert_true(ClassDB.class_has_method("BulletSpawner2D", "convert_live_bullets_to_pickups"), "spawner.convert_live_bullets_to_pickups")
	assert_true(factory.has_signal("pickup_collected"), "pickup_collected")
	assert_true(factory.has_signal("bullets_converted"), "bullets_converted")


# --- scopes ----------------------------------------------------------------------------------

func test_all_volleys_become_one_pickup_volley() -> void:
	var a := _bullets([Vector2(0, 0), Vector2(10, 0)])
	var b := _bullets([Vector2(0, 50), Vector2(10, 50), Vector2(20, 50)])
	var target := _target(Vector2(500, 0))
	var out: Array = factory.convert_all_bullets_to_pickups(_coin(), target)
	assert_eq(out.size(), 1, "one pickup volley for the call")
	var pv: BulletVolley2D = out[0]
	assert_true(pv.is_pickup_volley(), "it is a pickup volley")
	assert_eq(pv.get_amount_bullets(), 5, "all five bullets")
	assert_eq(_alive(a), [], "sources drained (a)")
	assert_eq(_alive(b), [], "sources drained (b)")
	assert_eq(factory.get_active_bullet_count(), 5, "five live bullets: the pickups")
	var xs: Array = []
	for i in 5:
		xs.append(pv.get_bullet_transform(i).origin)
	assert_true(Vector2(20, 50) in xs and Vector2(0, 0) in xs, "pickups appear where the bullets were")
	assert_eq(converted.size(), 1, "bullets_converted once")
	assert_eq(converted[0]["count"], 5, "with the count")
	assert_eq(converted[0]["volleys"], [pv], "and the new volleys")


func test_spawner_scope_converts_only_that_spawners_volleys() -> void:
	var sp1 := make_spawner(H.make_volley_data(3, 0.0, 60.0), BulletSpawner2D.PATTERN_FROM_HELPER_RING, 3)
	var sp2 := make_spawner(H.make_volley_data(2, 0.0, 60.0), BulletSpawner2D.PATTERN_FROM_HELPER_RING, 2)
	watch_signals(sp1)
	watch_signals(sp2)
	assert_true(sp1.shoot_once())
	assert_true(sp1.shoot_once())
	assert_true(sp2.shoot_once())
	var v2: BulletVolley2D = get_signal_parameters(sp2, "volley_fired", 0)[0]
	var out: Array = sp1.convert_live_bullets_to_pickups(_coin(), _target(Vector2(300, 0)))
	assert_eq(out.size(), 1, "one pickup volley")
	assert_eq(out[0].get_amount_bullets(), 6, "both volleys of spawner 1 (3 + 3)")
	assert_eq(_alive(v2).size(), 2, "spawner 2 is untouched")
	assert_eq(sp1.get_active_live_bullet_count(), 0, "spawner 1 owns nothing anymore")


func test_volley_scope_with_indexes() -> void:
	var v := _bullets([Vector2(0, 0), Vector2(10, 0), Vector2(20, 0), Vector2(30, 0)])
	var out: Array = v.convert_to_pickups(_coin(), _target(Vector2(400, 0)), PackedInt32Array([1, 3]))
	assert_eq(out.size(), 1, "one volley")
	assert_eq(out[0].get_amount_bullets(), 2, "two bullets")
	assert_eq(_alive(v), [0, 2], "the others stay")
	var all_out: Array = v.convert_to_pickups(_coin(), _target(Vector2(400, 0)))
	assert_eq(all_out[0].get_amount_bullets(), 2, "no indexes means every live bullet")
	assert_eq(_alive(v), [], "drained")


func test_circle_and_rect_scopes() -> void:
	var v := _bullets([Vector2(0, 0), Vector2(5, 0), Vector2(100, 0), Vector2(200, 200)])
	var target := _target(Vector2(900, 0))
	var c: Array = factory.convert_bullets_in_circle_to_pickups(Vector2.ZERO, 10.0, _coin(), target)
	assert_eq(c[0].get_amount_bullets(), 2, "two inside the circle")
	var r: Array = factory.convert_bullets_in_rect_to_pickups(Rect2(90, -10, 30, 20), _coin(), target)
	assert_eq(r[0].get_amount_bullets(), 1, "one inside the rect")
	assert_eq(_alive(v), [3], "the far one stays")


func test_nothing_to_convert_is_an_empty_result_not_an_error() -> void:
	var out: Array = factory.convert_all_bullets_to_pickups(_coin(), _target(Vector2(10, 0)))
	assert_eq(out, [], "empty factory")
	_bullets([Vector2(0, 0)])
	assert_eq(factory.convert_bullets_in_circle_to_pickups(Vector2(900, 900), 5.0, _coin(), _target(Vector2(10, 0))), [], "empty area")
	assert_eq(converted.size(), 0, "no signal for nothing")


func test_frozen_bullets_and_pickups_are_never_converted() -> void:
	var v := _bullets([Vector2(0, 0), Vector2(10, 0), Vector2(20, 0)])
	v.disable_bullet(1)
	var target := _target(Vector2(700, 0))
	var out: Array = factory.convert_all_bullets_to_pickups(_coin(), target)
	assert_eq(out[0].get_amount_bullets(), 2, "the frozen bullet is skipped")
	assert_true(v.is_bullet_status_enabled(1) == false, "and stays frozen")
	assert_eq(factory.convert_all_bullets_to_pickups(_coin(), target), [], "pickups are not converted again")
	assert_eq(factory.get_active_bullet_count(), 2, "still two pickups")


# --- look and motion -------------------------------------------------------------------------------

func test_look_scale_opacity_and_initial_velocity() -> void:
	var v := _bullets([Vector2(0, 0)], 100.0)
	var coin := _coin()
	coin.scale_multiplier = 2.0
	coin.tint = Color(1, 0.8, 0.2, 0.5)
	coin.initial_velocity_scale = 0.5
	coin.magnet_delay_sec = 5.0 # keep the burst phase for the whole check
	coin.drag = 0.0
	var pv: BulletVolley2D = factory.convert_all_bullets_to_pickups(coin, _target(Vector2(0, 900)))[0]
	assert_almost_eq(pv.get_bullet_transform(0).x.length(), 2.0 * v.get_bullet_transform(0).x.length(), 0.001, "scale_multiplier applied to the basis")
	assert_almost_eq(pv.get_bullet_velocity(0).length(), 50.0, 0.01, "half the bullet's velocity")
	assert_eq(pv.get_bullet_velocity(0).normalized(), Vector2.RIGHT, "same direction")
	await idle(2)
	assert_almost_eq(pv.self_modulate.a, 0.5, 0.001, "tint alpha is the opacity")
	assert_almost_eq(pv.self_modulate.g, 0.8, 0.001, "tint color applied")


func test_scatter_adds_a_random_burst() -> void:
	_bullets([Vector2(0, 0), Vector2(1, 0), Vector2(2, 0), Vector2(3, 0), Vector2(4, 0), Vector2(5, 0)])
	var coin := _coin()
	coin.scatter_speed_min = 100.0
	coin.scatter_speed_max = 200.0
	coin.initial_velocity_scale = 0.0
	coin.magnet_delay_sec = 5.0
	var pv: BulletVolley2D = factory.convert_all_bullets_to_pickups(coin, _target(Vector2(0, 900)))[0]
	var dirs := {}
	for i in 6:
		var s := pv.get_bullet_velocity(i).length()
		assert_between(s, 100.0 - 0.01, 200.0 + 0.01, "scatter speed in range")
		dirs[snappedf(pv.get_bullet_velocity(i).angle(), 0.01)] = true
	assert_gt(dirs.size(), 3, "random directions")


func test_drag_slows_the_burst_then_the_magnet_takes_over() -> void:
	_bullets([Vector2(0, 0)], 600.0)
	var coin := _coin()
	coin.initial_velocity_scale = 1.0
	coin.drag = 6.0
	coin.magnet_delay_sec = 0.5
	var pv: BulletVolley2D = factory.convert_all_bullets_to_pickups(coin, _target(Vector2(0, 5000)))[0]
	var s0 := pv.get_bullet_velocity(0).length()
	await idle(20)
	var s1 := pv.get_bullet_velocity(0).length()
	assert_lt(s1, s0 * 0.7, "drag eats the burst speed")
	await idle(60)
	assert_gt(pv.get_bullet_velocity(0).length(), s1, "after the delay the magnet accelerates it again")
	assert_gt(pv.get_bullet_velocity(0).y, 0.0, "towards the target")


# --- collection ---------------------------------------------------------------------------------------

func test_pickups_fly_to_a_node_target_and_are_collected_once_each() -> void:
	_bullets([Vector2(0, 0), Vector2(0, 20), Vector2(0, -20)])
	var target := _target(Vector2(400, 0))
	var pv: BulletVolley2D = factory.convert_all_bullets_to_pickups(_coin(), target)[0]
	await _wait_collected(3)
	assert_eq(collected.size(), 3, "three pickups, three collections")
	var seen := {}
	for c in collected:
		assert_same(c["target"], target, "the signal names the target")
		assert_eq(c["value"], 5, "and carries the pickup value")
		assert_same(c["volley"], pv, "and the pickup volley")
		assert_lt(c["position"].distance_to(target.position), 25.0, "collected within the radius")
		seen[c["index"]] = true
	assert_eq(seen.size(), 3, "each bullet exactly once")
	await idle(3)
	assert_eq(collected.size(), 3, "never collected twice")
	assert_eq(factory.get_active_bullet_count(), 0, "collected pickups are gone")


func test_position_target_and_mouse_target() -> void:
	_bullets([Vector2(0, 0)])
	factory.convert_all_bullets_to_pickups(_coin(), Vector2(300, 100))
	await _wait_collected(1)
	assert_eq(collected.size(), 1, "collected at a global position")
	assert_null(collected[0]["target"], "no node for a point")
	assert_lt(collected[0]["position"].distance_to(Vector2(300, 100)), 25.0, "at the point")
	collected.clear()
	_bullets([Vector2(0, 0)])
	mouse_to(Vector2(-200, 50))
	factory.convert_all_bullets_to_pickups(_coin(), null)
	await _wait_collected(1)
	assert_eq(collected.size(), 1, "null target: the mouse cursor")
	assert_lt(collected[0]["position"].distance_to(Vector2(-200, 50)), 25.0, "at the cursor")


func test_group_target_picks_the_nearest_member_per_bullet() -> void:
	_bullets([Vector2(-300, 0), Vector2(300, 0)])
	var left := _target(Vector2(-400, 0), "players")
	var right := _target(Vector2(400, 0), "players")
	factory.convert_all_bullets_to_pickups(_coin(), "players")
	await _wait_collected(2)
	assert_eq(collected.size(), 2, "both collected")
	var targets := collected.map(func(c): return c["target"])
	assert_true(left in targets and right in targets, "each bullet went to its nearest member")


func test_collect_sound_and_effect_fire_on_collection() -> void:
	godot_listener_at(Vector2.ZERO)
	factory.debug_set_sound_log_enabled(true)
	_bullets([Vector2(0, 0)])
	var coin := _coin()
	var ding := H.make_sound(BulletSoundData2D.SOUND_ON_SHOT)
	coin.collect_sound = ding
	coin.collect_effect = H.make_effect_layer(BulletEffectLayerData2D.EFFECT_ON_SPAWN)
	factory.convert_all_bullets_to_pickups(coin, Vector2(200, 0))
	assert_eq(factory.get_active_effect_count(), 0, "no effect before the pickup arrives")
	await _wait_collected(1)
	assert_gt(factory.get_active_effect_count(), 0, "the collect effect fired at the collection")
	await idle(3)
	var sounds := 0
	for e in factory.debug_get_sound_log():
		if int(e["sound"]) == ding.get_instance_id():
			sounds += 1
	assert_eq(sounds, 1, "the collect sound played once")


func test_a_collected_handler_may_free_the_target_and_convert_more() -> void:
	_bullets([Vector2(0, 0), Vector2(0, 30)])
	var target := _target(Vector2(300, 0))
	factory.pickup_collected.connect(func(t, _p, _v, _i, _val):
		if is_instance_valid(t):
			t.free())
	factory.convert_all_bullets_to_pickups(_coin(), target)
	await _wait_collected(1)
	for i in 60:
		await idle(1)
	assert_true(factory.debug_assert_no_dangling().get("ok", false), "nothing dangles after the target was freed mid-flight")
	assert_lte(collected.size(), 2, "at most one collection per pickup")


func test_a_freed_target_leaves_pickups_drifting_until_they_expire() -> void:
	_bullets([Vector2(0, 0)], 50.0)
	var target := _target(Vector2(5000, 0))
	var coin := _coin()
	coin.lifetime_sec = 1.0
	coin.fade_out_sec = 0.0
	factory.convert_all_bullets_to_pickups(coin, target)
	await idle(10)
	target.free()
	await idle(10)
	assert_eq(factory.get_active_bullet_count(), 1, "still flying without a target")
	await idle(90)
	assert_eq(factory.get_active_bullet_count(), 0, "and gone when the lifetime ends")
	assert_eq(collected.size(), 0, "never collected")


func test_collision_layer_lets_physics_see_pickups_too() -> void:
	var area := make_area(Vector2(200, 0), Vector2(60, 60), 4)
	_bullets([Vector2(0, 0)])
	var coin := _coin()
	coin.collision_mask = 4
	coin.collision_layer = 8
	coin.magnet_delay_sec = 0.0
	factory.convert_all_bullets_to_pickups(coin, Vector2(1000, 0))
	watch_signals(factory)
	for i in 200:
		await physics(1)
		if get_signal_emit_count(factory, "area_entered") > 0:
			break
	assert_signal_emitted(factory, "area_entered", "a player-like Area2D on the mask touches the pickup")
	assert_not_null(area)


# --- refusals ------------------------------------------------------------------------------------------------

func test_bad_input_converts_nothing_and_says_why() -> void:
	var v := _bullets([Vector2(0, 0), Vector2(10, 0)])
	var target := _target(Vector2(300, 0))
	assert_eq(factory.convert_all_bullets_to_pickups(null, target), [], "no pickup resource")
	expect_error_sequence(["BulletFactory2D.convert_all_bullets_to_pickups: pickup must be a BulletPickupData2D, nothing converted."])
	assert_eq(factory.convert_all_bullets_to_pickups(_coin(), 42), [], "a number is no target")
	expect_error_sequence(["BulletFactory2D.convert_all_bullets_to_pickups: target must be a Node2D, a Vector2, a group name or null (the mouse), nothing converted."])
	assert_eq(factory.convert_all_bullets_to_pickups(_coin(), Vector2(NAN, 0)), [], "non-finite point")
	expect_error_sequence(["BulletFactory2D.convert_all_bullets_to_pickups: target position must be finite, nothing converted."])
	assert_eq(factory.convert_all_bullets_to_pickups(_coin(), "nobody_home"), [], "an empty group")
	expect_error_sequence(["BulletFactory2D.convert_all_bullets_to_pickups: group \"nobody_home\" has no usable Node2D member, nothing converted."])
	var loose := Node2D.new()
	assert_eq(factory.convert_all_bullets_to_pickups(_coin(), loose), [], "a node outside the tree")
	expect_error_sequence(["BulletFactory2D.convert_all_bullets_to_pickups: target node is not inside the scene tree, nothing converted."])
	loose.free()
	assert_eq(_alive(v), [0, 1], "no bullet was touched by any refusal")
	assert_eq(target.position, Vector2(300, 0), "sanity")
	assert_eq(v.convert_to_pickups(_coin(), target, PackedInt32Array([5])), [], "an index out of range")
	expect_error_sequence(["BulletVolley2D.convert_to_pickups: bullet index 5 is out of range (0..1), nothing converted."])
	assert_eq(_alive(v), [0, 1], "still intact")
	assert_eq(converted.size(), 0, "and no signal")


func test_deferred_twin_runs_after_the_physics_step() -> void:
	make_wall(Vector2(100, 0))
	var data := H.make_volley_data(1, 300.0, 60.0)
	data.set_collision_mask_from_array([3])
	data.collision_shape = H.make_circle_shape(6.0)
	data.bullet_max_collision_count = 5
	data.monitorable = true
	factory.spawn_volley(data)
	var target := _target(Vector2(900, 0))
	var coin := _coin()
	factory.body_entered.connect(func(_b, _v, _i): factory.convert_all_bullets_to_pickups_deferred(coin, target))
	for i in 60:
		await physics(1)
		if converted.size() > 0:
			break
	assert_gt(converted.size(), 0, "the hit handler converted everything through the deferred twin")


# --- limits and pooling ------------------------------------------------------------------------------------------------

func test_a_huge_conversion_is_split_by_the_pattern_limit() -> void:
	var pts: Array = []
	for i in 1200:
		pts.append(Vector2(i, 0))
	_bullets(pts)
	BulletPatterns2D.set_max_bullets_per_pattern(500)
	var out: Array = factory.convert_all_bullets_to_pickups(_coin(), _target(Vector2(0, 900)))
	assert_eq(out.map(func(v): return v.get_amount_bullets()), [500, 500, 200], "chunks of at most the limit")
	assert_eq(converted[0]["count"], 1200, "one signal with the full count")


func test_inherit_custom_data_carries_the_source_resource() -> void:
	var v := _bullets([Vector2(0, 0), Vector2(10, 0)])
	var meta := Resource.new()
	v.bullet_set_custom_data(1, meta)
	var coin := _coin()
	coin.inherit_custom_data = true
	var pv: BulletVolley2D = factory.convert_all_bullets_to_pickups(coin, _target(Vector2(500, 0)))[0]
	var found := 0
	for i in 2:
		if pv.bullet_get_custom_data(i) == meta:
			found += 1
	assert_eq(found, 1, "the source's custom data follows its bullet")
	# Without the flag nothing is copied.
	var w := _bullets([Vector2(0, 0)])
	w.bullet_set_custom_data(0, meta)
	var bare: BulletVolley2D = factory.convert_all_bullets_to_pickups(_coin(), _target(Vector2(500, 0)))[0]
	assert_null(bare.bullet_get_custom_data(0), "no custom data without inherit_custom_data")


func test_pooled_pickup_volleys_come_back_as_plain_bullets() -> void:
	_bullets([Vector2(0, 0), Vector2(10, 0)])
	var pv: BulletVolley2D = factory.convert_all_bullets_to_pickups(_coin(), Vector2(30, 0))[0]
	await _wait_collected(2)
	await idle(2)
	assert_eq(factory.get_active_bullet_count(), 0, "both collected")
	collected.clear()
	factory.debug_reset_pool_stats()
	var data := H.make_volley_data(2, 100.0, 60.0)
	data.transforms = [Transform2D(0.0, Vector2(500, 0)), Transform2D(0.0, Vector2(510, 0))]
	var plain: BulletVolley2D = factory.spawn_volley(data)
	assert_same(plain, pv, "the pooled pickup volley was reused")
	assert_false(plain.is_pickup_volley(), "no longer a pickup volley")
	assert_almost_eq(plain.get_bullet_velocity(0).length(), 100.0, 0.01, "plain speed")
	await idle(30)
	assert_eq(collected.size(), 0, "plain bullets never report pickup collection")
	assert_almost_eq(plain.get_bullet_velocity(0).length(), 100.0, 0.5, "and no drag or magnet leaked in")
	assert_eq(plain.self_modulate, Color(1, 1, 1, 1), "default tint")
