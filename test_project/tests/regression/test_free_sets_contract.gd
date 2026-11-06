extends BlastTest
## After every way a volley's bullets leave (clear_bullet, clear_all_bullets,
## clear_bullets_in_circle, free_active_bullets, free_disabled_bullets, reset,
## queue_free of one, free_volley_deferred from a live handler) the factory's
## three sets still agree: the books are clean, the factory's live-bullet
## count is the sum over the volleys, and the live id list is exactly
## get_active_volleys().
## BY DESIGN that set is NOT get_active_volleys(true): the id list (like
## get_active_volleys() and get_active_volleys_owned_by(null)) lists ACTIVE
## volleys only, while the include_parked listing adds parked ones (a parked
## volley holds no live bullet; a pooled one is in neither listing). Pinned so
## the difference is never mistaken for lost bookkeeping again.


func _spread_data(n: int, lifetime := 60.0) -> BulletVolleyData2D:
	var d := H.make_volley_data(n, 0.0, lifetime)
	var transforms: Array = []
	for i in n:
		transforms.append(Transform2D(0.0, Vector2(24.0 * i, 0.0)))
	d.transforms = transforms
	return d


## The triple invariant: clean books, live ids == get_active_volleys(), and the
## factory's live-bullet count == the sum over every listed volley.
func _triple(where: String) -> void:
	var books: Dictionary = factory.debug_assert_no_dangling()
	assert_true(books.get("ok", false), "%s: the books are clean: %s" % [where, books.get("error", "")])
	var live: Array = factory.debug_get_live_volley_ids(0)
	var active_ids: Array = []
	for v in factory.get_active_volleys(): # active only: the same set by design
		active_ids.append((v as BulletVolley2D).get_instance_id())
	live.sort()
	active_ids.sort()
	assert_eq(live, active_ids, "%s: the live id list is exactly get_active_volleys()" % where)
	var total := 0
	for v in factory.get_active_volleys(true):
		total += (v as BulletVolley2D).get_active_bullet_count()
	assert_eq(factory.get_active_bullet_count(), total, "%s: the factory count is the volleys' sum (parked hold none)" % where)


## The op matrix: a fresh scene per parameter (GUT isolates before/after_each).
func test_the_triple_invariant_holds_after_every_free(op: String = use_parameters([
		"clear_bullet", "clear_all_bullets", "clear_bullets_in_circle",
		"free_active_bullets", "free_disabled_bullets", "reset",
		"queue_free_one", "free_volley_deferred_from_a_handler"])) -> void:
	var a: BulletVolley2D = factory.spawn_volley(_spread_data(3))
	var b: BulletVolley2D = factory.spawn_volley(_spread_data(2))
	_triple("at spawn")
	match op:
		"clear_bullet":
			a.clear_bullet(1)
			await idle(1)
		"clear_all_bullets":
			a.clear_all_bullets()
			await idle(1)
		"clear_bullets_in_circle":
			factory.clear_bullets_in_circle(Vector2.ZERO, 100.0)
			await idle(1)
		"free_active_bullets":
			factory.free_active_bullets()
			await idle(1)
		"free_disabled_bullets":
			# One fully disabled volley pools (auto pooling on), one parks
			# (off): free_disabled_bullets destroys the pooled one only.
			a.disable_bullet(0)
			a.disable_bullet(1)
			a.disable_bullet(2)
			b.set_is_auto_pooling_enabled(false)
			b.disable_bullet(0)
			b.disable_bullet(1)
			assert_true(a.is_pooled() or a.is_parked(), "A left play")
			assert_true(b.is_parked(), "B parked")
			factory.free_disabled_bullets()
			await idle(1)
		"reset":
			factory.reset()
			await idle(1)
		"queue_free_one":
			a.queue_free()
			await idle(2)
		"free_volley_deferred_from_a_handler":
			var expiring := _spread_data(2, 0.05)
			expiring.is_life_time_over_signal_enabled = true
			var c: BulletVolley2D = factory.spawn_volley(expiring)
			var free_b := func(volley: BulletVolley2D, _indexes: Array) -> void:
				if volley == c:
					factory.free_volley_deferred(b)
			factory.life_time_over.connect(free_b, CONNECT_ONE_SHOT)
			for k in 10:
				await idle(1)
				if not is_instance_valid(b):
					break
			await idle(2)
			assert_false(is_instance_valid(b), "the handler's deferred free destroyed B")
	_triple("after %s" % op)


## The by-design difference the old lead mistook for a bug: with an active, a
## parked and a pooled volley present, the id list and the default listing
## hold the active one, the include_parked listing adds the parked one, and
## the pooled one is in neither.
func test_the_live_id_list_is_active_only_while_include_parked_adds_parked() -> void:
	var active: BulletVolley2D = factory.spawn_volley(_spread_data(2))
	var parked: BulletVolley2D = factory.spawn_volley(_spread_data(2))
	parked.set_is_auto_pooling_enabled(false)
	parked.disable_bullet(0)
	parked.disable_bullet(1)
	var pooled: BulletVolley2D = factory.spawn_volley(_spread_data(2))
	pooled.disable_bullet(0)
	pooled.disable_bullet(1)
	await idle(1)
	assert_true(pooled.is_pooled(), "one volley pooled")
	assert_eq(parked.debug_get_life_state(), "parked", "one volley parked")
	var live: Array = factory.debug_get_live_volley_ids(0)
	assert_eq(live, [active.get_instance_id()], "the id list is ACTIVE only (never parked, never pooled)")
	assert_eq(factory.get_active_volleys(), [active], "the default listing is the same active set")
	assert_eq(factory.get_active_volleys(true), [active, parked], "include_parked adds the parked one, never the pooled one")
	_triple("with active, parked and pooled volleys present")


## free_volley_deferred from a live handler destroys exactly one volley: it is
## gone from the pool info, the id list, the owner listing and the
## include_parked listing, and the factory's bullet count drops by its own.
func test_a_deferred_free_from_a_handler_removes_the_volley_everywhere() -> void:
	var survivor: BulletVolley2D = factory.spawn_volley(_spread_data(4))
	var expiring := _spread_data(3, 0.05)
	expiring.is_life_time_over_signal_enabled = true
	var victim: BulletVolley2D = factory.spawn_volley(expiring)
	var free_victim := func(_volley: BulletVolley2D, _indexes: Array) -> void:
		factory.free_volley_deferred(victim)
	factory.life_time_over.connect(free_victim, CONNECT_ONE_SHOT)
	var bullets_before: int = factory.get_active_bullet_count()
	assert_eq(bullets_before, 7, "both volleys live")
	for k in 10:
		await idle(1)
		if not is_instance_valid(victim):
			break
	await idle(2)
	assert_false(is_instance_valid(victim), "the deferred free landed")
	assert_eq(factory.get_active_bullet_count(), bullets_before - 3, "the victim's bullets left the count")
	assert_eq(Array(factory.debug_get_live_volley_ids(0)), [survivor.get_instance_id()], "gone from the id list")
	assert_eq(factory.get_volleys_owned_by(null), [survivor], "gone from the owner listing")
	assert_eq(factory.get_active_volleys(true), [survivor], "gone from the include_parked listing too")
	assert_eq(factory.debug_get_bullets_pool_info().size(), 0, "nothing of it was pooled (queue_free destroyed it)")
	_triple("after the handler's deferred free")
