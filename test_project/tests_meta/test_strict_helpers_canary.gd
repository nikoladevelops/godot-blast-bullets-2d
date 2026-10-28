extends BlastTest
## Runner self-test only: every precise error helper MUST fail when the
## recorded errors do not match. Each test below is expected to FAIL; the
## runner's --self-test counts them. Never part of the normal run.


func test_sequence_missing_error_fails() -> void:
	expect_error_sequence(["this error never happened"])


func test_sequence_extra_error_fails() -> void:
	push_error("canary: first")
	push_error("canary: second")
	expect_error_sequence(["canary: first"])


func test_sequence_wrong_order_fails() -> void:
	push_error("canary: b")
	push_error("canary: a")
	expect_error_sequence(["canary: a", "canary: b"])


func test_containing_is_exact_by_default() -> void:
	push_error("canary: dup")
	push_error("canary: dup")
	expect_errors_containing("canary: dup", 1)


func test_no_errors_checkpoint_fails() -> void:
	push_error("canary: stray")
	expect_no_errors()


func test_unclaimed_warning_fails() -> void:
	# A plugin warning (a script push_warning is not classified as one by GUT).
	add(BulletFactory2D.new())
	var sp := BulletSpawner2D.new()
	sp.set_shooting_enabled(false)
	sp.spawn_data = H.make_volley_data(1, 0.0, 5.0)
	add(sp)
	sp.shoot_once() # an unwired spawner, two factories in its world: warns


func test_warning_of_a_late_tick_fails() -> void:
	# Bullets still flying at the end warn during after_each: still this test's.
	var v: BulletVolley2D = quick_volley(1, 0.0)
	v.set_homing_take_control_of_texture_rotation(false) # homing that cannot steer warns at its tick
	v.shared_homing_deque_push_back_global_position_target(Vector2(100, 0))
