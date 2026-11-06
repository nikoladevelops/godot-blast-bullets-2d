extends BlastTest
## The volley's per-bullet ACTIVE SET is the one source of truth for "which
## bullets are alive": the dense index list the per-bullet loops walk, the live
## counter, the per-bullet `active` flag of the debug readouts and the factory's
## bookkeeping all have to agree after EVERY operation that changes membership
## (disable, wake, circle/rect clears, teleport, expiry into the pool, a
## respawn, free_active_bullets, reset, free_volley_deferred). The sparse set
## reuses index slots, so the dangerous failures are aliasing (one index
## counted twice, a stale slot handed to a new bullet) and a counter that
## drifts from the set - every test below checks the set itself, never a
## summary number alone.

const SHAPES := 4 # amount of the storm volley


func _spread_data(n: int, gap := 24.0, lifetime := 60.0) -> BulletVolleyData2D:
	var d := H.make_volley_data(n, 0.0, lifetime)
	var transforms: Array = []
	for i in n:
		transforms.append(Transform2D(0.0, Vector2(gap * i, 0.0)))
	d.transforms = transforms
	return d


## The volley's live indexes, sorted (the dense order is internal).
func _live_set(v: BulletVolley2D) -> Array:
	var out := Array(v.get_active_bullet_indexes())
	out.sort()
	return out


func _sorted_keys(set: Dictionary) -> Array:
	var out: Array = set.keys()
	out.sort()
	return out


## The full triple invariant of one volley against `expected` (the tracked set
## of live indexes): indexes, live counter, sparse-set stats, volley info, the
## per-bullet flags and the volley's own invariants.
func _assert_set_matches(v: BulletVolley2D, expected: Dictionary, where: String) -> void:
	var want := _sorted_keys(expected)
	assert_eq(_live_set(v), want, "%s: the reported indexes are the tracked set" % where)
	assert_eq(v.get_active_bullet_count(), want.size(), "%s: the live counter matches the set" % where)
	var stats: Dictionary = v.debug_get_active_set_stats()
	assert_eq(int(stats["active"]), want.size(), "%s: debug_get_active_set_stats agrees" % where)
	assert_eq(int(stats["amount_bullets"]), v.get_amount_bullets(), "%s: over the volley's whole index space" % where)
	assert_eq(int(v.debug_get_volley_info()["active_bullets"]), want.size(), "%s: debug_get_volley_info agrees" % where)
	var flags := ""
	for i in v.get_amount_bullets():
		var active := bool(v.debug_get_bullet_info(i)["active"])
		if active != expected.has(i):
			flags += "bullet %d reads active=%s; " % [i, active]
	assert_eq(flags, "", "%s: every per-bullet active flag matches membership" % where)
	assert_eq(v.debug_check_invariants(), "", "%s: the volley's invariants hold" % where)


## A seeded 20-step storm of disable / wake / circle clear / rect clear /
## teleport: after EVERY step the reported set, the counters and the per-bullet
## flags still describe exactly the tracked membership.
func test_the_active_set_survives_a_seeded_operation_storm() -> void:
	seed(20260101)
	var v := factory.spawn_volley(_spread_data(SHAPES))
	var expected := {0: true, 1: true, 2: true, 3: true}
	_assert_set_matches(v, expected, "at spawn")
	for step in 20:
		var op := randi() % 5
		var i := randi() % SHAPES
		var where := "step %d (op %d, index %d)" % [step, op, i]
		match op:
			0: # disable one bullet (never the last one: the volley would pool)
				if expected.has(i) and expected.size() > 1:
					v.disable_bullet(i)
					expected.erase(i)
			1: # wake one disabled bullet
				if not expected.has(i):
					v.enable_bullet(i)
					expected[i] = true
			2: # clear a circle (only while it leaves someone alive)
				var center := Vector2(randf_range(0.0, 72.0), randf_range(-8.0, 8.0))
				var radius := randf_range(10.0, 60.0)
				var in_circle := _inside_circle(v, expected, center, radius)
				if not in_circle.is_empty() and in_circle.size() < expected.size():
					v.clear_bullets_in_circle(center, radius)
					for k in in_circle:
						expected.erase(k)
			3: # clear a rect (same guard)
				var rect := Rect2(randf_range(0.0, 60.0), randf_range(-20.0, 20.0), randf_range(20.0, 80.0), randf_range(20.0, 60.0))
				var in_rect := _inside_rect(v, expected, rect)
				if not in_rect.is_empty() and in_rect.size() < expected.size():
					v.clear_bullets_in_rect(rect)
					for k in in_rect:
						expected.erase(k)
			4: # teleport a live bullet (moves it; membership unchanged)
				if expected.has(i):
					v.teleport_bullet(i, Vector2(randf_range(-200.0, 200.0), randf_range(-200.0, 200.0)))
		_assert_set_matches(v, expected, where)
	assert_true(v.is_volley_active(), "the storm never emptied the volley")
	assert_eq(v.debug_get_life_state(), "active", "it is still the same live life")


func _inside_circle(v: BulletVolley2D, expected: Dictionary, center: Vector2, radius: float) -> Array:
	var out: Array = []
	for i in expected:
		if (v.get_bullet_transform(i).origin.distance_to(center) <= radius):
			out.append(i)
	return out


func _inside_rect(v: BulletVolley2D, expected: Dictionary, rect: Rect2) -> Array:
	var out: Array = []
	for i in expected:
		if rect.has_point(v.get_bullet_transform(i).origin):
			out.append(i)
	return out


## A circle clear and a rect clear each remove exactly the bullets inside them
## (the survivors are the exact complement), the survivors' transforms are not
## touched and the area keeps one shape per bullet (a clear never rewrites the
## shapes).
func test_a_circle_and_a_rect_clear_remove_exactly_their_region() -> void:
	var v := factory.spawn_volley(_spread_data(4, 40.0)) # x = 0, 40, 80, 120
	var before := origins(v)
	var shapes_before: int = int(v.debug_get_shape_state()["shape_count"])
	assert_eq(v.clear_bullets_in_circle(Vector2(40, 0), 25.0), 1, "the circle holds exactly bullet 1")
	_assert_set_matches(v, {0: true, 2: true, 3: true}, "after the circle clear")
	var after := origins(v)
	var moved := ""
	for i in [0, 2, 3]:
		if after[i] != before[i]:
			moved += "bullet %d moved from %s to %s; " % [i, before[i], after[i]]
	assert_eq(moved, "", "the survivors did not move")
	assert_eq(int(v.debug_get_shape_state()["shape_count"]), shapes_before, "the area's shapes are untouched")
	assert_eq(v.clear_bullets_in_rect(Rect2(60, -20, 80, 40)), 2, "the rect holds exactly bullets 2 and 3")
	_assert_set_matches(v, {0: true}, "after the rect clear")
	assert_eq(origins(v)[0], before[0], "the single survivor never moved")
	assert_eq(int(v.debug_get_shape_state()["shape_count"]), shapes_before, "still one shape per bullet")
	assert_true(v.is_volley_active(), "one live bullet keeps the volley active")


## Disabling twice and waking several times moves the index in and out of the
## set exactly once: index 1 is listed once, the counter returns to 4 (never 5)
## and every readout agrees.
func test_double_disable_and_repeated_wakes_never_double_count() -> void:
	var v := factory.spawn_volley(_spread_data(4))
	v.disable_bullet(1)
	v.disable_bullet(1)
	_assert_set_matches(v, {0: true, 2: true, 3: true}, "after a double disable")
	for k in 5:
		v.enable_bullet(1)
		_assert_set_matches(v, {0: true, 1: true, 2: true, 3: true}, "wake %d" % k)
	assert_eq(Array(v.get_active_bullet_indexes()).count(1), 1, "index 1 appears exactly once")
	assert_eq(v.get_active_bullet_count(), 4, "the counter is 4, never 5")
	assert_eq(v.debug_check_invariants(), "", "books ok")


## The life state follows the set: disabling every bullet of a volley with
## pooling OFF parks it (indexes empty, not active), and waking ONE bullet
## makes it active again with exactly that index.
func test_parking_and_a_single_wake_keep_the_state_and_the_set_exact() -> void:
	var v := factory.spawn_volley(_spread_data(4))
	v.set_is_auto_pooling_enabled(false)
	for i in [2, 0, 3, 1]: # shuffled disable order
		v.disable_bullet(i)
	assert_eq(v.debug_get_life_state(), "parked", "the empty volley parked")
	assert_eq(_live_set(v), [], "no index is live")
	assert_eq(v.get_active_bullet_count(), 0, "the counter is 0")
	assert_eq(v.is_volley_active(), (v.get_active_bullet_count() > 0), "is_volley_active follows the counter")
	assert_false(v.is_volley_active(), "a parked volley is not active")
	assert_true(is_instance_valid(v) and not v.is_pooled(), "parked, not pooled")
	_assert_set_matches(v, {}, "while parked")
	v.enable_bullet(2)
	assert_eq(_live_set(v), [2], "exactly the woken index")
	assert_eq(v.debug_get_life_state(), "active", "one bullet makes the volley active again")
	assert_eq(v.is_volley_active(), (v.get_active_bullet_count() > 0), "is_volley_active follows the counter")
	assert_eq(v.get_active_bullet_count(), 1, "one live bullet")
	_assert_set_matches(v, {2: true}, "after the wake")


## A life that expires completely pools the volley; the next spawn of the same
## key starts with the FULL set, a fresh life and a live state (a pooled handle
## never hands its old membership over).
func test_a_respawned_life_starts_with_the_whole_set_again() -> void:
	var d := _spread_data(5, 24.0, 0.05)
	var v: BulletVolley2D = factory.spawn_volley(d)
	var life := v.get_life_id()
	step_factory(6) # 0.05 s at 1/60 s per tick: the whole life expires
	assert_true(v.is_pooled(), "the expired volley pooled itself")
	assert_eq(v.debug_get_life_state(), "pooled", "and reads pooled")
	assert_eq(_live_set(v), [], "a pooled handle lists nothing")
	var w: BulletVolley2D = factory.spawn_volley(d)
	assert_same(w, v, "the pool handed it back")
	var all := {0: true, 1: true, 2: true, 3: true, 4: true}
	_assert_set_matches(w, all, "at the respawn")
	assert_eq(w.debug_get_life_state(), "active", "the new life is active at spawn")
	assert_eq(w.get_life_id(), life + 1, "one generation later")
	assert_eq(w.get_active_bullet_count(), 5, "all five bullets live")


## The factory's books after every structural operation: the live id list, the
## active-volley listing and the summed bullet counts of the volleys describe
## the same thing (spawn x3 -> keyed free_active_bullets -> reset -> spawn x2
## -> free_volley_deferred).
func test_live_ids_and_the_factory_count_agree_after_every_structural_op() -> void:
	factory.spawn_volley(_spread_data(2))
	factory.spawn_volley(_spread_data(3))
	factory.spawn_volley(_spread_data(4))
	_check_factory_books("after three spawns")
	factory.free_active_bullets(VolleyPoolKey2D.make(2, PhysicsServer2D.SHAPE_CIRCLE))
	await idle(1)
	_check_factory_books("after a keyed free_active_bullets")
	factory.reset()
	await idle(1)
	_check_factory_books("after a reset")
	factory.spawn_volley(_spread_data(2))
	factory.spawn_volley(_spread_data(3))
	await idle(1)
	_check_factory_books("after two fresh spawns")
	var v: BulletVolley2D = factory.get_active_volleys()[0]
	factory.free_volley_deferred(v)
	await idle(2)
	assert_false(is_instance_valid(v), "the deferred free destroyed it")
	_check_factory_books("after a deferred volley free")


## 51: walking EVERY live volley after each of those operations, each one's
## sparse set covers its whole index space (capacity >= amount_bullets) and
## agrees with its live counter.
func test_every_live_volley_keeps_its_sparse_set_contract() -> void:
	factory.spawn_volley(_spread_data(2))
	factory.spawn_volley(_spread_data(3))
	factory.spawn_volley(_spread_data(4))
	_check_every_volley("after three spawns")
	factory.free_active_bullets(VolleyPoolKey2D.make(3, PhysicsServer2D.SHAPE_CIRCLE))
	await idle(1)
	_check_every_volley("after a keyed free_active_bullets")
	factory.reset()
	await idle(1)
	_check_every_volley("after a reset")
	factory.spawn_volley(_spread_data(5))
	factory.spawn_volley(_spread_data(2))
	await idle(1)
	_check_every_volley("after two fresh spawns")
	var v: BulletVolley2D = factory.get_active_volleys()[0]
	factory.free_volley_deferred(v)
	await idle(2)
	_check_every_volley("after a deferred volley free")
	assert_eq(factory.get_active_volleys().size(), 1, "one live volley is left")


func _check_factory_books(where: String) -> void:
	var ids: Array = factory.debug_get_live_volley_ids(0)
	var listed: Array = factory.get_active_volleys()
	assert_eq(ids.size(), factory.debug_get_active_volleys_amount(), "%s: the live id list covers every counted volley" % where)
	assert_eq(ids.size(), listed.size(), "%s: get_active_volleys() lists the same volleys" % where)
	var total := 0
	for v in listed:
		total += (v as BulletVolley2D).get_active_bullet_count()
	assert_eq(factory.get_active_bullet_count(), total, "%s: the factory count is the sum of the volleys'" % where)


func _check_every_volley(where: String) -> void:
	var ids: Array = factory.debug_get_live_volley_ids(0)
	var listed: Array = factory.get_active_volleys(true)
	assert_eq(ids.size(), listed.size(), "%s: every live volley is listed" % where)
	for v in listed:
		var volley: BulletVolley2D = v
		assert_eq(volley.debug_check_invariants(), "", "%s: %s has no dangling state" % [where, volley])
		var stats: Dictionary = volley.debug_get_active_set_stats()
		var keys: Array = stats.keys()
		keys.sort()
		assert_eq(keys, ["active", "active_indexes", "amount_bullets", "capacity", "consistent"], "%s: the documented stats shape" % where)
		assert_true(bool(stats["consistent"]), "%s: the sparse set is internally consistent" % where)
		assert_eq(int(stats["active"]), volley.get_active_bullet_count(), "%s: stats.active == the live counter" % where)
		assert_gte(int(stats["capacity"]), int(stats["amount_bullets"]), "%s: the index space covers every bullet" % where)
		assert_eq(int(stats["amount_bullets"]), volley.get_amount_bullets(), "%s: amount_bullets" % where)
		assert_eq(Array(stats["active_indexes"]).size(), volley.get_active_bullet_count(), "%s: one index per live bullet" % where)


## Freeing one volley never aliases the next spawn: the freed volley is gone
## from every listing, the new one buckets under its own key, and the live id
## list holds exactly the surviving instances.
func test_freeing_one_volley_never_aliases_the_next_spawn() -> void:
	var a: BulletVolley2D = factory.spawn_volley(_spread_data(2))
	var b: BulletVolley2D = factory.spawn_volley(_spread_data(3))
	var c: BulletVolley2D = factory.spawn_volley(_spread_data(3))
	factory.free_active_bullets(factory.debug_get_pool_bucket(a))
	await idle(1)
	assert_false(is_instance_valid(a), "the (2, circle) volley is destroyed")
	var d: BulletVolley2D = factory.spawn_volley(_spread_data(2))
	var key: VolleyPoolKey2D = factory.debug_expected_pool_key(_spread_data(2))
	assert_true(factory.debug_get_pool_bucket(d).equals(key), "D spawned into its own bucket")
	var survivors: Array = [b.get_instance_id(), c.get_instance_id(), d.get_instance_id()]
	survivors.sort()
	var ids: Array = factory.debug_get_live_volley_ids(0)
	ids.sort()
	assert_eq(ids, survivors, "the live ids are exactly the survivors")
	_assert_set_matches(b, {0: true, 1: true, 2: true}, "B after the churn")
	_assert_set_matches(d, {0: true, 1: true}, "the new volley")
	assert_true(factory.debug_assert_no_dangling().get("ok", false), "books ok")
