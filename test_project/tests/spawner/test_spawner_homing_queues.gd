extends BlastTest
## Homing queues and invalid nodes: the 256-target queue cap is respected
## without errors, freed targets are trimmed mid-flight, retarget passes skip
## volleys that died, pooled, changed owner or belong to an old factory (and
## never push onto disabled bullets), and the homing signals fire with exact
## counts and payloads - including handlers that free the volley or target.

var target: Node2D


func before_each() -> void:
	await super()
	target = Node2D.new()
	target.position = Vector2(600, 0) # far: bullets never reach it in a test window
	add(target)


func _spawner(amount: int = 2, speed: float = 50.0) -> BulletSpawner2D:
	var sp := make_spawner(H.make_volley_data(amount, speed, 30.0), BulletSpawner2D.PATTERN_FROM_HELPER_RING, amount)
	sp.set_homing_enabled(true)
	sp.set_homing_retarget_mode(BulletSpawner2D.HOMING_RETARGET_OFF)
	sp.set_homing_target_source(BulletSpawner2D.HOMING_SOURCE_NODE_PATH)
	sp.set_homing_target_path(sp.get_path_to(target))
	watch_signals(sp)
	return sp


func _finite(v: BulletVolley2D) -> bool:
	for i in v.get_amount_bullets():
		if not v.get_bullet_transform(i).is_finite():
			return false
	return true


# --- No queue cap ------------------------------------------------------------

func test_queues_hold_every_target_without_a_cap() -> void:
	for i in 300:
		var n := Node2D.new()
		n.position = Vector2(100 + i, 0)
		add(n)
		n.add_to_group("swarm")
	var sp := _spawner()
	sp.set_homing_target_source(BulletSpawner2D.HOMING_SOURCE_NODE_GROUP)
	sp.set_homing_node_group(&"swarm")
	sp.set_homing_max_targets(10000)
	assert_true(sp.shoot_once(), "shared-mode shot")
	expect_no_errors("no errors")
	var shared: BulletVolley2D = sp.get_live_volleys()[0]
	assert_eq(shared.shared_homing_deque_check_homing_targets_amount(), 300, "the shared queue holds all 300")
	sp.set_homing_mode(BulletSpawner2D.HOMING_PER_BULLET)
	assert_true(sp.shoot_once(), "per-bullet shot")
	expect_no_errors("no errors per bullet either")
	var per: BulletVolley2D = sp.get_live_volleys()[1]
	for i in per.get_amount_bullets():
		assert_eq(per.bullet_homing_check_targets_amount(i), 300, "bullet %d queue holds all 300" % i)
	assert_eq(sp.retarget_live_volleys(), 2, "both volleys retargeted")
	expect_no_errors("a retarget pass raises nothing")
	assert_eq(per.bullet_homing_check_targets_amount(0), 300, "still 300 after the pass")


# --- Invalid nodes -----------------------------------------------------------

func test_target_freed_mid_flight_is_trimmed() -> void:
	var sp := _spawner()
	assert_true(sp.shoot_once(), "shared volley")
	sp.set_homing_mode(BulletSpawner2D.HOMING_PER_BULLET)
	assert_true(sp.shoot_once(), "per-bullet volley")
	var shared: BulletVolley2D = sp.get_live_volleys()[0]
	var per: BulletVolley2D = sp.get_live_volleys()[1]
	assert_true(shared.shared_homing_deque_check_has_homing_targets(), "chasing before")
	target.free()
	await physics(3)
	assert_false(shared.shared_homing_deque_check_has_homing_targets(), "the shared queue trimmed the dead target")
	for i in per.get_amount_bullets():
		assert_false(per.bullet_check_has_homing_targets(i), "bullet %d trimmed the dead target" % i)
	assert_true(_finite(shared) and _finite(per), "bullets stay finite")
	assert_signal_emit_count(sp, "volley_bullet_homing_target_reached", 0, "a freed target is never 'reached'")


func test_retarget_with_every_target_gone_keeps_queues_and_stays_silent() -> void:
	var sp := _spawner()
	sp.set_homing_target_source(BulletSpawner2D.HOMING_SOURCE_NODE_GROUP)
	sp.set_homing_node_group(&"wave")
	var foe := Node2D.new()
	foe.position = Vector2(500, 0)
	add(foe)
	foe.add_to_group("wave")
	assert_true(sp.shoot_once(), "volley chasing the wave")
	var v: BulletVolley2D = sp.get_live_volleys()[0]
	var other := Node2D.new()
	other.position = Vector2(-500, 0)
	add(other)
	v.shared_homing_deque_push_back_homing_targets_array([other]) # a queue the pass must not wipe
	foe.free()
	assert_eq(sp.retarget_live_volleys(), 0, "nothing to retarget to")
	assert_eq(v.shared_homing_deque_check_homing_targets_amount(), 2, "the old queue is left as it was")
	sp.set_homing_retarget_mode(BulletSpawner2D.HOMING_RETARGET_ON_INTERVAL)
	sp.set_homing_retarget_interval_sec(0.05)
	sp.set_homing_retarget_phase(0.05)
	await idle(12)
	assert_signal_emit_count(sp, "retarget_applied", 0, "no retarget_applied when nothing changed")


func test_retarget_skips_dead_pooled_foreign_and_old_factory_volleys() -> void:
	var sp := _spawner()
	sp.set_homing_target_source(BulletSpawner2D.HOMING_SOURCE_GLOBAL_POSITION)
	sp.set_homing_global_position(Vector2(400, 0))
	for i in 4:
		assert_true(sp.shoot_once(), "volley %d" % i)
	var vols: Array = sp.get_live_volleys()
	(vols[0] as BulletVolley2D).queue_free()
	(vols[1] as BulletVolley2D).clear_all_bullets() # parks in the pool
	var thief := make_spawner()
	thief.set_homing_enabled(true)
	assert_true(thief.adopt_live_volley(vols[2]), "another spawner takes volley 2")
	await idle(1)
	assert_eq(sp.retarget_live_volleys(), 1, "only the surviving own volley is retargeted")
	assert_eq(sp.get_live_volley_count(), 1, "the tracker pruned the rest")
	var other_factory := BulletFactory2D.new()
	add(other_factory)
	await idle(2)
	sp.set_bullet_factory(other_factory)
	assert_eq(sp.retarget_live_volleys(), 0, "volleys of the previous factory keep their last steering")


func test_retarget_never_pushes_onto_disabled_bullets() -> void:
	var sp := _spawner(3)
	sp.set_homing_mode(BulletSpawner2D.HOMING_PER_BULLET)
	assert_true(sp.shoot_once(), "shot")
	var v: BulletVolley2D = sp.get_live_volleys()[0]
	v.disable_bullet(1, true, true)
	assert_eq(v.bullet_homing_check_targets_amount(1), 0, "disabling with reset_state clears the queue")
	assert_eq(sp.retarget_live_volleys(), 1, "partly alive volley retargeted")
	assert_eq(v.bullet_homing_check_targets_amount(1), 0, "the disabled bullet stays empty (retarget skips it)")
	assert_eq(v.bullet_homing_check_targets_amount(0), 1, "live bullets get the target")
	assert_eq(v.bullet_homing_check_targets_amount(2), 1, "live bullets get the target")


func test_a_frozen_bullet_keeps_its_queue_and_retarget_skips_it() -> void:
	var sp := _spawner(3)
	sp.set_homing_mode(BulletSpawner2D.HOMING_PER_BULLET)
	assert_true(sp.shoot_once(), "shot")
	var v: BulletVolley2D = sp.get_live_volleys()[0]
	v.disable_bullet(1)
	assert_eq(v.bullet_homing_check_targets_amount(1), 1, "freeze keeps the queue for the wake")
	assert_eq(sp.retarget_live_volleys(), 1, "partly alive volley retargeted")
	assert_eq(v.bullet_homing_check_targets_amount(1), 1, "retarget never touches the frozen bullet")
	v.enable_bullet(1)
	assert_eq(v.bullet_homing_check_targets_amount(1), 1, "the wake resumes with the frozen queue")


# --- Signals -----------------------------------------------------------------

func test_homing_signals_count_and_payload() -> void:
	var sp := _spawner()
	sp.set_homing_enabled(false)
	assert_true(sp.shoot_once(), "plain shot")
	assert_signal_emit_count(sp, "homing_targets_resolved", 0, "homing off: no resolution signal")
	assert_signal_emit_count(sp, "volley_homing_configured", 0, "homing off: no configuration signal")
	sp.set_homing_enabled(true)
	for i in 3:
		assert_true(sp.shoot_once(), "homing shot %d" % i)
	assert_signal_emit_count(sp, "homing_targets_resolved", 3, "once per volley")
	for i in 3:
		var resolved: Array = get_signal_parameters(sp, "homing_targets_resolved", i)
		assert_eq(resolved[1], [target], "payload: the resolved targets")
		assert_eq(get_signal_parameters(sp, "volley_homing_configured", i)[1], i + 2, "volley index (the plain shot counted as 1)")
	assert_eq(get_signal_parameters(sp, "homing_targets_resolved", 0)[0], get_signal_parameters(sp, "volley_fired", 1)[0], "same volley object as volley_fired")


func test_reached_fires_once_per_bullet_with_payload() -> void:
	target.position = Vector2(90, 0)
	var sp := _spawner(2, 400.0)
	sp.helper_ring_radius = 10.0
	sp.set_homing_mode(BulletSpawner2D.HOMING_PER_BULLET)
	sp.set_homing_distance_before_reached(25.0)
	assert_true(sp.shoot_once(), "shot")
	for i in 120:
		await physics()
		if get_signal_emit_count(sp, "volley_bullet_homing_target_reached") >= 2:
			break
	await physics(10)
	assert_signal_emit_count(sp, "volley_bullet_homing_target_reached", 2, "once per bullet")
	var indexes: Array = []
	for i in 2:
		var p: Array = get_signal_parameters(sp, "volley_bullet_homing_target_reached", i)
		indexes.append(p[1])
		assert_eq(p[2], target, "the target reached")
		assert_eq(p[3], target.global_position, "its position")
	indexes.sort()
	assert_eq(indexes, [0, 1], "both bullets reported")


func test_retarget_applied_reports_the_live_homing_volleys() -> void:
	var sp := _spawner()
	sp.set_homing_target_source(BulletSpawner2D.HOMING_SOURCE_GLOBAL_POSITION)
	sp.set_homing_global_position(Vector2(400, 0))
	for i in 3:
		assert_true(sp.shoot_once(), "volley %d" % i)
	sp.set_homing_retarget_interval_sec(0.05)
	sp.set_homing_retarget_phase(0.05)
	sp.set_homing_retarget_mode(BulletSpawner2D.HOMING_RETARGET_ON_INTERVAL)
	for i in 30:
		await idle(1)
		if get_signal_emit_count(sp, "retarget_applied") > 0:
			break
	assert_eq(get_signal_parameters(sp, "retarget_applied", 0)[0], 3, "every live homing volley re-aimed")


# --- Re-entrant handlers -----------------------------------------------------

func test_handler_freeing_the_volley_drops_the_shot() -> void:
	var sp := _spawner()
	sp.homing_targets_resolved.connect(func(v, _t): (v as Node).queue_free(), CONNECT_ONE_SHOT)
	assert_false(sp.shoot_once(), "the configured volley died in a handler")
	assert_signal_emit_count(sp, "volley_fired", 0, "no volley_fired for a dropped volley")
	assert_eq(str(get_signal_parameters(sp, "volley_skipped", 0)[0]), "dropped", "reported as dropped")
	await idle(1)
	assert_true(sp.shoot_once(), "the next shot is normal")


func test_handler_freeing_the_target_is_safe() -> void:
	var sp := _spawner()
	sp.volley_homing_configured.connect(func(_v, _i): target.free(), CONNECT_ONE_SHOT)
	assert_true(sp.shoot_once(), "shot completes")
	var v: BulletVolley2D = sp.get_live_volleys()[0]
	await physics(3)
	assert_false(v.shared_homing_deque_check_has_homing_targets(), "the dead target was trimmed")
	assert_true(_finite(v), "bullets stay finite")
	assert_true(sp.shoot_once(), "later shots: plain volley, no crash")
