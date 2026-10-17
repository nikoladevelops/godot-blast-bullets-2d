extends BlastTest
## Splitting, one generation (BulletSplitStage2D on a factory volley armed
## with split_set_data): every trigger splits exactly once and only with its
## switch on (hit after the handler's veto, lifetime after an extension's
## veto, the volley timer, despawn, clear, manual); a parent becomes exactly
## `count` children in ONE new volley at the pose it died at, spawned in the
## same sweep; directions (Fan, Ring, Random In Spread, relative or world,
## jitter), speed modes and ramps, offset and scale; the next tick moves each
## child by its reported velocity; seeded splits repeat; chance 0 never
## splits; bullet_split reports position, cause, children and depth; the
## On Split effect and sound fire at the parent and fire_death_effects
## silences the parent's own death effects.

const DT := 1.0 / 60.0

var splits: Array = [] # [position, cause, children, depth] per bullet_split


func before_each() -> void:
	await super()
	splits.clear()
	factory.bullet_split.connect(func(pos, cause, children, depth): splits.append([pos, cause, children, depth]))


func _stage(count := 3) -> BulletSplitStage2D:
	var s := BulletSplitStage2D.new()
	s.count = count
	return s


func _chain(stages: Array) -> BulletSplitData2D:
	var d := BulletSplitData2D.new()
	d.stages = stages
	return d


## A factory volley of n bullets at (24 i, 0) flying +X, armed with `stages`.
func _armed(stages: Array, n := 1, speed := 100.0, lifetime := 0.1) -> BulletVolley2D:
	var data := H.make_volley_data(n, speed, lifetime)
	var v := factory.spawn_volley(data)
	assert_true(v.split_set_data(_chain(stages), data), "armed")
	return v


## Steps until the first split is reported (at most `ticks`).
func _step_until_split(ticks := 30) -> void:
	for i in ticks:
		factory.debug_advance_time(DT)
		if not splits.is_empty():
			return


func _angles(v: BulletVolley2D) -> Array:
	var out: Array = []
	for i in v.get_amount_bullets():
		out.append(snappedf(rad_to_deg(v.get_bullet_direction(i).angle()), 0.01))
	return out


func test_lifetime_over_splits_each_bullet_into_one_volley_of_count_children() -> void:
	var v := _armed([_stage(3)], 2, 0.0)
	var poses := origins(v)
	_step_until_split()
	assert_eq(splits.size(), 2, "one bullet_split per parent")
	for k in splits.size():
		var children: BulletVolley2D = splits[k][2]
		assert_eq(children.get_amount_bullets(), 3, "exactly count children")
		assert_eq(splits[k][1], BulletVolley2D.SPLIT_CAUSE_LIFETIME_OVER, "cause")
		assert_eq(splits[k][3], 1, "first generation")
		assert_eq(splits[k][0], poses[k], "reported where the parent died")
		for c in 3:
			assert_eq(children.get_bullet_transform(c).origin, poses[k], "child %d spawned at the parent's pose" % c)
		assert_ne(children, v, "a new volley")
		assert_eq(children.split_get_depth(), 1, "children know their generation")
	assert_eq(factory.get_pending_split_count(), 0, "nothing left queued")


func test_children_spawn_in_the_sweep_the_parent_died_and_tick_from_the_next() -> void:
	var v := _armed([_stage(2)], 1, 100.0, 0.05)
	factory.debug_advance_time(0.03)
	assert_true(splits.is_empty(), "alive")
	factory.debug_advance_time(0.03) # expires in this sweep
	assert_eq(splits.size(), 1, "split in the same sweep")
	var children: BulletVolley2D = splits[0][2]
	var at: Vector2 = children.get_bullet_transform(0).origin
	assert_eq(at, splits[0][0], "not moved yet")
	factory.debug_advance_time(0.1)
	assert_almost_eq(children.get_bullet_transform(0).origin.distance_to(at), 10.0, 0.01, "first move on the next sweep: 100 px/s x 0.1 s")
	assert_false(v.is_bullet_status_enabled(0), "the parent is gone")


func test_fan_spreads_around_the_parent_heading() -> void:
	var s := _stage(3)
	s.spread_degrees = 90.0
	var v := _armed([s])
	v.set_bullet_direction(0, Vector2.DOWN)
	_step_until_split()
	assert_eq(_angles(splits[0][2]), [45.0, 90.0, 135.0], "fan centered on the parent's heading (down)")


func test_world_relative_ring_ignores_the_parent_heading() -> void:
	var s := _stage(4)
	s.direction_mode = BulletSplitStage2D.DIRECTION_RING
	s.relative_to_parent_heading = false
	s.angle_offset_degrees = 45.0
	var v := _armed([s])
	v.set_bullet_direction(0, Vector2.DOWN)
	_step_until_split()
	assert_eq(_angles(splits[0][2]), [45.0, 135.0, -135.0, -45.0], "ring from the world offset")


func test_random_in_spread_stays_inside_the_spread_and_jitter_is_bounded() -> void:
	var s := _stage(32)
	s.direction_mode = BulletSplitStage2D.DIRECTION_RANDOM_IN_SPREAD
	s.spread_degrees = 40.0
	s.angle_jitter_degrees = 5.0
	s.seed = 7
	_armed([s])
	_step_until_split()
	var bad: Array = []
	for a in _angles(splits[0][2]):
		if absf(a) > 25.0 + 0.01:
			bad.append(a)
	assert_eq(bad, [], "every child within +-(spread/2 + jitter) of the heading")


func test_seeded_splits_repeat_and_other_seeds_differ() -> void:
	var runs: Array = []
	for seed_value in [11, 11, 12]:
		splits.clear()
		var s := _stage(6)
		s.direction_mode = BulletSplitStage2D.DIRECTION_RANDOM_IN_SPREAD
		s.speed_distribution = BulletSplitStage2D.SPEED_RANDOM
		s.speed_multiplier_min = 0.5
		s.speed_multiplier_max = 2.0
		s.seed = seed_value
		_armed([s])
		_step_until_split()
		var children: BulletVolley2D = splits[0][2]
		var speeds: Array = []
		for i in children.get_amount_bullets():
			speeds.append(snappedf(children.get_bullet_speed_data(i).speed, 0.001))
		runs.append([_angles(children), speeds])
	assert_eq(runs[0], runs[1], "same seed, same split")
	assert_ne(runs[0], runs[2], "another seed, another split")


func test_speed_modes_and_ramp() -> void:
	var multiply := _stage(3)
	multiply.speed_multiplier_min = 1.0
	multiply.speed_multiplier_max = 2.0
	_armed([multiply], 1, 100.0)
	_step_until_split()
	var c: BulletVolley2D = splits[0][2]
	assert_eq([c.get_bullet_speed_data(0).speed, c.get_bullet_speed_data(1).speed, c.get_bullet_speed_data(2).speed], [100.0, 150.0, 200.0], "parent speed ramped x1..x2")
	assert_eq(c.get_bullet_speed_data(2).max_speed, 6000.0, "top speed scaled with it")
	splits.clear()
	var fixed := _stage(1)
	fixed.speed_mode = BulletSplitStage2D.SPEED_FIXED
	fixed.fixed_speed = 40.0
	_armed([fixed], 1, 100.0)
	_step_until_split()
	assert_eq((splits[0][2] as BulletVolley2D).get_bullet_speed_data(0).speed, 40.0, "fixed speed")
	splits.clear()
	var from_data := _stage(1)
	from_data.speed_mode = BulletSplitStage2D.SPEED_FROM_SPAWN_DATA
	from_data.speed_multiplier_min = 0.5
	from_data.speed_multiplier_max = 0.5
	from_data.child_data = H.make_volley_data(1, 30.0, 5.0)
	_armed([from_data], 1, 100.0)
	_step_until_split()
	assert_eq((splits[0][2] as BulletVolley2D).get_bullet_speed_data(0).speed, 15.0, "the child data's speed x0.5")


func test_every_child_moves_by_its_reported_velocity() -> void:
	var s := _stage(5)
	s.direction_mode = BulletSplitStage2D.DIRECTION_RING
	s.speed_multiplier_min = 0.5
	s.speed_multiplier_max = 3.0
	_armed([s], 1, 120.0)
	_step_until_split()
	var c: BulletVolley2D = splits[0][2]
	var before := origins(c)
	var velocities: Array = []
	for i in c.get_amount_bullets():
		velocities.append(c.get_bullet_velocity(i))
	factory.debug_advance_time(DT)
	var bad: Array = []
	for i in c.get_amount_bullets():
		var moved: Vector2 = c.get_bullet_transform(i).origin - before[i]
		if moved.distance_to(velocities[i] * DT) > 0.01:
			bad.append("%d: moved %s, velocity %s" % [i, moved, velocities[i]])
	assert_eq(bad, [], "position += velocity x dt for every child")
	assert_eq(broken_bullets(c), [], "finite poses, unit headings")


func test_offset_and_scale_shape_the_children() -> void:
	var s := _stage(4)
	s.direction_mode = BulletSplitStage2D.DIRECTION_RING
	s.spawn_offset_px = 10.0
	s.scale_multiplier = 0.5
	var v := _armed([s], 1, 0.0)
	var at: Vector2 = v.get_bullet_transform(0).origin
	_step_until_split()
	var c: BulletVolley2D = splits[0][2]
	for i in 4:
		var t: Transform2D = c.get_bullet_transform(i)
		assert_almost_eq(t.origin.distance_to(at), 10.0, 0.001, "child %d sits 10 px out" % i)
		assert_almost_eq(t.get_scale().x, 0.5, 0.001, "child %d at half size" % i)
	assert_gt(min_pair_distance(origins(c)), 0.5, "no two children stacked")


func test_chance_zero_never_splits_and_manual_ignores_it() -> void:
	var s := _stage(2)
	s.chance = 0.0
	var v := _armed([s], 2, 0.0, 5.0)
	assert_true(v.split_bullet(0), "a manual split ignores the odds")
	v.set_life_time_left(0.01)
	_step_until_split(3)
	for i in 3:
		factory.debug_advance_time(DT)
	assert_eq(splits.size(), 1, "only the manual split; expiry with chance 0 never splits")
	assert_eq(splits[0][1], BulletVolley2D.SPLIT_CAUSE_MANUAL, "manual cause")


func test_extending_the_lifetime_in_the_handler_vetoes_the_split() -> void:
	var data := H.make_volley_data(1, 0.0, 0.05)
	data.is_life_time_over_signal_enabled = true
	var v := factory.spawn_volley(data)
	v.split_set_data(_chain([_stage(2)]), data)
	factory.life_time_over.connect(func(volley, _idx): volley.set_life_time_left(5.0), CONNECT_ONE_SHOT)
	for i in 6:
		factory.debug_advance_time(DT)
	assert_eq(splits.size(), 0, "no expiry, no split")
	assert_true(v.is_bullet_status_enabled(0), "alive")


func test_timer_splits_the_whole_volley_once() -> void:
	var s := _stage(2)
	s.split_on_lifetime_over = false
	s.split_after_sec = 0.1
	var v := _armed([s], 3, 50.0, 5.0)
	for i in 5:
		factory.debug_advance_time(DT)
	assert_eq(splits.size(), 0, "before the timer")
	for i in 3:
		factory.debug_advance_time(DT)
	assert_eq(splits.size(), 3, "every live bullet split at 0.1 s")
	for e in splits:
		assert_eq(e[1], BulletVolley2D.SPLIT_CAUSE_TIMER, "timer cause")
	assert_eq(int(v.debug_get_volley_info()["active_bullets"]), 0, "the parents left")
	for i in 20:
		factory.debug_advance_time(DT)
	assert_eq(splits.size(), 3, "children of a one-stage chain never split again")


func test_clear_and_despawn_split_only_when_switched_on() -> void:
	var s := _stage(2)
	var v := _armed([s], 2, 0.0, 5.0)
	v.clear_all_bullets()
	factory.debug_advance_time(DT)
	assert_eq(splits.size(), 0, "clears never split by default")
	s.split_on_clear = true
	var w := _armed([s], 2, 0.0, 5.0)
	w.clear_bullet(1)
	factory.debug_advance_time(DT)
	assert_eq(splits.size(), 1, "split_on_clear: the cleared bullet split")
	assert_eq(splits[0][1], BulletVolley2D.SPLIT_CAUSE_CLEAR, "clear cause")
	splits.clear()
	factory.despawn_bounds_rect = Rect2(-50, -50, 100, 100)
	factory.despawn_bounds_enabled = true
	var d := _stage(2)
	d.split_on_lifetime_over = false
	var leaving := _armed([d], 1, 3000.0, 5.0)
	for i in 3:
		factory.debug_advance_time(DT)
	assert_false(leaving.is_bullet_status_enabled(0), "culled")
	assert_eq(splits.size(), 0, "despawn never splits by default")
	d.split_on_despawn = true
	_armed([d], 1, 3000.0, 5.0)
	for i in 3:
		factory.debug_advance_time(DT)
	assert_eq(splits.size(), 1, "split_on_despawn")
	assert_eq(splits[0][1], BulletVolley2D.SPLIT_CAUSE_DESPAWN, "despawn cause")


func test_pickup_conversion_reset_and_free_never_split() -> void:
	var s := _stage(2)
	s.split_on_clear = true
	_armed([s], 2, 0.0, 5.0)
	var pickup := BulletPickupData2D.new()
	pickup.fire_clear_effects = true
	factory.convert_all_bullets_to_pickups(pickup, Vector2.ZERO)
	_armed([s], 2, 0.0, 0.01)
	factory.debug_advance_time(DT) # expires: two splits queued, then spawned
	splits.clear()
	_armed([s], 2, 0.0, 5.0).clear_all_bullets() # queued for the next sweep
	assert_eq(factory.get_pending_split_count(), 2, "queued")
	factory.free_active_bullets()
	assert_eq(factory.get_pending_split_count(), 0, "free_active_bullets drops queued splits")
	_armed([s], 1, 0.0, 5.0).clear_all_bullets()
	factory.reset()
	assert_eq(factory.get_pending_split_count(), 0, "reset drops them too")
	factory.debug_advance_time(DT)
	assert_eq(splits.size(), 0, "converted, freed and reset bullets never split")


func test_split_effects_and_death_effect_silencing() -> void:
	factory.debug_set_effect_log_enabled(true)
	var s := _stage(2)
	var data := H.make_volley_data(1, 0.0, 0.03)
	data.effect_layers = [H.make_effect_layer(BulletEffectLayerData2D.EFFECT_ON_SPLIT), H.make_effect_layer(BulletEffectLayerData2D.EFFECT_ON_LIFETIME_OVER)]
	var v := factory.spawn_volley(data)
	v.split_set_data(_chain([s]), H.make_volley_data(1, 0.0, 5.0))
	_step_until_split()
	var kinds: Array = factory.debug_get_effect_log().map(func(e): return int(e["trigger"]))
	assert_eq(kinds, [BulletEffectLayerData2D.EFFECT_ON_SPLIT, BulletEffectLayerData2D.EFFECT_ON_LIFETIME_OVER], "On Split, then the parent's own death effect")
	factory.debug_clear_effect_log()
	s.fire_death_effects = false
	var w := factory.spawn_volley(data)
	w.split_set_data(_chain([s]), H.make_volley_data(1, 0.0, 5.0))
	splits.clear()
	_step_until_split()
	kinds = factory.debug_get_effect_log().map(func(e): return int(e["trigger"]))
	assert_eq(kinds, [BulletEffectLayerData2D.EFFECT_ON_SPLIT], "fire_death_effects off: only On Split")


func test_split_set_data_and_manual_api_contract() -> void:
	var v := quick_volley(3, 0.0, 5.0)
	assert_true(v.has_method("split_set_data"), "bound")
	assert_false(v.split_set_data(null))
	expect_error_sequence(["split_set_data: split must be a BulletSplitData2D (split_clear() stops splitting), nothing changed."])
	assert_false(v.split_set_data(_chain([_stage()])))
	expect_error_sequence(["split_set_data: this volley has no spawn data to copy (pass child_spawn_data), nothing changed."])
	assert_false(v.split_bullet(0), "not armed: nothing splits")
	assert_true(v.is_bullet_status_enabled(0), "and the bullet stays")
	assert_true(v.split_set_data(_chain([_stage(2)]), H.make_volley_data(1, 0.0, 5.0)))
	assert_eq(v.split_get_depth(), 0, "generation 0")
	assert_not_null(v.split_get_data(), "armed")
	assert_false(v.split_bullet(9))
	expect_errors_containing("split_bullet", 1, "OOB index")
	assert_eq(v.all_bullets_split(2, 0), 0, "inverted range")
	expect_errors_containing("Invalid index range in all_bullets_split", 1, "range contract")
	assert_eq(v.all_bullets_split(), 3, "every live bullet split")
	factory.debug_advance_time(DT)
	assert_eq(splits.size(), 3, "three parents reported")
	var w := quick_volley(1, 0.0, 5.0)
	w.split_set_data(_chain([_stage(2)]), H.make_volley_data(1, 0.0, 5.0))
	w.split_clear()
	assert_null(w.split_get_data(), "cleared")
	assert_false(w.split_bullet(0), "cleared volleys never split")


func test_hit_split_after_the_handler_and_a_heal_vetoes_it() -> void:
	make_wall(Vector2(200, 0))
	await physics()
	var data := H.make_volley_data(2, 300.0, 8.0)
	data.transforms = [Transform2D(0.0, Vector2(0, 0)), Transform2D(0.0, Vector2(0, 60))]
	data.monitorable = true
	data.set_collision_mask_from_array([3])
	data.collision_shape = H.make_circle_shape(6.0)
	data.bullet_max_collision_count = 1
	var s := _stage(3)
	s.direction_mode = BulletSplitStage2D.DIRECTION_RING
	var v := factory.spawn_volley(data)
	var child_data := H.make_volley_data(1, 0.0, 5.0)
	child_data.set_collision_mask_from_array([]) # children never hit the wall again
	v.split_set_data(_chain([s]), child_data)
	# Bullet 1 is healed in the handler: it survives and never splits.
	factory.body_entered.connect(func(_body, volley, idx):
		if idx == 1:
			volley.set_bullet_collision_count(1, 0))
	for i in 60:
		await physics()
		if not v.is_bullet_status_enabled(0) and splits.size() > 0:
			break
	assert_eq(splits.size(), 1, "only the killed bullet split")
	assert_eq(splits[0][1], BulletVolley2D.SPLIT_CAUSE_HIT, "hit cause")
	assert_almost_eq((splits[0][0] as Vector2).y, 0.0, 0.001, "bullet 0 split")
	assert_gt((splits[0][0] as Vector2).x, 150.0, "at the impact pose near the wall")
	assert_eq((splits[0][2] as BulletVolley2D).get_amount_bullets(), 3, "three children")


func test_a_manual_split_that_cannot_split_keeps_the_bullet() -> void:
	factory.debug_set_effect_log_enabled(true)
	var data := H.make_volley_data(2, 0.0, 5.0)
	data.effect_layers = [H.make_effect_layer(BulletEffectLayerData2D.EFFECT_ON_CLEAR)]
	var v := factory.spawn_volley(data)
	var chain := _chain([_stage(2)])
	v.split_set_data(chain, data)
	chain.stages = [] # no stage for generation 0 any more
	assert_false(v.split_bullet(0), "nothing to split by")
	assert_eq(v.all_bullets_split(), 0, "nor for the range")
	assert_true(v.is_bullet_status_enabled(0) and v.is_bullet_status_enabled(1), "the bullets stay")
	assert_eq(factory.debug_get_effect_log().size(), 0, "no On Clear for a bullet that never left")
	var pickup := factory.convert_all_bullets_to_pickups(BulletPickupData2D.new(), Vector2.ZERO)
	var coin: BulletVolley2D = pickup[0]
	assert_false(coin.split_set_data(_chain([_stage()]), data), "pickups never split")
	expect_error_sequence(["split_set_data: pickups never split, nothing changed."])
