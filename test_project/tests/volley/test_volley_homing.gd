extends BlastTest
## Homing deques (per-bullet + shared): push/check/clear, target types, real
## steering convergence, a freed target never crashes, the reached signal
## fires near the target, gating setters reject bad values, per-bullet
## smoothing fan.

var target: Node2D
var _reached: Array = []


func _on_reached(_volley, idx: int, _target: Object, _pos: Vector2) -> void:
	_reached.append(idx)


func before_each() -> void:
	await super()
	_reached.clear()
	target = add(Node2D.new())
	target.position = Vector2(500, 0)


func test_deque_push_check_clear() -> void:
	var v: BulletVolley2D = quick_volley(2, 250.0)
	v.bullet_homing_push_back_node2d_target(0, target)
	assert_true(v.bullet_check_has_homing_targets(0), "per-bullet has targets")
	assert_eq(v.bullet_homing_check_targets_amount(0), 1)
	assert_false(v.bullet_check_has_homing_targets(1), "sibling empty")
	v.all_bullets_push_back_homing_target(target)
	assert_eq(v.bullet_homing_check_targets_amount(1), 1, "all_bullets push fans out")
	v.shared_homing_deque_push_back_node2d_target(target)
	assert_true(v.shared_homing_deque_check_has_homing_targets(), "shared deque has targets")
	assert_eq(v.shared_homing_deque_check_homing_targets_amount(), 1, "shared amount counts pushes")
	v.bullet_clear_homing_targets(0)
	assert_false(v.bullet_check_has_homing_targets(0), "per-bullet clear")
	v.shared_homing_deque_clear_homing_targets()
	assert_false(v.shared_homing_deque_check_has_homing_targets(), "shared clear")


func test_target_types() -> void:
	var v: BulletVolley2D = quick_volley(2, 250.0)
	v.bullet_homing_push_back_global_position_target(0, Vector2(400, 100))
	assert_true(v.bullet_check_has_homing_targets(0), "global position target accepted")
	v.bullet_homing_push_back_mouse_position_target(1)
	assert_true(v.bullet_check_has_homing_targets(1), "mouse target accepted")
	assert_eq(v.bullet_homing_check_current_target_type(1), 3, "mouse target type")
	v.all_bullets_clear_homing_targets()


func test_steering_converges() -> void:
	var v: BulletVolley2D = quick_volley(2, 250.0)
	v.set_homing_smoothing(5.0)
	v.set_homing_take_control_of_texture_rotation(true)
	v.all_bullets_push_back_homing_target(target)
	var to_target := func() -> float:
		return absf(v.get_bullet_direction(0).angle_to((target.global_position - v.get_bullet_global_transform(0).origin).normalized()))
	var before: float = to_target.call()
	await physics(20)
	assert_lte(to_target.call(), before + 0.05, "direction converged toward the target")


func test_freed_target_survives() -> void:
	var doomed: Node2D = add(Node2D.new())
	doomed.position = Vector2(300, 300)
	var w: BulletVolley2D = quick_volley(1, 200.0)
	w.set_homing_smoothing(5.0)
	w.set_homing_take_control_of_texture_rotation(true)
	w.bullet_homing_push_back_node2d_target(0, doomed)
	doomed.queue_free()
	await physics(10)
	assert_true(w.get_bullet_transform(0).is_finite(), "volley alive after the target was freed")
	assert_false(w.bullet_check_has_homing_targets(0), "freed target trimmed from the queue")


func test_reached_signal_and_gating() -> void:
	var s: BulletVolley2D = quick_volley(1, 400.0)
	s.set_homing_smoothing(8.0)
	s.set_homing_take_control_of_texture_rotation(true)
	s.set_homing_distance_before_reached(30.0)
	s.bullet_homing_target_reached.connect(_on_reached)
	s.bullet_homing_push_back_node2d_target(0, target)
	for i in 120:
		await physics()
		if _reached.size() >= 1:
			break
	assert_eq(_reached, [0], "reached signal emitted once for bullet 0")
	s.set_homing_delay_sec(-1.0)
	expect_any_error()
	assert_eq(s.get_homing_delay_sec(), 0.0, "negative delay rejected")


func test_per_bullet_smoothing_fan() -> void:
	var f: BulletVolley2D = quick_volley(3, 200.0)
	f.bullet_set_homing_smoothing(0, 2.0)
	f.bullet_set_homing_smoothing(1, 4.0)
	f.bullet_set_homing_smoothing(2, 6.0)
	assert_almost_eq(f.bullet_get_homing_smoothing(2), 6.0, 0.01, "per-bullet smoothing stored")
	f.all_bullets_set_homing_smoothing(3.0)
	assert_almost_eq(f.bullet_get_homing_smoothing(0), 3.0, 0.01, "all_bullets smoothing fans out")
