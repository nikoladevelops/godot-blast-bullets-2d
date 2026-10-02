class_name BlastTest
extends GutTest
## Base class for every BlastBullets2D GUT test file.
##
## Per test (before_each/after_each):
##   - a fresh BulletFactory2D is added under the test (`factory`), ready after
##     two idle frames;
##   - afterwards the factory must report no dangling entries, is reset at
##     idle (structural calls are refused inside physics frames), and the test
##     must leave zero new orphan nodes.
## GUT runs in strict mode: any push_error / engine error the test did not
## declare with expect_error()/expect_errors_containing() fails the test.
##
## Waiting rules (important):
##   - physics(n) resumes INSIDE a physics frame: structural factory calls
##     (reset/free_*/populate_*) are refused there. Call idle() first.
##   - idle(n) resumes on an idle frame (safe for structural calls).

const H := preload("res://tests/common/blast_test_helpers.gd")

var factory: BulletFactory2D
## Set false in a test (before it ends) to skip the automatic factory checks,
## e.g. when the test frees the factory itself.
var check_factory_after := true


func before_each() -> void:
	check_factory_after = true
	factory = BulletFactory2D.new()
	add_child_autofree(factory)
	await idle()


func after_each() -> void:
	await idle()
	if check_factory_after and is_instance_valid(factory) and factory.is_inside_tree():
		var state: Dictionary = factory.debug_assert_no_dangling()
		assert_true(state.get("ok", false), "factory has no dangling entries: %s" % state.get("error", ""))
		factory.reset()
		await idle()
	assert_no_new_orphans("test left orphan nodes behind")


## Resume on an idle (process) frame: safe for structural factory calls.
## Awaits the SceneTree signal directly: GUT's wait_process_frames(n) resumes
## after n+1 frames, which would skew every frame-counted assertion.
func idle(frames: int = 2) -> void:
	for i in frames:
		await get_tree().process_frame


## Resume inside a physics frame after exactly `frames` ticks (bullets moved,
## physics callbacks queued and drained). Same n+1 caveat as idle() for GUT's
## wait_physics_frames.
func physics(frames: int = 1) -> void:
	for i in frames:
		await get_tree().physics_frame


## Declares one expected error (push_error from GDScript or C++, case
## insensitive substring). Fails if it did not happen.
func expect_error(text: String, msg := "") -> void:
	assert_push_error(text, msg if msg != "" else "expected error: " + text)


## Marks EVERY recorded error/warning containing `text` as handled and asserts
## at least `min_count` were seen. For loops that error once per element.
func expect_errors_containing(text: String, min_count := 1, msg := "") -> int:
	var seen := 0
	for err in get_errors():
		if not err.handled and err.contains_text(text):
			err.handled = true
			seen += 1
	assert_true(seen >= min_count, msg if msg != "" else "expected >= %d error(s) containing '%s' (got %d)" % [min_count, text, seen])
	return seen


## Asserts that at least `min_count` NEW (unhandled) errors were recorded since
## the last expect_* call, then marks them handled. For hostile-input calls
## whose contract is "fail loud" when the exact wording is not the point.
func expect_any_error(msg := "", min_count := 1) -> int:
	var seen := 0
	for err in get_errors():
		if not err.handled and (err.is_push_error() or err.is_engine_error()):
			err.handled = true
			seen += 1
	assert_true(seen >= min_count, msg if msg != "" else "expected the call to fail loud (>= %d error(s), got %d)" % [min_count, seen])
	return seen


## Marks every recorded error as handled without asserting. Only for fuzz /
## crash-proof tests whose contract is "never crash, never NaN", where the
## exact error set is not the point. Returns how many were swallowed.
func swallow_errors() -> int:
	var n := 0
	for err in get_errors():
		if not err.handled:
			err.handled = true
			n += 1
	return n


## Builds a factory-owned directional volley (shared test data builders).
func spawn_dir(n: int = 4, speed: float = 200.0, lifetime: float = 5.0) -> DirectionalBullets2D:
	return factory.spawn_controllable_directional_bullets(H.make_directional_data(n, speed, lifetime))


## Spawner wired to `factory`, idle by default: shooting and homing are
## switched off BEFORE it enters the tree (a default spawner would otherwise
## auto-fire on its first frame). Freed automatically after the test.
func make_spawner(data: DirectionalBulletsData2D = null, source: int = BulletSpawner2D.PATTERN_FROM_HELPER_RING, amount: int = 4) -> BulletSpawner2D:
	var s := BulletSpawner2D.new()
	s.set_shooting_enabled(false)
	s.set_homing_enabled(false)
	s.set_spawn_data(data if data != null else H.make_directional_data(amount))
	s.pattern_source = source
	s.helper_bullets_amount = amount
	add(s)
	s.set_bullet_factory(factory)
	return s


## Like make_spawner, with the pattern preview live at runtime.
func make_preview_spawner(source: int = BulletSpawner2D.PATTERN_FROM_HELPER_RING, amount: int = 4) -> BulletSpawner2D:
	var s := make_spawner(H.make_directional_data(1, 0.0, 60.0), source, amount)
	s.show_pattern_preview = true
	s.show_preview_during_runtime = true
	return s


## Whether `prop` is shown in the inspector (PROPERTY_USAGE_EDITOR).
func is_editor_visible(obj: Object, prop: StringName) -> bool:
	for p in obj.get_property_list():
		if StringName(p.get("name", "")) == prop:
			return (int(p.get("usage", 0)) & PROPERTY_USAGE_EDITOR) != 0
	return false


## Static obstacle on `layer_bits` (collision_layer value), rect `size`.
func make_wall(pos: Vector2, size: Vector2 = Vector2(20, 400), layer_bits: int = 4, mask_bits: int = 2) -> StaticBody2D:
	var wall := StaticBody2D.new()
	wall.position = pos
	wall.collision_layer = layer_bits
	wall.collision_mask = mask_bits
	var col := CollisionShape2D.new()
	var box := RectangleShape2D.new()
	box.size = size
	col.shape = box
	wall.add_child(col)
	add(wall)
	return wall


## Monitoring Area2D on `layer_bits`, rect `size`.
func make_area(pos: Vector2, size: Vector2 = Vector2(20, 400), layer_bits: int = 4) -> Area2D:
	var area := Area2D.new()
	area.position = pos
	area.collision_layer = layer_bits
	area.monitoring = true
	area.monitorable = true
	var col := CollisionShape2D.new()
	var box := RectangleShape2D.new()
	box.size = size
	col.shape = box
	area.add_child(col)
	add(area)
	return area


## PackedScene wrapping one AttachmentProbe2D (counts every attachment
## callback: spawn/enable/disable/in_pool).
func make_probe_scene() -> PackedScene:
	var probe = load("res://tests/scenes/attachment_probe.gd").new()
	var ps := PackedScene.new()
	assert_eq(ps.pack(probe), OK, "probe scene packs")
	probe.free()
	return ps


## Adds a node under the test, freed automatically after it.
func add(node: Node) -> Node:
	return add_child_autofree(node)
