extends BlastTest
## VolleyTracker2D bound: 260 homing shots cap at 256 tracked (oldest dropped
## first, one warning), the newest volleys still retarget at the cap, and
## forget_tracked_volleys() empties and re-arms tracking. The census
## (get_live_volleys / get_live_volley_count) is never capped: it reports
## every live volley the spawner owns.


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
	assert_eq(sp.get_tracked_volley_count(), 256, "tracked caps at 256")
	assert_eq(sp.get_live_volley_count(), 260, "the census sees all 260 live volleys")
	assert_eq(sp.get_live_volleys().size(), 260, "the live list is the full census")
	assert_eq(sp.retarget_live_volleys(), 256, "retarget pass reaches volleys at the cap")
	sp.forget_tracked_volleys()
	assert_eq(sp.get_tracked_volley_count(), 0, "forget empties the retarget list")
	assert_eq(sp.get_live_volley_count(), 260, "forgetting never touches the bullets")
	assert_true(sp.shoot_once(), "shot after forget fires")
	assert_eq(sp.get_tracked_volley_count(), 1, "tracking resumes")
