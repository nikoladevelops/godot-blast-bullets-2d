extends BlastTest
## A nested disable from on_bullet_disable is rejected (disable_bullet holds
## the same latch as enable_bullet): the counter, live set and pool entry stay
## exact, the outer disable completes, the volley pools exactly once, reuse is
## clean, and plain sequential disables are unaffected.

const Probe := preload("res://tests/scenes/reentrant_probe.gd")


func _probe_scene() -> PackedScene:
	var probe = Probe.new()
	var ps := PackedScene.new()
	assert_eq(ps.pack(probe), OK)
	probe.free()
	return ps


func after_each() -> void:
	Probe.reset_state()
	await super()


func test_nested_disable_rejected_then_single_pool() -> void:
	Probe.reset_state()
	var v: BulletVolley2D = factory.spawn_volley(H.make_still_data(2))
	v.bullet_set_attachment(0, _probe_scene(), Vector2.ZERO, true)
	await idle(1)
	Probe.volley = v
	Probe.victim = 1
	v.disable_bullet(0)
	expect_error("re-entrant call")
	assert_eq(Probe.attempts, 1, "handler ran exactly once")
	assert_true(v.is_bullet_status_enabled(1), "nested disable rejected: victim still live")
	assert_false(v.is_bullet_status_enabled(0), "outer disable completed")
	assert_eq(factory.debug_get_bullets_pool_amount(), 0, "volley NOT pooled (one bullet still live)")
	Probe.reset_state()
	v.disable_bullet(1)
	await idle(1)
	assert_eq(factory.debug_get_bullets_pool_amount(), 1, "volley pooled exactly once")
	factory.debug_reset_pool_stats()
	var w: BulletVolley2D = factory.spawn_volley(H.make_still_data(2))
	assert_eq(int(factory.debug_get_pool_hit_stats().get("hits", 0)), 1, "reuse is a pool hit")
	assert_true(w.is_bullet_status_enabled(0) and w.is_bullet_status_enabled(1), "reused volley fully live")
	assert_false(w.debug_get_attachment_info(0).get("has_attachment", true), "no attachment leaked across")


func test_plain_sequential_disables_unaffected() -> void:
	var v: BulletVolley2D = factory.spawn_volley(H.make_still_data(2))
	v.disable_bullet(0)
	v.disable_bullet(1)
	await idle(1)
	assert_eq(factory.debug_get_bullets_pool_amount(), 1, "sequential disables pool normally")
