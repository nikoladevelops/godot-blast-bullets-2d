extends BlastTest
## BulletAttachment2D lifecycle through the pool (AttachmentProbe2D fixture
## counts callbacks): fresh attach -> spawn; disable -> disable; wake ->
## enable; pooled volley reuse starts with blank slots; pre-population runs
## on_spawn_in_pool and pops wake via enable; null / wrong-type scenes reject
## loudly; double free is safe; teleport carries stick attachments; scoped
## free with live attachments leaves nothing dangling.

var ps: PackedScene


func before_each() -> void:
	await super()
	ps = make_probe_scene()


func test_attach_disable_wake_sequence() -> void:
	var v: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(H.make_directional_data(2, 200.0))
	v.bullet_set_attachment(0, ps, Vector2.ZERO, true)
	await idle(1)
	var info: Dictionary = v.debug_get_attachment_info(0)
	assert_true(info.get("has_attachment", false), "slot holds the attachment")
	assert_true(info.get("owner_match", false), "owner ids point back")
	var probe = v.bullet_get_attachment(0)
	assert_eq(probe.get("spawn_calls"), 1, "on_bullet_spawn fired once")
	v.disable_bullet(0, false)
	assert_true(v.debug_get_attachment_info(0).get("has_attachment", false), "keep-slot disable preserves the slot")
	assert_eq(probe.get("disable_calls"), 0, "kept attachment skips the disable callback")
	v.wake_bullet(0)
	await idle(1)
	assert_eq(probe.get("enable_calls"), 1, "on_bullet_enable fired on wake")
	assert_true(v.debug_get_attachment_info(0).get("owner_match", false), "owner still matches after wake")
	v.disable_bullet(0)
	assert_eq(probe.get("disable_calls"), 1, "pooling disable fires the disable callback")


func test_pooled_reuse_starts_blank() -> void:
	var v: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(H.make_directional_data(2, 200.0))
	v.bullet_set_attachment(0, ps, Vector2.ZERO, true)
	for i in 2:
		v.disable_bullet(i)
	await idle(1)
	assert_gte(factory.debug_get_bullets_pool_amount(0), 1, "volley pooled")
	var w: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(H.make_directional_data(2, 200.0))
	assert_eq(w, v, "pool reuses the volley")
	assert_false(w.debug_get_attachment_info(0).get("has_attachment", false), "reuse starts with blank slots")


func test_prepopulated_attachments() -> void:
	factory.populate_attachments_pool(ps, 2)
	assert_eq(factory.debug_get_attachments_pool_amount(), 2, "2 probes pre-pooled")
	var x: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(H.make_directional_data(1, 200.0))
	x.bullet_set_attachment(0, ps, Vector2.ZERO, true)
	await idle(1)
	var xp = x.bullet_get_attachment(0)
	assert_eq(xp.get("in_pool_calls"), 1, "pre-pooled probe ran on_spawn_in_pool")
	assert_eq(xp.get("enable_calls"), 1, "popped probe wakes via enable")
	assert_eq(xp.get("spawn_calls"), 0, "spawn only for brand-new probes")


func test_invalid_scenes_reject_and_double_free_is_safe() -> void:
	var v: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(H.make_directional_data(1, 200.0))
	v.bullet_set_attachment(0, null, Vector2.ZERO, true)
	expect_error("invalid attachment scene")
	assert_false(v.debug_get_attachment_info(0).get("has_attachment", false), "null scene rejected")
	var foreign := Node2D.new()
	var bad_ps := PackedScene.new()
	bad_ps.pack(foreign)
	foreign.free()
	v.bullet_set_attachment(0, bad_ps, Vector2.ZERO, true)
	expect_error("not of type BulletAttachment2D")
	assert_false(v.debug_get_attachment_info(0).get("has_attachment", false), "wrong-type scene rejected")
	v.bullet_set_attachment(0, ps, Vector2.ZERO, true)
	var probe = v.bullet_get_attachment(0)
	v.bullet_free_attachment(0)
	v.bullet_free_attachment(0)
	assert_false(v.debug_get_attachment_info(0).get("has_attachment", false), "slot cleared")
	assert_true(probe.is_queued_for_deletion(), "freed attachment queued for deletion")


func test_teleport_carries_stick_attachment() -> void:
	var v: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(H.make_directional_data(1, 0.0))
	v.bullet_set_attachment(0, ps, Vector2(10, 0), true)
	await physics(3)
	var before: Vector2 = (v.bullet_get_attachment(0) as Node2D).global_position
	v.teleport_shift_all_bullets(Vector2(50, 0))
	await physics(3)
	var after: Vector2 = (v.bullet_get_attachment(0) as Node2D).global_position
	assert_almost_eq(after, before + Vector2(50, 0), Vector2(2, 2), "stick attachment rides the teleport")
	v.bullet_disable_attachment(0)
	assert_false(v.debug_get_attachment_info(0).get("has_attachment", false), "disable detaches the slot")
	assert_gte(factory.debug_get_attachments_pool_amount(), 1, "detached probe returned to the pool")


func test_scoped_free_with_live_attachments() -> void:
	var v: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(H.make_directional_data(2, 200.0))
	v.bullet_set_attachment(0, ps, Vector2.ZERO, true)
	v.bullet_set_attachment(1, ps, Vector2.ZERO, true)
	await idle(1)
	factory.free_attachments_pool_for_scene(ps)
	assert_true(factory.debug_assert_no_dangling().get("ok", false), "no dangling after a scene-scoped free")
	assert_eq(v.get_amount_active_attachments(), 2, "live attachments untouched by the pool free")
