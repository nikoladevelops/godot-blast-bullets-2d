extends SceneTree
## Max-volleys edge suite: pins the documented setter/signal split.
##
## Contract under test (by design, not a bug): the shooting path emits
## shooting_finished exactly once for the volley that trips the cap, while
## set_max_volleys() only reports start/stop transitions. Lowering the cap
## onto (or below) the fired count therefore emits shooting_stopped and never
## a second shooting_finished. This suite pins that split so it cannot drift.
##
## Covers: T1 cap trip by shot emits finished once, T2 lowering onto the
## count emits stopped only, T3 raising re-arms started, T4 no dangling.
##
## Run: godot --headless --path test_project --script tests/spawner/test_spawner_max_volleys_edge.gd
## Exit code 0 = all pass. Any FAIL = behavior drift or a real bug.

var failures := 0
var _started := 0
var _stopped := 0
var _finished := 0

func _check(cond: bool, label: String) -> void:
	if cond:
		print("PASS  ", label)
	else:
		failures += 1
		printerr("FAIL  ", label)

func _on_started() -> void:
	_started += 1

func _on_stopped() -> void:
	_stopped += 1

func _on_finished() -> void:
	_finished += 1

func _data() -> DirectionalBulletsData2D:
	var d := DirectionalBulletsData2D.new()
	d.transforms = [Transform2D(0.0, Vector2.ZERO)]
	var s := BulletSpeedData2D.new()
	s.speed = 0.0
	s.max_speed = 3000.0
	d.all_bullet_speed_data = [s]
	d.max_life_time = 60.0
	d.texture_size = Vector2(16, 16)
	d.set_collision_layer_from_array([2])
	d.set_collision_mask_from_array([3])
	return d

func _spawner(factory: BulletFactory2D) -> BulletSpawner2D:
	var sp := BulletSpawner2D.new()
	get_root().add_child(sp)
	sp.set_bullet_factory(factory)
	sp.set_spawn_data(_data())
	sp.set_shooting_enabled(false)
	sp.shooting_started.connect(_on_started)
	sp.shooting_stopped.connect(_on_stopped)
	sp.shooting_finished.connect(_on_finished)
	return sp

func _initialize() -> void:
	var factory := BulletFactory2D.new()
	get_root().add_child(factory)
	await process_frame
	await process_frame

	# ---------------------------------------------------------------
	printerr("CAPEDGE T1 tripping the cap by shot emits finished once")
	var sp := _spawner(factory)
	sp.max_volleys = 2
	_started = 0
	_stopped = 0
	_finished = 0
	_check(sp.shoot_once(), "T1 shot 1 fires")
	_check(sp.shoot_once(), "T1 shot 2 fires")
	_check(_finished == 1, "T1 finished exactly once (got %d)" % _finished)
	_check(sp.shoot_once(), "T1 manual shot past cap still fires")
	_check(_finished == 1, "T1 no second finish past the cap")
	sp.queue_free()
	factory.free_active_bullets()
	await process_frame

	# ---------------------------------------------------------------
	printerr("CAPEDGE T2 lowering onto the count emits stopped only")
	var sp2 := _spawner(factory)
	sp2.max_volleys = -1
	sp2.set_shooting_enabled(true)
	_check(sp2.shoot_once(), "T2 shot 1 fires")
	_check(sp2.shoot_once(), "T2 shot 2 fires")
	_started = 0
	_stopped = 0
	_finished = 0
	# Lower exactly onto the fired count: Auto-shooting was active, so this
	# is a live crossing (synchronous: no auto tick can interleave here).
	sp2.max_volleys = sp2.get_volleys_fired()
	_check(_stopped == 1, "T2 stopped on crossing (got %d)" % _stopped)
	_check(_finished == 0, "T2 no finish from the setter (got %d)" % _finished)
	_check(not sp2.is_shooting_active(), "T2 inactive at the lowered cap")

	# ---------------------------------------------------------------
	printerr("CAPEDGE T3 raising re-arms started")
	_started = 0
	sp2.max_volleys = sp2.get_volleys_fired() + 3
	_check(_started == 1, "T3 started on raising (got %d)" % _started)
	_check(sp2.is_shooting_active(), "T3 active again")
	sp2.queue_free()
	factory.free_active_bullets()
	await process_frame

	_check(factory.debug_assert_no_dangling().get("ok", false) == true, "no dangling at end")

	factory.reset()
	factory.queue_free()
	await process_frame
	print("----")
	if failures == 0:
		print("ALL MAX-VOLLEYS-EDGE TESTS PASSED")
	else:
		printerr(str(failures) + " TEST(S) FAILED")
	quit(failures)
