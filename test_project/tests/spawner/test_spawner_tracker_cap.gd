extends BlastTest
## VolleyTracker2D bound: 260 homing shots cap at 256 tracked (oldest dropped
## first, one warning), the newest volleys still retarget at the cap, and
## clear_live_volleys() empties and re-arms tracking.


func test_tracker_cap() -> void:
	var d := H.make_volley_data(1, 0.0, 600.0)
	d.set_collision_mask_from_array([3])
	d.collision_shape = H.make_circle_shape(6.0)
	d.bullet_max_collision_count = 0
	var sp := make_spawner(d, BulletSpawner2D.PATTERN_FROM_HELPER_RING, 1)
	sp.homing_enabled = true
	sp.homing_target_source = BulletSpawner2D.HOMING_SOURCE_GLOBAL_POSITION
	sp.homing_global_position = Vector2(500, 0)
	var fired := 0
	for i in 260:
		if sp.shoot_once():
			fired += 1
	assert_eq(fired, 260, "all 260 shots fired")
	assert_push_warning("tracked volleys exceeded 256", "cap eviction warns once")
	assert_eq(sp.get_live_volley_count(), 256, "tracked caps at 256")
	assert_eq(sp.get_live_volleys().size(), 256, "live list matches the cap")
	assert_gte(sp.retarget_live_volleys(), 1, "retarget pass reaches volleys at the cap")
	sp.clear_live_volleys()
	assert_eq(sp.get_live_volley_count(), 0, "clear empties")
	assert_true(sp.shoot_once(), "shot after clear fires")
	assert_eq(sp.get_live_volley_count(), 1, "tracking resumes")
