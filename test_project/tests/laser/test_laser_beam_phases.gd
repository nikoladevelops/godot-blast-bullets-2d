extends BlastTest
## Laser phases: CHARGING (warning line, no hits) -> GROWING (width 0 ->
## full) -> ACTIVE (active_sec, 0 = until ended) -> FADING (width full -> 0)
## -> finished (laser_finished, then pooled). Zero-length phases pass in the
## same tick, one laser_phase_changed per phase entered (factory lasers signal
## on the factory). end_laser fades or finishes at once; set_active_time_left
## moves the end; the factory clock (time scale, pause) drives every phase.

const DT := 0.0625 # exact binary ticks: phases end exactly on their tick

var log: Array = [] # [tick, signal, payload...]
var tick := 0


func before_each() -> void:
	await super()
	log.clear()
	tick = 0
	factory.laser_phase_changed.connect(func(l: LaserVolley2D, p: int) -> void: log.append([tick, "phase", p]))
	factory.laser_finished.connect(func(l: LaserVolley2D) -> void: log.append([tick, "finished"]))


func _step(n: int) -> void:
	for i in n:
		tick += 1
		step_factory(1, DT)


func _fire(data: LaserData2D, beams := 1) -> LaserVolley2D:
	var t: Array[Transform2D] = []
	for i in beams:
		t.append(Transform2D(TAU * i / beams, Vector2(100, 100)))
	var l := factory.spawn_laser(data, t)
	assert_not_null(l, "fired")
	return l


func test_the_api_is_bound() -> void:
	for m in ["spawn_laser", "get_active_lasers", "get_active_laser_count", "clear_active_lasers", "debug_get_laser_pool_amount"]:
		assert_true(factory.has_method(m), "BulletFactory2D.%s is bound" % m)
	for s in ["laser_phase_changed", "laser_pierce_started", "laser_pierce_ended", "laser_hit", "laser_wall_hit", "laser_finished"]:
		assert_true(factory.has_signal(s), "BulletFactory2D.%s exists" % s)
	assert_eq([LaserVolley2D.PHASE_CHARGING, LaserVolley2D.PHASE_GROWING, LaserVolley2D.PHASE_ACTIVE, LaserVolley2D.PHASE_FADING, LaserVolley2D.PHASE_FINISHED], [0, 1, 2, 3, 4], "serialized phase ids")


func test_phases_run_in_order_on_their_ticks() -> void:
	var l := _fire(H.make_laser(0.125, 0.0625, 0.25, 0.125))
	assert_eq(l.get_phase(), LaserVolley2D.PHASE_CHARGING, "charging from the start")
	_step(10)
	# charge 2 ticks, grow 1, active 4, fade 2.
	assert_eq(log, [[2, "phase", 1], [3, "phase", 2], [7, "phase", 3], [9, "phase", 4], [9, "finished"]], "one signal per phase, on its tick")
	assert_true(l.is_pooled(), "pooled after laser_finished")
	assert_eq(factory.get_active_laser_count(), 0, "nothing firing")
	assert_eq(factory.debug_get_laser_pool_amount(), 1, "one in the pool")


func test_zero_length_phases_pass_in_one_tick() -> void:
	var l := _fire(H.make_laser(0.0, 0.0, 0.125, 0.0))
	_step(1)
	assert_eq(log, [[1, "phase", 1], [1, "phase", 2]], "charging and growing pass on the first tick")
	assert_eq(l.get_phase(), LaserVolley2D.PHASE_ACTIVE, "active on its first tick")
	_step(2)
	# Active for its whole first tick (nothing came before it), so 0.125 s ends on tick 2.
	assert_eq(log.slice(2), [[2, "phase", 3], [2, "phase", 4], [2, "finished"]], "no fade: straight to finished")


func test_widths_follow_the_phase() -> void:
	var d := H.make_laser(0.125, 0.25, 0.0, 0.25)
	d.width = 16.0
	d.warning_width = 3.0
	var l := _fire(d)
	var widths: Array = []
	for i in 6:
		_step(1)
		widths.append(l.get_current_width())
	# Charging (2 ticks): the warning width. Growing (4 ticks): 0 -> 16.
	assert_eq(widths, [3.0, 0.0, 4.0, 8.0, 12.0, 16.0], "warning, then growing linearly")
	l.end_laser()
	_step(2)
	assert_eq(l.get_current_width(), 8.0, "fading: half way after two of its four ticks")


func test_active_zero_lasts_until_ended() -> void:
	var l := _fire(H.make_laser(0.0, 0.0, 0.0, 0.0625))
	_step(40)
	assert_eq(l.get_phase(), LaserVolley2D.PHASE_ACTIVE, "still firing after 2.5 s")
	assert_eq(l.get_active_time_left(), 0.0, "0 = until ended")
	l.end_laser()
	assert_eq(l.get_phase(), LaserVolley2D.PHASE_FADING, "fading at once")
	_step(1)
	assert_eq(log.slice(-3), [[41, "phase", 3], [41, "phase", 4], [41, "finished"]], "end_laser announced at the next tick, then gone after the fade")


func test_end_laser_without_fade_finishes_next_tick() -> void:
	var l := _fire(H.make_laser(0.0, 0.0, 0.0, 1.0))
	_step(1)
	l.end_laser(false)
	assert_eq(l.get_current_width(), 0.0, "drawn no more at once")
	_step(1)
	assert_eq(log.slice(-2), [[2, "phase", 4], [2, "finished"]], "finished on the next tick, no fade")
	assert_true(l.is_pooled(), "pooled")
	l.end_laser() # a pooled laser is a stale handle: refused, nothing happens
	expect_error_sequence(["end_laser: this laser finished (it waits in the pool), so this handle is stale. Fire a new laser instead."])


func test_set_active_time_left_moves_the_end() -> void:
	var l := _fire(H.make_laser(0.0, 0.0, 1.0, 0.0))
	_step(2)
	assert_almost_eq(l.get_active_time_left(), 0.875, 1e-9, "1 s minus two ticks")
	l.set_active_time_left(0.125)
	_step(2)
	assert_eq(log.back(), [4, "finished"], "ended 0.125 s later")
	var charged := _fire(H.make_laser(0.125, 0.0, 1.0, 0.0))
	charged.set_active_time_left(0.0625)
	assert_eq(charged.get_active_time_left(), 0.0625, "before ACTIVE: the length of ACTIVE")
	charged.set_active_time_left(NAN)
	expect_error_sequence(["set_active_time_left: seconds must be finite and >= 0 (0 = until ended), keeping the old value."])


func test_time_scale_and_pause_drive_the_phases() -> void:
	var l := _fire(H.make_laser(0.25, 0.0, 1.0, 0.0))
	factory.time_scale = 0.0
	_step(8)
	assert_eq(l.get_phase(), LaserVolley2D.PHASE_CHARGING, "time scale 0: frozen")
	factory.time_scale = 2.0
	_step(2)
	assert_eq(l.get_phase(), LaserVolley2D.PHASE_ACTIVE, "twice as fast: 0.25 s in two ticks")


func test_the_pool_reuses_a_laser_with_a_clean_life() -> void:
	var first := _fire(H.make_laser(0.0, 0.0, 0.0625, 0.0), 2)
	var anchor := Node2D.new()
	add(anchor)
	first.set_anchor(anchor)
	first.set_targets([Vector2(5, 5)])
	expect_warning_sequence(["LaserVolley2D: laser_data aims along the pattern, so a target steers nothing (kept for a Track or Lock On aim)."], "a pattern laser keeps the target for a later aim")
	_step(2)
	assert_true(first.is_pooled(), "finished into the pool")
	assert_eq(first.debug_check_invariants(), "", "a pooled laser holds nothing")
	assert_eq(first.get_beam_count(), 0, "no beams while pooled")
	var again := _fire(H.make_laser(0.0, 0.0, 1.0, 0.0), 2)
	assert_eq(again, first, "the same node, a new life")
	assert_gt(again.get_life_id(), 1, "a new life id")
	assert_null(again.get_anchor(), "no anchor from the last life")
	assert_eq(again.get_beam_end(0), Vector2(100, 100) + Vector2(400, 0), "a fresh beam from its own transform")
	assert_eq(again.get_owner_spawner(), null, "factory-owned")


func test_spawn_refusals() -> void:
	var t: Array[Transform2D] = [Transform2D(0.0, Vector2.ZERO)]
	assert_null(factory.spawn_laser(null, t), "null data")
	expect_error_sequence(["spawn_laser: data is null, nothing fired."])
	var none: Array[Transform2D] = []
	assert_null(factory.spawn_laser(H.make_laser(), none), "no transforms")
	expect_error_sequence(["spawn_laser: no transforms, nothing fired."])
	var bad: Array[Transform2D] = [Transform2D(Vector2.ZERO, Vector2.ZERO, Vector2.ZERO)]
	assert_null(factory.spawn_laser(H.make_laser(), bad), "singular transform")
	expect_error_sequence(["spawn_laser: every transform must be finite and invertible, nothing fired."])
	BulletPatterns2D.set_max_bullets_per_pattern(2)
	var many: Array[Transform2D] = [Transform2D(), Transform2D(), Transform2D()]
	assert_null(factory.spawn_laser(H.make_laser(), many), "over the limit")
	expect_error_sequence(["spawn_laser: 3 beams is more than BulletPatterns2D.max_bullets_per_pattern (2), nothing fired."])
	assert_eq(factory.get_active_laser_count(), 0, "every refusal fired nothing")


## end_laser from a live handler inside the tick: every phase it entered is
## announced exactly once, before laser_finished (the laser finishes in the
## same tick, so "the next tick" never comes).
func test_end_laser_from_a_live_handler_announces_every_phase(source: String = use_parameters(["hit", "wall", "phase"])) -> void:
	var d := H.make_laser(0.0, 0.0, 0.0, 0.25)
	match source:
		"hit":
			make_area(Vector2(200, 100), Vector2(20, 60))
		"wall":
			d.wall_mask = 16
			make_wall(Vector2(200, 100), Vector2(20, 200), 16)
	await idle(2)
	var l := _fire(d)
	var ended := [false]
	var end_now := func() -> void:
		if not ended[0]:
			ended[0] = true
			l.end_laser(false)
	match source:
		"hit":
			factory.laser_hit.connect(func(_t, _l, _b, _p) -> void: end_now.call())
		"wall":
			factory.laser_wall_hit.connect(func(_l, _b, _p, _n) -> void: end_now.call())
		"phase":
			factory.laser_phase_changed.connect(func(_l, p: int) -> void:
				if p == LaserVolley2D.PHASE_ACTIVE:
					end_now.call())
	_step(2)
	assert_true(ended[0], "the handler ran")
	var kinds: Array = []
	for e in log:
		kinds.append(e.slice(1))
	assert_eq(kinds.count(["phase", LaserVolley2D.PHASE_FINISHED]), 1, "FINISHED announced exactly once (%s)" % [log])
	assert_eq(kinds.back(), ["finished"], "laser_finished last (%s)" % [log])
	assert_lt(kinds.find(["phase", LaserVolley2D.PHASE_FINISHED]), kinds.find(["finished"]), "the phase before the finish")
	assert_true(l.is_pooled(), "pooled")


## end_laser(true) from a phase handler with no fade: FADING is announced
## before FINISHED.
func test_a_zero_fade_ended_in_a_phase_handler_announces_fading() -> void:
	var l := _fire(H.make_laser(0.0, 0.0, 1.0, 0.0))
	factory.laser_phase_changed.connect(func(_l, p: int) -> void:
		if p == LaserVolley2D.PHASE_ACTIVE:
			l.end_laser(true))
	_step(1)
	assert_eq(log, [[1, "phase", 1], [1, "phase", 2], [1, "phase", 3], [1, "phase", 4], [1, "finished"]], "every phase once, in order")


## A bolt spent in the tick a hit handler faded it, whose FADING handler then
## ends it at once: FINISHED is announced exactly once.
func test_a_spent_bolt_ended_from_its_fading_handler_finishes_once() -> void:
	var d := H.make_laser(0.0, 0.0, 0.0, 0.25, 40.0)
	d.type = LaserData2D.LASER_BOLT
	d.speed = 320.0
	d.pierce_limit = 1 # spent by its first hit
	make_area(Vector2(160, 100), Vector2(20, 60))
	await idle(2)
	var l := _fire(d)
	factory.laser_hit.connect(func(_t, laser, _b, _p) -> void: laser.end_laser(true))
	factory.laser_phase_changed.connect(func(laser, p: int) -> void:
		if p == LaserVolley2D.PHASE_FADING:
			laser.end_laser(false))
	for i in 20:
		_step(1)
		if l.is_pooled():
			break
	var kinds: Array = []
	for e in log:
		kinds.append(e.slice(1))
	assert_true(l.is_pooled(), "finished")
	assert_eq(kinds.count(["phase", LaserVolley2D.PHASE_FINISHED]), 1, "FINISHED announced once (%s)" % [log])
	assert_eq(kinds.count(["finished"]), 1, "one laser_finished")
