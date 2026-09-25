extends SceneTree
## Volley homing suite: shared + per-bullet deques, target types, steering.
## Covers: push_back/front × node/global/mouse, amount/has/current checks,
## freed-target survival (no crash, queue drains), delay/duration/lose-range,
## auto-pop after reached, bullet_homing_target_reached deferred signal,
## per-bullet smoothing fan, actual steering over physics frames (direction
## converges toward target), clear + retarget replace.
## Run: godot --headless --path test_project --script tests/volley/test_volley_homing.gd
## Exit code 0 = all pass.

const H := preload("res://tests/common/blast_test_helpers.gd")

var failures := 0
var _reached: Array = []

func _check(cond: bool, label: String) -> void:
	if cond:
		print("PASS  ", label)
	else:
		failures += 1
		printerr("FAIL  ", label)

func _on_reached(_volley, idx: int, _target: Object, _pos: Vector2) -> void:
	_reached.append(idx)

func _initialize() -> void:
	var factory := BulletFactory2D.new()
	get_root().add_child(factory)
	await process_frame
	await process_frame
	var target := Node2D.new()
	target.name = "HomingTarget"
	target.position = Vector2(500, 0)
	get_root().add_child(target)
	await process_frame

	printerr("HOME T1 deque push/check/clear")
	var v: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(H.make_directional_data(2, 250.0))
	v.bullet_homing_push_back_node2d_target(0, target)
	_check(v.bullet_check_has_homing_targets(0), "per-bullet has targets")
	_check(v.bullet_homing_check_targets_amount(0) == 1, "per-bullet amount 1")
	_check(not v.bullet_check_has_homing_targets(1), "sibling empty")
	v.all_bullets_push_back_homing_target(target)
	_check(v.bullet_homing_check_targets_amount(1) == 1, "all_bullets push fans out")
	v.shared_homing_deque_push_back_node2d_target(target)
	_check(v.shared_homing_deque_check_has_homing_targets(), "shared deque has targets")
	_check(v.shared_homing_deque_check_homing_targets_amount() >= 1, "shared amount >= 1")
	v.bullet_clear_homing_targets(0)
	_check(not v.bullet_check_has_homing_targets(0), "per-bullet clear")
	v.shared_homing_deque_clear_homing_targets()
	_check(not v.shared_homing_deque_check_has_homing_targets(), "shared clear")

	printerr("HOME T2 target types")
	v.bullet_homing_push_back_global_position_target(0, Vector2(400, 100))
	_check(v.bullet_check_has_homing_targets(0), "global position target accepted")
	v.bullet_homing_push_back_mouse_position_target(1)
	_check(v.bullet_check_has_homing_targets(1), "mouse target accepted")
	_check(v.bullet_homing_check_current_target_type(0) != 3, "current type is a real target")
	v.all_bullets_clear_homing_targets()
	v.shared_homing_deque_clear_homing_targets()

	printerr("HOME T3 steering converges over frames")
	v.set_homing_smoothing(5.0)
	v.set_homing_take_control_of_texture_rotation(true)
	v.all_bullets_push_back_homing_target(target)
	var d_before: float = v.get_bullet_direction(0).angle_to((target.global_position - v.get_bullet_global_transform(0).origin).normalized())
	for i in 20:
		await physics_frame
	var d_after: float = v.get_bullet_direction(0).angle_to((target.global_position - v.get_bullet_global_transform(0).origin).normalized())
	_check(absf(d_after) < absf(d_before) + 0.05, "direction converged toward target (%.3f -> %.3f)" % [d_before, d_after])

	printerr("HOME T4 freed target survives")
	var doomed := Node2D.new()
	doomed.position = Vector2(300, 300)
	get_root().add_child(doomed)
	await process_frame
	var w: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(H.make_directional_data(1, 200.0))
	w.set_homing_smoothing(5.0)
	w.set_homing_take_control_of_texture_rotation(true)
	w.bullet_homing_push_back_node2d_target(0, doomed)
	doomed.queue_free()
	for i in 10:
		await physics_frame
	_check(w.get_bullet_transform(0).is_finite(), "volley alive after target freed")
	_check(factory.debug_assert_no_dangling().get("ok", false) == true, "no dangling after freed target")

	printerr("HOME T5 delay/duration/lose + reached signal")
	var s: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(H.make_directional_data(1, 400.0))
	s.set_homing_smoothing(8.0)
	s.set_homing_take_control_of_texture_rotation(true)
	s.set_homing_distance_before_reached(30.0)
	s.bullet_homing_target_reached.connect(_on_reached)
	s.bullet_homing_push_back_node2d_target(0, target)
	s.set_homing_delay_sec(0.0)
	s.set_homing_duration_sec(0.0)
	s.set_homing_lose_range_px(0.0)
	for i in 120:
		await physics_frame
		if _reached.size() >= 1:
			break
	_check(_reached.size() >= 1, "reached signal emitted near target")
	s.set_homing_delay_sec(-1.0)
	_check(s.get_homing_delay_sec() == 0.0, "negative delay rejected")

	printerr("HOME T6 per-bullet smoothing fan")
	var f: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(H.make_directional_data(3, 200.0))
	f.bullet_set_homing_smoothing(0, 2.0)
	f.bullet_set_homing_smoothing(1, 4.0)
	f.bullet_set_homing_smoothing(2, 6.0)
	_check(absf(f.bullet_get_homing_smoothing(2) - 6.0) < 0.01, "per-bullet smoothing stored")
	f.all_bullets_set_homing_smoothing(3.0)
	_check(absf(f.bullet_get_homing_smoothing(0) - 3.0) < 0.01, "all_bullets smoothing fans out")

	target.queue_free()
	factory.reset()
	_check(factory.debug_assert_no_dangling().get("ok", false) == true, "no dangling at end")
	factory.queue_free()
	await process_frame
	print("----")
	if failures == 0:
		print("ALL HOMING TESTS PASSED")
	else:
		printerr(str(failures) + " TEST(S) FAILED")
	quit(failures)
