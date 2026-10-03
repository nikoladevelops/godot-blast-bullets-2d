extends BlastTest
## Profiling API: get_frame_stats() counters are exact (spawns, ticked
## volleys/bullets, expiries, drained collision records), the tick time is
## measured, reset_frame_stats() zeroes cumulative counters, and the
## BlastBullets2D/* editor monitors are owned by exactly one factory.

const MON_ACTIVE := "BlastBullets2D/Active Bullets"


func test_keys_present() -> void:
	var s: Dictionary = factory.get_frame_stats()
	for key in ["physics_ticks", "physics_tick_usec", "peak_physics_tick_usec", "render_usec",
			"volleys_ticked", "bullets_ticked", "collision_records_total", "expired_bullets_total",
			"spawned_bullets_total", "pool_hits", "pool_misses", "active_bullets", "active_volleys",
			"pooled_volleys", "active_effects", "active_attachments"]:
		assert_has(s, key)


func test_spawn_and_tick_counters_are_exact() -> void:
	factory.reset_frame_stats()
	quick_volley(4, 100.0, 30.0)
	quick_volley(3, 100.0, 30.0)
	var s: Dictionary = factory.get_frame_stats()
	assert_eq(int(s["spawned_bullets_total"]), 7, "every spawned bullet counted once")
	assert_eq(int(s["active_bullets"]), 7, "live bullets across volleys")
	assert_eq(factory.get_active_bullet_count(), 7)
	assert_eq(int(s["active_volleys"]), 2)
	# physics(n) resumes at the START of a physics frame (SceneTree emits
	# physics_frame before nodes tick): sample both ends at that same phase.
	await physics()
	var ticks_before: int = factory.get_frame_stats()["physics_ticks"]
	await physics(3)
	s = factory.get_frame_stats()
	assert_eq(int(s["physics_ticks"]), ticks_before + 3, "one stats tick per physics frame")
	assert_eq(int(s["volleys_ticked"]), 2, "last tick moved both volleys")
	assert_eq(int(s["bullets_ticked"]), 7, "last tick moved every live bullet")
	assert_gte(int(s["physics_tick_usec"]), 0)
	assert_gte(int(s["peak_physics_tick_usec"]), int(s["physics_tick_usec"]), "peak >= last")


func test_expiry_counter() -> void:
	factory.reset_frame_stats()
	quick_volley(5, 0.0, 0.1)
	for i in 30:
		await physics()
		if factory.get_active_bullet_count() == 0:
			break
	assert_eq(factory.get_active_bullet_count(), 0, "volley expired")
	assert_eq(int(factory.get_frame_stats()["expired_bullets_total"]), 5, "every expired bullet counted once")
	await idle()
	assert_eq(int(factory.get_frame_stats()["pooled_volleys"]), 1, "expired volley parked in the pool")


func test_collision_record_counter() -> void:
	factory.reset_frame_stats()
	make_wall(Vector2(200, 0))
	await physics()
	var d := H.make_volley_data(1, 900.0, 30.0)
	d.transforms = [Transform2D()]
	d.monitorable = true
	d.set_collision_mask_from_array([3])
	d.collision_shape = H.make_circle_shape(6.0)
	d.bullet_max_collision_count = 1
	factory.spawn_volley(d)
	for i in 40:
		await physics()
		if factory.get_active_bullet_count() == 0:
			break
	assert_eq(int(factory.get_frame_stats()["collision_records_total"]), 1, "one drained record for one killing hit")


func test_reset_zeroes_cumulative_counters() -> void:
	quick_volley(2)
	await physics(2)
	factory.reset_frame_stats()
	var s: Dictionary = factory.get_frame_stats()
	for key in ["physics_ticks", "peak_physics_tick_usec", "collision_records_total", "expired_bullets_total", "spawned_bullets_total"]:
		assert_eq(int(s[key]), 0, "%s reset" % key)
	assert_eq(int(s["active_bullets"]), 2, "live state untouched by the reset")


func test_monitors_owned_by_one_factory() -> void:
	assert_true(Performance.has_custom_monitor(MON_ACTIVE), "first factory registers the monitors")
	quick_volley(3)
	assert_eq(int(Performance.get_custom_monitor(MON_ACTIVE)), 3, "monitor reports live bullets")
	var second := BulletFactory2D.new()
	add(second)
	await idle()
	assert_true(Performance.has_custom_monitor(MON_ACTIVE), "second factory does not steal or break the ids")
	assert_eq(int(Performance.get_custom_monitor(MON_ACTIVE)), 3, "still reports the owner")
	factory.register_performance_monitors = false
	assert_false(Performance.has_custom_monitor(MON_ACTIVE), "opting out removes the monitors")
	factory.register_performance_monitors = true
	assert_true(Performance.has_custom_monitor(MON_ACTIVE), "opting back in re-registers")


func test_monitors_removed_when_owner_leaves() -> void:
	assert_true(Performance.has_custom_monitor(MON_ACTIVE))
	var parent := factory.get_parent()
	parent.remove_child(factory)
	assert_false(Performance.has_custom_monitor(MON_ACTIVE), "exit_tree unregisters (no dangling callables)")
	parent.add_child(factory)
	await idle()
	check_factory_after = true
