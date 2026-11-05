extends BlastTest
## Every GDScript-callable "how many instances" entry is bounded by the
## developer's BulletPatterns2D.max_bullets_per_pattern: a typo cannot ask for
## billions of volleys/attachments/lasers/pickups (the reserve/memnew/
## instantiate loops would throw std::length_error or exhaust memory).
## play_sound rejects a non-finite position: it would become a NaN Node2D
## transform on the voice and defeat the distance/camera gates.


func test_populate_bullets_pool_rejects_a_count_above_the_limit() -> void:
	var limit := BulletPatterns2D.get_max_bullets_per_pattern()
	var key := VolleyPoolKey2D.make(1, PhysicsServer2D.SHAPE_CIRCLE)
	var data := H.make_volley_data(1)
	factory.populate_bullets_pool(key, data, limit + 1)
	expect_error_sequence(["BulletFactory2D.populate_bullets_pool: instance_count must be <= %d, ignoring the request." % limit])
	assert_eq(factory.debug_get_bullets_pool_amount(), 0, "nothing prewarmed")
	factory.populate_bullets_pool(key, data, 1) # the legal path still works
	assert_eq(factory.debug_get_bullets_pool_amount(), 1, "one prewarmed")
	factory.free_bullets_pool()
	assert_eq(factory.debug_get_bullets_pool_amount(), 0, "freed again")


func test_populate_attachments_pool_rejects_a_count_above_the_limit() -> void:
	var limit := BulletPatterns2D.get_max_bullets_per_pattern()
	factory.populate_attachments_pool(make_probe_scene(), limit + 1)
	expect_error_sequence(["BulletFactory2D.populate_attachments_pool: amount_instances must be <= %d, ignoring the request." % limit])
	assert_eq(factory.debug_get_attachments_pool_amount(), 0, "nothing pooled")


func test_populate_lasers_pool_rejects_a_count_above_the_limit() -> void:
	var limit := BulletPatterns2D.get_max_bullets_per_pattern()
	factory.populate_lasers_pool(4, limit + 1)
	expect_error_sequence(["populate_lasers_pool: amount must be <= BulletPatterns2D.max_bullets_per_pattern (%d), nothing was created." % limit])
	assert_eq(factory.debug_get_laser_pool_amount(), 0, "nothing pooled")


func test_populate_pickup_pool_rejects_a_count_above_the_limit() -> void:
	var limit := BulletPatterns2D.get_max_bullets_per_pattern()
	var pickup := BulletPickupData2D.new()
	factory.populate_pickup_pool(pickup, 1, limit + 1)
	expect_error_sequence(["BulletFactory2D.populate_pickup_pool: instance_count must be between 1 and %d, nothing populated." % limit])
	assert_eq(factory.debug_get_bullets_pool_amount(), 0, "nothing prewarmed")


func test_play_sound_rejects_a_non_finite_position() -> void:
	var sound := H.make_sound(BulletSoundData2D.SOUND_ON_HIT)
	for bad in [Vector2(NAN, 0), Vector2(0, INF), Vector2(-INF, NAN)]:
		assert_false(factory.play_sound(sound, bad), "refused %s" % bad)
		expect_error_sequence(["BulletFactory2D.play_sound: global_position must be finite (NaN/Inf is rejected)."])
	assert_eq(int(factory.debug_get_sound_stats().get("plays_total", -1)), 0, "no voice played")
