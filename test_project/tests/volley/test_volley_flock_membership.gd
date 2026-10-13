extends BlastTest
## Who flies together (BulletVolley2D.flock_join / flock_leave / flock_get):
## separate flocks never see each other (bit-identical to flying alone), the
## scopes group factory volleys as documented, frozen bullets are not
## flockmates, live edits of the resource reach the flock on the next tick,
## bad arguments fail loud and change nothing, and user code that frees,
## leaves, joins, edits or wakes during a sweep never steers with stale data
## (a volley that joined mid-sweep turns from the next tick on). The factory
## stats and its armed count stay exact through every exit path.

const DT := 1.0 / 60.0


func _volley(transforms: Array, speed := 100.0, lifetime := 1000.0) -> BulletVolley2D:
	return factory.spawn_volley(H.make_flight_data(transforms, speed, lifetime))


func _cluster(origin: Vector2, count := 6, seed_value := 1) -> Array:
	seed(seed_value)
	var tr: Array = []
	for k in count:
		tr.append(Transform2D(randf_range(-PI, PI), origin + Vector2(randf_range(0, 40), randf_range(0, 40))))
	return tr


func _sees_across(a: BulletVolley2D, b: BulletVolley2D) -> bool:
	# a's bullets are counted by b's flock readout when they share a flock.
	return b.debug_get_bullet_flock_neighbors(0) > b.get_amount_bullets() - 1


func test_separate_flocks_never_see_each_other() -> void:
	var tr := _cluster(Vector2(0, 0), 8, 4)
	var a := _volley(tr)
	a.flock_join(H.make_flock(1.5, 1.0, 1.0))
	var b := _volley(_cluster(Vector2(5, 5), 8, 5))
	b.flock_join(H.make_flock(3.0, 0.5, 2.0))
	var solo := _volley(tr) # a's twin in a third flock with a's rules
	solo.flock_join(H.make_flock(1.5, 1.0, 1.0))
	step_factory(60)
	assert_eq(origins(a), origins(solo), "a flies exactly as if b did not exist")
	assert_eq(factory.debug_get_flock_stats()["flocks"], 3, "three flocks")


func test_shared_scope_merges_volleys_of_one_resource() -> void:
	var f := H.make_flock(1.5, 1.0, 1.0)
	var a := _volley(_cluster(Vector2(0, 0), 4, 1))
	var b := _volley(_cluster(Vector2(0, 0), 4, 2))
	a.flock_join(f)
	b.flock_join(f, BulletFlock2D.SCOPE_SHARED)
	step_factory(1)
	assert_eq(a.flock_get_scope(), BulletFlock2D.SCOPE_SHARED, "Shared is the default scope of flock_join")
	assert_true(_sees_across(a, b), "the two volleys flock as one")
	assert_eq(factory.debug_get_flock_stats()["flocks"], 1, "one flock")


func test_scopes_split_one_resource_into_separate_flocks() -> void:
	var f := H.make_flock(1.5, 1.0, 1.0)
	var tr := _cluster(Vector2(0, 0), 5, 3)
	var shared := _volley(tr)
	shared.flock_join(f, BulletFlock2D.SCOPE_SHARED)
	var factory_group_a := _volley(_cluster(Vector2(0, 0), 5, 6))
	var factory_group_b := _volley(_cluster(Vector2(0, 0), 5, 7))
	factory_group_a.flock_join(f, BulletFlock2D.SCOPE_PER_SPAWNER)
	factory_group_b.flock_join(f, BulletFlock2D.SCOPE_PER_SPAWNER)
	var alone_a := _volley(_cluster(Vector2(0, 0), 5, 8))
	var alone_b := _volley(_cluster(Vector2(0, 0), 5, 9))
	alone_a.flock_join(f, BulletFlock2D.SCOPE_PER_VOLLEY)
	alone_b.flock_join(f, BulletFlock2D.SCOPE_PER_VOLLEY)
	var solo := _volley(tr)
	solo.flock_join(H.make_flock(1.5, 1.0, 1.0))
	step_factory(1)
	assert_eq(factory.debug_get_flock_stats()["flocks"], 5, "shared + the factory's own group + two single volleys + the twin")
	assert_true(_sees_across(factory_group_a, factory_group_b), "Per Spawner groups the factory's own volleys")
	assert_false(_sees_across(alone_a, alone_b), "Per Volley keeps each volley alone")
	assert_eq(alone_a.debug_get_bullet_flock_neighbors(0), 4, "a Per Volley bullet sees its own volley only")
	step_factory(45)
	assert_eq(origins(shared), origins(solo), "Shared never sees the Per Spawner / Per Volley users of the same resource")


func test_frozen_bullets_are_not_flockmates() -> void:
	var tr := [Transform2D(0.0, Vector2(0, 0)), Transform2D(0.0, Vector2(0, 20)), Transform2D(0.0, Vector2(0, 40))]
	var v := _volley(tr)
	v.flock_join(H.make_flock(0.0, 0.0, 1.0))
	v.disable_bullet(1)
	step_factory(1)
	assert_eq(v.debug_get_bullet_flock_neighbors(0), 1, "bullet 0 sees bullet 2 only")
	assert_eq(v.debug_get_bullet_flock_neighbors(1), 0, "the frozen bullet is not gathered")
	assert_almost_eq(v.debug_get_bullet_flock_steering(0).y, 40.0 / 100.0, 1e-5, "bullet 0 is pulled toward bullet 2 alone")
	var frozen_dir: Vector2 = v.get_bullet_direction(1)
	step_factory(10)
	assert_eq(v.get_bullet_direction(1), frozen_dir, "and never turns")


func test_join_leave_get_contract() -> void:
	var v := _volley(_cluster(Vector2(0, 0), 4, 1))
	var f := H.make_flock(1.0, 1.0, 1.0)
	assert_null(v.flock_get(), "not flocking by default")
	assert_eq(v.flock_get_scope(), -1, "no scope")
	assert_eq(factory.debug_get_flock_stats()["armed_volleys"], 0, "nothing armed")
	assert_true(v.flock_join(f, BulletFlock2D.SCOPE_PER_VOLLEY), "joins")
	assert_eq(v.flock_get(), f, "the joined flock")
	assert_eq(v.flock_get_scope(), BulletFlock2D.SCOPE_PER_VOLLEY, "the joined scope")
	assert_eq(factory.debug_get_flock_stats()["armed_volleys"], 1, "one armed volley")
	assert_true(v.flock_join(f, BulletFlock2D.SCOPE_SHARED), "joining again replaces the membership")
	assert_eq(factory.debug_get_flock_stats()["armed_volleys"], 1, "still one armed volley")
	step_factory(1)
	assert_gt(v.debug_get_bullet_flock_neighbors(0), 0, "flocking")
	v.flock_leave()
	assert_null(v.flock_get(), "left")
	assert_eq(factory.debug_get_flock_stats()["armed_volleys"], 0, "disarmed")
	assert_eq(v.debug_get_bullet_flock_neighbors(0), 0, "no readout once left")
	var directions: Array = []
	for i in 4:
		directions.append(v.get_bullet_direction(i))
	step_factory(10)
	for i in 4:
		assert_eq(v.get_bullet_direction(i), directions[i], "bullet %d flies straight after leaving" % i)
	v.flock_leave()
	expect_no_errors("leaving twice is harmless")


func test_stats_keys_are_exact() -> void:
	var stats: Dictionary = factory.debug_get_flock_stats()
	var keys: Array = stats.keys()
	keys.sort()
	assert_eq(keys, ["armed_volleys", "capped_members", "flocks", "grid_members", "members", "neighbor_checks", "pass_usec", "queried_members"], "debug_get_flock_stats keys")
	assert_true(factory.get_frame_stats().has("flock_usec"), "the frame stats carry the pass time")
	var v := _volley(_cluster(Vector2(0, 0), 5, 1))
	v.disable_bullet(4)
	v.flock_join(H.make_flock(1.0, 1.0, 1.0))
	step_factory(1)
	stats = factory.debug_get_flock_stats()
	assert_eq(stats["flocks"], 1, "one flock")
	assert_eq(stats["members"], 5, "every bullet has a slot")
	assert_eq(stats["grid_members"], 4, "the frozen one is not in the grid")
	v.flock_leave()
	step_factory(1)
	stats = factory.debug_get_flock_stats()
	assert_eq([stats["flocks"], stats["members"], stats["grid_members"], stats["neighbor_checks"]], [0, 0, 0, 0], "no armed volley: the old results are dropped")
	assert_eq(factory.get_frame_stats()["flock_usec"], 0, "and the pass costs nothing")


func test_bad_arguments_fail_loud_and_change_nothing() -> void:
	var v := _volley(_cluster(Vector2(0, 0), 3, 1))
	var f := H.make_flock(1.0, 1.0, 1.0)
	assert_false(v.flock_join(f, 3), "scope 3 refused")
	assert_false(v.flock_join(f, -1), "scope -1 refused")
	expect_error_sequence([
		"flock_join: scope must be 0 (Per Spawner), 1 (Shared) or 2 (Per Volley), nothing changed.",
		"flock_join: scope must be 0 (Per Spawner), 1 (Shared) or 2 (Per Volley), nothing changed.",
	])
	assert_null(v.flock_get(), "still not flocking")
	v.flock_join(f)
	assert_true(v.flock_join(null), "null joins the factory's default flock")
	expect_no_errors("null is the default rules, not an error")
	assert_eq(v.flock_get(), factory.get_default_flock(), "the default flock")
	assert_eq(v.flock_get_scope(), BulletFlock2D.SCOPE_SHARED, "with the default scope")
	assert_eq(v.debug_get_bullet_flock_steering(3), Vector2(), "OOB reads zero")
	assert_eq(v.debug_get_bullet_flock_neighbors(-1), 0, "OOB reads zero")
	expect_error_sequence([
		"Invalid bullet index in debug_get_bullet_flock_steering: 3 (amount_bullets: 3)",
		"Invalid bullet index in debug_get_bullet_flock_neighbors: -1 (amount_bullets: 3)",
	])
	var loose := BulletVolley2D.new()
	assert_false(loose.flock_join(f), "a volley no factory spawned is refused")
	expect_error("flock_join: this volley was never spawned by a BulletFactory2D, nothing changed.")
	loose.free()


func test_a_pooled_handle_is_refused() -> void:
	var v := _volley(_cluster(Vector2(0, 0), 3, 1), 100.0, 0.05)
	v.flock_join(H.make_flock(1.0, 1.0, 1.0))
	step_factory(6)
	assert_true(v.debug_get_volley_info()["is_pooled"], "expired into the pool")
	assert_null(v.flock_get(), "the pool dropped the flock")
	assert_eq(factory.debug_get_flock_stats()["armed_volleys"], 0, "disarmed with the life")
	assert_false(v.flock_join(H.make_flock()), "stale handle refused")
	v.flock_leave()
	expect_error_sequence([
		"flock_join: this volley is in the pool (its last bullet died), so this handle is stale. Spawn a new volley instead.",
		"flock_leave: this volley is in the pool (its last bullet died), so this handle is stale. Spawn a new volley instead.",
	])


func test_live_edits_of_the_resource_reach_the_flock_next_tick() -> void:
	var f := H.make_flock(0.0, 0.0, 0.0)
	var v := _volley([Transform2D(0.0, Vector2(0, 0)), Transform2D(0.0, Vector2(0, 30))])
	v.flock_join(f)
	step_factory(1)
	assert_eq(v.debug_get_bullet_flock_steering(0), Vector2(), "weightless")
	f.cohesion = 1.0
	step_factory(1)
	assert_gt(v.debug_get_bullet_flock_steering(0).y, 0.0, "cohesion reaches the flying flock")
	f.neighbor_range = 10.0
	f.separation_range = 0.0
	step_factory(1)
	assert_eq(v.debug_get_bullet_flock_neighbors(0), 0, "a shorter range drops the flockmate")
	assert_eq(v.debug_get_bullet_flock_steering(0), Vector2(), "and its pull")


func test_handlers_mid_sweep_never_steer_with_stale_data() -> void:
	# The trigger volley expires first in the sweep; its handler edits the
	# volleys the sweep has not ticked yet.
	var trigger_data := H.make_flight_data([Transform2D(0.0, Vector2(-500, -500))], 0.0, 0.05)
	trigger_data.is_life_time_over_signal_enabled = true
	var trigger: BulletVolley2D = factory.spawn_volley(trigger_data)
	var f := H.make_flock(0.0, 0.0, 3.0)
	var freed := _volley(_cluster(Vector2(0, 0), 4, 1))
	var leaver := _volley(_cluster(Vector2(200, 0), 4, 2))
	var joiner := _volley(_cluster(Vector2(400, 0), 4, 3))
	var woken := _volley([Transform2D(0.0, Vector2(600, 0)), Transform2D(PI * 0.5, Vector2(600, 30))])
	woken.set_is_auto_pooling_enabled(false)
	var rejoiner := _volley(_cluster(Vector2(800, 0), 4, 4))
	for flocked in [freed, leaver, woken, rejoiner]:
		flocked.flock_join(f)
	woken.disable_bullet(1) # not gathered while frozen
	var seen := {}
	factory.life_time_over.connect(func(v: BulletVolley2D, _idx: Array) -> void:
		if v != trigger:
			return
		freed.free()
		leaver.flock_leave()
		joiner.flock_join(f, BulletFlock2D.SCOPE_PER_VOLLEY)
		f.cohesion = 50.0 # applies from the next pass
		woken.enable_bullet(1)
		rejoiner.flock_join(H.make_flock(0.0, 0.0, 3.0), BulletFlock2D.SCOPE_PER_VOLLEY)
		seen["rejoiner"] = rejoiner.get_bullet_direction(0)
		seen["leaver"] = [leaver.get_bullet_direction(0), leaver.get_bullet_direction(1)]
		seen["joiner"] = joiner.get_bullet_direction(0)
		seen["woken"] = woken.get_bullet_direction(1)
	)
	for tick in 10: # early break once the trigger expired
		step_factory(1)
		if seen.has("joiner"):
			break
	assert_true(seen.has("joiner"), "the handler ran inside a sweep")
	assert_eq([leaver.get_bullet_direction(0), leaver.get_bullet_direction(1)], seen["leaver"], "a volley that left mid-sweep does not turn")
	assert_eq(joiner.get_bullet_direction(0), seen["joiner"], "a volley that joined mid-sweep waits for the next pass")
	assert_eq(woken.get_bullet_direction(1), seen["woken"], "a bullet woken mid-sweep was not gathered: no turn")
	assert_eq(woken.debug_get_bullet_flock_steering(1), Vector2(), "its slot reads zero")
	assert_eq(rejoiner.get_bullet_direction(0), seen["rejoiner"], "a volley that switched flocks mid-sweep never steers by the old flock's result")
	step_factory(1)
	assert_ne(joiner.get_bullet_direction(0), seen["joiner"], "the joiner turns from the next tick on")
	assert_gt(woken.debug_get_bullet_flock_neighbors(1), 0, "the woken bullet is gathered next pass")
	assert_eq(factory.debug_get_flock_stats()["armed_volleys"], 3, "the freed volley left the armed count")


func test_every_exit_path_keeps_the_armed_count_exact() -> void:
	var f := H.make_flock(1.0, 1.0, 1.0)
	for k in 3:
		_volley(_cluster(Vector2(k * 100, 0), 3, k)).flock_join(f)
	var queued := _volley(_cluster(Vector2(0, 300), 3, 9))
	queued.flock_join(f)
	assert_eq(factory.debug_get_flock_stats()["armed_volleys"], 4, "four armed")
	queued.queue_free()
	await idle(1)
	assert_eq(factory.debug_get_flock_stats()["armed_volleys"], 3, "queue_free disarms")
	assert_true(factory.debug_assert_no_dangling()["ok"], "consistent")
	factory.free_active_bullets()
	assert_eq(factory.debug_get_flock_stats()["armed_volleys"], 0, "free_active_bullets disarms")
	for k in 2:
		_volley(_cluster(Vector2(k * 100, 0), 3, k)).flock_join(f)
	factory.reset()
	assert_eq(factory.debug_get_flock_stats()["armed_volleys"], 0, "reset disarms")
	_volley(_cluster(Vector2(0, 0), 3, 1)).flock_join(f)
	factory.clear_active_bullets()
	assert_eq(factory.debug_get_flock_stats()["armed_volleys"], 0, "clearing into the pool disarms")
	assert_true(factory.debug_assert_no_dangling()["ok"], "consistent after every exit")


func test_a_freed_factory_takes_its_flocks_along() -> void:
	var other := BulletFactory2D.new()
	add_child(other)
	await idle(1)
	var f := H.make_flock(1.0, 1.0, 1.0)
	for k in 3:
		var v: BulletVolley2D = other.spawn_volley(H.make_flight_data(_cluster(Vector2(k * 50, 0), 4, k), 100.0, 100.0))
		v.flock_join(f)
	for k in 5:
		other.debug_advance_time(DT)
	assert_eq(other.debug_get_flock_stats()["armed_volleys"], 3, "armed")
	other.free()
	await idle(1)
	var ref: WeakRef = weakref(f)
	f = null
	assert_null(ref.get_ref(), "no volley keeps the flock alive after its factory died")


func test_debug_get_bullet_flock_info_reads_one_bullet_at_a_glance() -> void:
	var tr := [Transform2D(0.0, Vector2(2, 2), 0.0, Vector2(0, 0)), Transform2D(0.0, Vector2(0, 30))]
	var d := H.make_flight_data(tr, 100.0)
	d.texture_size = Vector2(16, 16)
	d.texture_rotation_radians = 0.5
	var v: BulletVolley2D = factory.spawn_volley(d)
	var plain: Dictionary = v.debug_get_bullet_flock_info(0)
	assert_eq(plain.keys(), ["flocking", "flock", "scope", "steering", "neighbors", "blend", "radius", "heading", "facing", "max_turn_deg"], "exact keys")
	assert_eq([plain["flocking"], plain["blend"], plain["radius"], plain["scope"]], [false, 0.0, 0.0, -1], "a volley without a flock")
	var f := BulletFlock2D.new()
	f.blend_in_sec = 0.5
	v.flock_join(f)
	for k in 15:
		factory.debug_advance_time(DT)
	var info: Dictionary = v.debug_get_bullet_flock_info(0)
	assert_true(info["flocking"], "flocking")
	assert_eq(info["flock"], f, "its flock")
	assert_eq(info["scope"], BulletFlock2D.SCOPE_SHARED, "join's default scope")
	assert_eq(info["steering"], v.debug_get_bullet_flock_steering(0), "the pass's steering")
	assert_eq(info["neighbors"], 1, "one flockmate")
	var x := 0.25 / 0.5
	assert_almost_eq(float(info["blend"]), x * x * (3.0 - 2.0 * x), 1e-6, "halfway through the blend-in")
	assert_almost_eq(float(info["radius"]), 16.0, 1e-4, "a 16 px quad at 2x: radius 16")
	assert_almost_eq(float(v.debug_get_bullet_flock_info(1)["radius"]), 8.0, 1e-4, "at 1x: radius 8")
	assert_eq(info["heading"], v.get_bullet_direction(0), "the flight heading")
	assert_almost_eq(float(info["facing"]), v.get_bullet_direction(0).angle(), 1e-4, "face_heading: the sprite (texture rotation stripped) faces the heading")
	assert_eq(info["max_turn_deg"], 270.0, "the cap")
	assert_eq(v.debug_get_bullet_flock_info(5), {}, "OOB: empty")
	expect_error("Invalid bullet index in debug_get_bullet_flock_info: 5 (amount_bullets: 2)")
