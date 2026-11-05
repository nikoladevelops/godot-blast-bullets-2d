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
	var v: BulletVolley2D = factory.spawn_volley(H.make_volley_data(2, 200.0))
	v.bullet_set_attachment(0, ps, Vector2.ZERO, true)
	await idle(1)
	var info: Dictionary = v.debug_get_attachment_info(0)
	assert_true(info.get("has_attachment", false), "slot holds the attachment")
	assert_true(info.get("owner_match", false), "owner ids point back")
	var probe = v.bullet_get_attachment(0)
	assert_eq(probe.get("spawn_calls"), 1, "on_bullet_spawn fired once")
	v.disable_bullet(0, false)
	assert_true(v.debug_get_attachment_info(0).get("has_attachment", false), "keep-slot disable preserves the slot")
	assert_eq(probe.get("disable_calls"), 1, "a kept (suspended) attachment is told its bullet is gone")
	assert_same(v.bullet_get_attachment(0), probe, "the same node stays in the slot")
	v.enable_bullet(0)
	await idle(1)
	assert_eq(probe.get("enable_calls"), 1, "on_bullet_enable fired on wake (symmetric resume)")
	assert_true(v.debug_get_attachment_info(0).get("owner_match", false), "owner still matches after wake")
	v.disable_bullet(0)
	assert_eq(probe.get("disable_calls"), 2, "releasing disable fires the disable callback again")
	assert_null(v.bullet_get_attachment(0), "released to the attachment pool")


func test_pooled_reuse_starts_blank() -> void:
	var v: BulletVolley2D = factory.spawn_volley(H.make_volley_data(2, 200.0))
	v.bullet_set_attachment(0, ps, Vector2.ZERO, true)
	for i in 2:
		v.disable_bullet(i)
	await idle(1)
	assert_eq(factory.debug_get_bullets_pool_amount(), 1, "volley pooled")
	var w: BulletVolley2D = factory.spawn_volley(H.make_volley_data(2, 200.0))
	assert_eq(w, v, "pool reuses the volley")
	assert_false(w.debug_get_attachment_info(0).get("has_attachment", false), "reuse starts with blank slots")


func test_prepopulated_attachments() -> void:
	factory.populate_attachments_pool(ps, 2)
	assert_eq(factory.debug_get_attachments_pool_amount(), 2, "2 probes pre-pooled")
	var x: BulletVolley2D = factory.spawn_volley(H.make_volley_data(1, 200.0))
	x.bullet_set_attachment(0, ps, Vector2.ZERO, true)
	await idle(1)
	var xp = x.bullet_get_attachment(0)
	assert_eq(xp.get("in_pool_calls"), 1, "pre-pooled probe ran on_spawn_in_pool")
	assert_eq(xp.get("enable_calls"), 1, "popped probe wakes via enable")
	assert_eq(xp.get("spawn_calls"), 0, "spawn only for brand-new probes")


func test_invalid_scenes_reject_and_double_free_is_safe() -> void:
	var v: BulletVolley2D = factory.spawn_volley(H.make_volley_data(1, 200.0))
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
	var v: BulletVolley2D = factory.spawn_volley(H.make_volley_data(1, 0.0))
	v.bullet_set_attachment(0, ps, Vector2(10, 0), true)
	await physics(3)
	var before: Vector2 = (v.bullet_get_attachment(0) as Node2D).global_position
	v.teleport_shift_all_bullets(Vector2(50, 0))
	await physics(3)
	var after: Vector2 = (v.bullet_get_attachment(0) as Node2D).global_position
	assert_almost_eq(after, before + Vector2(50, 0), Vector2(2, 2), "stick attachment rides the teleport")
	v.bullet_disable_attachment(0)
	assert_false(v.debug_get_attachment_info(0).get("has_attachment", false), "disable detaches the slot")
	assert_eq(factory.debug_get_attachments_pool_amount(), 1, "detached probe returned to the pool")


func test_scoped_free_with_live_attachments() -> void:
	var v: BulletVolley2D = factory.spawn_volley(H.make_volley_data(2, 200.0))
	v.bullet_set_attachment(0, ps, Vector2.ZERO, true)
	v.bullet_set_attachment(1, ps, Vector2.ZERO, true)
	await idle(1)
	factory.free_attachments_pool_for_scene(ps)
	assert_true(factory.debug_assert_no_dangling().get("ok", false), "no dangling after a scene-scoped free")
	assert_eq(v.get_amount_active_attachments(), 2, "live attachments untouched by the pool free")


## The pool remembers each scene it validated (for its debug labels and error
## wording). In-memory scenes get a key per PackedScene: a game that packs a
## fresh scene per shot must not grow that memory without bound, so scenes
## that no longer exist are forgotten.
func test_dead_in_memory_scenes_are_forgotten() -> void:
	for k in 300:
		var scene := make_probe_scene()
		factory.populate_attachments_pool(scene, 1)
		factory.free_attachments_pool_for_scene(scene)
		await idle(1) # the queued frees run: nothing holds the scene any more
	assert_lte(factory.debug_get_attachment_scene_memory_count(), 128, "bounded by the live scenes, not every scene ever seen")
	factory.populate_attachments_pool(ps, 2)
	assert_eq(factory.debug_get_attachments_pool_amount(), 2, "the live scene still pools")


## A PackedScene whose root is a plain Node2D (never a BulletAttachment2D).
func _foreign_scene() -> PackedScene:
	var foreign := Node2D.new()
	var scene := PackedScene.new()
	scene.pack(foreign)
	foreign.free()
	return scene


func test_a_broken_shared_attachment_reports_once_per_resource() -> void:
	# The data's attachment describes the DATA: a broken one reports once per
	# resource (like every data warning), never once per shot (AGENTS §14).
	var data := H.make_volley_data(3, 200.0)
	data.shared_bullet_attachment = _foreign_scene()
	for i in 4:
		var v: BulletVolley2D = factory.spawn_volley(data)
		assert_not_null(v, "the bullets still spawn")
		assert_false(v.debug_get_attachment_info(0).get("has_attachment", false), "no attachment from a foreign scene")
	expect_error_sequence(["BulletVolleyData2D: shared_bullet_attachment is not a BulletAttachment2D scene (its root is a Node2D), the bullets spawn without attachments."])
	data.shared_bullet_attachment = PackedScene.new()
	for i in 3:
		factory.spawn_volley(data)
	expect_error_sequence(["BulletVolleyData2D: shared_bullet_attachment is an empty PackedScene (nothing to instantiate), the bullets spawn without attachments."])


func test_an_empty_attachment_scene_is_refused_before_instantiating() -> void:
	# Instantiating an empty PackedScene is an engine error on top of ours: the
	# plugin refuses it first, with one error of its own.
	var v: BulletVolley2D = factory.spawn_volley(H.make_volley_data(2, 200.0))
	v.bullet_set_attachment(0, PackedScene.new(), Vector2.ZERO, true)
	expect_error_sequence(["Tried to set an empty attachment scene (a PackedScene with no nodes) to bullet index: 0"])
	factory.populate_attachments_pool(PackedScene.new(), 3)
	expect_error_sequence(["PackedScene is empty (no nodes). Nothing was pooled."])
	assert_eq(factory.debug_get_attachments_pool_amount(), 0, "nothing pooled")


func test_all_bullets_set_attachment_reports_a_bad_scene_once() -> void:
	# Every bullet would fail the same way: one report for the call, not one per bullet.
	var v: BulletVolley2D = factory.spawn_volley(H.make_volley_data(6, 200.0))
	v.all_bullets_set_attachment(_foreign_scene(), Vector2.ZERO)
	expect_error_sequence(["Tried to instantiate an attachment scene that is not of type BulletAttachment2D at bullet index: 0"])
	v.all_bullets_set_attachment(PackedScene.new(), Vector2.ZERO)
	expect_error_sequence(["Tried to set an empty attachment scene (a PackedScene with no nodes) to bullet index: 0"])
	for i in 6:
		assert_false(v.debug_get_attachment_info(i).get("has_attachment", false), "bullet %d has no attachment" % i)
	v.all_bullets_set_attachment(ps, Vector2.ZERO)
	expect_no_errors()
	for i in 6:
		assert_true(v.debug_get_attachment_info(i).get("has_attachment", false), "a good scene still attaches bullet %d" % i)


func _attached_data(count: int, scene: PackedScene) -> BulletVolleyData2D:
	var d := H.make_volley_data(count, 0.0, 30.0)
	d.shared_bullet_attachment = scene
	return d


## The attachment pool is read out as data ({scene: count}) and a scoped free
## empties exactly one scene's probes: the other scene's pre-pooled probes
## survive AND still come out of the pool (they are popped with
## in_pool_calls == 1, never reinstantiated), which is what proves the
## survivors belong to the untouched scene.
func test_scoped_free_empties_only_that_scene_and_the_rest_still_pops() -> void:
	var other := make_probe_scene()
	factory.populate_attachments_pool(ps, 3)
	var info: Dictionary = factory.debug_get_attachments_pool_info()
	assert_eq(info.size(), 1, "one entry for the in-memory scene")
	assert_eq(int(info.values()[0]), 3, "the info dict counts the 3 pooled probes")
	assert_eq(factory.debug_get_attachments_pool_amount(), 3, "and the plain count agrees")
	factory.populate_attachments_pool(other, 2)
	assert_eq(factory.debug_get_attachments_pool_amount(), 5, "five probes pooled in total")
	factory.free_attachments_pool_for_scene(ps)
	await idle()
	assert_eq(factory.debug_get_attachments_pool_amount(), 2, "exactly the other scene's 2 probes are left")
	var v: BulletVolley2D = factory.spawn_volley(_attached_data(2, other))
	await idle(1)
	assert_eq(factory.debug_get_attachments_pool_amount(), 0, "the other scene's probes are popped by the spawn")
	assert_eq(factory.debug_get_active_attachments_amount(), 2, "both bullets carry one")
	for i in 2:
		var probe = v.bullet_get_attachment(i)
		assert_eq(probe.get("in_pool_calls"), 1, "bullet %d got a pre-pooled probe" % i)
		assert_eq(probe.get("spawn_calls"), 0, "bullet %d did not instantiate a fresh one" % i)


## A pre-warmed attachment pool feeds the next volley's bullets (the pool empties
## into the volley: 2 -> 0) and the drain puts every probe back.
func test_pre_pooled_attachments_are_handed_out_and_returned() -> void:
	factory.populate_attachments_pool(ps, 2)
	assert_eq(factory.debug_get_attachments_pool_amount(), 2, "2 probes pre-pooled")
	assert_eq(factory.debug_get_active_attachments_amount(), 0, "nothing attached yet")
	var v: BulletVolley2D = factory.spawn_volley(_attached_data(2, ps))
	await idle(1)
	assert_eq(factory.debug_get_attachments_pool_amount(), 0, "both probes went to the volley")
	assert_eq(factory.debug_get_active_attachments_amount(), 2, "both bullets are attached")
	for i in 2:
		assert_true(v.debug_get_attachment_info(i).get("has_attachment", false), "bullet %d is attached" % i)
		assert_eq(v.bullet_get_attachment(i).get("in_pool_calls"), 1, "bullet %d took one from the pool" % i)
	v.disable_bullet(0)
	v.disable_bullet(1)
	await idle(1)
	assert_eq(factory.debug_get_attachments_pool_amount(), 2, "the drain returned both to the pool")
	assert_eq(factory.debug_get_active_attachments_amount(), 0, "nothing is attached any more")
	assert_true(factory.debug_assert_no_dangling().get("ok", false), "books ok")


## Freeing the attachment pool spares what is IN USE: the two attached probes
## keep their slots and stay alive, and the drain afterwards fills the empty
## pool again.
func test_freeing_the_pool_spares_the_attachments_in_use() -> void:
	factory.populate_attachments_pool(ps, 2)
	var v: BulletVolley2D = factory.spawn_volley(_attached_data(2, ps))
	await idle(1)
	assert_eq(factory.debug_get_active_attachments_amount(), 2, "2 in use")
	var in_use: Array = [v.bullet_get_attachment(0), v.bullet_get_attachment(1)]
	factory.free_attachments_pool()
	await idle(1)
	assert_eq(factory.debug_get_attachments_pool_amount(), 0, "the pool is empty")
	assert_eq(factory.debug_get_active_attachments_amount(), 2, "the in-use attachments are untouched")
	for i in 2:
		assert_true(is_instance_valid(in_use[i]), "attachment %d is alive" % i)
		assert_false((in_use[i] as Node).is_queued_for_deletion(), "attachment %d is not queued for deletion" % i)
		assert_true(v.debug_get_attachment_info(i).get("has_attachment", false), "slot %d is still attached" % i)
	v.disable_bullet(0)
	v.disable_bullet(1)
	await idle(1)
	assert_eq(factory.debug_get_attachments_pool_amount(), 2, "the drain put both into the emptied pool")
	assert_eq(factory.debug_get_active_attachments_amount(), 0, "nothing is attached any more")
	assert_true(factory.debug_assert_no_dangling().get("ok", false), "books ok")
