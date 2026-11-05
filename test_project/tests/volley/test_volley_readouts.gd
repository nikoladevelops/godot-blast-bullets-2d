extends BlastTest
## Script readouts of a volley's live state: the live bullet count and
## indexes, whether the volley is alive, its life clock, how often each bullet
## split, the scale-in window, the pickup settings of a pickup volley and when
## an emitter bullet fires next. A pooled handle reads as empty (0, [], false,
## -1) and never errors.

const DT := 0.0625


func test_every_readout_is_bound() -> void:
	var v := quick_volley(2)
	for m in ["get_active_bullet_count", "get_active_bullet_indexes", "is_volley_active", "get_life_age", "bullet_get_split_count", "bullet_get_last_bounce_normal", "bullet_set_bounce_count", "get_scale_in_sec", "get_scale_in_from", "get_scale_in_factor", "get_pickup_data", "bullet_get_emitter_next_shot_sec", "debug_get_active_set_stats"]:
		assert_true(v.has_method(m), "BulletVolley2D.%s is bound" % m)


func test_live_count_indexes_and_clock_follow_the_bullets_into_the_pool() -> void:
	var v := quick_volley(4, 0.0, 30.0)
	assert_eq([v.get_active_bullet_count(), Array(v.get_active_bullet_indexes()), v.is_volley_active()], [4, [0, 1, 2, 3], true], "a fresh volley")
	assert_eq(v.get_life_age(), 0.0, "its clock starts at 0")
	step_factory(4, DT)
	assert_almost_eq(v.get_life_age(), 0.25, 0.0001, "four ticks of bullet time")
	v.disable_bullet(2)
	assert_eq([v.get_active_bullet_count(), Array(v.get_active_bullet_indexes())], [3, [0, 1, 3]], "one out")
	assert_eq(v.get_active_bullet_count(), v.get_all_bullets_status().count(true), "the count agrees with the statuses")
	for i in [0, 1, 3]:
		v.disable_bullet(i)
	assert_true(v.is_pooled(), "the last one out pools it")
	assert_eq([v.get_active_bullet_count(), Array(v.get_active_bullet_indexes()), v.is_volley_active(), v.get_life_age()], [0, [], false, 0.0], "a pooled handle reads empty")
	expect_no_errors()


func test_a_parked_volley_is_not_active_and_wakes() -> void:
	var v := quick_volley(2, 0.0, 30.0)
	v.set_is_auto_pooling_enabled(false)
	v.disable_bullet(0)
	v.disable_bullet(1)
	assert_true(v.is_parked(), "parked, not pooled")
	assert_eq([v.is_volley_active(), v.get_active_bullet_count()], [false, 0], "parked: not active")
	v.enable_bullet(1)
	assert_eq([v.is_volley_active(), Array(v.get_active_bullet_indexes())], [true, [1]], "woken")


func test_split_count_counts_the_splits_of_a_surviving_parent() -> void:
	var data := H.make_volley_data(2, 0.0, 30.0)
	var v := factory.spawn_volley(data)
	var stage := BulletSplitStage2D.new()
	stage.count = 2
	stage.parent_survives = true
	stage.split_on_lifetime_over = false
	var chain := BulletSplitData2D.new()
	chain.stages = [stage]
	assert_true(v.split_set_data(chain, data), "armed")
	assert_eq(v.bullet_get_split_count(0), 0, "no split yet")
	for i in 3:
		assert_true(v.split_bullet(0), "split %d" % i)
		factory.debug_advance_time(DT)
	assert_eq([v.bullet_get_split_count(0), v.bullet_get_split_count(1)], [3, 0], "three splits of bullet 0 only")
	assert_true(v.is_bullet_status_enabled(0), "and the parent survived")
	assert_eq(v.bullet_get_split_count(5), 0, "an index out of range reads 0")
	expect_error_sequence(["Invalid bullet index in bullet_get_split_count: 5 (amount_bullets: 2)"])


func test_scale_in_readouts_follow_the_window() -> void:
	var data := H.make_volley_data(1, 0.0, 30.0)
	data.scale_in_sec = 0.5
	data.scale_in_from = 0.2
	var v := factory.spawn_volley(data)
	assert_eq([v.get_scale_in_sec(), v.get_scale_in_from()], [0.5, 0.2], "the data's window")
	assert_almost_eq(v.get_scale_in_factor(), 0.2, 0.0001, "spawned at scale_in_from")
	step_factory(4, DT) # half the window
	assert_almost_eq(v.get_scale_in_factor(), 0.6, 0.0001, "halfway grown")
	step_factory(8, DT)
	assert_eq(v.get_scale_in_factor(), 1.0, "full size at the end")
	assert_eq(quick_volley(1).get_scale_in_factor(), 1.0, "no window: full size")


func test_a_pickup_volley_reports_its_pickup_data() -> void:
	var v := quick_volley(2, 0.0, 30.0)
	assert_null(v.get_pickup_data(), "a bullet volley carries no pickup")
	var target := Node2D.new()
	target.position = Vector2(400, 0)
	add(target)
	var coin := BulletPickupData2D.new()
	coin.value = 5
	var out: Array = v.convert_to_pickups(coin, target)
	assert_false(out.is_empty(), "converted")
	var pickups: BulletVolley2D = out[0]
	assert_same(pickups.get_pickup_data(), coin, "the pickup volley reports its settings")


func test_emitter_next_shot_counts_down_to_each_shot() -> void:
	var tpl := BulletSpawner2D.new()
	tpl.set_shooting_enabled(false)
	tpl.set_spawn_data(H.make_volley_data(1, 0.0, 30.0))
	tpl.helper_bullets_amount = 1
	add(tpl)
	tpl.set_bullet_factory(factory)
	var timing := BulletEmitterData2D.new()
	timing.start_delay_sec = 0.25
	timing.interval_sec = 0.5
	timing.max_shots = 2
	var v := factory.spawn_volley(H.make_volley_data(1, 0.0, 30.0))
	assert_eq(v.bullet_get_emitter_next_shot_sec(0), -1.0, "not an emitter: -1")
	assert_true(v.emitter_set(timing, tpl), "armed")
	assert_almost_eq(v.bullet_get_emitter_next_shot_sec(0), 0.25, 0.0001, "the start delay")
	step_factory(2, DT)
	assert_almost_eq(v.bullet_get_emitter_next_shot_sec(0), 0.125, 0.0001, "counts down in bullet time")
	var shots := []
	for i in 20:
		step_factory(1, DT)
		shots.append(v.get_bullet_emitter_shots(0))
	assert_eq(v.get_bullet_emitter_shots(0), 2, "max_shots fired")
	assert_eq(v.bullet_get_emitter_next_shot_sec(0), -1.0, "done firing: -1")
	timing.max_shots = 0
	assert_true(v.bullet_get_emitter_next_shot_sec(0) >= 0.0, "a raised cap schedules it again")
	v.emitter_clear()
	assert_eq(v.bullet_get_emitter_next_shot_sec(0), -1.0, "disarmed: -1")


## The volley's own active-bullet sparse set, read out as data: the active
## count, its index space, the live counter the ticker uses and the active
## indexes (the order the per-bullet loops walk: the dense vector). Reads
## empty on a pooled handle, exactly like the other readouts.
func test_active_set_stats_agree_with_the_live_counter() -> void:
	var v := quick_volley(6, 0.0, 30.0)
	var st: Dictionary = v.debug_get_active_set_stats()
	assert_eq(st["active"], v.debug_get_volley_info()["active_bullets"], "a fresh volley: every bullet active")
	assert_eq(st["amount_bullets"], 6, "the volley's bullet count")
	assert_eq(Array(st["active_indexes"]), [0, 1, 2, 3, 4, 5], "all six, in index order")
	assert_gte(st["capacity"], st["active"], "the index space covers the active indexes")
	v.disable_bullet(2)
	var live := Array(v.get_active_bullet_indexes())
	live.sort()
	st = v.debug_get_active_set_stats()
	var indexes := Array(st["active_indexes"])
	indexes.sort()
	assert_eq(st["active"], v.debug_get_volley_info()["active_bullets"], "the count matches the live counter")
	assert_eq(st["active"], 5, "one disabled")
	assert_eq(indexes, live, "the indexes match get_active_bullet_indexes()")
	v.enable_bullet(2)
	st = v.debug_get_active_set_stats()
	assert_eq(st["active"], v.debug_get_volley_info()["active_bullets"], "re-enabled: still in step")
	for i in 6:
		v.disable_bullet(i)
	assert_true(v.is_pooled(), "the last one out pools it")
	st = v.debug_get_active_set_stats()
	assert_eq([st["active"], Array(st["active_indexes"])], [0, []], "a pooled handle reads empty")
	expect_no_errors()


## A life that did everything (a hit, a bounce, a split, a homing reach and a
## graze) leaves nothing for the reused instance: before its first tick the new
## life counts 0 hits / bounces / splits, holds no homing target, has an empty
## dedup window and no queued events, and its generation moved exactly one.
func _plain_three() -> BulletVolleyData2D:
	var d := H.make_volley_data(3, 300.0, 60.0)
	var transforms: Array = []
	for i in 3:
		transforms.append(Transform2D(0.0, Vector2(24.0 * i, 0.0)))
	d.transforms = transforms
	d.collision_shape = H.make_circle_shape(6.0)
	return d


func test_a_rich_life_leaves_no_counters_for_the_reused_volley() -> void:
	handle_events(factory)
	var target := make_graze_target(Vector2(60, 0))
	var reached: Array = [] # lambda locals are captured by value: collect through an Array
	make_wall(Vector2(200, 0), Vector2(20, 400), 4)
	var d := H.make_volley_data(3, 300.0, 60.0)
	d.transforms = [Transform2D(0.0, Vector2(-40, 0)), Transform2D(0.0, Vector2(-40, 30)), Transform2D(0.0, Vector2(-40, -30))]
	d.set_collision_mask_from_array([3])
	d.collision_shape = H.make_circle_shape(6.0)
	d.bullet_max_collision_count = 0
	d.monitorable = true
	d.bounce_mask = 4
	d.bounce_strength = 0.5
	d.bounce_hit_consumed = true # a consumed bounce counts exactly one hit
	d.bounce_mode = BulletVolleyData2D.BOUNCE_PRECISE_SHAPE
	var v: BulletVolley2D = factory.spawn_volley(d)
	v.graze_set_zones([H.make_graze_zone([40.0, 300.0])], &"graze_targets")
	v.bullet_homing_target_reached.connect(func(_volley: BulletVolley2D, index: int, _t: Node2D, _p: Vector2) -> void: reached.append(index))
	var stage := BulletSplitStage2D.new()
	stage.parent_survives = false
	stage.split_on_lifetime_over = false
	var chain := BulletSplitData2D.new()
	chain.stages = [stage]
	assert_true(v.split_set_data(chain, d), "split chain armed")
	await physics(2)
	v.bullet_homing_push_back_node2d_target(0, target)
	for i in 120:
		await physics()
		if v.get_bullet_collision_count(1) > 0 and not reached.is_empty():
			break
	assert_gte(v.get_bullet_collision_count(1), 1, "the life registered a hit")
	assert_gte(v.bullet_get_bounce_count(1), 1, "and a bounce")
	assert_gte(reached.size(), 1, "and a homing reach")
	assert_gt(int(factory.debug_get_graze_stats()["events_total"]), 0, "and a graze")
	assert_true(v.split_bullet(2), "and a manual split of bullet 2")
	assert_eq(v.bullet_get_split_count(2), 1, "counted on that bullet")
	await physics(2) # the split flush spawns the children (a volley of its own)
	var life := v.get_life_id()
	var left := Array(v.get_active_bullet_indexes())
	assert_eq(left.size(), 2, "the split took bullet 2 out of this volley")
	for i in left:
		v.disable_bullet(i)
	await idle(1)
	assert_true(v.is_pooled(), "the volley pooled itself")
	var reused: BulletVolley2D = factory.spawn_volley(_plain_three())
	assert_same(reused, v, "the pool handed it back")
	assert_eq(reused.get_life_id(), life + 1, "one generation later")
	assert_true(reused.is_volley_active(), "active before its first tick")
	for i in 3:
		assert_eq(reused.get_bullet_collision_count(i), 0, "bullet %d starts with no hits" % i)
		assert_eq(reused.bullet_get_bounce_count(i), 0, "bullet %d starts with no bounces" % i)
		assert_eq(reused.bullet_get_split_count(i), 0, "bullet %d starts with no splits" % i)
		assert_false(reused.debug_get_bullet_info(i)["has_homing_targets"], "bullet %d holds no homing target" % i)
	assert_eq(int(reused.debug_get_graze_info()["pending_events"]), 0, "no queued graze events")
	assert_false(reused.is_graze_armed(), "and no zones")
	assert_eq(int(reused.debug_dedup_stats()["used"]), 0, "an empty dedup window")
	assert_eq(reused.get_active_bullet_count(), 3, "all three bullets live")
	step_factory(3, DT)
	expect_no_errors()
