extends BlastTest
## BulletSpawner2D movement along a Path2D: timing (duration / speed),
## easing parity with Tween, ONCE / LOOP / PING_PONG with leg counts,
## reverse direction, ATTACH vs RELATIVE_TO_START, rotate-with-path,
## start ratio / delay / endpoint pause, progress Curve, playback API,
## signals (exact counts), shooting while moving (pattern + preview follow,
## inherited velocity), hostile inputs and re-entrant frees.
## Time: the runner uses --fixed-fps 60, so one idle frame = 1/60 s exactly.

const FPS := 60.0

var path: Path2D


## Straight path from `a` to `b` (Path2D at the origin).
func _line_path(a: Vector2, b: Vector2) -> Path2D:
	var p := Path2D.new()
	var c := Curve2D.new()
	c.add_point(a)
	c.add_point(b)
	p.curve = c
	add(p)
	return p


func _mover(p: Path2D, configure: Callable = func(_s): pass) -> BulletSpawner2D:
	var sp := make_spawner(H.make_directional_data(4, 200.0, 2.0), BulletSpawner2D.PATTERN_FROM_HELPER_RING, 8)
	sp.movement_transition = 0 # linear unless a test says otherwise
	sp.movement_ease = 0
	sp.movement_duration_sec = 1.0
	sp.set_movement_path_node(p)
	configure.call(sp)
	sp.movement_enabled = true # autostart: plays now (already in the tree)
	return sp


func before_each() -> void:
	await super()
	path = _line_path(Vector2(0, 0), Vector2(300, 0))


func test_once_linear_duration_and_signals() -> void:
	var sp := _mover(path, func(s): watch_signals(s))
	assert_true(sp.is_movement_playing(), "autostart plays")
	assert_almost_eq(sp.global_position, Vector2(0, 0), Vector2(0.5, 0.5), "starts at the path start")
	await idle(30) # 0.5 s of a 1 s leg
	assert_almost_eq(sp.global_position.x, 150.0, 6.0, "linear: half the path at half time")
	assert_almost_eq(sp.get_movement_velocity().x, 300.0, 15.0, "velocity = path length / duration")
	await idle(40)
	assert_almost_eq(sp.global_position, Vector2(300, 0), Vector2(0.5, 0.5), "ends exactly at the path end")
	assert_false(sp.is_movement_playing(), "ONCE stops")
	assert_signal_emit_count(sp, "movement_started", 1)
	assert_signal_emit_count(sp, "movement_endpoint_reached", 1)
	assert_signal_emitted_with_parameters(sp, "movement_endpoint_reached", [true])
	assert_signal_emit_count(sp, "movement_finished", 1)
	assert_signal_emit_count(sp, "movement_loop_completed", 0)
	assert_eq(sp.get_movement_velocity(), Vector2.ZERO, "velocity zero when stopped")


func test_eased_progress_matches_tween() -> void:
	var sp := _mover(path, func(s):
		s.movement_transition = Tween.TRANS_CUBIC
		s.movement_ease = Tween.EASE_IN_OUT)
	await idle(15) # t = 0.25
	var want: float = Tween.interpolate_value(0.0, 300.0, 15.0 / FPS, 1.0, Tween.TRANS_CUBIC, Tween.EASE_IN_OUT)
	assert_almost_eq(sp.global_position.x, want, 1.0, "cubic in-out position equals a Tween's")


func test_speed_timing() -> void:
	var sp := _mover(path, func(s):
		s.movement_timing = BulletSpawner2D.MOVEMENT_TIMING_SPEED
		s.movement_speed = 150.0) # 300 px / 150 = 2 s
	await idle(60)
	assert_almost_eq(sp.global_position.x, 150.0, 6.0, "speed timing: 150 px after 1 s")
	await idle(70)
	assert_false(sp.is_movement_playing(), "done after 2 s")


func test_loop_counts_legs() -> void:
	var sp := _mover(path, func(s):
		watch_signals(s)
		s.movement_loop_mode = BulletSpawner2D.MOVEMENT_LOOP_LOOP
		s.movement_loops = 3)
	await idle(30)
	assert_almost_eq(sp.global_position.x, 150.0, 6.0, "first leg mid")
	await idle(45) # 1.25 s: second leg, a quarter in (jumped back to start)
	assert_almost_eq(sp.global_position.x, 75.0, 8.0, "LOOP restarts from the beginning")
	await idle(130)
	assert_false(sp.is_movement_playing(), "3 legs then finished")
	assert_eq(sp.get_movement_leg(), 3, "3 legs completed")
	assert_signal_emit_count(sp, "movement_loop_completed", 2, "loop_completed between legs only")
	assert_eq(get_signal_parameters(sp, "movement_loop_completed", 0), [0])
	assert_eq(get_signal_parameters(sp, "movement_loop_completed", 1), [1])
	assert_signal_emit_count(sp, "movement_finished", 1)


func test_ping_pong_turns_around() -> void:
	var sp := _mover(path, func(s):
		watch_signals(s)
		s.movement_loop_mode = BulletSpawner2D.MOVEMENT_LOOP_PING_PONG)
	await idle(90) # 1.5 s: halfway back
	assert_almost_eq(sp.global_position.x, 150.0, 6.0, "halfway back")
	assert_lt(sp.get_movement_velocity().x, 0.0, "moving backwards")
	await idle(60) # 2.5 s: halfway out again
	assert_gt(sp.get_movement_velocity().x, 0.0, "forwards again")
	assert_true(sp.is_movement_playing(), "infinite ping-pong keeps playing")
	assert_eq(get_signal_parameters(sp, "movement_endpoint_reached", 0), [true], "first endpoint = end")
	assert_eq(get_signal_parameters(sp, "movement_endpoint_reached", 1), [false], "second endpoint = start")


func test_reverse_direction_starts_at_end() -> void:
	var sp := _mover(path, func(s): s.movement_direction = BulletSpawner2D.MOVEMENT_DIRECTION_REVERSE)
	assert_almost_eq(sp.global_position, Vector2(300, 0), Vector2(0.5, 0.5), "reverse starts at the path end")
	await idle(30)
	assert_almost_eq(sp.global_position.x, 150.0, 6.0, "and travels to the start")


func test_relative_to_start_replays_the_shape() -> void:
	var sp := make_spawner()
	sp.global_position = Vector2(1000, 500)
	sp.movement_transition = 0
	sp.movement_ease = 0
	sp.movement_duration_sec = 1.0
	sp.movement_space = BulletSpawner2D.MOVEMENT_SPACE_RELATIVE_TO_START
	sp.set_movement_path_node(path)
	sp.movement_enabled = true
	await idle(30)
	assert_almost_eq(sp.global_position, Vector2(1150, 500), Vector2(6, 0.5), "path shape replayed from the spawner's start")


func test_rotate_with_path_faces_travel() -> void:
	var down := _line_path(Vector2(0, 0), Vector2(0, 300))
	var sp := _mover(down, func(s):
		s.movement_rotate_with_path = true
		s.movement_rotation_offset_deg = 10.0
		s.movement_loop_mode = BulletSpawner2D.MOVEMENT_LOOP_PING_PONG)
	await idle(20)
	assert_almost_eq(angle_difference(sp.global_rotation, PI / 2.0 + deg_to_rad(10.0)), 0.0, 0.01, "faces +Y (+offset) going out")
	await idle(60)
	assert_almost_eq(angle_difference(sp.global_rotation, -PI / 2.0 + deg_to_rad(10.0)), 0.0, 0.01, "turns around coming back")


func test_start_ratio_delay_and_endpoint_pause() -> void:
	var sp := _mover(path, func(s):
		s.movement_start_ratio = 0.5
		s.movement_start_delay_sec = 0.5
		s.movement_endpoint_pause_sec = 0.5
		s.movement_loop_mode = BulletSpawner2D.MOVEMENT_LOOP_PING_PONG)
	assert_almost_eq(sp.global_position.x, 150.0, 1.0, "start ratio: begins halfway")
	await idle(25)
	assert_almost_eq(sp.global_position.x, 150.0, 1.0, "start delay: still waiting")
	# Timeline: delay ends at frame 30, the remaining half leg ends at 60,
	# the endpoint pause holds until 90. Sample at 70.
	await idle(45)
	assert_almost_eq(sp.global_position.x, 300.0, 1.0, "endpoint pause holds the end")
	await idle(30)
	assert_lt(sp.global_position.x, 300.0, "after the pause it heads back")


func test_progress_curve_overrides_easing() -> void:
	var c := Curve.new()
	c.add_point(Vector2(0, 0))
	c.add_point(Vector2(0.5, 1.0)) # reach the end at half time, then stay
	c.add_point(Vector2(1, 1))
	var sp := _mover(path, func(s): s.movement_progress_curve = c)
	await idle(31)
	assert_almost_eq(sp.global_position.x, 300.0, 15.0, "curve drives progress, not the transition")
	assert_false(is_editor_visible(sp, &"movement_transition"), "transition hidden while a curve is set")


func test_playback_api() -> void:
	var sp := _mover(path)
	await idle(15)
	sp.movement_pause()
	var held := sp.global_position
	await idle(20)
	assert_eq(sp.global_position, held, "pause holds")
	sp.movement_play()
	await idle(5)
	assert_gt(sp.global_position.x, held.x, "play resumes from where it paused")
	sp.movement_seek(0.8)
	assert_almost_eq(sp.global_position.x, 240.0, 1.0, "seek jumps to 80%")
	sp.movement_reverse()
	await idle(6)
	assert_lt(sp.global_position.x, 240.0, "reverse keeps the spot and turns back")
	sp.movement_stop()
	assert_almost_eq(sp.global_position.x, 0.0, 0.5, "stop(reset) snaps to the start")
	assert_false(sp.is_movement_playing())
	sp.movement_seek(1.5)
	expect_error_sequence(["movement_seek: ratio must be in [0, 1]"])


func test_setters_reject_and_keep() -> void:
	var sp := make_spawner()
	var cases := [
		["movement_space", 5, "movement_space must be 0 (Attach) or 1 (Relative To Start)"],
		["movement_loop_mode", -1, "movement_loop_mode must be 0 (Once), 1 (Loop) or 2 (Ping Pong)"],
		["movement_direction", 2, "movement_direction must be 0 (Forward) or 1 (Reverse)"],
		["movement_loops", -1, "movement_loops must be >= 0"],
		["movement_timing", 9, "movement_timing must be 0 (Duration) or 1 (Speed)"],
		["movement_duration_sec", 0.0, "movement_duration_sec must be finite and > 0"],
		["movement_duration_sec", NAN, "movement_duration_sec must be finite and > 0"],
		["movement_speed", -5.0, "movement_speed must be finite and > 0"],
		["movement_transition", 12, "movement_transition must be a Tween.TransitionType"],
		["movement_ease", 4, "movement_ease must be a Tween.EaseType"],
		["movement_start_ratio", 1.5, "movement_start_ratio must be in [0, 1]"],
		["movement_start_delay_sec", -1.0, "movement_start_delay_sec must be finite and >= 0"],
		["movement_endpoint_pause_sec", INF, "movement_endpoint_pause_sec must be finite and >= 0"],
		["movement_rotation_offset_deg", NAN, "movement_rotation_offset_deg must be finite"],
		["movement_velocity_inherit_factor", INF, "movement_velocity_inherit_factor must be finite"],
	]
	for case in cases:
		var before: Variant = sp.get(case[0])
		sp.set(case[0], case[1])
		expect_error_sequence([case[2]], "%s = %s" % [case[0], case[1]])
		assert_true(sp.get(case[0]) == before or (typeof(before) == TYPE_FLOAT and is_nan(before) and is_nan(sp.get(case[0]))), "%s kept its old value" % case[0])


func test_unusable_paths_never_crash() -> void:
	# No path, an empty curve, and a path freed mid-movement: the spawner
	# stays put, warns ONCE, never errors or NaNs.
	var sp := make_spawner()
	sp.global_position = Vector2(50, 60)
	sp.movement_enabled = true
	await idle(5)
	assert_eq(sp.global_position, Vector2(50, 60), "no path: stays where it is")
	var empty := Path2D.new()
	empty.curve = Curve2D.new()
	add(empty)
	sp.set_movement_path_node(empty)
	sp.movement_play()
	await idle(5)
	assert_eq(sp.global_position, Vector2(50, 60), "empty curve: stays")
	sp.set_movement_path_node(path)
	sp.movement_stop()
	sp.movement_play()
	await idle(10)
	path.free()
	await idle(10)
	assert_true(sp.global_position.is_finite(), "path freed mid-movement: finite pose")
	var warnings := 0
	for err in get_errors():
		if err.is_push_warning() and err.contains_text("movement_path is not a Path2D"):
			warnings += 1
	assert_between(warnings, 1, 3, "warned once per unusable path, not per frame")


func test_handlers_that_free_things() -> void:
	# (free() of the emitter itself is refused by Godot: an emitting object is
	# locked. queue_free() is the sanctioned way and must be clean.)
	var sp := _mover(path)
	sp.movement_endpoint_reached.connect(func(_at_end): sp.queue_free())
	await idle(80)
	assert_false(is_instance_valid(sp), "spawner queue_freed inside its own signal, no crash")
	# Freeing the PATH from a handler: the next frames see a freed node.
	var p2 := _line_path(Vector2(0, 0), Vector2(100, 0))
	var sp2 := _mover(p2, func(s): s.movement_loop_mode = BulletSpawner2D.MOVEMENT_LOOP_PING_PONG)
	sp2.movement_endpoint_reached.connect(func(_at_end): if is_instance_valid(p2): p2.free())
	await idle(90)
	assert_true(sp2.global_position.is_finite(), "path freed from a handler: still finite")
	var warned := 0
	for err in get_errors():
		if err.is_push_warning() and err.contains_text("movement_path is not a Path2D"):
			warned += 1
	assert_eq(warned, 1, "warned once")


func test_shooting_while_moving_follows_and_inherits() -> void:
	var sp := _mover(path, func(s):
		s.inherit_movement_velocity = true
		s.movement_velocity_inherit_factor = 0.5)
	watch_signals(sp)
	await idle(20)
	assert_true(sp.shoot_once(), "fires while moving")
	var v: DirectionalBullets2D = get_signal_parameters(sp, "volley_fired", 0)[0]
	assert_almost_eq(v.get_inherited_velocity_offset(), sp.get_movement_velocity() * 0.5, Vector2(1, 1), "volley inherits half the spawner velocity")
	var expected: Array = sp.collect_spawn_transforms()
	assert_almost_eq(v.get_bullet_global_transform(0).origin, (expected[0] as Transform2D).origin, Vector2(0.5, 0.5), "pattern centered on the moving spawner")


func test_moving_spawner_hits_the_bake_and_never_rebuilds_preview() -> void:
	var sp := _mover(path, func(s):
		s.show_pattern_preview = true
		s.show_preview_during_runtime = true
		s.helper_bullets_amount = 400)
	await idle(4)
	var r0: int = sp.debug_get_preview_stats()["rebuilds"]
	var hits0: int = sp.debug_get_pattern_cache_info()["hits"]
	for i in 20:
		await idle(1)
		sp.shoot_once()
	assert_eq(int(sp.debug_get_preview_stats()["rebuilds"]), r0, "moving spawner: zero preview rebuilds")
	assert_gte(int(sp.debug_get_pattern_cache_info()["hits"]) - hits0, 19, "moving shots served from the bake (verifier on)")


func test_process_sleeps_when_movement_ends() -> void:
	var sp := _mover(path)
	assert_true(sp.is_processing(), "movement keeps _process awake")
	await idle(70)
	assert_false(sp.is_movement_playing())
	assert_false(sp.is_processing(), "nothing left to do: _process sleeps")
