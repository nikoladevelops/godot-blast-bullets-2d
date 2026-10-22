extends BlastTest
## Live-volley API: adopt_live_volley rejects bad instances loudly,
## clear_live_volleys_homing really empties the queues, and the live control
## calls act on every volley the spawner owns (homing or not):
## override_live_volleys_velocity, apply_to_live_volleys (homing, split),
## home_live_volleys_to (node, point, group, mouse), aim_live_volleys_at and
## the live speed calls, each rejecting bad input with its exact text.

var target: Node2D


func before_each() -> void:
	await super()
	target = Node2D.new()
	target.position = Vector2(400, 0)
	add(target)


func _homing_spawner() -> BulletSpawner2D:
	var sp := make_spawner(H.make_volley_data(2, 50.0, 30.0))
	sp.set_homing_enabled(true)
	sp.set_homing_target_source(BulletSpawner2D.HOMING_SOURCE_NODE_PATH)
	sp.set_homing_target_path(sp.get_path_to(target))
	return sp


func test_adopt_rejects_null_pooled_and_dying_volleys() -> void:
	var sp := _homing_spawner()
	assert_false(sp.adopt_live_volley(null), "null")
	expect_error_sequence(["adopt_live_volley: instance is null"])
	var pooled := quick_volley(2)
	await idle(1)
	factory.clear_active_bullets()
	await idle(1)
	assert_false(sp.adopt_live_volley(pooled), "pooled")
	expect_error_sequence(["adopt_live_volley: instance is not live"])
	var dying := quick_volley(2)
	dying.queue_free()
	assert_false(sp.adopt_live_volley(dying), "queued for deletion")
	expect_error_sequence(["adopt_live_volley: instance is queued for deletion"])
	assert_eq(sp.get_live_volley_count(), 0, "nothing adopted")


func test_clear_live_volleys_homing_empties_the_queues() -> void:
	var sp := _homing_spawner()
	assert_true(sp.shoot_once(), "homing shot")
	var v: BulletVolley2D = sp.get_live_volleys()[0]
	assert_eq(v.shared_homing_deque_get_current_homing_target(), target, "chasing before")
	assert_eq(sp.clear_live_volleys_homing(), 1, "one volley cleared")
	assert_null(v.shared_homing_deque_get_current_homing_target(), "queue empty after")


func test_override_velocity_changes_flight_and_rejects_nan() -> void:
	var sp := _homing_spawner()
	sp.set_homing_retarget_mode(BulletSpawner2D.HOMING_RETARGET_OFF) # no re-aim mid-measure
	assert_true(sp.shoot_once(), "shot")
	var v: BulletVolley2D = sp.get_live_volleys()[0]
	sp.clear_live_volleys_homing() # straight flight from here
	assert_eq(sp.override_live_volleys_velocity(Vector2(0, 600)), 1, "one volley overridden")
	var before: Vector2 = v.get_bullet_transform(0).origin
	await physics(6)
	var moved: Vector2 = v.get_bullet_transform(0).origin - before
	assert_gt(moved.y, 40.0, "flies along the new velocity (+Y)")
	assert_lt(absf(moved.x), 5.0, "no leftover sideways motion")
	sp.override_live_volleys_velocity(Vector2(NAN, 0))
	expect_error_sequence(["override_live_volleys_velocity: velocity must be finite"])


func _plain_spawner(n := 2, speed := 50.0) -> BulletSpawner2D:
	var sp := make_spawner(H.make_volley_data(n, speed, 30.0), BulletSpawner2D.PATTERN_FROM_HELPER_RING, n)
	sp.helper_ring_radius = 20.0
	return sp


func _shoot(sp: BulletSpawner2D) -> BulletVolley2D:
	var got: Array = []
	sp.volley_fired.connect(func(v, _n) -> void: got.append(v), CONNECT_ONE_SHOT)
	assert_true(sp.shoot_once(), "fires")
	return got[0]


func test_override_velocity_reaches_volleys_fired_without_homing() -> void:
	var sp := _plain_spawner()
	var v := _shoot(sp)
	assert_eq(sp.override_live_volleys_velocity(Vector2(0, 90)), 1, "a plain volley counts too")
	assert_eq(v.get_bullet_velocity(0), Vector2(0, 90), "and flies the new velocity")


func test_apply_homing_arms_volleys_fired_while_it_was_off() -> void:
	assert_true(ClassDB.class_has_method("BulletSpawner2D", "apply_to_live_volleys"), "bound")
	var sp := _plain_spawner()
	var v := _shoot(sp)
	assert_false(v.shared_homing_deque_check_has_homing_targets(), "fired without homing")
	sp.set_homing_target_source(BulletSpawner2D.HOMING_SOURCE_NODE_PATH)
	sp.set_homing_target_path(sp.get_path_to(target))
	sp.set_homing_enabled(true)
	watch_signals(sp)
	assert_eq(sp.apply_to_live_volleys(BulletSpawner2D.LIVE_HOMING), 1, "armed now")
	assert_signal_emit_count(sp, "volley_homing_configured", 1, "with the shot's homing signals")
	assert_true(v.shared_homing_deque_check_has_homing_targets(), "it chases")
	assert_eq(sp.apply_to_live_volleys(BulletSpawner2D.LIVE_HOMING), 0, "armed alike: left alone")
	sp.set_homing_enabled(false)
	assert_eq(sp.apply_to_live_volleys(BulletSpawner2D.LIVE_HOMING), 1, "off: the chase is cleared")
	assert_false(v.shared_homing_deque_check_has_homing_targets(), "no target left")


func test_apply_split_arms_volleys_fired_while_it_was_off() -> void:
	var sp := _plain_spawner(1, 0.0)
	sp.spawn_data.max_life_time = 0.1
	var v := _shoot(sp)
	var stage := BulletSplitStage2D.new()
	stage.count = 3
	var chain := BulletSplitData2D.new()
	chain.stages = [stage]
	sp.split_data = chain
	sp.split_enabled = true
	var children: Array = []
	sp.bullet_split.connect(func(_p, _c, ch, _d) -> void: children.append(ch))
	assert_eq(sp.apply_to_live_volleys(BulletSpawner2D.LIVE_ALL), 1, "every group at once counts the volley once")
	for f in 20:
		await physics(1)
		if not children.is_empty():
			break
	assert_eq(children.size(), 1, "the volley fired while splitting was off now splits")
	assert_eq(children[0].get_amount_bullets(), 3, "by the chain")


func test_home_live_volleys_to_every_kind_of_target() -> void:
	var sp := _plain_spawner(2, 100.0)
	sp.homing_smoothing = 0.0 # snap: the heading shows the target at once
	var v := _shoot(sp)
	assert_eq(sp.home_live_volleys_to(target), 1, "a node")
	step_factory(1)
	assert_almost_eq(v.get_bullet_direction(0), (target.global_position - v.get_bullet_transform(0).origin).normalized(), Vector2(0.05, 0.05), "fired without homing, it turns to the node")
	assert_eq(sp.home_live_volleys_to(Vector2(0, -500)), 1, "a point")
	step_factory(1)
	assert_lt(v.get_bullet_direction(0).y, -0.9, "it turns to the point")
	var left := make_graze_target(Vector2(-300, 0), &"live_hunt")
	var right := make_graze_target(Vector2(300, 0), &"live_hunt")
	assert_eq(sp.home_live_volleys_to(&"live_hunt"), 1, "a group")
	assert_eq(v.bullet_get_current_homing_target(0), right, "bullet 0 (east of the ring) chases the nearest member")
	assert_eq(v.bullet_get_current_homing_target(1), left if v.get_bullet_transform(1).origin.x < 0.0 else right, "bullet 1 too")
	assert_eq(sp.home_live_volleys_to(null), 1, "null: the mouse")
	assert_true(v.shared_homing_deque_check_has_homing_targets(), "a mouse target queued")
	for bad in [3, Vector2(NAN, 0), &""]:
		assert_eq(sp.home_live_volleys_to(bad), 0, "refused")
		expect_error_sequence(["BulletSpawner2D::home_live_volleys_to: target must be a Node2D in the tree, a finite Vector2, a group name or null (the mouse), nothing changed."])


func test_aim_and_speed_reach_every_live_bullet() -> void:
	var sp := _plain_spawner(4, 100.0)
	var v := _shoot(sp)
	assert_eq(sp.aim_live_volleys_at(Vector2(0, 1000)), 1, "aimed")
	for i in 4:
		assert_almost_eq(v.get_bullet_direction(i), (Vector2(0, 1000) - v.get_bullet_transform(i).origin).normalized(), Vector2(0.001, 0.001), "bullet %d faces the point" % i)
	assert_eq(sp.set_live_volleys_speed(250.0), 1, "speed set")
	assert_eq(Array(v.all_bullets_get_speed()), [250.0, 250.0, 250.0, 250.0], "read back")
	assert_eq(sp.scale_live_volleys_speed(0.5), 1, "scaled")
	assert_eq(Array(v.all_bullets_get_speed()), [125.0, 125.0, 125.0, 125.0], "halved")
	assert_eq(sp.scale_live_volleys_speed(0.0), 1, "zero stops them")
	step_factory(2)
	assert_eq(v.get_bullet_velocity(0), Vector2.ZERO, "standing still")
	assert_eq(sp.aim_live_volleys_at(Vector2(NAN, 0)), 0, "bad aim")
	expect_error_sequence(["BulletSpawner2D::aim_live_volleys_at: position must be finite, nothing changed."])
	assert_eq(sp.set_live_volleys_speed(INF), 0, "bad speed")
	expect_error_sequence(["BulletSpawner2D::set_live_volleys_speed: speed must be finite, nothing changed."])
	assert_eq(sp.scale_live_volleys_speed(-1.0), 0, "bad factor")
	expect_error_sequence(["BulletSpawner2D::scale_live_volleys_speed: factor must be finite and >= 0, nothing changed."])


func _park(v: BulletVolley2D) -> void:
	v.set_is_auto_pooling_enabled(false)
	for i in v.get_amount_bullets():
		v.disable_bullet(i)
	assert_true(v.is_parked(), "parked")


func _chases(v: BulletVolley2D) -> bool:
	if v.shared_homing_deque_check_has_homing_targets():
		return true
	for amount in v.all_bullets_get_homing_targets_amount():
		if amount > 0:
			return true
	return false


## A parked homing volley (auto pooling off, every bullet out) stays on the
## retarget list: woken, it is retargeted again like any live volley.
func test_a_woken_parked_volley_is_still_retargeted() -> void:
	var sp := _homing_spawner()
	var v := _shoot(sp)
	assert_eq(sp.get_tracked_volley_count(), 1, "tracked")
	_park(v)
	assert_eq(sp.get_tracked_volley_count(), 1, "a parked volley stays on the list (a retarget pass prunes it like this)")
	assert_eq(sp.retarget_live_volleys(), 0, "but a pass never touches it while parked")
	v.enable_bullet(0)
	assert_eq(sp.get_tracked_volley_count(), 1, "still on the retarget list once woken")
	var other: Node2D = add(Node2D.new())
	other.position = Vector2(0, 400)
	sp.set_homing_target_path(sp.get_path_to(other))
	assert_eq(sp.retarget_live_volleys(), 1, "the woken volley is retargeted")
	assert_eq(v.shared_homing_deque_get_current_homing_target(), other, "chasing the new target")


## apply_to_live_volleys(LIVE_HOMING) reaches parked volleys (documented:
## live or parked): homing off clears their chase, which would otherwise
## resume on a wake; homing on arms them when they wake, like a fresh shot
## (Shared and Per Bullet, with the shot's homing signals).
func test_apply_homing_reaches_parked_volleys(mode: int = use_parameters([0, 1])) -> void:
	var sp := _homing_spawner()
	sp.set_homing_mode(mode)
	var v := _shoot(sp)
	assert_true(_chases(v), "fired chasing")
	_park(v)
	sp.set_homing_enabled(false)
	assert_eq(sp.apply_to_live_volleys(BulletSpawner2D.LIVE_HOMING), 1, "off: the parked chase is cleared")
	v.enable_bullet(0)
	assert_false(_chases(v), "woken, it flies straight")
	v.disable_bullet(0)
	assert_true(v.is_parked(), "parked again")
	sp.set_homing_enabled(true)
	watch_signals(sp)
	assert_eq(sp.apply_to_live_volleys(BulletSpawner2D.LIVE_HOMING), 1, "on: the parked volley is armed for its wake")
	assert_signal_emit_count(sp, "volley_homing_configured", 0, "nothing configures while it is parked")
	v.enable_bullet(1)
	assert_signal_emit_count(sp, "volley_homing_configured", 1, "armed at the wake")
	assert_true(_chases(v), "it chases once woken")
	assert_eq(sp.apply_to_live_volleys(BulletSpawner2D.LIVE_HOMING), 0, "armed alike now")


## Off then on again re-arms what the off cleared: a cleared volley leaves
## the retarget list, so it never counts as "armed alike" afterwards.
func test_apply_homing_off_then_on_rearms_the_cleared_volleys() -> void:
	var sp := _homing_spawner()
	var v := _shoot(sp)
	sp.set_homing_enabled(false)
	assert_eq(sp.apply_to_live_volleys(BulletSpawner2D.LIVE_HOMING), 1, "off: cleared")
	assert_false(_chases(v), "flies straight")
	assert_eq(sp.get_tracked_volley_count(), 0, "and leaves the retarget list")
	sp.set_homing_enabled(true)
	assert_eq(sp.apply_to_live_volleys(BulletSpawner2D.LIVE_HOMING), 1, "on again: re-armed")
	assert_true(_chases(v), "it chases again")


## The wake arming runs the homing signals (user code): a handler that frees
## the volley it is arming ends the arming, nothing touches the volley again.
func test_a_wake_arming_handler_may_free_the_volley() -> void:
	var sp := _plain_spawner()
	var v := _shoot(sp)
	_park(v)
	sp.set_homing_target_source(BulletSpawner2D.HOMING_SOURCE_NODE_PATH)
	sp.set_homing_target_path(sp.get_path_to(target))
	sp.set_homing_enabled(true)
	assert_eq(sp.apply_to_live_volleys(BulletSpawner2D.LIVE_HOMING), 1, "marked for its wake")
	var configured: Array = []
	sp.homing_targets_resolved.connect(func(vol: BulletVolley2D, _t) -> void: vol.free(), CONNECT_ONE_SHOT)
	sp.volley_homing_configured.connect(func(_vol, _n) -> void: configured.append(true))
	v.enable_bullet(0)
	assert_false(is_instance_valid(v), "freed by the handler")
	assert_eq(configured, [], "the arming stops with the volley")
	expect_no_errors("no error, no touch of the freed volley")
	assert_eq(sp.get_tracked_volley_count(), 0, "nothing left on the list")
