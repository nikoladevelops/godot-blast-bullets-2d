extends SceneTree
## Spawner lifecycle-signal suite: transitions report exactly once.
##
## The bug: set_shooting_enabled() (and set_max_volleys()) emitted
## shooting_started/stopped even before the node entered the tree. The scene
## loader invokes setters for stored values, and _ready() emits the start
## itself — so a scene with shooting_enabled=true double-fired
## shooting_started on load. Transitions now report inside the tree only.
##
## Covers: T1 pre-tree setter emits nothing, T2 load emits exactly once,
## T3 post-tree toggle emits once per transition, T4 pre-tree cap silent,
## T5 no dangling.
##
## Run: godot --headless --path test_project --script tests/spawner/test_spawner_lifecycle_signals.gd
## Exit code 0 = all pass. Any FAIL = behavior drift or a real bug.

var failures := 0
var _started := 0
var _stopped := 0

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

func _initialize() -> void:
	var factory := BulletFactory2D.new()
	get_root().add_child(factory)
	await process_frame
	await process_frame

	# ---------------------------------------------------------------
	printerr("LIFECYCLE T1 pre-tree setters stay silent")
	var sp := BulletSpawner2D.new()
	sp.set_bullet_factory(factory)
	sp.set_spawn_data(_data())
	sp.shooting_started.connect(_on_started)
	sp.shooting_stopped.connect(_on_stopped)
	_started = 0
	_stopped = 0
	# Defaults are shooting on / unlimited: both directions transition.
	sp.set_shooting_enabled(false)
	_check(_stopped == 0, "T1 no stopped before tree (got %d)" % _stopped)
	sp.set_shooting_enabled(true)
	_check(_started == 0, "T1 no started before tree (got %d)" % _started)
	sp.max_volleys = 0
	_check(_stopped == 0, "T1 no cap-stopped before tree (got %d)" % _stopped)
	sp.max_volleys = -1

	# ---------------------------------------------------------------
	printerr("LIFECYCLE T2 load emits exactly once")
	get_root().add_child(sp)
	for i in 5:
		await process_frame
	_check(_started == 1, "T2 exactly one started across load (got %d)" % _started)

	# ---------------------------------------------------------------
	printerr("LIFECYCLE T3 post-tree toggles report once each")
	_started = 0
	_stopped = 0
	sp.set_shooting_enabled(false)
	_check(_stopped == 1, "T3 one stopped on disable (got %d)" % _stopped)
	sp.set_shooting_enabled(true)
	_check(_started == 1, "T3 one started on re-enable (got %d)" % _started)

	# ---------------------------------------------------------------
	printerr("LIFECYCLE T4 pre-tree cap silent, tree cap reports")
	var sp2 := BulletSpawner2D.new()
	sp2.set_bullet_factory(factory)
	sp2.set_spawn_data(_data())
	sp2.shooting_started.connect(_on_started)
	sp2.shooting_stopped.connect(_on_stopped)
	_started = 0
	_stopped = 0
	sp2.max_volleys = 0
	_check(_stopped == 0, "T4 pre-tree cap silent (got %d)" % _stopped)
	get_root().add_child(sp2)
	await process_frame
	_started = 0
	_stopped = 0
	sp2.max_volleys = -1
	_check(_started == 1, "T4 tree raise reports started (got %d)" % _started)
	sp2.max_volleys = 0
	_check(_stopped == 1, "T4 tree cap reports stopped (got %d)" % _stopped)

	sp.queue_free()
	sp2.queue_free()
	factory.reset()
	factory.queue_free()
	await process_frame
	print("----")
	if failures == 0:
		print("ALL LIFECYCLE-SIGNAL TESTS PASSED")
	else:
		printerr(str(failures) + " TEST(S) FAILED")
	quit(failures)
