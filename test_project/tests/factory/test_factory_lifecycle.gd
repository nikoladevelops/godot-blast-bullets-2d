extends BlastTest
## Factory lifecycle: lazy init + state, spawn-data validation (null/empty/
## NaN/invisible lint), expected pool key, pool hit/miss accounting, deferred
## structural wrappers, pooling-flag reset on a new life, wake alias,
## free_volley_deferred, NaN-spawn atomicity, live ids + reset.
##
## reset_finished: emitted exactly once per accepted reset (full, scoped and
## deferred), never for a rejected one, after every volley is gone. A handler
## may respawn right away (the "next wave" pattern): its volley spawns, ticks
## and is not wiped by the reset that announced itself.

func _data(n: int = 4) -> BulletVolleyData2D:
	var data := H.make_volley_data(n, 200.0, 5.0)
	var arr: Array = []
	for i in n:
		arr.append(Transform2D(0.0, Vector2(20.0 * i, 0.0)))
	data.transforms = arr
	return data


func test_ensure_and_state() -> void:
	assert_true(factory.ensure_factory_initialized(), "ensure_factory_initialized true in tree")
	var st: Dictionary = factory.debug_get_factory_state()
	assert_true(st.get("is_ready", false), "factory state is_ready")
	var keys: Array = st.keys()
	keys.sort()
	assert_eq(keys, ["attachments_pooled", "is_busy", "is_iterating", "is_ready", "is_tearing_down", "processing", "volleys_pooled", "volleys_total"], "exact state keys")
	var interp: Dictionary = factory.debug_check_interpolation_status()
	assert_has(interp, "mismatch")
	assert_has(interp, "hint")


func test_validate_and_invisible_lint() -> void:
	assert_false(BulletFactory2D.debug_validate_spawn_data(null).get("ok", true), "null rejected")
	assert_false(BulletFactory2D.debug_validate_spawn_data(BulletVolleyData2D.new()).get("ok", true), "empty transforms rejected")
	var nan_data := _data(2)
	nan_data.transforms = [Transform2D(0.0, Vector2(NAN, 0)), nan_data.transforms[1]]
	assert_false(BulletFactory2D.debug_validate_spawn_data(nan_data).get("ok", true), "NaN rejected")
	expect_error_sequence(["Error in debug_validate_spawn_data: transforms[0] contains NaN/Inf. Nothing was spawned."])
	var invis := BulletVolleyData2D.new()
	invis.transforms = [Transform2D.IDENTITY]
	invis.max_life_time = 2.0
	var invis_res: Dictionary = BulletFactory2D.debug_validate_spawn_data(invis)
	assert_true(invis_res.get("ok", false), "invisible data still valid")
	assert_ne(invis_res.get("warning", ""), "", "invisible bullets warn")
	var good := _data(3)
	assert_true(BulletFactory2D.debug_validate_spawn_data(good).get("ok", false), "good data ok")
	var key: VolleyPoolKey2D = BulletFactory2D.debug_expected_pool_key(good)
	assert_not_null(key)
	assert_eq(key.get_amount_bullets(), 3, "expected pool key amount")
	assert_null(BulletFactory2D.debug_expected_pool_key(null), "expected key null on null data")
	expect_error_sequence(["debug_expected_pool_key: spawn_data is null or has no transforms."])


func test_spawn_pool_stats() -> void:
	factory.debug_reset_pool_stats()
	assert_eq(factory.debug_get_pool_hit_stats().get("misses", -1), 0, "pool stats reset")
	factory.spawn_volley(_data(3))
	await physics()
	assert_eq(factory.debug_get_pool_hit_stats().get("misses", 0), 1, "first spawn is a miss")
	assert_eq(factory.debug_get_total_bullets_amount(), 1, "total volleys")
	assert_eq(factory.debug_get_active_volleys_amount(), 1, "active volleys")


func test_deferred_wrappers_survive_physics() -> void:
	factory.spawn_volley(_data(2))
	await physics()
	factory.free_active_bullets_deferred(null)
	factory.free_disabled_bullets_deferred(null)
	factory.free_bullets_pool_deferred(null)
	factory.free_attachments_pool_deferred()
	factory.reset_deferred(null)
	factory.free_volley_deferred(null)
	expect_error("volley is null")
	await idle()
	await physics()
	assert_false(factory.debug_get_factory_state().get("is_tearing_down", true), "factory alive after deferred structural ops")
	assert_eq(factory.debug_get_active_volleys_amount(), 0, "deferred free/reset ran")


func test_pooling_flags_reset_on_new_life() -> void:
	var v1: BulletVolley2D = factory.spawn_volley(_data(2))
	assert_not_null(v1)
	v1.set_is_auto_pooling_enabled(false)
	v1.set_is_attachments_auto_pooling_enabled(false)
	assert_false(v1.debug_get_volley_info().get("auto_pool_volley", true), "flags flipped")
	v1.set_is_auto_pooling_enabled(true)
	for i in v1.get_amount_bullets():
		v1.disable_bullet(i)
	await physics()
	var v2: BulletVolley2D = factory.spawn_volley(_data(2))
	assert_not_null(v2)
	var info2: Dictionary = v2.debug_get_volley_info()
	assert_true(info2.get("auto_pool_volley", false), "pooling flags reset on new life")
	assert_eq(info2.get("amount_bullets", 0), 2, "volley info amount")
	assert_has(info2, "generation")
	assert_has(info2, "is_pooled")


func test_wake_alias_and_free_volley_deferred() -> void:
	var v3: BulletVolley2D = factory.spawn_volley(_data(2))
	v3.disable_bullet(0)
	v3.enable_bullet(0)
	assert_true(v3.is_bullet_status_enabled(0), "enable_bullet revives slot")
	factory.free_volley_deferred(v3)
	assert_true(v3.is_queued_for_deletion(), "volley queued for deletion")
	await idle()
	assert_false(is_instance_valid(v3), "volley freed at the flush")


func test_nan_spawn_is_atomic() -> void:
	var before: int = factory.debug_get_total_bullets_amount()
	var nan_spawn := _data(2)
	nan_spawn.transforms = [Transform2D(0.0, Vector2(INF, 0)), Transform2D.IDENTITY]
	factory.spawn_volley(nan_spawn)
	expect_error_sequence(["Error in spawn_volley: transforms[0] contains NaN/Inf. Nothing was spawned."])
	await physics()
	assert_eq(factory.debug_get_total_bullets_amount(), before, "NaN spawn left no volley")


func test_live_ids_and_reset() -> void:
	var spawner := BulletSpawner2D.new()
	spawner.set_shooting_enabled(false)
	spawner.set_homing_enabled(false)
	add(spawner)
	await idle(1)
	spawner.set_bullet_factory(factory)
	spawner.set_spawn_data(_data(2))
	assert_eq(factory.debug_get_live_volley_ids(spawner.get_instance_id()).size(), 0, "no live volleys for fresh spawner")
	factory.spawn_volley(_data(2))
	await idle()
	factory.reset()
	await physics()
	assert_eq(factory.debug_get_total_bullets_amount(), 0, "reset drained volleys")


func test_freeing_the_factory_frees_volleys_moved_out_of_its_tree() -> void:
	# A script may move a volley under another node (local-space bullets on
	# a moving ship). The volley still belongs to the factory: it must die
	# with it, never outlive it holding a dangling factory pointer (its own
	# teardown later would call into the freed factory).
	var other := BulletFactory2D.new()
	add_child(other)
	await idle(1)
	var moved: BulletVolley2D = other.spawn_volley(H.make_flight_data([Transform2D(0.0, Vector2.ZERO), Transform2D(0.0, Vector2(0, 20))], 100.0, 100.0))
	moved.flock_join(BulletFlock2D.new())
	var stays: BulletVolley2D = other.spawn_volley(H.make_volley_data(2))
	var holder := Node2D.new()
	add_child(holder)
	moved.reparent(holder)
	var moved_ref: WeakRef = weakref(moved)
	var stays_ref: WeakRef = weakref(stays)
	other.free()
	assert_false(is_instance_valid(moved_ref.get_ref()), "the moved volley died with its factory")
	assert_false(is_instance_valid(stays_ref.get_ref()), "the volley in the factory's tree too")
	assert_eq(holder.get_child_count(), 0, "the holder lost the dead volley")
	holder.free()


func test_every_accepted_reset_emits_once_and_a_rejected_one_never() -> void:
	watch_signals(factory)
	factory.spawn_volley(H.make_still_data(2))
	factory.reset()
	assert_signal_emit_count(factory, "reset_finished", 1, "a full reset")
	assert_eq(factory.get_active_bullet_count(), 0, "every bullet gone when it fires")
	var scoped := H.make_still_data(3)
	factory.spawn_volley(scoped)
	factory.reset(BulletFactory2D.debug_expected_pool_key(scoped))
	assert_signal_emit_count(factory, "reset_finished", 2, "a scoped reset")
	await physics()
	factory.reset()
	expect_errors_containing("reset", 1, "structural calls are rejected inside the physics step")
	assert_signal_emit_count(factory, "reset_finished", 2, "a rejected reset emits nothing")
	await idle(1)
	factory.reset_deferred()
	assert_signal_emit_count(factory, "reset_finished", 2, "deferred: not yet")
	for i in 5:
		await idle(1)
		if get_signal_emit_count(factory, "reset_finished") > 2:
			break
	assert_signal_emit_count(factory, "reset_finished", 3, "the deferred reset emits once")


func test_a_reset_finished_handler_can_spawn_the_next_wave() -> void:
	var next: Array = []
	factory.spawn_volley(H.make_volley_data(2, 60.0))
	factory.reset_finished.connect(func(): next.append(factory.spawn_volley(H.make_volley_data(2, 60.0))))
	factory.reset()
	assert_eq(next.size(), 1, "the handler ran once")
	assert_not_null(next[0], "the next wave spawned inside the handler")
	assert_eq(factory.get_active_bullet_count(), 2, "and is alive after the reset returns")
	await idle(3)
	assert_almost_eq((next[0] as BulletVolley2D).get_curves_elapsed_time(), 3.0 / 60.0, 1e-6, "and ticks normally")


## A PARKED volley (auto pooling off, every bullet out) is frozen but still
## the caller's, documented as wakeable: free_disabled_bullets() and
## free_bullets_pool() free POOLED volleys only and leave it alone (it wakes
## afterwards); free_active_bullets() still destroys everything.
func test_free_disabled_and_pool_spare_parked_volleys() -> void:
	var v: BulletVolley2D = factory.spawn_volley(H.make_still_data(2))
	v.set_is_auto_pooling_enabled(false)
	var pooled: BulletVolley2D = factory.spawn_volley(H.make_still_data(3))
	await idle(1)
	v.disable_bullet(0)
	v.disable_bullet(1)
	factory.clear_active_bullets() # pools the other one
	await idle(1)
	assert_true(v.is_parked(), "drained with pooling off: parked")
	assert_true(pooled.is_pooled(), "the other volley is pooled")
	factory.free_disabled_bullets()
	assert_true(is_instance_valid(v) and v.is_parked(), "free_disabled_bullets spares the parked volley")
	assert_false(is_instance_valid(pooled), "and frees the pooled one")
	factory.free_bullets_pool()
	assert_true(is_instance_valid(v) and v.is_parked(), "free_bullets_pool spares it too")
	v.enable_bullet(0)
	assert_true(v.is_bullet_status_enabled(0), "the parked volley still wakes")
	factory.free_active_bullets()
	assert_false(is_instance_valid(v), "free_active_bullets destroys it")


## Pool keys are Resources, so == compares identity: equals() compares the
## bucket (amount + shape), which is how debug_expected_pool_key is matched
## against the keys of debug_get_bullets_pool_info.
func test_pool_keys_compare_by_value_with_equals() -> void:
	var a := VolleyPoolKey2D.make(4, PhysicsServer2D.SHAPE_CIRCLE)
	assert_has_method(a, "equals", "bound equals")
	assert_true(a.equals(VolleyPoolKey2D.make(4, PhysicsServer2D.SHAPE_CIRCLE)), "same bucket")
	assert_false(a.equals(VolleyPoolKey2D.make(5, PhysicsServer2D.SHAPE_CIRCLE)), "another amount")
	assert_false(a.equals(VolleyPoolKey2D.make(4, PhysicsServer2D.SHAPE_RECTANGLE)), "another shape")
	assert_false(a.equals(null), "null is no bucket")
	var data := H.make_still_data(4)
	factory.spawn_volley(data)
	await idle(1)
	factory.clear_active_bullets()
	await idle(1)
	var expected: VolleyPoolKey2D = BulletFactory2D.debug_expected_pool_key(data)
	var found := 0
	for key in factory.debug_get_bullets_pool_info():
		if expected.equals(key):
			found += 1
	assert_eq(found, 1, "the expected key matches its pool bucket by value")


## debug_get_live_volley_ids lists live volleys only, like every sibling
## counter: a volley queued for deletion is not live.
func test_live_volley_ids_skip_volleys_queued_for_deletion() -> void:
	var keep: BulletVolley2D = factory.spawn_volley(H.make_still_data(2))
	var gone: BulletVolley2D = factory.spawn_volley(H.make_still_data(2))
	gone.queue_free()
	assert_eq(Array(factory.debug_get_live_volley_ids(0)), [keep.get_instance_id()], "the queued volley is not listed")
	assert_eq(factory.debug_get_active_volleys_amount(), 1, "the sibling counter agrees")


## A factory script overriding _ready / _physics_process / _process (GDScript
## refuses super._ready() on an extension class) still gets the full native
## start and tick: no warning, one set of containers, bullets fly.
func test_a_script_with_its_own_callbacks_keeps_the_factory_running() -> void:
	var s := GDScript.new()
	s.source_code = "extends BulletFactory2D\nvar readied := false\nvar physics := 0\nfunc _ready() -> void:\n\treadied = true\nfunc _physics_process(_d: float) -> void:\n\tphysics += 1\nfunc _process(_d: float) -> void:\n\tpass\n"
	assert_eq(s.reload(), OK, "the script compiles")
	var f := BulletFactory2D.new()
	f.set_script(s)
	add(f)
	expect_no_errors("started without a warning")
	assert_true(f.readied, "the script's _ready ran")
	assert_true(f.debug_get_factory_state()["is_ready"], "the native start ran too")
	var plain := BulletFactory2D.new()
	add(plain)
	assert_eq(f.get_child_count(), plain.get_child_count(), "one set of containers, like a plain factory")
	var v: BulletVolley2D = f.spawn_volley(H.make_volley_data(1, 600.0, 5.0))
	assert_not_null(v, "spawns")
	var start: Vector2 = v.get_bullet_transform(0).origin
	for i in 10:
		await physics(1)
	assert_gt(v.get_bullet_transform(0).origin.distance_to(start), 50.0, "the native tick moves the bullets")
	assert_gt(f.physics, 0, "the script's own _physics_process ran too")
	await idle(1)
	f.free_active_bullets()


## The pool twins work from inside a physics frame (where the direct calls are
## refused): populate_bullets_pool_deferred, populate_attachments_pool_deferred
## and free_attachments_pool_for_scene_deferred queue onto the next idle
## frame; a missing argument is refused at once, nothing queued.
func test_pool_deferred_twins_run_on_the_next_idle_frame() -> void:
	var scene := make_probe_scene()
	var data := _data(3)
	await physics()
	assert_true(Engine.is_in_physics_frame(), "inside physics")
	factory.populate_bullets_pool_deferred(VolleyPoolKey2D.make(3, PhysicsServer2D.SHAPE_CIRCLE), data, 2)
	factory.populate_attachments_pool_deferred(scene, 4)
	assert_eq(factory.debug_get_bullets_pool_amount(), 0, "queued, not run")
	await idle()
	assert_eq(factory.debug_get_bullets_pool_amount(), 2, "two volleys pooled")
	assert_eq(factory.debug_get_attachments_pool_amount(), 4, "four attachments pooled")
	assert_eq(str(factory.debug_get_attachments_pool_info()).is_empty(), false, "a readable pool readout")
	await physics()
	factory.free_attachments_pool_for_scene_deferred(scene)
	await idle()
	assert_eq(factory.debug_get_attachments_pool_amount(), 0, "the scene's attachments freed")
	factory.populate_bullets_pool_deferred(null, data, 1)
	factory.populate_bullets_pool_deferred(VolleyPoolKey2D.make(3, PhysicsServer2D.SHAPE_CIRCLE), null, 1)
	factory.populate_attachments_pool_deferred(null, 2)
	factory.populate_attachments_pool_deferred(scene, 0)
	factory.free_attachments_pool_for_scene_deferred(null)
	expect_error_sequence([
		"populate_bullets_pool_deferred requires an explicit VolleyPoolKey2D. Nothing was queued.",
		"populate_bullets_pool_deferred: spawn_data is null. Nothing was queued.",
		"populate_attachments_pool_deferred: needs a scene and amount_attachments > 0. Nothing was queued.",
		"populate_attachments_pool_deferred: needs a scene and amount_attachments > 0. Nothing was queued.",
		"free_attachments_pool_for_scene_deferred: attachment_scene is null. Nothing was queued.",
	])
