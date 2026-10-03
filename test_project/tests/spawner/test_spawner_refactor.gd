extends BlastTest
## Refactor proofs for bullet_spawner2d.cpp internals: keep-awake predicate,
## cached-node validation, shared aim resolvers, pattern-source table lock,
## and setter reject-and-keep on the outline knobs.
## Adapted from the SceneTree original: spawners go through add()/make_spawner
## (autofreed), frames via idle(), rejections assert fail-loud.
const EXPECTED_PATTERN_HINT := "From Children:0,From Self:1,Path2D:29,Aimed:7,Custom:24,Circle:25,Square:27,Rectangle:26,Triangle:30,Diamond:32,Trapezoid:31,Polygon:28,Ellipse:9,Ring:3,Star:15,Heart:16,Star Polygon:12,Flower:8,Rose:20,Lissajous:23,Line:6,Grid:2,Lattice:19,Rain:10,Waterfall:18,Wave:17,Fan:4,Corridor:22,Spiral:5,Multi Spiral:13,Counter Spiral:21,Cross:14,Scatter:11"

func _approx(a: float, b: float, eps: float = 0.0001) -> bool:
	return absf(a - b) <= eps

func _fresh() -> BulletSpawner2D:
	var s := BulletSpawner2D.new()
	s.name = "Spawner"
	add(s)
	return s


func test_keep_awake() -> void:
	# Wired pre-tree (pre-tree setters stay silent; load emits one started),
	# then every keep-awake toggle must flip the process loop. The shooting
	# transition signals track auto-shooting only: subsystem wakes stay
	# signal-silent (set_spin_enabled/burst/retarget/preview never emit).
	var s := BulletSpawner2D.new()
	s.set_bullet_factory(factory)
	s.set_spawn_data(H.make_volley_data(1, 50.0, 2.0))
	watch_signals(s)
	add(s)
	await idle(1)
	assert_true(s.is_processing(), "fresh spawner processes (auto shooting)")
	assert_signal_emit_count(s, "shooting_started", 1, "load emits one started")
	assert_signal_emit_count(s, "shooting_stopped", 0, "nothing stopped yet")
	s.set_shooting_enabled(false)
	assert_true(not s.is_processing(), "idle spawner sleeps")
	assert_signal_emit_count(s, "shooting_stopped", 1, "disable reports stopped")
	s.set_spin_enabled(true)
	assert_true(s.is_processing(), "spin wakes")
	assert_signal_emit_count(s, "shooting_started", 1, "spin wake is signal-silent")
	s.set_spin_enabled(false)
	assert_true(not s.is_processing(), "spin off sleeps again")
	assert_signal_emit_count(s, "shooting_stopped", 1, "spin off is signal-silent")
	# Burst pending must survive stop_pattern_list (used to sleep: the set
	# spelled at that site forgot burst_shots_left).
	s.set_burst_enabled(true)
	s.set_burst_count(3)
	assert_signal_emit_count(s, "shooting_started", 1, "armed-but-idle burst stays silent")
	s.begin_burst()
	assert_true(s.is_processing(), "burst chain processes")
	assert_signal_emit_count(s, "shooting_started", 1, "pending burst is signal-silent")
	s.stop_pattern_list()
	assert_true(s.is_processing(), "stop_pattern_list keeps pending burst awake")
	assert_signal_emit_count(s, "shooting_started", 1, "surviving burst emits nothing new")
	s.set_burst_enabled(false)
	await idle(1)
	await idle(1)
	assert_true(not s.is_processing(), "cleared burst self-sleeps")
	assert_signal_emit_count(s, "shooting_stopped", 1, "cleared burst is signal-silent")
	# Interval retargeting owns the loop while armed.
	s.set_homing_enabled(true)
	s.set_homing_retarget_mode(1)
	assert_true(s.is_processing(), "retarget interval wakes")
	assert_signal_emit_count(s, "shooting_started", 1, "retarget arm is signal-silent")
	s.set_homing_retarget_mode(0)
	assert_true(not s.is_processing(), "retarget off sleeps again")
	assert_signal_emit_count(s, "shooting_stopped", 1, "retarget off is signal-silent")
	s.set_homing_enabled(false)
	assert_signal_emit_count(s, "shooting_started", 1, "already-idle homing off stays silent")
	# Runtime preview piggy-backs the loop.
	s.set_show_preview_during_runtime(true)
	assert_true(s.is_processing(), "runtime preview wakes")
	assert_signal_emit_count(s, "shooting_started", 1, "preview wake is signal-silent")
	s.set_show_preview_during_runtime(false)
	assert_true(not s.is_processing(), "runtime preview off sleeps again")
	assert_signal_emit_count(s, "shooting_stopped", 1, "preview off is signal-silent")

func test_freed_nodes() -> void:
	var s := _fresh()
	s.set_shooting_enabled(false)
	var local_factory := BulletFactory2D.new()
	local_factory.name = "Factory"
	add(local_factory)
	var marker := Node2D.new()
	marker.name = "Marker"
	s.add_child(marker)
	var target := Node2D.new()
	target.name = "Target"
	add(target)
	s.set_bullet_factory(local_factory)
	s.set_transforms_generator(marker)
	s.set_helper_aimed_target(target)
	s.set_helper_path2d_node(target)
	assert_true(s.get_bullet_factory() == local_factory, "factory resolves")
	assert_true(s.get_transforms_generator() == marker, "generator resolves")
	assert_true(s.get_helper_aimed_target() == target, "aimed target resolves")
	assert_true(s.get_helper_path2d_node() == target, "path2d node resolves")
	local_factory.free()
	assert_true(s.get_bullet_factory() == null, "freed factory reports null")
	marker.free()
	# Generator falls back to the spawner itself, never null/dangling.
	assert_true(s.get_transforms_generator() == null, "freed generator reports null")
	assert_true(s.get_effective_generator() == s, "effective generator falls back to self")
	target.free()
	assert_true(s.get_helper_aimed_target() == null, "freed aimed target reports null")
	assert_true(s.get_helper_path2d_node() == null, "freed path2d node reports null")

func test_corridor_door() -> void:
	var s := _fresh()
	s.set_shooting_enabled(false)
	# PatternSource ids: fan=4, aimed=7, corridor=22 (see hint string).
	s.set_pattern_source(22)
	s.set_helper_bullets_amount(25)
	# With a live target straight down: wall runs flank the door, nothing
	# inside it, nothing outside the wall.
	var target := Node2D.new()
	target.name = "Target"
	add(target)
	target.global_position = Vector2(0, 500)
	s.global_position = Vector2.ZERO
	s.set_helper_aimed_target(target)
	var volley: Array = s.collect_spawn_transforms()
	# The dodge door eats slots: fewer than requested, but non-empty.
	assert_eq(volley.size(), 25, "corridor places all 25 on the walls, door stays clear")
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
	assert_true(door_clean, "corridor door empty (gap %.0f)" % (half_gap * 2.0))
	assert_true(walls_ok, "corridor inside wall bounds (width %.0f)" % (half_width * 2.0))
	# Without a target the static fallback direction draws the same shape.
	s.set_helper_aimed_target(null)
	s.set_helper_corridor_aim_direction(Vector2(0, 1))
	var fallback: Array = s.collect_spawn_transforms()
	assert_true(fallback.size() == volley.size(), "corridor fallback matches (%d)" % fallback.size())
	var door_clean2 := true
	for t in fallback:
		if absf((t as Transform2D).origin.x - s.global_position.x) < half_gap - 1.0:
			door_clean2 = false
	assert_true(door_clean2, "corridor fallback door empty")
	target.free()

func test_cone_parity() -> void:
	# Fan aimed down must equal an aimed volley at a target straight down:
	# same cone builder on both paths (facings agree, origins stacked).
	var s := _fresh()
	s.set_shooting_enabled(false)
	s.global_position = Vector2.ZERO
	s.set_helper_bullets_amount(7)
	var target := Node2D.new()
	target.name = "Target"
	add(target)
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
	assert_true(fan.size() == 7 and aimed.size() == 7, "cone volleys emit 7+7")
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
	assert_true(worst < 0.02, "fan-vs-aimed cone facings agree (worst %.4f)" % worst)
	target.free()

func test_pattern_hint() -> void:
	var s := _fresh()
	s.set_shooting_enabled(false)
	var hint := ""
	for p in s.get_property_list():
		if p.get("name") == "pattern_source":
			hint = str(p.get("hint_string", ""))
	assert_true(hint == EXPECTED_PATTERN_HINT, "pattern hint locked")

func test_outline_setters() -> void:
	# Reject-and-keep on every outline knob touched by the refactors.
	var s := _fresh()
	s.set_shooting_enabled(false)
	s.set_helper_outline_distribution(1)
	s.set_helper_outline_distribution(99)
	expect_error_sequence(["helper_outline_distribution must be 0 (legacy) or 1 (symmetric)"])
	assert_true(s.get_helper_outline_distribution() == 1, "distribution rejects 99")
	s.set_helper_outline_layer_layout(1)
	s.set_helper_outline_layer_layout(-1)
	expect_error_sequence(["helper_outline_layer_layout must be 0 (shared loop) or 1 (even per layer)"])
	assert_true(s.get_helper_outline_layer_layout() == 1, "layout rejects -1")
	s.set_helper_outline_corner_priority(0)
	s.set_helper_outline_corner_priority(7)
	expect_error_sequence(["helper_outline_corner_priority must be 0 (horizontal), 1 (vertical) or 2 (balanced)"])
	assert_true(s.get_helper_outline_corner_priority() == 0, "priority rejects 7")
	s.set_helper_outline_corner_mode(0)
	s.set_helper_outline_corner_mode(5)
	expect_error_sequence(["helper_outline_corner_mode must be 0 (pin corners) or 1 (even arc)"])
	assert_true(s.get_helper_outline_corner_mode() == 0, "corner mode rejects 5")
	s.set_helper_outline_edge_margin(3.0)
	s.set_helper_outline_edge_margin(-2.0)
	expect_error_sequence(["helper_outline_edge_margin must be finite and >= 0"])
	assert_true(_approx(s.get_helper_outline_edge_margin(), 3.0), "margin rejects -2")
	s.set_helper_outline_corner_facing(2)
	s.set_helper_outline_corner_facing(9)
	expect_error_sequence(["helper_outline_corner_facing must be 0 (side), 1 (miter) or 2 (smooth)"])
	assert_true(s.get_helper_outline_corner_facing() == 2, "facing rejects 9")
