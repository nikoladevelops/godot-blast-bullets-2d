extends BlastTest
## Spawner handler contracts under fire: pre_shoot/volley_fired payloads, a
## volley freed inside pre_shoot drops the shot, burst chains, telegraph
## warn-then-fire, pattern lists (sequential, simultaneous, malformed
## entries), the max_volleys cap, adopt/clear/override on live volleys.

var sp: BulletSpawner2D
var _free_in_preshoot := false


func _on_preshoot(volley: Object, _idx: int) -> void:
	if _free_in_preshoot and volley is Node:
		(volley as Node).queue_free()


func before_each() -> void:
	await super()
	_free_in_preshoot = false
	sp = make_spawner(H.make_volley_data(4), BulletSpawner2D.PATTERN_FROM_HELPER_RING, 4)
	sp.pre_shoot.connect(_on_preshoot)
	watch_signals(sp)


func test_basic_signal_payloads() -> void:
	assert_true(sp.shoot_once(), "shot fires")
	assert_signal_emit_count(sp, "pre_shoot", 1, "pre_shoot ran")
	assert_signal_emit_count(sp, "volley_fired", 1)
	assert_eq(get_signal_parameters(sp, "volley_fired")[1], 1, "volley_fired index 1")


func test_free_in_pre_shoot_drops_the_shot() -> void:
	_free_in_preshoot = true
	assert_false(sp.shoot_once(), "freed-volley shot returns false")
	assert_eq(sp.get_volleys_fired(), 0, "dropped shot not counted")
	assert_signal_not_emitted(sp, "volley_fired", "no volley_fired for a dead volley")


func test_burst_chain() -> void:
	sp.set_burst_enabled(true)
	sp.set_burst_count(3)
	sp.set_burst_interval_sec(0.05)
	sp.begin_burst()
	for i in 60:
		await idle(1)
		if get_signal_emit_count(sp, "burst_finished") > 0:
			break
	assert_eq(sp.get_volleys_fired(), 3, "burst fired exactly 3 volleys")
	assert_signal_emit_count(sp, "burst_finished", 1, "burst_finished once")


func test_telegraph_warn_then_fire() -> void:
	sp.set_telegraph_enabled(true)
	sp.set_telegraph_sec(0.05) # 3 frames at the fixed 60 fps
	sp.set_shoot_interval_sec(10.0) # one trigger pull inside the test window
	sp.set_shooting_enabled(true) # first pull is due immediately (no initial delay)
	for i in 10:
		await idle(1)
		if get_signal_emit_count(sp, "volley_telegraphed") > 0:
			break
	assert_signal_emit_count(sp, "volley_telegraphed", 1, "warning first")
	var aim: Array = get_signal_parameters(sp, "volley_telegraphed", 0)[0]
	assert_eq(aim.size(), sp.helper_bullets_amount, "warning carries one transform per bullet")
	assert_eq(sp.get_volleys_fired(), 0, "nothing fired during the warning")
	for i in 20:
		await idle(1)
		if sp.get_volleys_fired() > 0:
			break
	assert_eq(sp.get_volleys_fired(), 1, "exactly one telegraphed shot after the warning")
	sp.set_shooting_enabled(false)


func test_pattern_lists() -> void:
	var n: int = sp.spawn_pattern_list([{"helper_bullets_amount": 3}, {"helper_bullets_amount": 5}], false, 0.02)
	assert_eq(n, 2, "sequential list queues 2")
	assert_true(sp.is_pattern_list_active())
	await physics(20)
	assert_false(sp.is_pattern_list_active(), "sequential list drains")
	assert_signal_emitted(sp, "pattern_list_finished")
	assert_eq(sp.spawn_pattern_list([{"helper_bullets_amount": 2}, {"helper_bullets_amount": 2}], true, 0.0), 2, "simultaneous list fires both")
	var amount_before: int = sp.get_helper_bullets_amount()
	assert_eq(sp.spawn_pattern_list([42, {"helper_bullets_amount": "x"}], true, 0.0), 1, "non-dict skipped, valid entry still fires")
	expect_error("every entry must be a Dictionary")
	expect_error("must be an int")
	assert_eq(sp.get_helper_bullets_amount(), amount_before, "bad-typed amount rejected")
	# Unknown keys FAIL LOUD with a suggestion (contract fix: a typo used to
	# be ignored silently); the valid keys of the entry still apply.
	var src_before: int = sp.get_pattern_source()
	assert_eq(sp.spawn_pattern_list([{"patern_source": 5, "helper_bullets_amount": 2}], true, 0.0), 1, "entry with a typo still fires its valid keys")
	expect_error_sequence(["unknown entry key 'patern_source' (did you mean 'pattern_source'?)"])
	assert_eq(sp.get_pattern_source(), src_before, "the misspelled key changes nothing")


func test_cap_and_live_ops() -> void:
	sp.set_homing_enabled(true)
	sp.set_homing_target_source(BulletSpawner2D.HOMING_SOURCE_GLOBAL_POSITION)
	sp.set_homing_global_position(Vector2(400, 0))
	sp.set_max_volleys(1)
	assert_true(sp.shoot_once())
	assert_eq(sp.get_volleys_fired(), sp.get_max_volleys(), "cap reached")
	sp.set_max_volleys(-1)
	var live: Array = sp.get_live_volleys()
	assert_eq(live.size(), 1, "one live homing volley")
	assert_true(sp.adopt_live_volley(live[0]), "adopt the live volley (idempotent for its owner)")
	assert_eq(sp.clear_live_volleys_homing(), 1, "clear homing touches one volley")
	assert_eq(sp.override_live_volleys_velocity(Vector2(100, 0)), 1, "override velocity touches one volley")
	sp.forget_tracked_volleys()
	assert_eq(sp.get_tracked_volley_count(), 0, "forget empties the retarget list")
	var live_bullets: int = sp.get_active_live_bullet_count()
	assert_gt(live_bullets, 0, "bullets in flight")
	assert_eq(sp.clear_active_bullets(false), live_bullets, "clear_active_bullets removes every live bullet the spawner owns")
	assert_eq(sp.get_live_volley_count(), 0, "nothing live afterwards")
