extends BlastTest
## Three spawners (ring/circle, fan/rect, line/capsule) plus direct spawns
## interleave on ONE factory: census attribution by owner, retarget and the
## live-bullet fuse stay per-spawner, per-bucket free touches only its bucket.

var s_ring: BulletSpawner2D
var s_fan: BulletSpawner2D
var s_line: BulletSpawner2D


func _spawner(src: int, amount: int, shape: Shape2D) -> BulletSpawner2D:
	var d := H.make_directional_data(amount, 220.0)
	d.collision_shape = shape
	return make_spawner(d, src, amount)


func before_each() -> void:
	await super()
	var rect := RectangleShape2D.new()
	rect.size = Vector2(14, 10)
	var cap := CapsuleShape2D.new()
	cap.radius = 4.0
	cap.height = 20.0
	s_ring = _spawner(BulletSpawner2D.PATTERN_FROM_HELPER_RING, 6, H.make_circle_shape(6.0))
	s_fan = _spawner(BulletSpawner2D.PATTERN_FROM_HELPER_LINE, 4, rect)
	s_line = _spawner(BulletSpawner2D.PATTERN_FROM_HELPER_LINE, 5, cap)
	s_line.set_helper_line_direction(Vector2(1, 0))
	s_line.set_helper_line_spacing(24.0)
	await idle(1)
	for i in 60:
		s_ring.shoot_once()
		if i % 2 == 0:
			s_fan.shoot_once()
		if i % 3 == 0:
			s_line.shoot_once()
		if i % 5 == 0:
			factory.spawn_controllable_directional_bullets(H.make_directional_data(6, 220.0))
		await physics()
	await idle()


func test_interleaved_churn_counts() -> void:
	assert_eq(s_ring.get_volleys_fired(), 60)
	assert_eq(s_fan.get_volleys_fired(), 30)
	assert_eq(s_line.get_volleys_fired(), 20)
	assert_gte(factory.debug_get_total_bullets_amount(), 60 + 30 + 20 + 12, "every volley tracked by the factory")


func test_census_attribution() -> void:
	assert_gt(s_ring.get_active_live_bullet_count(), 0, "ring owns live bullets")
	assert_gt(s_fan.get_active_live_bullet_count(), 0, "fan owns live bullets")
	assert_gt(factory.debug_get_live_volley_ids(s_ring.get_instance_id()).size(), 0, "live ids attributed to the ring spawner")


func test_retarget_and_fuse_are_per_spawner() -> void:
	s_ring.set_homing_enabled(true)
	s_ring.set_homing_target_source(BulletSpawner2D.HOMING_SOURCE_GLOBAL_POSITION)
	s_ring.set_homing_global_position(Vector2(600, 0))
	assert_eq(s_ring.retarget_live_volleys(), 0, "plain volleys fired before homing are not tracked")
	assert_true(s_ring.shoot_once(), "a homing volley")
	assert_eq(s_ring.retarget_live_volleys(), 1, "only the homing volley is retargeted")
	var fan_before: int = s_fan.get_volleys_fired()
	s_fan.set_max_live_bullets(1)
	s_fan.shoot_once()
	assert_eq(s_fan.get_volleys_fired(), fan_before, "fan fuse holds while the ring flies free")


func test_per_bucket_free_isolates() -> void:
	var ring_key: MultiMeshPoolKey2D = BulletFactory2D.debug_expected_pool_key(s_ring.get_spawn_data())
	var fan_live_before: int = s_fan.get_active_live_bullet_count()
	factory.free_active_bullets(ring_key)
	await idle(1)
	assert_eq(s_fan.get_active_live_bullet_count(), fan_live_before, "fan untouched by the ring-bucket free")
	assert_eq(s_ring.get_active_live_bullet_count(), 0, "ring bucket freed")
