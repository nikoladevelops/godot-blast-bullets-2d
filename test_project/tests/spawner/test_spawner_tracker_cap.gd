extends BlastTest
## VolleyTracker2D has no cap: 300 homing shots are all tracked (no warning)
## and every one is retargeted, forget_tracked_volleys() empties the list
## and tracking resumes on the next shot. The census (get_live_volleys /
## get_live_volley_count) reports every live volley the spawner owns.


func test_every_live_homing_volley_is_tracked() -> void:
	var d := H.make_volley_data(1, 0.0, 600.0)
	d.set_collision_mask_from_array([3])
	d.collision_shape = H.make_circle_shape(6.0)
	d.bullet_max_collision_count = 0
	var sp := make_spawner(d, BulletSpawner2D.PATTERN_FROM_HELPER_RING, 1)
	sp.homing_enabled = true
	sp.homing_target_source = BulletSpawner2D.HOMING_SOURCE_GLOBAL_POSITION
	sp.homing_global_position = Vector2(500, 0)
	var fired := 0
	for i in 300:
		if sp.shoot_once():
			fired += 1
	assert_eq(fired, 300, "all 300 shots fired")
	expect_no_errors("no errors")
	var warned := 0
	for err in get_errors():
		if err.is_push_warning():
			warned += 1
	assert_eq(warned, 0, "no cap, no eviction warning")
	assert_eq(sp.get_tracked_volley_count(), 300, "every live homing volley is tracked")
	assert_eq(sp.get_live_volley_count(), 300, "the census sees all 300 live volleys")
	assert_eq(sp.get_live_volleys().size(), 300, "the live list is the full census")
	assert_eq(sp.retarget_live_volleys(), 300, "a retarget pass reaches every one")
	sp.forget_tracked_volleys()
	assert_eq(sp.get_tracked_volley_count(), 0, "forget empties the retarget list")
	assert_eq(sp.get_live_volley_count(), 300, "forgetting never touches the bullets")
	assert_true(sp.shoot_once(), "shot after forget fires")
	assert_eq(sp.get_tracked_volley_count(), 1, "tracking resumes")
