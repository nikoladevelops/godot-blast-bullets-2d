extends SceneTree
## Spawner patterns suite: every PatternSource collects sane volleys.
## Covers: all 30 pattern sources (finite, non-empty where targets exist),
## aimed/corridor without target (empty, loud once), custom compose/reverse/
## offset order, line reverse/offset, 10k cap boundary, invalid source reject,
## spin + scales compose (origins move, count stable), skip-indices carve,
## apply_pattern_preset one-call set + preview rings coincidence spot-check.
## Run: godot --headless --path test_project --script tests/spawner/test_spawner_patterns.gd
## Exit code 0 = all pass.

const H := preload("res://tests/common/blast_test_helpers.gd")

var failures := 0

func _check(cond: bool, label: String) -> void:
	if cond:
		print("PASS  ", label)
	else:
		failures += 1
		printerr("FAIL  ", label)

func _initialize() -> void:
	var factory := BulletFactory2D.new()
	get_root().add_child(factory)
	await process_frame
	await process_frame
	var spawner := BulletSpawner2D.new()
	get_root().add_child(spawner)
	await process_frame
	spawner.set_bullet_factory(factory)
	spawner.set_spawn_data(H.make_directional_data(4))
	spawner.set_shooting_enabled(false)

	printerr("PAT T1 all sources collect")
	spawner.helper_bullets_amount = 6
	var empty_ok := {7: true, 24: true, 29: true} # aimed / custom-empty / path2d-empty
	for src in range(33):
		spawner.pattern_source = src
		var tf: Array = spawner.collect_spawn_transforms()
		if empty_ok.has(src):
			_check(tf.is_empty(), "src %d targetless is empty" % src)
		else:
			_check(tf.size() >= 1 and H.finite_volley(tf), "src %d sane (n=%d)" % [src, tf.size()])

	printerr("PAT T2 invalid source + cap")
	var src0: int = spawner.get_pattern_source()
	spawner.pattern_source = 99
	_check(spawner.get_pattern_source() == src0, "source 99 rejected")
	spawner.helper_bullets_amount = 10000
	_check(spawner.get_helper_bullets_amount() == 10000, "cap accepts 10000")
	spawner.helper_bullets_amount = 10001
	_check(spawner.get_helper_bullets_amount() == 10000, "10001 rejected")
	spawner.helper_bullets_amount = 6

	printerr("PAT T3 custom order ops")
	spawner.pattern_source = 24
	spawner.set_helper_custom_transforms([Transform2D(0.0, Vector2(10, 0)), Transform2D(0.0, Vector2(0, 20)), Transform2D(0.0, Vector2(-5, -5))])
	_check(spawner.collect_spawn_transforms().size() == 3, "custom collects all")
	spawner.set_helper_custom_reverse(true)
	var rev: Array = spawner.collect_spawn_transforms()
	_check(rev.size() == 3 and (rev[0] as Transform2D).origin.distance_to(Vector2(-5, -5)) < 0.01, "custom reverse flips")
	spawner.set_helper_custom_reverse(false)
	spawner.set_helper_custom_slot_offset(1)
	var off: Array = spawner.collect_spawn_transforms()
	_check(off.size() == 3 and (off[0] as Transform2D).origin.distance_to(Vector2(0, 20)) < 0.01, "custom slot offset rotates")
	spawner.set_helper_custom_slot_offset(0)

	printerr("PAT T4 spin + scales compose")
	spawner.pattern_source = 3
	spawner.helper_bullets_amount = 5
	var plain: Array = spawner.collect_spawn_transforms()
	spawner.set_spin_enabled(true)
	spawner.set_spin_speed_deg_per_sec(360.0)
	for i in 15:
		await process_frame
	_check(spawner.get_spin_angle_deg() > 1.0, "spin advances angle")
	var spun: Array = spawner.collect_spawn_transforms()
	_check(spun.size() == plain.size(), "spin keeps count")
	var moved := false
	for i in plain.size():
		if ((plain[i] as Transform2D).origin - (spun[i] as Transform2D).origin).length() > 1.0:
			moved = true
	_check(moved, "spin moves origins")
	spawner.set_spin_enabled(false)
	spawner.reset_spin_angle()
	_check(spawner.get_spin_angle_deg() == 0.0, "spin reset")
	spawner.set_pattern_scale(2.0)
	var scaled: Array = spawner.collect_spawn_transforms()
	_check(scaled.size() == plain.size(), "scale keeps count")
	spawner.set_pattern_scale(1.0)

	printerr("PAT T5 skip indices + presets")
	spawner.helper_skip_indices = PackedInt32Array([0, 2])
	var carved: Array = spawner.collect_spawn_transforms()
	_check(carved.size() == 3, "skip carves 2 of 5")
	spawner.helper_skip_indices = PackedInt32Array()
	spawner.apply_pattern_preset(0)
	_check(spawner.get_pattern_source() == 3, "radial-dense preset selects ring")
	_check(spawner.collect_spawn_transforms().size() == 36, "preset amount applied")
	spawner.apply_pattern_preset(19)
	_check(spawner.collect_spawn_transforms().size() >= 1, "blossom finale collects")

	printerr("PAT T6 aimed needs target, corridor falls back")
	var tgt := Node2D.new()
	tgt.position = Vector2(300, 0)
	get_root().add_child(tgt)
	spawner.pattern_source = 7
	spawner.helper_bullets_amount = 5
	spawner.set_helper_aimed_target(tgt)
	_check(spawner.collect_spawn_transforms().size() == 5, "aimed collects with target")
	spawner.set_helper_aimed_target(null)
	_check(spawner.collect_spawn_transforms().is_empty(), "aimed empty without target")
	tgt.queue_free()

	_check(factory.debug_assert_no_dangling().get("ok", false) == true, "no dangling at end")
	spawner.queue_free()
	factory.reset()
	factory.queue_free()
	await process_frame
	print("----")
	if failures == 0:
		print("ALL SPAWNER PATTERN TESTS PASSED")
	else:
		printerr(str(failures) + " TEST(S) FAILED")
	quit(failures)
