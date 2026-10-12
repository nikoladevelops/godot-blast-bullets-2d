extends BlastTest
## Attachments follow the volley's time scale: the plugin multiplies the
## speed_scale of descendant particles, animated sprites and animation
## players (originals restored exactly at 1.0), tells the attachment through
## on_time_scale_changed, honors apply_time_scale = false, and pooled
## attachments come back neutral.

const ORIGINAL := [2.0, 1.5, 0.5, 3.0]

var scene: PackedScene


func before_each() -> void:
	await super()
	scene = AttachmentTimeProbe2D.make_scene()


func _volley_with_attachment() -> Array:
	var v: BulletVolley2D = factory.spawn_volley(H.make_volley_data(2, 0.0, 60.0))
	v.bullet_set_attachment(0, scene, Vector2.ZERO, true)
	return [v, v.bullet_get_attachment(0)]


func _scaled(base: Array, s: float) -> Array:
	return base.map(func(x): return x * s)


func test_a_fresh_attachment_is_untouched_at_scale_one() -> void:
	var pair := _volley_with_attachment()
	var a: AttachmentTimeProbe2D = pair[1]
	await idle(3)
	assert_eq(a.speeds(), ORIGINAL, "originals kept: nothing is touched at scale 1")
	assert_eq(a.scale_calls, [], "and the virtual is never called")
	assert_false(a.gpu().has_meta("_blast_time_scale_base"), "no bookkeeping left on the children")
	assert_eq(a.get_applied_time_scale(), 1.0, "applied scale is 1")


func test_factory_scale_scales_every_kind_and_restores_exactly() -> void:
	var pair := _volley_with_attachment()
	var a: AttachmentTimeProbe2D = pair[1]
	await idle(2)
	factory.time_scale = 0.5
	await idle(2)
	assert_eq(a.speeds(), _scaled(ORIGINAL, 0.5), "particles, sprite and player scaled by 0.5")
	assert_eq(a.get_node("Deep/Gpu2").speed_scale, 2.0, "nested children too")
	assert_eq(a.scale_calls, [0.5], "on_time_scale_changed(0.5) once")
	factory.time_scale = 0.25
	await idle(2)
	assert_eq(a.speeds(), _scaled(ORIGINAL, 0.25), "a second change scales the ORIGINAL, not the scaled value")
	factory.time_scale = 1.0
	await idle(2)
	assert_eq(a.speeds(), ORIGINAL, "back at 1.0 the originals return exactly")
	assert_eq(a.get_node("Deep/Gpu2").speed_scale, 4.0, "nested original restored")
	assert_false(a.gpu().has_meta("_blast_time_scale_base"), "bookkeeping removed")
	assert_eq(a.scale_calls, [0.5, 0.25, 1.0], "one call per change")


func test_scale_zero_pauses_everything() -> void:
	var pair := _volley_with_attachment()
	var a: AttachmentTimeProbe2D = pair[1]
	await idle(2)
	factory.time_scale = 0.0
	await idle(2)
	assert_eq(a.speeds(), [0.0, 0.0, 0.0, 0.0], "speed_scale 0 on every child")
	factory.time_scale = 1.0
	await idle(2)
	assert_eq(a.speeds(), ORIGINAL, "and back")


func test_apply_time_scale_off_leaves_children_alone_but_still_notifies() -> void:
	var pair := _volley_with_attachment()
	var a: AttachmentTimeProbe2D = pair[1]
	a.apply_time_scale = false
	await idle(2)
	factory.time_scale = 0.5
	await idle(2)
	assert_eq(a.speeds(), ORIGINAL, "children untouched")
	assert_eq(a.scale_calls, [0.5], "the virtual still tells the script")
	a.apply_time_scale = true
	assert_eq(a.speeds(), _scaled(ORIGINAL, 0.5), "turning it on applies the current scale at once")
	a.apply_time_scale = false
	assert_eq(a.speeds(), ORIGINAL, "turning it off restores the originals")


func test_spawner_scale_reaches_its_volleys_attachments() -> void:
	var sp := make_spawner(H.make_volley_data(1, 0.0, 60.0), BulletSpawner2D.PATTERN_FROM_HELPER_RING, 1)
	sp.time_scale = 0.5
	watch_signals(sp)
	assert_true(sp.shoot_once(), "shot")
	var v: BulletVolley2D = get_signal_parameters(sp, "volley_fired", 0)[0]
	v.bullet_set_attachment(0, scene, Vector2.ZERO, true)
	var a: AttachmentTimeProbe2D = v.bullet_get_attachment(0)
	await idle(2)
	assert_eq(a.speeds(), _scaled(ORIGINAL, 0.5), "spawner scale applies")
	factory.time_scale = 0.5
	await idle(2)
	assert_eq(a.speeds(), _scaled(ORIGINAL, 0.25), "factory x spawner")
	sp.time_scale_affects_volleys = false
	await idle(2)
	assert_eq(a.speeds(), _scaled(ORIGINAL, 0.5), "opted out: only the factory scale")


func test_attachment_added_while_slowed_is_scaled_by_the_next_tick() -> void:
	factory.time_scale = 0.5
	var pair := _volley_with_attachment()
	var a: AttachmentTimeProbe2D = pair[1]
	await idle(2)
	assert_eq(a.speeds(), _scaled(ORIGINAL, 0.5), "scaled after the first tick")


func test_pooled_attachment_comes_back_neutral_and_is_rescaled_on_reuse() -> void:
	var pair := _volley_with_attachment()
	var v: BulletVolley2D = pair[0]
	var a: AttachmentTimeProbe2D = pair[1]
	factory.time_scale = 0.5
	await idle(2)
	assert_eq(a.speeds(), _scaled(ORIGINAL, 0.5), "scaled while attached")
	v.disable_bullet(0) # releases the attachment to the pool
	assert_eq(a.speeds(), ORIGINAL, "released: originals restored before pooling")
	assert_eq(a.get_applied_time_scale(), 1.0, "applied scale reset")
	# Reuse at scale 1: not scaled.
	factory.time_scale = 1.0
	var v2: BulletVolley2D = factory.spawn_volley(H.make_volley_data(1, 0.0, 60.0))
	v2.bullet_set_attachment(0, scene, Vector2.ZERO, true)
	var b: AttachmentTimeProbe2D = v2.bullet_get_attachment(0)
	assert_same(b, a, "the pooled node was reused")
	await idle(2)
	assert_eq(b.speeds(), ORIGINAL, "reused at scale 1: untouched")


func test_children_added_later_are_picked_up_by_refresh() -> void:
	var pair := _volley_with_attachment()
	var a: AttachmentTimeProbe2D = pair[1]
	factory.time_scale = 0.5
	await idle(2)
	var late := AnimationPlayer.new()
	late.speed_scale = 2.0
	a.add_child(late)
	assert_eq(late.speed_scale, 2.0, "not scaled yet")
	a.refresh_time_scale()
	assert_eq(late.speed_scale, 1.0, "refresh_time_scale() scales it")
	factory.time_scale = 1.0
	await idle(2)
	assert_eq(late.speed_scale, 2.0, "and it is restored with the rest")


func test_freed_children_and_freed_attachments_never_crash() -> void:
	var pair := _volley_with_attachment()
	var v: BulletVolley2D = pair[0]
	var a: AttachmentTimeProbe2D = pair[1]
	a.get_node("Gpu").free()
	factory.time_scale = 0.5
	await idle(2)
	assert_eq(a.get_node("Cpu").speed_scale, 0.75, "the rest still scales")
	a.free()
	factory.time_scale = 0.25
	await idle(2)
	assert_null(v.bullet_get_attachment(0), "the freed attachment left the slot")
	assert_true(factory.debug_assert_no_dangling().get("ok", false), "nothing dangles")


func test_many_volleys_scale_independently() -> void:
	var sp1 := make_spawner(H.make_volley_data(1, 0.0, 60.0), BulletSpawner2D.PATTERN_FROM_HELPER_RING, 1)
	var sp2 := make_spawner(H.make_volley_data(1, 0.0, 60.0), BulletSpawner2D.PATTERN_FROM_HELPER_RING, 1)
	sp1.time_scale = 0.5
	sp2.time_scale = 0.25
	watch_signals(sp1)
	watch_signals(sp2)
	sp1.shoot_once()
	sp2.shoot_once()
	var v1: BulletVolley2D = get_signal_parameters(sp1, "volley_fired", 0)[0]
	var v2: BulletVolley2D = get_signal_parameters(sp2, "volley_fired", 0)[0]
	v1.bullet_set_attachment(0, scene, Vector2.ZERO, true)
	v2.bullet_set_attachment(0, scene, Vector2.ZERO, true)
	await idle(2)
	assert_eq(v1.bullet_get_attachment(0).speeds(), _scaled(ORIGINAL, 0.5), "volley 1")
	assert_eq(v2.bullet_get_attachment(0).speeds(), _scaled(ORIGINAL, 0.25), "volley 2")
