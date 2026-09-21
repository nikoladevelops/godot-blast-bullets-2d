extends SceneTree

# Refactor proofs for bullet_spawner2d.cpp internals. Run with:
#   godot --headless --path test_project --script test_spawner_refactor.gd
# Exit code 0 = all checks pass, 1 = a failure printed below.
#
# Covers the maintainability refactors (behavior-preserving by contract):
#   1. keep-awake predicate: one canonical set, incl. the fixed
#      stop_pattern_list-must-not-sleep-a-pending-burst case
#   2. cached-node validation: freed factory/generator/target/path2d report
#      missing instead of dangling (unified validate_cached_node helper)
#   3. shared aim resolvers: corridor volley honors the door + wall bounds
#      with and without a live target; fan-vs-aimed cone parity
#   4. pattern-source table: inspector hint string locked byte-identical
#   5. setter reject-and-keep contract on the outline knobs
var failures := 0

const EXPECTED_PATTERN_HINT := "From Children:0,From Self:1,Path2D:29,Aimed:7,Custom:24,Circle:25,Square:27,Rectangle:26,Triangle:30,Diamond:32,Trapezoid:31,Polygon:28,Ellipse:9,Ring:3,Star:15,Heart:16,Star Polygon:12,Flower:8,Rose:20,Lissajous:23,Line:6,Grid:2,Lattice:19,Rain:10,Waterfall:18,Wave:17,Fan:4,Corridor:22,Spiral:5,Multi Spiral:13,Counter Spiral:21,Cross:14,Scatter:11"

func _check(cond: bool, label: String) -> void:
	if cond:
		print("PASS  ", label)
	else:
		failures += 1
		printerr("FAIL  ", label)

func _approx(a: float, b: float, eps: float = 0.0001) -> bool:
	return absf(a - b) <= eps

func _make_spawner() -> BulletSpawner2D:
	var s := BulletSpawner2D.new()
	s.name = "Spawner"
	get_root().add_child(s)
	return s

func _initialize() -> void:
	_test_keep_awake()
	await process_frame
	_test_freed_nodes()
	await process_frame
	_test_corridor_door()
	_test_cone_parity()
	_test_pattern_hint()
	_test_outline_setters()
	print("----")
	if failures == 0:
		print("ALL SPAWNER REFACTOR TESTS PASSED")
	else:
		printerr(str(failures) + " TEST(S) FAILED")
	quit(failures)

func _test_keep_awake() -> void:
	var s := _make_spawner()
	await process_frame
	_check(s.is_processing(), "fresh spawner processes (auto shooting)")
	s.set_shooting_enabled(false)
	_check(not s.is_processing(), "idle spawner sleeps")
	s.set_spin_enabled(true)
	_check(s.is_processing(), "spin wakes")
	s.set_spin_enabled(false)
	_check(not s.is_processing(), "spin off sleeps again")
	# Burst pending must survive stop_pattern_list (used to sleep: the set
	# spelled at that site forgot burst_shots_left).
	s.set_burst_enabled(true)
	s.set_burst_count(3)
	s.begin_burst()
	_check(s.is_processing(), "burst chain processes")
	s.stop_pattern_list()
	_check(s.is_processing(), "stop_pattern_list keeps pending burst awake")
	s.set_burst_enabled(false)
	await process_frame
	await process_frame
	_check(not s.is_processing(), "cleared burst self-sleeps")
	# Interval retargeting owns the loop while armed.
	s.set_homing_enabled(true)
	s.set_homing_retarget_mode(1)
	_check(s.is_processing(), "retarget interval wakes")
	s.set_homing_retarget_mode(0)
	_check(not s.is_processing(), "retarget off sleeps again")
	s.set_homing_enabled(false)
	# Runtime preview piggy-backs the loop.
	s.set_show_preview_during_runtime(true)
	_check(s.is_processing(), "runtime preview wakes")
	s.set_show_preview_during_runtime(false)
	_check(not s.is_processing(), "runtime preview off sleeps again")
	s.queue_free()

func _test_freed_nodes() -> void:
	var s := _make_spawner()
	var factory := BulletFactory2D.new()
	factory.name = "Factory"
	get_root().add_child(factory)
	var marker := Node2D.new()
	marker.name = "Marker"
	s.add_child(marker)
	var target := Node2D.new()
	target.name = "Target"
	get_root().add_child(target)
	s.set_bullet_factory(factory)
	s.set_transforms_generator(marker)
	s.set_helper_aimed_target(target)
	s.set_helper_path2d_node(target)
	_check(s.get_bullet_factory() == factory, "factory resolves")
	_check(s.get_transforms_generator() == marker, "generator resolves")
	_check(s.get_helper_aimed_target() == target, "aimed target resolves")
	_check(s.get_helper_path2d_node() == target, "path2d node resolves")
	factory.free()
	_check(s.get_bullet_factory() == null, "freed factory reports null")
	marker.free()
	# Generator falls back to the spawner itself, never null/dangling.
	_check(s.get_transforms_generator() == null, "freed generator reports null")
	_check(s.get_effective_generator() == s, "effective generator falls back to self")
	target.free()
	_check(s.get_helper_aimed_target() == null, "freed aimed target reports null")
	_check(s.get_helper_path2d_node() == null, "freed path2d node reports null")
	s.queue_free()

func _test_corridor_door() -> void:
	var s := _make_spawner()
	# PatternSource ids: fan=4, aimed=7, corridor=22 (see hint string).
	s.set_pattern_source(22)
	s.set_helper_bullets_amount(25)
	# With a live target straight down: wall runs flank the door, nothing
	# inside it, nothing outside the wall.
	var target := Node2D.new()
	target.name = "Target"
	get_root().add_child(target)
	target.global_position = Vector2(0, 500)
	s.global_position = Vector2.ZERO
	s.set_helper_aimed_target(target)
	var volley: Array = s.collect_spawn_transforms()
	# The dodge door eats slots: fewer than requested, but non-empty.
	_check(volley.size() > 0 and volley.size() < 25, "corridor carves door (%d/25)" % volley.size())
	var half_gap: float = s.get_helper_corridor_gap_width() * 0.5
	var half_width: float = s.get_helper_corridor_width() * 0.5
	var door_clean := true
	var walls_ok := true
	for t in volley:
		var x: float = absf((t as Transform2D).origin.x - s.global_position.x)
		if x < half_gap - 1.0:
			door_clean = false
		if x > half_width + 1.0:
			walls_ok = false
	_check(door_clean, "corridor door empty (gap %.0f)" % (half_gap * 2.0))
	_check(walls_ok, "corridor inside wall bounds (width %.0f)" % (half_width * 2.0))
	# Without a target the static fallback direction draws the same shape.
	s.set_helper_aimed_target(null)
	s.set_helper_corridor_aim_direction(Vector2(0, 1))
	var fallback: Array = s.collect_spawn_transforms()
	_check(fallback.size() == volley.size(), "corridor fallback matches (%d)" % fallback.size())
	var door_clean2 := true
	for t in fallback:
		if absf((t as Transform2D).origin.x - s.global_position.x) < half_gap - 1.0:
			door_clean2 = false
	_check(door_clean2, "corridor fallback door empty")
	target.free()
	s.queue_free()

func _test_cone_parity() -> void:
	# Fan aimed down must equal an aimed volley at a target straight down:
	# same cone builder on both paths (facings agree, origins stacked).
	var s := _make_spawner()
	s.global_position = Vector2.ZERO
	s.set_helper_bullets_amount(7)
	var target := Node2D.new()
	target.name = "Target"
	get_root().add_child(target)
	target.global_position = Vector2(0, 400)
	s.set_pattern_source(4)
	s.set_helper_fan_direction_angle(PI * 0.5)
	s.set_helper_fan_spread(0.6)
	s.set_helper_fan_centered(true)
	s.set_helper_fan_step_offset(0.0)
	var fan: Array = s.collect_spawn_transforms()
	s.set_pattern_source(7)
	s.set_helper_aimed_target(target)
	s.set_helper_aimed_spread(0.6)
	s.set_helper_aimed_centered(true)
	s.set_helper_aimed_step_offset(0.0)
	var aimed: Array = s.collect_spawn_transforms()
	_check(fan.size() == 7 and aimed.size() == 7, "cone volleys emit 7+7")
	var fa: Array = []
	var aa: Array = []
	for t in fan:
		fa.append(wrapf((t as Transform2D).get_rotation(), -PI, PI))
	for t in aimed:
		aa.append(wrapf((t as Transform2D).get_rotation(), -PI, PI))
	fa.sort()
	aa.sort()
	var worst := 0.0
	for i in 7:
		worst = maxf(worst, absf(wrapf(float(fa[i]) - float(aa[i]), -PI, PI)))
	_check(worst < 0.02, "fan-vs-aimed cone facings agree (worst %.4f)" % worst)
	target.free()
	s.queue_free()

func _test_pattern_hint() -> void:
	var s := _make_spawner()
	var hint := ""
	for p in s.get_property_list():
		if p.get("name") == "pattern_source":
			hint = str(p.get("hint_string", ""))
	_check(hint == EXPECTED_PATTERN_HINT, "pattern hint locked")
	s.queue_free()

func _test_outline_setters() -> void:
	# Reject-and-keep on every outline knob touched by the refactors.
	var s := _make_spawner()
	s.set_helper_outline_distribution(1)
	s.set_helper_outline_distribution(99)
	_check(s.get_helper_outline_distribution() == 1, "distribution rejects 99")
	s.set_helper_outline_layer_layout(1)
	s.set_helper_outline_layer_layout(-1)
	_check(s.get_helper_outline_layer_layout() == 1, "layout rejects -1")
	s.set_helper_outline_corner_priority(0)
	s.set_helper_outline_corner_priority(7)
	_check(s.get_helper_outline_corner_priority() == 0, "priority rejects 7")
	s.set_helper_outline_corner_mode(0)
	s.set_helper_outline_corner_mode(5)
	_check(s.get_helper_outline_corner_mode() == 0, "corner mode rejects 5")
	s.set_helper_outline_edge_margin(3.0)
	s.set_helper_outline_edge_margin(-2.0)
	_check(_approx(s.get_helper_outline_edge_margin(), 3.0), "margin rejects -2")
	s.set_helper_outline_corner_facing(2)
	s.set_helper_outline_corner_facing(9)
	_check(s.get_helper_outline_corner_facing() == 2, "facing rejects 9")
	s.queue_free()
