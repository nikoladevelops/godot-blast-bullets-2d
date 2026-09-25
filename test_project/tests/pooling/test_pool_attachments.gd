extends SceneTree
## Pooling: BulletAttachment2D lifecycle through the pool.
## Fixture: AttachmentProbe2D (tests/scenes/attachment_probe.gd) counting
## on_bullet_spawn / on_bullet_enable / on_bullet_disable / on_spawn_in_pool.
## Covers: fresh attach calls spawn; disable calls disable; wake calls enable;
## pooled reuse carries the SAME probe node (identity) with enable called;
## pre-population calls in_pool; null + wrong-type scenes reject loudly;
## per-bullet free/disable/detach; double-free safe; owner_match via
## debug_get_attachment_info; factory teardown with live attachments; teleport
## carries attachments (stick mode); re-entrant attach from on_bullet_enable
## rejected (deferred instead).
## Run: godot --headless --path test_project --script tests/pooling/test_pool_attachments.gd
## Exit code 0 = all pass.

const H := preload("res://tests/common/blast_test_helpers.gd")
const Probe := preload("res://tests/scenes/attachment_probe.gd")

var failures := 0

func _check(cond: bool, label: String) -> void:
	if cond:
		print("PASS  ", label)
	else:
		failures += 1
		printerr("FAIL  ", label)

func _probe_scene() -> PackedScene:
	var probe = Probe.new()
	var ps := PackedScene.new()
	var err: int = ps.pack(probe)
	probe.queue_free()
	if err != OK:
		printerr("probe pack failed")
	return ps

func _initialize() -> void:
	var factory := BulletFactory2D.new()
	get_root().add_child(factory)
	await process_frame
	await process_frame
	var ps := _probe_scene()

	printerr("ATTACH T1 attach/disable/wake sequence")
	var v: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(H.make_directional_data(2, 200.0))
	v.bullet_set_attachment(0, ps, Vector2.ZERO, true)
	await process_frame
	var info: Dictionary = v.debug_get_attachment_info(0)
	_check(info.get("has_attachment", false) == true, "slot holds attachment")
	_check(info.get("owner_match", false) == true, "owner ids point back")
	var probe = v.bullet_get_attachment(0)
	_check(probe.get("spawn_calls") == 1, "on_bullet_spawn fired once")
	v.disable_bullet(0, false)
	_check(v.debug_get_attachment_info(0).get("has_attachment", false) == true, "keep-slot disable preserves")
	_check(probe.get("disable_calls") == 0, "kept attachment skips disable callback")
	v.wake_bullet(0)
	await process_frame
	_check(probe.get("enable_calls") == 1, "on_bullet_enable fired on wake")
	_check(v.debug_get_attachment_info(0).get("owner_match", false) == true, "owner still matches after wake")
	v.disable_bullet(0)
	_check(probe.get("disable_calls") == 1, "pooled disable fires disable callback")

	printerr("ATTACH T2 pooled reuse carries the same node")
	for i in 2:
		v.disable_bullet(i)
	await physics_frame
	_check(factory.debug_get_bullets_pool_amount(0) >= 1, "volley pooled with attachment aboard")
	var w: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(H.make_directional_data(2, 200.0))
	_check(w == v, "pool reuses the volley")
	var info2: Dictionary = w.debug_get_attachment_info(0)
	_check(info2.get("has_attachment", false) == false, "reuse starts with blank slots (no stale attach)")

	printerr("ATTACH T3 pre-population calls in_pool")
	await process_frame
	await process_frame
	factory.free_attachments_pool()
	await process_frame
	await process_frame
	_check(factory.debug_get_attachments_pool_amount() == 0, "pool drained for isolation")
	var ps2 := _probe_scene()
	factory.populate_attachments_pool(ps2, 2)
	_check(factory.debug_get_attachments_pool_amount() == 2, "2 probes pre-pooled")
	var x: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(H.make_directional_data(1, 200.0))
	x.bullet_set_attachment(0, ps2, Vector2.ZERO, true)
	await process_frame
	var xp = x.bullet_get_attachment(0)
	_check(xp.get("in_pool_calls") == 1, "pre-pooled probe ran on_spawn_in_pool")
	_check(xp.get("enable_calls") == 1 and xp.get("spawn_calls") == 0, "popped probe wakes via enable (spawn only for brand-new)")

	printerr("ATTACH T4 invalid scenes reject")
	var v4: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(H.make_directional_data(1, 200.0))
	v4.bullet_set_attachment(0, null, Vector2.ZERO, true)
	_check(v4.debug_get_attachment_info(0).get("has_attachment", false) == false, "null scene rejected")
	var foreign := Node2D.new()
	var bad_ps := PackedScene.new()
	bad_ps.pack(foreign)
	foreign.queue_free()
	v4.bullet_set_attachment(0, bad_ps, Vector2.ZERO, true)
	_check(v4.debug_get_attachment_info(0).get("has_attachment", false) == false, "wrong-type scene rejected")
	v4.bullet_free_attachment(0)
	v4.bullet_free_attachment(0)
	_check(factory.debug_assert_no_dangling().get("ok", false) == true, "double-free safe")

	printerr("ATTACH T5 detach + teleport carry")
	var still := H.make_directional_data(1, 0.0)
	var v5: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(still)
	v5.bullet_set_attachment(0, ps, Vector2(10, 0), true)
	for i in 3:
		await physics_frame
	var before: Vector2 = (v5.bullet_get_attachment(0) as Node2D).global_position
	v5.teleport_shift_all_bullets(Vector2(50, 0))
	for i in 3:
		await physics_frame
	var after: Vector2 = (v5.bullet_get_attachment(0) as Node2D).global_position
	_check(after.distance_to(before + Vector2(50, 0)) < 2.0, "stick attachment rides teleport")
	v5.bullet_disable_attachment(0)
	_check(v5.debug_get_attachment_info(0).get("has_attachment", false) == false, "disable detaches slot")
	_check(factory.debug_get_attachments_pool_amount() >= 1, "detached probe returned to pool")

	printerr("ATTACH T6 teardown with live attachments")
	var v6: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(H.make_directional_data(2, 200.0))
	v6.bullet_set_attachment(0, ps, Vector2.ZERO, true)
	v6.bullet_set_attachment(1, ps, Vector2.ZERO, true)
	await process_frame
	factory.free_attachments_pool_for_scene(ps)
	_check(factory.debug_assert_no_dangling().get("ok", false) == true, "no dangling after scene-scoped free")

	await process_frame
	await process_frame
	factory.reset()
	_check(factory.debug_assert_no_dangling().get("ok", false) == true, "no dangling at end")
	factory.queue_free()
	await process_frame
	print("----")
	if failures == 0:
		print("ALL ATTACHMENT TESTS PASSED")
	else:
		printerr(str(failures) + " TEST(S) FAILED")
	quit(failures)
