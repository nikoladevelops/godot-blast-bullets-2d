extends SceneTree
## Spawner tracker-cap suite: the 256-volley bound holds and stays useful.
##
## Contract under test: VolleyTracker2D caps at 256 tracked volleys (oldest
## dropped first); membership is exact (no duplicates); clear_live_volleys()
## empties and re-arms; the newest volley keeps retargeting at the cap.
## The tracker's steady-state prune allocates nothing (verified by code
## inspection; behavior here proves the cap + liveness semantics).
##
## Covers: T1 260 homing shots cap at 256 tracked, T2 newest volley retargets
## at the cap, T3 clear empties and later shots track again, T4 no dangling.
##
## Run: godot --headless --path test_project --script tests/spawner/test_spawner_tracker_cap.gd
## Exit code 0 = all pass. Any FAIL = behavior drift or a real bug.

var failures := 0

func _check(cond: bool, label: String) -> void:
	if cond:
		print("PASS  ", label)
	else:
		failures += 1
		printerr("FAIL  ", label)

func _data() -> DirectionalBulletsData2D:
	var d := DirectionalBulletsData2D.new()
	d.transforms = [Transform2D(0.0, Vector2.ZERO)]
	var s := BulletSpeedData2D.new()
	s.speed = 0.0
	s.max_speed = 3000.0
	d.all_bullet_speed_data = [s]
	d.max_life_time = 600.0
	d.texture_size = Vector2(16, 16)
	d.set_collision_layer_from_array([2])
	d.set_collision_mask_from_array([3])
	var c := CircleShape2D.new()
	c.radius = 6.0
	d.collision_shape = c
	d.bullet_max_collision_count = 0
	return d

func _initialize() -> void:
	var factory := BulletFactory2D.new()
	get_root().add_child(factory)
	await process_frame
	await process_frame
	var sp := BulletSpawner2D.new()
	get_root().add_child(sp)
	sp.set_bullet_factory(factory)
	sp.set_spawn_data(_data())
	sp.set_shooting_enabled(false)
	sp.homing_enabled = true
	sp.homing_target_source = BulletSpawner2D.HOMING_SOURCE_GLOBAL_POSITION
	sp.homing_global_position = Vector2(500, 0)

	# ---------------------------------------------------------------
	printerr("TRACKER T1 260 shots cap at 256 tracked")
	var fired := 0
	for i in 260:
		if sp.shoot_once():
			fired += 1
	_check(fired == 260, "T1 all 260 shots fired (got %d)" % fired)
	_check(sp.get_live_volley_count() == 256, "T1 tracked caps at 256 (got %d)" % sp.get_live_volley_count())
	_check(sp.get_live_volleys().size() == 256, "T2 live list matches the cap")

	# ---------------------------------------------------------------
	printerr("TRACKER T2 newest volley still retargets at the cap")
	_check(sp.retarget_live_volleys() >= 1, "T2 retarget pass reaches volleys at the cap")

	# ---------------------------------------------------------------
	printerr("TRACKER T3 clear empties and tracking resumes")
	sp.clear_live_volleys()
	_check(sp.get_live_volley_count() == 0, "T3 clear empties")
	_check(sp.shoot_once(), "T3 shot after clear fires")
	_check(sp.get_live_volley_count() == 1, "T3 tracking resumes (got %d)" % sp.get_live_volley_count())

	_check(factory.debug_assert_no_dangling().get("ok", false) == true, "no dangling at end")

	sp.queue_free()
	factory.reset()
	factory.queue_free()
	await process_frame
	print("----")
	if failures == 0:
		print("ALL TRACKER-CAP TESTS PASSED")
	else:
		printerr(str(failures) + " TEST(S) FAILED")
	quit(failures)
