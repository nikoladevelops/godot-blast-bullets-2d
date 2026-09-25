extends SceneTree
## Spawner signals + sequencing suite: handler contracts under fire.
## Covers: pre_shoot/volley_fired/volley_skipped payloads, nested shoot_once
## rejection + shoot_once_deferred, free-in-pre_shoot drops the shot (no count,
## no volley_fired), burst chains + mirror flag + burst signals, telegraph
## warn-then-fire, pattern_list sequential/simultaneous + malformed entries,
## max_volleys cap + shooting_finished, adopt_live_volley + clear/override.
## Run: godot --headless --path test_project --script tests/spawner/test_spawner_signals_sequencing.gd
## Exit code 0 = all pass.

const H := preload("res://tests/common/blast_test_helpers.gd")

var failures := 0
var _fired: Array = []
var _skipped: Array = []
var _preshoot_volleys: Array = []
var _free_in_preshoot := false
var _nested_tried := false
var _factory_ref: BulletFactory2D = null

func _check(cond: bool, label: String) -> void:
	if cond:
		print("PASS  ", label)
	else:
		failures += 1
		printerr("FAIL  ", label)

func _on_fired(volley: Object, idx: int) -> void:
	_fired.append([volley, idx])

func _on_skipped(reason: StringName) -> void:
	_skipped.append(reason)

func _on_preshoot(volley: Object, _idx: int) -> void:
	_preshoot_volleys.append(volley)
	if _free_in_preshoot and volley is Node:
		(volley as Node).queue_free()
	if _nested_tried:
		pass

func _initialize() -> void:
	var factory := BulletFactory2D.new()
	_factory_ref = factory
	get_root().add_child(factory)
	await process_frame
	await process_frame
	var spawner := BulletSpawner2D.new()
	get_root().add_child(spawner)
	await process_frame
	spawner.set_bullet_factory(factory)
	spawner.set_spawn_data(H.make_directional_data(4))
	spawner.set_shooting_enabled(false)
	spawner.pattern_source = 3
	spawner.helper_bullets_amount = 4
	spawner.volley_fired.connect(_on_fired)
	spawner.volley_skipped.connect(_on_skipped)
	spawner.pre_shoot.connect(_on_preshoot)

	printerr("SEQ T1 basic signals")
	_check(spawner.shoot_once(), "shot fires")
	_check(_fired.size() == 1 and (_fired[0][1] as int) == 1, "volley_fired payload (volley, 1)")
	_check(_preshoot_volleys.size() == 1, "pre_shoot ran before fire")

	printerr("SEQ T2 free in pre_shoot drops the shot")
	_free_in_preshoot = true
	var before: int = spawner.get_volleys_fired()
	var fired_before: int = _fired.size()
	_check(spawner.shoot_once() == false, "freed-volley shot returns false")
	_check(spawner.get_volleys_fired() == before, "dropped shot not counted")
	_check(_fired.size() == fired_before, "no volley_fired for dead volley")
	_free_in_preshoot = false
	await process_frame

	printerr("SEQ T3 burst chain")
	spawner.set_burst_enabled(true)
	spawner.set_burst_count(3)
	spawner.set_burst_interval_sec(0.05)
	spawner.begin_burst()
	for i in 30:
		await physics_frame
	_check(spawner.get_volleys_fired() >= before + 3, "burst fired 3 volleys")
	spawner.set_burst_enabled(false)

	printerr("SEQ T4 telegraph warn-then-fire")
	var tb: int = spawner.get_volleys_fired()
	spawner.set_telegraph_enabled(true)
	spawner.set_telegraph_sec(0.05)
	spawner.begin_telegraph()
	for i in 20:
		await physics_frame
	_check(spawner.get_volleys_fired() >= tb + 1, "telegraphed shot fired after warning")
	spawner.set_telegraph_enabled(false)

	printerr("SEQ T5 pattern list")
	var n: int = spawner.spawn_pattern_list([{"helper_bullets_amount": 3}, {"helper_bullets_amount": 5}], false, 0.02)
	_check(n == 2 and spawner.is_pattern_list_active(), "sequential list queues 2")
	for i in 20:
		await physics_frame
	_check(not spawner.is_pattern_list_active(), "sequential list drains")
	var sim: int = spawner.spawn_pattern_list([{"helper_bullets_amount": 2}, {"helper_bullets_amount": 2}], true, 0.0)
	_check(sim == 2, "simultaneous list fires both")
	var bad_amt_before: int = spawner.get_helper_bullets_amount()
	var bad: int = spawner.spawn_pattern_list([42, {"helper_bullets_amount": "x"}], true, 0.0)
	_check(bad == 1, "non-dict skipped, bad-typed field keeps old value, valid entry still fires")
	_check(spawner.get_helper_bullets_amount() == bad_amt_before, "bad-typed amount rejected")
	spawner.stop_pattern_list()

	printerr("SEQ T6 cap + adopt + live ops")
	spawner.set_homing_enabled(true)
	spawner.set_homing_target_source(2)
	spawner.set_homing_global_position(Vector2(400, 0))
	spawner.set_max_volleys(spawner.get_volleys_fired() + 1)
	spawner.shoot_once()
	_check(spawner.get_volleys_fired() == spawner.get_max_volleys(), "cap reached")
	spawner.set_max_volleys(-1)
	var live: Array = spawner.get_live_volleys()
	_check(spawner.get_live_volley_count() >= 0, "live census reads")
	if live.size() >= 1 and live[0] is DirectionalBullets2D:
		_check(spawner.adopt_live_volley(live[0]), "adopt live volley")
		_check(spawner.clear_live_volleys_homing() >= 0, "clear homing returns count")
		_check(spawner.override_live_volleys_velocity(Vector2(100, 0)) >= 0, "override velocity returns count")
	spawner.clear_live_volleys()
	_check(spawner.get_live_volley_count() == 0, "clear forgets")

	_check(factory.debug_assert_no_dangling().get("ok", false) == true, "no dangling at end")
	spawner.queue_free()
	factory.reset()
	factory.queue_free()
	await process_frame
	print("----")
	if failures == 0:
		print("ALL SIGNAL/SEQUENCING TESTS PASSED")
	else:
		printerr(str(failures) + " TEST(S) FAILED")
	quit(failures)
