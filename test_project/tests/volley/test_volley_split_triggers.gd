extends BlastTest
## The split triggers that do not kill a bullet: bounce (REAL physics, after
## the bounce handler, at the bounced pose: children fan around the NEW
## heading), a hit the bullet survives (piercing), reaching its homing target
## and grazing a target, each only with its switch on and only for the same
## live bullet after every handler. parent_survives decides whether the
## parent stays (it then splits at most max_splits_per_bullet times per life,
## 0 = no limit, counted over every trigger; a manual split ignores the
## limit) or is replaced by its children (no death effect of its own); the
## timer and manual splits honor it too; death triggers always take the
## parent; the split count comes back clean through the pool.

const DT := 1.0 / 60.0

var splits: Array = [] # [position, cause, children, depth]


func before_each() -> void:
	await super()
	splits.clear()
	factory.bullet_split.connect(func(pos, cause, children, depth): splits.append([pos, cause, children, depth]))


func _stage(count := 2) -> BulletSplitStage2D:
	var s := BulletSplitStage2D.new()
	s.count = count
	s.split_on_hit = false
	s.split_on_lifetime_over = false
	return s


func _chain(stage: BulletSplitStage2D) -> BulletSplitData2D:
	var d := BulletSplitData2D.new()
	d.stages = [stage]
	return d


func _quiet_child_data() -> BulletVolleyData2D:
	var d := H.make_volley_data(1, 0.0, 30.0)
	d.set_collision_mask_from_array([])
	return d


func _causes() -> Array:
	return splits.map(func(e): return e[1])


## Two bullets flying +X at 300 px/s: bullet 0 into a bounce wall at x = 120,
## bullet 1 past it (y = 300). The wall bounces (layer 8 = bit 4).
func _bouncing_volley(stage: BulletSplitStage2D) -> BulletVolley2D:
	make_wall(Vector2(120, 0), Vector2(20, 200), 8, 2)
	await physics()
	var d := H.make_volley_data(2, 300.0, 8.0)
	d.transforms = [Transform2D(0.0, Vector2(0, 0)), Transform2D(0.0, Vector2(0, 300))]
	d.monitorable = true
	d.set_collision_mask_from_array([4])
	d.set_bounce_mask_from_array([4])
	d.collision_shape = H.make_circle_shape(6.0)
	var v := factory.spawn_volley(d)
	v.split_set_data(_chain(stage), _quiet_child_data())
	return v


func _await_split(frames := 60) -> void:
	for i in frames:
		await physics()
		if not splits.is_empty():
			return


func test_bounce_splits_around_the_new_heading_and_the_parent_leaves() -> void:
	var s := _stage(3)
	s.split_on_bounce = true
	s.spread_degrees = 60.0
	var v := await _bouncing_volley(s)
	await _await_split()
	assert_eq(_causes(), [BulletVolley2D.SPLIT_CAUSE_BOUNCE], "one bounce split")
	var children: BulletVolley2D = splits[0][2]
	var headings: Array = []
	for i in 3:
		headings.append(snappedf(rad_to_deg(children.get_bullet_direction(i).angle()), 1.0))
	assert_eq(headings, [150.0, 180.0, -150.0], "the fan points back from the wall (the bounced heading)")
	assert_lt((splits[0][0] as Vector2).x, 120.0, "split on the near side of the wall")
	assert_false(v.is_bullet_status_enabled(0), "the parent was replaced")
	assert_true(v.is_bullet_status_enabled(1), "the other bullet flies on")


func test_bounce_never_splits_without_its_switch_or_after_a_handler_disables() -> void:
	var s := _stage()
	var v := await _bouncing_volley(s)
	for i in 40:
		await physics()
	assert_eq(splits.size(), 0, "split_on_bounce off: bounces never split")
	assert_true(int(v.bullet_get_bounce_count(0)) >= 1, "it did bounce")
	splits.clear()
	s.split_on_bounce = true
	var w := await _bouncing_volley(s)
	factory.bounce_body_entered.connect(func(_b, volley, idx): volley.disable_bullet(idx), CONNECT_ONE_SHOT)
	for i in 40:
		await physics()
	assert_eq(splits.size(), 0, "a handler that disabled the bullet owns it: no split")
	assert_false(w.is_bullet_status_enabled(0), "disabled by the handler")


func test_a_surviving_parent_splits_once_per_life_then_keeps_bouncing() -> void:
	var s := _stage(2)
	s.split_on_bounce = true
	s.parent_survives = true
	# Two walls: the bullet bounces back and forth.
	make_wall(Vector2(-120, 0), Vector2(20, 200), 8, 2)
	var v := await _bouncing_volley(s)
	for i in 120:
		await physics()
	assert_gt(int(v.bullet_get_bounce_count(0)), 1, "bounced more than once")
	assert_eq(splits.size(), 1, "max_splits_per_bullet 1: one split")
	assert_true(v.is_bullet_status_enabled(0), "the parent kept flying")
	s.max_splits_per_bullet = 0
	splits.clear()
	var bounces_before := int(v.bullet_get_bounce_count(0))
	for i in 120:
		await physics()
	var new_bounces := int(v.bullet_get_bounce_count(0)) - bounces_before
	assert_eq(splits.size(), new_bounces, "no limit: every later bounce splits")


func test_a_survived_hit_splits_a_piercing_bullet() -> void:
	var area := make_area(Vector2(100, 0), Vector2(20, 200))
	await physics()
	var d := H.make_volley_data(1, 300.0, 8.0)
	d.monitorable = true
	d.set_collision_mask_from_array([3])
	d.collision_shape = H.make_circle_shape(6.0)
	d.bullet_max_collision_count = 5
	var v := factory.spawn_volley(d)
	var s := _stage(2)
	s.split_on_non_lethal_hit = true
	s.parent_survives = true
	v.split_set_data(_chain(s), _quiet_child_data())
	factory.area_entered.connect(func(_a, _v, _i): pass)
	await _await_split()
	assert_eq(_causes(), [BulletVolley2D.SPLIT_CAUSE_NON_LETHAL_HIT], "split on the hit it survived")
	assert_true(v.is_bullet_status_enabled(0), "pierced on")
	assert_eq(v.get_bullet_collision_count(0), 1, "the hit counted")
	assert_not_null(area)


func test_reaching_the_homing_target_splits() -> void:
	var d := H.make_volley_data(2, 300.0, 8.0)
	d.transforms = [Transform2D(0.0, Vector2(0, 0)), Transform2D(0.0, Vector2(0, 300))]
	d.homing_distance_before_reached = 10.0
	var v := factory.spawn_volley(d)
	var s := _stage(4)
	s.split_on_homing_reached = true
	s.direction_mode = BulletSplitStage2D.DIRECTION_RING
	s.keep_homing = false
	v.split_set_data(_chain(s), _quiet_child_data())
	v.bullet_homing_push_back_homing_target(0, Vector2(60, 0))
	for i in 30:
		factory.debug_advance_time(DT)
		if not splits.is_empty():
			break
	assert_eq(_causes(), [BulletVolley2D.SPLIT_CAUSE_HOMING_REACHED], "burst at the target")
	assert_lt((splits[0][0] as Vector2).distance_to(Vector2(60, 0)), 11.0, "within the reach distance")
	assert_eq((splits[0][2] as BulletVolley2D).get_amount_bullets(), 4, "four children")
	assert_false(v.is_bullet_status_enabled(0), "the missile is replaced")
	assert_true(v.is_bullet_status_enabled(1), "the other bullet flies on")


func test_grazing_splits_once_per_graze() -> void:
	make_graze_target(Vector2(100, 20), &"split_grazers")
	factory.bullet_grazed.connect(func(_t, _v, _i, _z, _r): pass)
	var v := graze_volley([Transform2D(0.0, Vector2(0, 0))], 300.0)
	v.graze_set_zones([H.make_graze_zone([30.0])], &"split_grazers")
	var s := _stage(2)
	s.split_on_graze = true
	s.parent_survives = true
	v.split_set_data(_chain(s), _quiet_child_data())
	for i in 40:
		factory.debug_advance_time(DT)
	assert_eq(_causes(), [BulletVolley2D.SPLIT_CAUSE_GRAZE], "one graze, one split")
	assert_true(v.is_bullet_status_enabled(0), "the parent survived (parent_survives)")


func test_timer_and_manual_splits_honor_parent_survives_and_deaths_never_do() -> void:
	var s := _stage(2)
	s.split_after_sec = 0.05
	s.parent_survives = true
	s.max_splits_per_bullet = 0
	var data := H.make_volley_data(3, 0.0, 0.5)
	var v := factory.spawn_volley(data)
	v.split_set_data(_chain(s), _quiet_child_data())
	for i in 5:
		factory.debug_advance_time(DT)
	assert_eq(splits.size(), 3, "the timer split every bullet")
	assert_eq(int(v.debug_get_volley_info()["active_bullets"]), 3, "and every parent stayed")
	assert_eq(v.all_bullets_split(), 3, "a manual split of surviving parents")
	assert_eq(int(v.debug_get_volley_info()["active_bullets"]), 3, "they stay again")
	splits.clear()
	s.split_on_lifetime_over = true
	for i in 40:
		factory.debug_advance_time(DT)
	assert_eq(_causes().filter(func(c): return c == BulletVolley2D.SPLIT_CAUSE_LIFETIME_OVER).size(), 3, "expiry still splits")
	assert_eq(int(v.debug_get_volley_info()["active_bullets"]), 0, "and a death always takes the parent")


func test_the_limit_counts_every_trigger_and_manual_ignores_it() -> void:
	var s := _stage(2)
	s.parent_survives = true
	s.split_after_sec = 0.02
	s.split_on_lifetime_over = true
	var v := factory.spawn_volley(H.make_volley_data(1, 0.0, 0.2))
	v.split_set_data(_chain(s), _quiet_child_data())
	for i in 3:
		factory.debug_advance_time(DT)
	assert_eq(splits.size(), 1, "the timer used the one split")
	assert_true(v.split_bullet(0), "manual ignores the limit")
	factory.debug_advance_time(DT) # its children spawn (and report) on this sweep
	assert_eq(splits.size(), 2, "the manual split reported")
	splits.clear()
	for i in 20:
		factory.debug_advance_time(DT)
	assert_eq(splits.size(), 0, "expiry past the limit: no split, the bullet just dies")
	assert_false(v.is_bullet_status_enabled(0), "dead")


func test_the_split_count_comes_back_clean_through_the_pool() -> void:
	var s := _stage(2)
	s.parent_survives = true
	var data := H.make_volley_data(1, 0.0, 5.0)
	var v := factory.spawn_volley(data)
	v.split_set_data(_chain(s), _quiet_child_data())
	assert_true(v.split_bullet(0), "split once (count 1)")
	v.clear_all_bullets()
	factory.debug_advance_time(DT)
	assert_true(bool(v.debug_get_volley_info()["is_pooled"]), "pooled")
	var again := factory.spawn_volley(data)
	assert_eq(again, v, "reused")
	s.split_after_sec = 0.01
	again.split_set_data(_chain(s), _quiet_child_data())
	splits.clear()
	for i in 3:
		factory.debug_advance_time(DT)
	assert_eq(splits.size(), 1, "a new life starts with a fresh count")
	var state: Dictionary = factory.debug_assert_no_dangling()
	assert_true(state["ok"], str(state["error"]))
