extends BlastTest
## Splitting and the volley pool: a parent whose last bullet's death pools
## its volley in the middle of the capture still yields exact children (the
## split is recorded before the disable); a pooled split volley comes back
## plain (no chain, no spawn data, no arming snapshot: a plain spawn reusing
## it never splits); spawn -> split -> pool -> respawn over many lives leaks
## nothing; a parked volley (auto pooling off) keeps its chain and a woken
## one splits again; a freed parent volley or a reset mid-queue never
## reaches freed memory; handlers that free things inside the flush stop it
## cleanly.

const DT := 1.0 / 60.0

var splits: Array = []


func before_each() -> void:
	await super()
	splits.clear()
	factory.bullet_split.connect(func(pos, cause, children, depth): splits.append([pos, cause, children, depth]))


func _chain(count := 2) -> BulletSplitData2D:
	var s := BulletSplitStage2D.new()
	s.count = count
	var d := BulletSplitData2D.new()
	d.stages = [s]
	return d


func _armed(n := 1, lifetime := DT * 0.5, child_lifetime := 5.0) -> BulletVolley2D:
	var v := factory.spawn_volley(H.make_volley_data(n, 0.0, lifetime))
	assert_true(v.split_set_data(_chain(), H.make_volley_data(1, 0.0, child_lifetime)), "armed")
	return v


func test_a_parent_pooled_by_its_own_death_still_yields_exact_children() -> void:
	var v := _armed(1)
	var at: Vector2 = v.get_bullet_transform(0).origin
	factory.debug_advance_time(DT)
	assert_true(bool(v.debug_get_volley_info()["is_pooled"]), "the parent volley pooled during the expiry")
	assert_eq(splits.size(), 1, "split anyway")
	var c: BulletVolley2D = splits[0][2]
	assert_eq([c.get_bullet_transform(0).origin, c.get_bullet_transform(1).origin], [at, at], "children at the parent's pose")
	assert_ne(c, v, "never the pooled parent itself in the same sweep")


func test_a_pooled_split_volley_comes_back_plain() -> void:
	var v := _armed(1)
	factory.debug_advance_time(DT)
	assert_true(bool(v.debug_get_volley_info()["is_pooled"]), "pooled")
	assert_null(v.split_get_data(), "no chain")
	assert_eq(v.split_get_depth(), 0, "generation reset")
	# A plain spawn of the same key reuses it and must never split.
	var plain := factory.spawn_volley(H.make_volley_data(1, 0.0, DT * 0.5))
	assert_eq(plain, v, "the same instance reused")
	splits.clear()
	factory.debug_advance_time(DT)
	factory.debug_advance_time(DT)
	assert_eq(splits.size(), 0, "a reused plain volley never splits")
	var state: Dictionary = factory.debug_assert_no_dangling()
	assert_true(state["ok"], str(state["error"]))


func test_many_lives_of_split_and_reuse_leak_nothing() -> void:
	for life in 6:
		_armed(3, DT * 0.5, DT * 0.5)
		for i in 3:
			factory.debug_advance_time(DT)
	assert_eq(splits.size(), 18, "3 parents per life, 6 lives")
	assert_eq(factory.get_active_bullet_count(), 0, "everything expired")
	var state: Dictionary = factory.debug_assert_no_dangling()
	assert_true(state["ok"], str(state["error"]))


func test_a_parked_volley_keeps_its_chain_and_splits_again_when_woken() -> void:
	var data := H.make_volley_data(2, 0.0, 5.0)
	var v := factory.spawn_volley(data)
	v.set_is_auto_pooling_enabled(false)
	v.split_set_data(_chain(), H.make_volley_data(1, 0.0, 5.0))
	v.all_bullets_split()
	factory.debug_advance_time(DT)
	assert_eq(splits.size(), 2, "both split")
	assert_false(bool(v.debug_get_volley_info()["is_pooled"]), "parked, not pooled")
	assert_not_null(v.split_get_data(), "parked: the chain stays")
	v.enable_bullet(0)
	assert_true(v.split_bullet(0), "a woken bullet splits again")
	factory.debug_advance_time(DT)
	assert_eq(splits.size(), 3, "split")


func test_a_queued_split_outlives_its_freed_parent_volley() -> void:
	var v := _armed(2, 5.0)
	v.split_bullet(0)
	assert_eq(factory.get_pending_split_count(), 1, "queued")
	v.free()
	factory.debug_advance_time(DT)
	assert_eq(splits.size(), 1, "the children spawn from the recorded data alone")
	assert_eq((splits[0][2] as BulletVolley2D).get_amount_bullets(), 2, "exact")


func test_a_handler_reset_cancels_the_rest_of_the_flush() -> void:
	_armed(3, 5.0).all_bullets_split()
	var resets := [0]
	factory.bullet_split.connect(func(_p, _c, _children, _d):
		if resets[0] == 0:
			resets[0] += 1
			factory.reset())
	factory.debug_advance_time(DT)
	assert_eq(splits.size(), 1, "the first split reported, then the reset dropped the other two")
	assert_eq(factory.get_pending_split_count(), 0, "nothing left")


func test_handlers_that_free_the_children_or_split_again_are_survived() -> void:
	var v := _armed(3, 5.0)
	factory.bullet_split.connect(func(_p, _c, children, _d):
		children.queue_free())
	v.all_bullets_split()
	factory.debug_advance_time(DT)
	assert_eq(splits.size(), 3, "every split reported")
	await idle(2)
	# A handler that splits the children right away: queued for the next sweep, never chained inside one flush.
	splits.clear()
	var w := factory.spawn_volley(H.make_volley_data(1, 0.0, 5.0))
	var chain := _chain()
	chain.repeat_last_stage = true
	chain.max_depth = 64
	w.split_set_data(chain, H.make_volley_data(1, 0.0, 5.0))
	factory.bullet_split.connect(func(_p, _c, children, _d): children.all_bullets_split())
	w.split_bullet(0)
	factory.debug_advance_time(DT)
	assert_eq(splits.size(), 1, "one generation per flush even when every handler splits again")
	assert_eq(factory.get_pending_split_count(), 2, "the children's splits wait")
	factory.debug_advance_time(DT)
	assert_eq(splits.size(), 3, "next sweep: the two children split")


func test_a_cached_template_outlives_its_factory_safely() -> void:
	# Per-bullet spawn data makes the factory cache a template and listen to
	# the data's `changed`: an edit after the factory is gone must be silent.
	var other := BulletFactory2D.new()
	add_child_autofree(other)
	await idle(2)
	var data := H.make_volley_data(2, 0.0, DT * 0.5) # one speed entry per bullet
	var v := other.spawn_volley(data)
	v.split_set_data(_chain(), data)
	other.debug_advance_time(DT)
	assert_eq(int(other.debug_get_split_stats()["templates"]), 1, "cached")
	other.free()
	data.max_life_time = 2.0 # emits changed
	expect_no_errors("the freed factory's listener is gone")


## A scoped reset / free_active_bullets drops the queued splits of the
## volleys it resets (their PARENTS were in that bucket), never the splits of
## other buckets whose children happen to use that key; and it never cancels
## the rest of a running flush.
func test_a_scoped_reset_drops_only_the_splits_of_its_bucket() -> void:
	# Parents of 3 bullets whose children (2) use the 2-bullet bucket, and
	# parents of 2 bullets (the 2-bullet bucket) whose children use 2 too.
	var three := factory.spawn_volley(H.make_volley_data(3, 0.0, 5.0))
	three.split_set_data(_chain(), H.make_volley_data(1, 0.0, 5.0))
	var two := _armed(2, 5.0)
	three.split_bullet(0)
	two.split_bullet(0)
	assert_eq(factory.get_pending_split_count(), 2, "two queued")
	factory.free_active_bullets(factory.debug_get_pool_bucket(two))
	assert_eq(factory.get_pending_split_count(), 1, "only the freed bucket's split went")
	factory.debug_advance_time(DT)
	assert_eq(splits.size(), 1, "the other bucket's parent still splits")
	assert_eq(splits[0][0], Vector2(0, 0), "the 3-bullet parent's split")
	# A scoped reset from a handler during the flush keeps the rest of the batch.
	splits.clear()
	var a := factory.spawn_volley(H.make_volley_data(3, 0.0, 5.0))
	a.split_set_data(_chain(), H.make_volley_data(1, 0.0, 5.0))
	a.all_bullets_split()
	var key := factory.debug_get_pool_bucket(_armed(5, 5.0))
	factory.bullet_split.connect(func(_p, _c, _ch, _d) -> void:
		if splits.size() == 1:
			factory.free_active_bullets(key), CONNECT_ONE_SHOT)
	factory.debug_advance_time(DT)
	assert_eq(splits.size(), 3, "every split of the batch spawned")


## reset() and free_active_bullets() drop the cached child templates with
## the queued splits (a template keeps its source's sub-resources alive).
func test_reset_and_free_drop_the_cached_templates() -> void:
	for op in ["reset", "free_active_bullets"]:
		var data := H.make_volley_data(2, 0.0, DT * 0.5) # one speed entry per bullet: a template
		var v := factory.spawn_volley(data)
		v.split_set_data(_chain(), data)
		factory.debug_advance_time(DT)
		assert_eq(int(factory.debug_get_split_stats()["templates"]), 1, "%s: cached" % op)
		factory.call(op)
		assert_eq(int(factory.debug_get_split_stats()["templates"]), 0, "%s: dropped" % op)
