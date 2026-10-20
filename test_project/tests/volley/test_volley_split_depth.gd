extends BlastTest
## Split chains: stage d splits generation d and the chain ends where the
## stages end (or at max_depth with repeat_last_stage); each generation
## splits one sweep after the one before it (children spawn after the sweep
## their parent died in and tick from the next); a stage's child_data
## replaces the spawn data from its generation on; the factory's per-sweep
## children cap carries splits over in order and never drops one (a first
## split larger than the cap still spawns); edits to the chain reach bullets
## in flight.

const DT := 1.0 / 60.0

var splits: Array = [] # [position, cause, children, depth, sweep]
var sweep := 0


func before_each() -> void:
	await super()
	splits.clear()
	sweep = 0
	factory.bullet_split.connect(func(pos, cause, children, depth): splits.append([pos, cause, children, depth, sweep]))


func _stage(count: int) -> BulletSplitStage2D:
	var s := BulletSplitStage2D.new()
	s.count = count
	s.direction_mode = BulletSplitStage2D.DIRECTION_RING
	return s


func _chain(stages: Array) -> BulletSplitData2D:
	var d := BulletSplitData2D.new()
	d.stages = stages
	return d


## One still bullet whose every generation expires after one tick (lifetime
## shorter than a tick: children copy the data, so they expire on their
## first tick too).
func _armed(chain: BulletSplitData2D, n := 1) -> BulletVolley2D:
	var data := H.make_volley_data(n, 0.0, DT * 0.5)
	var v := factory.spawn_volley(data)
	assert_true(v.split_set_data(chain, data), "armed")
	return v


func _run(ticks: int) -> void:
	for i in ticks:
		sweep += 1
		factory.debug_advance_time(DT)


func _depths() -> Array:
	return splits.map(func(e): return e[3])


func test_each_stage_splits_its_generation_and_the_chain_ends() -> void:
	_armed(_chain([_stage(2), _stage(3)]))
	_run(6)
	assert_eq(_depths(), [1, 2, 2], "one parent -> 2 children -> 3 grandchildren each; generation 2 has no stage")
	assert_eq((splits[0][2] as BulletVolley2D).get_amount_bullets(), 2, "stage 0 count")
	assert_eq((splits[1][2] as BulletVolley2D).get_amount_bullets(), 3, "stage 1 count")
	assert_eq(splits.map(func(e): return e[4]), [1, 2, 2], "one generation per sweep")


func test_repeat_last_stage_runs_until_max_depth() -> void:
	var chain := _chain([_stage(2)])
	chain.repeat_last_stage = true
	chain.max_depth = 3
	_armed(chain)
	_run(8)
	assert_eq(_depths(), [1, 2, 2, 3, 3, 3, 3], "generations 0, 1 and 2 split (1 + 2 + 4 parents)")
	assert_eq(factory.get_pending_split_count(), 0, "generation 3 never split")


func test_an_empty_stage_entry_stops_the_chain() -> void:
	_armed(_chain([_stage(2), null, _stage(2)]))
	_run(5)
	assert_eq(_depths(), [1], "generation 1 has an empty entry: it never splits, nor do later ones")


func test_child_data_replaces_the_spawn_data_from_its_generation() -> void:
	var first := _stage(1)
	first.speed_mode = BulletSplitStage2D.SPEED_FROM_SPAWN_DATA
	first.child_data = H.make_volley_data(1, 77.0, DT * 0.5)
	_armed(_chain([first, _stage(1)]))
	_run(4)
	assert_eq(_depths(), [1, 2], "two generations")
	assert_eq((splits[0][2] as BulletVolley2D).get_bullet_speed_data(0).speed, 77.0, "generation 1 spawns from child_data (its per-bullet speed entry used exactly)")
	assert_eq((splits[1][2] as BulletVolley2D).get_bullet_speed_data(0).speed, 77.0, "generation 2 copies its parent (child_data) and its parent's speed")


func test_the_children_cap_carries_splits_over_and_never_drops_one() -> void:
	factory.split_max_children_per_sweep = 4
	_armed(_chain([_stage(2)]), 3) # 3 parents x 2 children, cap 4
	_run(1)
	assert_eq(splits.size(), 2, "two parents fit under the cap")
	assert_eq(factory.get_pending_split_count(), 1, "the third waits")
	assert_eq(int(factory.debug_get_split_stats()["carried_over_last"]), 1, "reported as carried over")
	_run(1)
	assert_eq(splits.size(), 3, "spawned on the next sweep")
	factory.split_max_children_per_sweep = 1
	_armed(_chain([_stage(3)]))
	_run(1)
	assert_eq(splits.size(), 4, "a split larger than the cap still spawns when it comes first")
	factory.split_max_children_per_sweep = -1
	expect_error_sequence(["BulletFactory2D: split_max_children_per_sweep must be >= 0 (0 = no cap), keeping the old value."])
	assert_eq(factory.split_max_children_per_sweep, 1, "kept")


func test_chain_edits_reach_bullets_in_flight() -> void:
	var stage := _stage(2)
	var chain := _chain([stage])
	var data := H.make_volley_data(1, 0.0, 0.1)
	var v := factory.spawn_volley(data)
	v.split_set_data(chain, data)
	stage.count = 5 # edited after arming
	_run(10)
	assert_eq(splits.size(), 1, "split")
	assert_eq((splits[0][2] as BulletVolley2D).get_amount_bullets(), 5, "the edited count")
	splits.clear()
	var w := factory.spawn_volley(data)
	w.split_set_data(chain, data)
	chain.stages = [] # the chain emptied in flight
	_run(10)
	assert_eq(splits.size(), 0, "an emptied chain never splits")


func test_a_huge_split_follows_the_pattern_limit() -> void:
	BulletPatterns2D.set_max_bullets_per_pattern(64)
	var s := _stage(64)
	BulletPatterns2D.set_max_bullets_per_pattern(16) # lowered after the stage was set
	_armed(_chain([s]))
	_run(2)
	assert_eq((splits[0][2] as BulletVolley2D).get_amount_bullets(), 16, "children never exceed the current pattern limit")


func test_children_of_per_bullet_data_take_their_parents_values_without_warnings() -> void:
	# The parent's data has one entry per parent bullet: entry i means
	# nothing to child i. Children spawn without the per-bullet arrays (no
	# size warning) and take their parent bullet's own values instead.
	var data := H.make_volley_data(2, 0.0, DT * 0.5)
	data.all_bullet_rotation_data = [H.make_rotation(1.0, 10.0), H.make_rotation(3.0, 10.0)]
	data.all_bullet_gravity = [Vector2(0, 5), Vector2(0, 7)]
	data.bullets_current_collision_count = [0, 0]
	var v := factory.spawn_volley(data)
	var stage := _stage(4)
	stage.speed_mode = BulletSplitStage2D.SPEED_FROM_SPAWN_DATA
	v.split_set_data(_chain([stage]), data)
	_run(1)
	expect_no_errors("no per-bullet array size warning for the children")
	assert_eq(splits.size(), 2, "both split")
	var problems: Array = []
	for k in 2:
		var c: BulletVolley2D = splits[k][2]
		for i in 4:
			if not is_equal_approx(c.bullet_get_rotation_speed(i), [1.0, 3.0][k]):
				problems.append("parent %d child %d spin %s" % [k, i, c.bullet_get_rotation_speed(i)])
			if c.bullet_get_gravity(i) != [Vector2(0, 5), Vector2(0, 7)][k]:
				problems.append("parent %d child %d gravity %s" % [k, i, c.bullet_get_gravity(i)])
			if c.get_bullet_speed_data(i).speed != 0.0:
				problems.append("parent %d child %d speed %s (the data has no shared speed)" % [k, i, c.get_bullet_speed_data(i).speed])
	assert_eq(problems, [], "every child carries its own parent's per-bullet values")
	assert_eq(int(factory.debug_get_split_stats()["templates"]), 1, "one cached template for the parent data")
	data.max_life_time = DT * 0.25 # an edit drops the cached template
	assert_eq(int(factory.debug_get_split_stats()["templates"]), 0, "dropped on changed")


## split_set_data starts a fresh chain at generation 0, even on a volley of
## children (it used to keep the old generation and never split again).
func test_split_set_data_on_children_starts_a_fresh_chain() -> void:
	_armed(_chain([_stage(2)]))
	_run(1)
	assert_eq(splits.size(), 1, "the parent split")
	var children: BulletVolley2D = splits[0][2]
	assert_eq(children.split_get_depth(), 1, "children are generation 1")
	var data := H.make_volley_data(1, 0.0, 5.0)
	assert_true(children.split_set_data(_chain([_stage(3)]), data), "re-armed")
	assert_eq(children.split_get_depth(), 0, "a script call starts at generation 0")
	assert_true(children.split_bullet(0), "and the stage for generation 0 splits it")


## split_max_pending bounds the queue (exponential chains, carried-over
## backlogs): past it a new split is dropped (the bullet dies or survives as
## if it never split), counted in dropped_total, with one warning per
## overflow. 0 = no cap. The setter rejects negatives and keeps the old value.
func test_the_pending_cap_drops_new_splits_with_one_warning() -> void:
	assert_eq(factory.split_max_pending, 100000, "default")
	factory.split_max_pending = -1
	expect_error_sequence(["BulletFactory2D: split_max_pending must be >= 0 (0 = no cap), keeping the old value."])
	assert_eq(factory.split_max_pending, 100000, "kept")
	factory.split_max_pending = 3
	_armed(_chain([_stage(2)]), 10) # ten parents expire in one sweep
	_run(1)
	expect_warning_sequence(["BulletFactory2D: split_max_pending (3) reached, new splits are dropped until the queue drains (raise it, or 0 = no cap)."])
	assert_eq(int(factory.debug_get_split_stats()["dropped_total"]), 7, "seven of ten dropped")
	_run(2)
	assert_eq(_depths(), [1, 1, 1], "the three queued splits spawned")
	_armed(_chain([_stage(2)]), 5) # the queue drained: a new overflow warns again
	_run(2)
	expect_warning_sequence(["BulletFactory2D: split_max_pending (3) reached, new splits are dropped until the queue drains (raise it, or 0 = no cap)."])
	assert_eq(int(factory.debug_get_split_stats()["dropped_total"]), 9, "two more dropped")
	factory.split_max_pending = 0
	_armed(_chain([_stage(2)]), 10)
	_run(2)
	assert_eq(int(factory.debug_get_split_stats()["dropped_total"]), 9, "0 = no cap: nothing dropped")
	assert_eq(splits.size(), 3 + 3 + 10, "every accepted split spawned")
