extends BlastTest
## Flocking under stress: thousands of stacked bullets cost exactly
## members x max_neighbors checks and part without NaN, bullets at huge or
## saturated coordinates stay out of the grid (bounded work, never NaN),
## tiny and huge ranges and extreme weights stay finite with unit headings,
## hundreds of flocks resolve in one pass, and a seeded storm of joins,
## leaves, freezes, wakes, edits, frees and clears keeps every invariant.

const DT := 1.0 / 60.0


func _stacked(count: int, at: Vector2) -> Array:
	var tr: Array = []
	for k in count:
		tr.append(Transform2D(0.0, at))
	return tr


func _volley(transforms: Array, speed := 100.0) -> BulletVolley2D:
	var d := H.make_flight_data(transforms, speed, 1000.0)
	d.set_collision_mask_from_array([])
	return factory.spawn_volley(d)


func test_ten_thousand_stacked_bullets_cost_exactly_the_cap() -> void:
	var v := _volley(_stacked(10000, Vector2(100, 100)), 50.0)
	v.flock_join(H.make_flock(1.5, 1.0, 1.0))
	step_factory(1)
	var stats: Dictionary = factory.debug_get_flock_stats()
	assert_eq(stats["grid_members"], 10000, "every bullet is in the grid")
	assert_eq(stats["neighbor_checks"], 10000 * 32, "stacked: exactly 32 checks per bullet")
	assert_eq(stats["capped_members"], 10000, "every bullet hit the cap")
	step_factory(59) # a stack fans out at the separation turn rate (1.5 rad/s here)
	assert_eq("\n".join(broken_bullets(v).slice(0, 5)), "", "finite, unit headings")
	var spread := Rect2(v.get_bullet_transform(0).origin, Vector2())
	for i in range(0, 10000, 97):
		spread = spread.expand(v.get_bullet_transform(i).origin)
	assert_gt(spread.size.length(), 20.0, "the stack parts")


func test_huge_coordinates_stay_out_of_the_grid() -> void:
	var far := _volley([Transform2D(0.0, Vector2(1e12, 1e12)), Transform2D(0.0, Vector2(1e12, 1e12 + 5)), Transform2D(0.0, Vector2(-3e38, 3e38))])
	var near := _volley([Transform2D(0.0, Vector2(0, 0)), Transform2D(0.0, Vector2(0, 20))])
	var f := H.make_flock(1.5, 1.0, 1.0)
	far.flock_join(f)
	near.flock_join(f)
	step_factory(5)
	var stats: Dictionary = factory.debug_get_flock_stats()
	assert_eq(stats["grid_members"], 2, "only the near pair is in the grid")
	assert_lte(stats["neighbor_checks"], 2, "the far bullets cost nothing")
	assert_eq(far.debug_get_bullet_flock_steering(0), Vector2(), "a far bullet does not steer")
	assert_gt(near.debug_get_bullet_flock_neighbors(0), 0, "the near pair flocks")
	assert_eq("\n".join(broken_bullets(near)), "", "near finite")
	for i in 2:
		assert_true(far.get_bullet_direction(i).is_finite(), "far heading %d finite" % i)


func test_tiny_and_huge_ranges_stay_bounded(range_px: float = use_parameters([0.001, 1e30])) -> void:
	seed(2)
	var tr: Array = []
	for k in 200:
		tr.append(Transform2D(randf_range(-PI, PI), Vector2(randf_range(0, 400), randf_range(0, 400))))
	var v := _volley(tr)
	var f := H.make_flock(1.5, 1.0, 1.0, range_px, range_px)
	v.flock_join(f)
	step_factory(20)
	var stats: Dictionary = factory.debug_get_flock_stats()
	assert_lte(stats["neighbor_checks"], 200 * 32, "never more than the cap per bullet")
	if range_px < 1.0:
		assert_eq(v.debug_get_bullet_flock_neighbors(0), 0, "a 0.001 px range sees nobody")
	else:
		assert_eq(v.debug_get_bullet_flock_neighbors(0), 32, "a 1e30 px range sees the cap")
	assert_eq("\n".join(broken_bullets(v).slice(0, 5)), "", "finite, unit headings")


func test_extreme_weights_stay_finite() -> void:
	seed(4)
	var tr: Array = []
	for k in 60:
		tr.append(Transform2D(randf_range(-PI, PI), Vector2(randf_range(0, 50), randf_range(0, 50))))
	var v := _volley(tr)
	v.flock_join(H.make_flock(1e30, 1e30, 1e30))
	step_factory(60)
	assert_eq("\n".join(broken_bullets(v).slice(0, 5)), "", "finite, unit headings")
	assert_lte(v.debug_get_bullet_flock_steering(0).length(), 1e6 * 1.0001, "the steering vector is capped")


func test_hundreds_of_flocks_resolve_in_one_pass() -> void:
	var f := H.make_flock(1.0, 1.0, 1.0)
	var volleys: Array = []
	for k in 500:
		var at := Vector2((k % 25) * 10.0, (k / 25) * 10.0)
		var v := _volley([Transform2D(0.0, at), Transform2D(0.0, at + Vector2(3, 0))])
		v.flock_join(f, BulletFlock2D.SCOPE_PER_VOLLEY)
		volleys.append(v)
	step_factory(1)
	assert_eq(factory.debug_get_flock_stats()["flocks"], 500, "one flock per volley")
	var problems: Array = []
	for k in 500:
		if volleys[k].debug_get_bullet_flock_neighbors(0) != 1:
			problems.append("volley %d sees %d" % [k, volleys[k].debug_get_bullet_flock_neighbors(0)])
	assert_eq("\n".join(problems.slice(0, 5)), "", "each bullet sees its own twin only, whatever is around")


func test_a_storm_of_membership_changes_keeps_every_invariant() -> void:
	seed(1234)
	var flocks := [H.make_flock(1.5, 1.0, 1.0), H.make_flock(3.0, 0.0, 2.0, 60.0, 20.0), H.make_flock(0.5, 2.0, 0.5, 150.0, 60.0)]
	flocks[1].max_neighbors = 4
	var volleys: Array = []
	var problems: Array = []
	for tick in 240:
		for op in 3:
			var roll := randi() % 9
			var live: Array = volleys.filter(func(v): return is_instance_valid(v) and v.debug_get_life_state() == "active")
			var pick: BulletVolley2D = null if live.is_empty() else live[randi() % live.size()]
			match roll:
				0, 1:
					var tr: Array = []
					var at := Vector2(randf_range(0, 300), randf_range(0, 300))
					for k in 1 + randi() % 12:
						tr.append(Transform2D(randf_range(-PI, PI), at + Vector2(randf_range(-20, 20), randf_range(-20, 20))))
					var v := _volley(tr, randf_range(-80.0, 160.0))
					v.flock_join(flocks[randi() % 3], randi() % 3)
					volleys.append(v)
				2:
					if pick != null:
						pick.flock_leave()
				3:
					if pick != null:
						pick.flock_join(flocks[randi() % 3], randi() % 3)
				4:
					if pick != null:
						var i := randi() % pick.get_amount_bullets()
						if pick.is_bullet_status_enabled(i):
							pick.disable_bullet(i)
				5:
					if pick != null:
						pick.set_is_auto_pooling_enabled(false)
						var i := randi() % pick.get_amount_bullets()
						if not pick.is_bullet_status_enabled(i):
							pick.enable_bullet(i)
				6:
					var f: BulletFlock2D = flocks[randi() % 3]
					f.cohesion = randf_range(0.0, 4.0)
					f.neighbor_range = randf_range(5.0, 200.0)
					f.max_neighbors = randi() % 40
				7:
					if pick != null and randi() % 4 == 0:
						pick.free()
				8:
					if randi() % 30 == 0:
						factory.clear_active_bullets()
		step_factory(1)
		for v in volleys:
			if is_instance_valid(v) and v.debug_get_life_state() == "active":
				for line in broken_bullets(v):
					problems.append("tick %d %s" % [tick, line])
		var dangling: Dictionary = factory.debug_assert_no_dangling()
		if not dangling["ok"]:
			problems.append("tick %d: %s" % [tick, dangling["error"]])
		if problems.size() > 5:
			break
	expect_no_errors("the storm is made of valid calls only")
	assert_eq("\n".join(problems.slice(0, 6)), "", "finite unit headings and a consistent factory every tick")
	assert_gt(factory.debug_get_flock_stats()["armed_volleys"], 0, "flocks were still flying at the end")
