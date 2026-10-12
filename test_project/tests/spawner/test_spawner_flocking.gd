extends BlastTest
## The spawner's Flocking group: flock_enabled + flock (BulletFlock2D) +
## flock_scope sit between Orbiting and Graze, the flock and scope hide while
## flocking is off, bad scopes are refused with the standard wording, the
## setup warnings name an empty flock and homing that re-aims every tick,
## fired volleys are armed before volley_fired, the scopes group bullets
## across spawners and shots as documented, edits (off, on, a new flock, a
## new scope) reach bullets in flight while bullets fired with flocking off
## never flock, a handler turning flocking off mid-sweep stops the turn at
## once, orphans and adopted volleys keep their flock, the spawner's own
## time scale freezes its bullets' turning, and a saved scene (the demo:
## a Shared swarm chasing the mouse) loads with every setting intact.

const DT := 1.0 / 60.0
const SCENE := preload("res://tests/scenes/flock_scene.tscn")


func _flock_spawner(flock: BulletFlock2D, scope := BulletFlock2D.SCOPE_PER_SPAWNER, at := Vector2(0, 0), amount := 4) -> BulletSpawner2D:
	var sp := make_spawner(H.make_volley_data(amount, 100.0, 1000.0), BulletSpawner2D.PATTERN_FROM_HELPER_RING, amount)
	sp.global_position = at
	sp.flock_enabled = true
	sp.flock = flock
	sp.flock_scope = scope
	return sp


func _wide_flock() -> BulletFlock2D:
	return H.make_flock(1.0, 1.0, 1.0, 400.0, 40.0)


func _fire(sp: BulletSpawner2D) -> BulletVolley2D:
	var got: Array = []
	sp.volley_fired.connect(func(v: BulletVolley2D, _n: int) -> void: got.append(v), CONNECT_ONE_SHOT)
	assert_true(sp.shoot_once(), "fires")
	assert_eq(got.size(), 1, "one volley")
	return got[0] if got.size() > 0 else null


func _step(n: int) -> void:
	for k in n:
		assert_true(factory.debug_advance_time(DT), "tick %d runs" % k)


func _headings(v: BulletVolley2D) -> Array:
	var out: Array = []
	for i in v.get_amount_bullets():
		out.append(v.get_bullet_direction(i))
	return out


func test_the_flocking_group_sits_after_orbiting_and_gates_its_knobs() -> void:
	var sp := make_spawner()
	assert_false(sp.flock_enabled, "off by default")
	assert_null(sp.flock, "no flock by default")
	assert_eq(sp.flock_scope, BulletFlock2D.SCOPE_PER_SPAWNER, "Per Spawner by default")
	var group := ""
	var found := {}
	for p in sp.get_property_list():
		var name := str(p["name"])
		if int(p["usage"]) & PROPERTY_USAGE_GROUP:
			group = name
		elif name in ["flock_enabled", "flock", "flock_scope"]:
			found[name] = [group, int(p["type"]), str(p["hint_string"])]
	assert_eq(found.get("flock_enabled", [""])[0], "Flocking", "flock_enabled in Flocking")
	assert_eq(found.get("flock", [""])[0], "Flocking", "flock in Flocking")
	assert_eq(found.get("flock", ["", 0, ""])[2], "BulletFlock2D", "flock takes a BulletFlock2D")
	assert_eq(found.get("flock_scope", ["", 0, ""])[2], "Per Spawner:0,Shared:1,Per Volley:2", "serialized scope ids")
	assert_false(is_editor_visible(sp, &"flock"), "hidden while off")
	assert_false(is_editor_visible(sp, &"flock_scope"), "hidden while off")
	assert_true(is_editor_visible(sp, &"flock_enabled"), "the switch is always shown")
	sp.flock_enabled = true
	assert_true(is_editor_visible(sp, &"flock"), "shown while on")
	assert_true(is_editor_visible(sp, &"flock_scope"), "shown while on")


func test_a_bad_scope_is_refused_and_kept() -> void:
	var sp := make_spawner()
	sp.flock_scope = BulletFlock2D.SCOPE_SHARED
	sp.flock_scope = 3
	sp.flock_scope = -1
	expect_error_sequence([
		"BulletSpawner2D: flock_scope must be 0 (Per Spawner), 1 (Shared) or 2 (Per Volley), keeping the old value.",
		"BulletSpawner2D: flock_scope must be 0 (Per Spawner), 1 (Shared) or 2 (Per Volley), keeping the old value.",
	])
	assert_eq(sp.flock_scope, BulletFlock2D.SCOPE_SHARED, "kept")


func test_setup_warnings_name_an_empty_flock_and_snapping_homing() -> void:
	var sp := make_spawner()
	assert_false(_has(sp.get_setup_warnings(), "Flocking"), "off: no flocking warning")
	sp.flock_enabled = true
	assert_true(_has(sp.get_setup_warnings(), "Flocking is enabled but flock is empty"), "an empty flock is named")
	sp.flock = BulletFlock2D.new()
	assert_false(_has(sp.get_setup_warnings(), "Flocking"), "a flock without homing is fine")
	sp.set_homing_enabled(true)
	assert_true(_has(sp.get_setup_warnings(), "Flocking with homing that re-aims every tick"), "homing_smoothing 0 snaps every tick")
	var data := H.make_volley_data(4)
	data.homing_smoothing = 3.0
	sp.set_spawn_data(data)
	assert_false(_has(sp.get_setup_warnings(), "Flocking with homing"), "a homing turn rate leaves room to flock")
	data.shared_bullet_rotation_data = H.make_rotation(1.0)
	assert_true(_has(sp.get_setup_warnings(), "Flocking with homing that re-aims every tick"), "rotation data makes homing snap")


func test_fired_volleys_are_armed_before_volley_fired() -> void:
	var f := _wide_flock()
	var sp := _flock_spawner(f, BulletFlock2D.SCOPE_PER_VOLLEY)
	var seen: Array = []
	sp.volley_fired.connect(func(v: BulletVolley2D, _n: int) -> void: seen.append([v.flock_get(), v.flock_get_scope()]))
	sp.shoot_once()
	assert_eq(seen, [[f, BulletFlock2D.SCOPE_PER_VOLLEY]], "the handler sees the armed flock and scope")
	assert_eq(factory.debug_get_flock_stats()["armed_volleys"], 1, "armed")


func test_scopes_group_bullets_across_spawners_and_shots() -> void:
	var f := _wide_flock()
	var a := _flock_spawner(f, BulletFlock2D.SCOPE_PER_SPAWNER, Vector2(0, 0))
	var b := _flock_spawner(f, BulletFlock2D.SCOPE_PER_SPAWNER, Vector2(30, 0))
	var va := _fire(a)
	var vb := _fire(b)
	_step(1)
	assert_eq(va.debug_get_bullet_flock_neighbors(0), 3, "Per Spawner: a's bullets see each other only")
	assert_eq(factory.debug_get_flock_stats()["flocks"], 2, "two flocks")
	a.flock_scope = BulletFlock2D.SCOPE_SHARED
	b.flock_scope = BulletFlock2D.SCOPE_SHARED
	_step(1)
	assert_eq(va.debug_get_bullet_flock_neighbors(0), 7, "Shared: both spawners' bullets fly as one flock (the edit reached them in flight)")
	assert_eq(factory.debug_get_flock_stats()["flocks"], 1, "one flock")
	a.flock_scope = BulletFlock2D.SCOPE_PER_VOLLEY
	var va2 := _fire(a)
	_step(1)
	assert_eq(va.debug_get_bullet_flock_neighbors(0), 3, "Per Volley: each shot alone")
	assert_eq(va2.debug_get_bullet_flock_neighbors(0), 3, "the second shot too")
	assert_eq(vb.debug_get_bullet_flock_neighbors(0), 3, "b (Shared) flies alone now that a left")


func test_edits_reach_bullets_in_flight_but_never_arm_old_shots() -> void:
	var f := _wide_flock()
	var sp := _flock_spawner(f)
	var v := _fire(sp)
	_step(1)
	assert_gt(v.debug_get_bullet_flock_neighbors(0), 0, "flocking")
	sp.flock_enabled = false
	assert_null(v.flock_get(), "off: the flying volley stops flocking")
	var headings := _headings(v)
	_step(10)
	assert_eq(_headings(v), headings, "no turn while off")
	var plain := _fire(sp)
	sp.flock_enabled = true
	assert_eq(v.flock_get(), f, "on again: the flying volley resumes")
	assert_null(plain.flock_get(), "a shot fired while flocking was off never flocks")
	var other := H.make_flock(0.0, 0.0, 2.0, 400.0, 0.0)
	sp.flock = other
	assert_eq(v.flock_get(), other, "a new flock reaches bullets in flight")
	_step(1)
	assert_gt(v.debug_get_bullet_flock_neighbors(0), 0, "flocking by the new rules")
	sp.flock = null
	assert_null(v.flock_get(), "no flock: no flocking")
	_step(1)
	assert_eq(v.debug_get_bullet_flock_neighbors(0), 0, "nothing gathered")


func test_a_handler_turning_flocking_off_mid_sweep_stops_the_turn_at_once() -> void:
	var trigger_data := H.make_flight_data([Transform2D(0.0, Vector2(-500, -500))], 0.0, 0.05)
	trigger_data.is_life_time_over_signal_enabled = true
	var trigger: BulletVolley2D = factory.spawn_volley(trigger_data)
	var sp := _flock_spawner(H.make_flock(0.0, 0.0, 5.0, 400.0, 0.0))
	var v := _fire(sp)
	var seen := {}
	factory.life_time_over.connect(func(volley: BulletVolley2D, _idx: Array) -> void:
		if volley == trigger:
			sp.flock_enabled = false
			seen["headings"] = _headings(v)
	)
	for tick in 10: # early break once the trigger expired
		_step(1)
		if seen.has("headings"):
			break
	assert_true(seen.has("headings"), "the handler ran inside a sweep")
	assert_eq(_headings(v), seen["headings"], "the spawner volley ticked after the handler without turning")


func test_orphans_and_adopted_volleys_keep_their_flock() -> void:
	var f := _wide_flock()
	var sp := _flock_spawner(f)
	sp.orphaned_volleys = BulletSpawner2D.ORPHANED_VOLLEYS_KEEP_FLYING
	var v := _fire(sp)
	var keeper := _flock_spawner(H.make_flock(0.0, 0.0, 0.0), BulletFlock2D.SCOPE_PER_SPAWNER, Vector2(500, 0))
	var adopted := _fire(sp)
	assert_true(keeper.adopt_live_volley(adopted), "adopted")
	assert_eq(adopted.flock_get(), f, "adoption leaves flocking alone")
	sp.queue_free()
	await idle(1)
	_step(1)
	assert_eq(v.flock_get(), f, "the orphan keeps its flock")
	assert_eq(v.debug_get_bullet_flock_neighbors(0), 7, "and its swarm: both of the dead spawner's volleys still fly as one flock")


func test_the_spawner_time_scale_freezes_its_bullets_turning() -> void:
	var sp := _flock_spawner(H.make_flock(0.0, 0.0, 5.0, 400.0, 0.0))
	sp.time_scale_affects_volleys = true
	var v := _fire(sp)
	_step(1)
	sp.time_scale = 0.0
	var headings := _headings(v)
	_step(10)
	assert_eq(_headings(v), headings, "a frozen spawner clock turns nothing")
	assert_gt(v.debug_get_bullet_flock_neighbors(0), 0, "the flock is still measured")


func test_the_demo_scene_loads_and_flocks() -> void:
	var root: Node2D = SCENE.instantiate()
	var swarm: BulletSpawner2D = root.get_node("Swarm")
	swarm.set_shooting_enabled(false) # before the tree: the demo auto-fires
	add(root)
	await idle(2)
	assert_true(swarm.flock_enabled, "flock_enabled survives")
	assert_not_null(swarm.flock, "the flock sub-resource loads")
	assert_eq(swarm.flock.separation, 2.0, "its rules too")
	assert_eq(swarm.flock.max_turn_rate_degrees, 360.0, "and the turn cap")
	assert_eq(swarm.flock_scope, BulletFlock2D.SCOPE_SHARED, "scope id 1 = Shared")
	assert_eq(swarm.homing_target_source, BulletSpawner2D.HOMING_SOURCE_MOUSE, "the swarm chases the mouse")
	var scene_factory: BulletFactory2D = root.get_node("BulletFactory2D")
	var got: Array = []
	swarm.volley_fired.connect(func(v: BulletVolley2D, _n: int) -> void: got.append(v))
	swarm.shoot_once()
	swarm.shoot_once()
	for k in 30:
		scene_factory.debug_advance_time(DT)
	assert_eq(got.size(), 2, "two shots")
	var stats: Dictionary = scene_factory.debug_get_flock_stats()
	assert_eq(stats["flocks"], 1, "Shared: both shots fly as one swarm")
	assert_eq(stats["grid_members"], 48, "24 + 24 bullets")
	assert_gt(got[0].debug_get_bullet_flock_neighbors(0), 0, "flocking")
	for i in got[0].get_amount_bullets():
		assert_true(got[0].get_bullet_transform(i).is_finite(), "bullet %d finite" % i)
	assert_true(scene_factory.debug_assert_no_dangling()["ok"], "consistent")


func _has(w: PackedStringArray, needle: String) -> bool:
	for s in w:
		if s.contains(needle):
			return true
	return false


func test_flock_live_volleys_arms_the_bullets_already_flying() -> void:
	var f := _wide_flock()
	var sp := _flock_spawner(f)
	sp.flock_enabled = false
	var early := _fire(sp)
	var early2 := _fire(sp)
	var other := _flock_spawner(H.make_flock(), BulletFlock2D.SCOPE_PER_SPAWNER, Vector2(600, 0))
	other.flock_enabled = false
	var foreign := _fire(other)
	var loose: BulletVolley2D = factory.spawn_volley(H.make_volley_data(3))
	assert_eq(sp.flock_live_volleys(), 0, "flocking off: nothing to arm")
	sp.flock_enabled = true
	assert_null(early.flock_get(), "flying bullets do not flock by themselves")
	assert_eq(sp.flock_live_volleys(), 2, "both flying volleys of this spawner join")
	assert_eq([early.flock_get(), early2.flock_get()], [f, f], "armed with the spawner's flock")
	assert_null(foreign.flock_get(), "another spawner's bullets are untouched")
	assert_null(loose.flock_get(), "factory volleys are untouched")
	assert_eq(sp.flock_live_volleys(), 0, "already flocking: nothing new to arm")
	_step(1)
	assert_gt(early.debug_get_bullet_flock_neighbors(0), 0, "they flock from the next tick")
	assert_eq(early.debug_get_bullet_flock_neighbors(0), 7, "with the other early volley (Per Spawner)")
	sp.flock = null
	assert_eq(sp.flock_live_volleys(), 0, "no flock: nothing to arm")


func test_flock_live_volleys_replaces_a_script_join() -> void:
	var f := _wide_flock()
	var sp := _flock_spawner(f)
	var v := _fire(sp)
	v.flock_join(H.make_flock(), BulletFlock2D.SCOPE_PER_VOLLEY)
	assert_ne(v.flock_get(), f, "the script override")
	assert_eq(sp.flock_live_volleys(), 1, "the explicit call takes it back")
	assert_eq(v.flock_get(), f, "flying with the spawner's flock again")
	sp.flock = _wide_flock()
	assert_eq(v.flock_get(), sp.flock, "and its edits reach it again")
