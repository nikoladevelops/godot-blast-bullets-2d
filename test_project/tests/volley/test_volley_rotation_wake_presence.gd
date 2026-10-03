extends BlastTest
## Plain (same-owner) disables keep rotation presence decisions for the wake:
## authored entries survive disable -> wake -> shared write, genuine gaps
## still fill; a new pooled life re-derives presence from its seed.


func _data() -> DirectionalBulletsData2D:
	var d := H.make_still_data(3)
	# Slot 0 deliberate zero, slot 1 authored spin, slot 2 a genuine gap.
	d.all_bullet_rotation_data = [H.make_rotation(0.0), H.make_rotation(9.0)]
	return d


func test_same_owner_wake_keeps_decisions() -> void:
	var v: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(_data())
	v.set_is_multimesh_auto_pooling_enabled(false)
	for i in 3:
		v.disable_bullet(i)
	await idle(1)
	assert_eq(factory.debug_get_bullets_pool_amount(), 0, "unpooled disable parks nothing")
	for i in 3:
		v.wake_bullet(i)
	v.set_shared_bullet_rotation_data(H.make_rotation(7.0))
	assert_almost_eq(v.bullet_get_rotation_speed(0), 0.0, 0.01, "authored zero survives wake + shared")
	assert_almost_eq(v.bullet_get_rotation_speed(1), 9.0, 0.01, "authored spin survives wake + shared")
	assert_almost_eq(v.bullet_get_rotation_speed(2), 7.0, 0.01, "gap fills after the wake")


func test_new_pooled_life_rederives_presence() -> void:
	var a: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(_data())
	for i in 3:
		a.disable_bullet(i)
	await idle(1)
	factory.debug_reset_pool_stats()
	var w: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(_data())
	assert_eq(int(factory.debug_get_pool_hit_stats().get("directional_hits", 0)), 1, "second life reuses the pool")
	w.set_shared_bullet_rotation_data(H.make_rotation(7.0))
	assert_almost_eq(w.bullet_get_rotation_speed(0), 0.0, 0.01, "new life honors the authored zero")
	assert_almost_eq(w.bullet_get_rotation_speed(1), 9.0, 0.01, "new life honors the authored spin")
