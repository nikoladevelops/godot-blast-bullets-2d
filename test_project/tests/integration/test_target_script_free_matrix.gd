extends BlastTest
## Scripts on the things bullets hit or aim at are user code the plugin runs
## outside any signal: a bounce reads the hit target's properties with
## Object.get (a script _get or a property getter runs), an aimed pattern
## calls the target's get_velocity(). Such code may free the volley, the
## factory or the spawner (with free(), or the way an exported release game
## does: BulletFactory2D.debug_free_like_release). The plugin never touches
## freed memory (tools/run_sanitizer_tests.py runs this under
## AddressSanitizer), reports nothing, and the books stay balanced.

## Frees `victim()` the first time property `prop` is read for the `nth` time.
const HOSTILE_GET := """
extends %s
var prop: StringName
var nth := 1
var reads := 0
var action: Callable
func _get(property: StringName) -> Variant:
	if property == prop:
		reads += 1
		if reads == nth and action.is_valid():
			action.call()
	return null
"""

## Reports a velocity and frees `victim()` on the first call.
const HOSTILE_VELOCITY := """
extends Node2D
var action: Callable
var calls := 0
func get_velocity() -> Vector2:
	calls += 1
	if calls == 1 and action.is_valid():
		action.call()
	return Vector2(30, 0)
"""


func _script(source: String) -> GDScript:
	var s := GDScript.new()
	s.source_code = source
	assert_eq(s.reload(), OK, "the hostile script compiles")
	return s


func _free_now(obj: Object, how: String) -> void:
	if not is_instance_valid(obj):
		return
	if how == "release":
		BulletFactory2D.debug_free_like_release(obj)
	else:
		obj.free()


## Bouncing volley data on layer value 16 (bit 5): detects and bounces off it.
func _bounce_data(n: int) -> BulletVolleyData2D:
	var d := H.make_volley_data(n, 200.0, 5.0)
	d.monitorable = true
	d.collision_shape = H.make_circle_shape(6.0)
	d.set_collision_mask_from_array([5])
	d.set_bounce_mask_from_array([5])
	return d


## A hit target carrying HOSTILE_GET, overlapping the origin.
func _hostile_target(kind: String, prop: StringName, nth: int) -> CollisionObject2D:
	var target: CollisionObject2D
	if kind == "area":
		target = make_area(Vector2(20, 0), Vector2(40, 200), 16)
	else:
		target = make_wall(Vector2(20, 0), Vector2(40, 200), 16)
	target.set_script(_script(HOSTILE_GET % ("Area2D" if kind == "area" else "StaticBody2D")))
	target.prop = prop
	target.nth = nth
	return target


## [target kind, property, nth read, victim, free kind]. Queue time: the
## first velocity read (physics callback). Drain time: the layer read, and
## the velocity fallback of an Area2D (it exposes no velocity at queue time,
## so the drain reads again).
func _target_cases() -> Array:
	var out: Array = []
	var reads := [["body", &"linear_velocity", 1], ["area", &"collision_layer", 1], ["area", &"linear_velocity", 2]]
	for r in reads:
		for victim in ["volley", "factory", "spawner"]:
			for how in ["free", "release"]:
				out.append([r[0], r[1], r[2], victim, how])
		out.append([r[0], r[1], r[2], "clear", "free"]) # takes the reading bullet out
	return out


func test_a_hit_target_getter_that_frees(p: Array = use_parameters(_target_cases())) -> void:
	handle_events(factory) # the hits / grazes / expiries are handled; this test pins something else
	var kind: String = p[0]
	var victim: String = p[3]
	var how: String = p[4]
	if victim == "factory":
		check_factory_after = false
	var target := _hostile_target(kind, p[1], p[2])
	var volley: BulletVolley2D
	var spawner: BulletSpawner2D
	if victim == "spawner":
		spawner = make_spawner(_bounce_data(1), BulletSpawner2D.PATTERN_FROM_HELPER_RING, 1)
		spawner.helper_ring_radius = 0.0
		spawner.orphaned_volleys = BulletSpawner2D.ORPHANED_VOLLEYS_REMOVE
		assert_true(spawner.shoot_once(), "the spawner fires")
		volley = spawner.get_live_volleys()[0]
	else:
		volley = factory.spawn_volley(_bounce_data(1))
	var f := factory
	var fired: Array = []
	var hits_after: Array = [] # hits reported once the getter ran
	var on_hit := func(_t, _v, i) -> void:
		if not fired.is_empty():
			hits_after.append(i)
	var hit_emitter: Object = spawner if spawner != null else factory
	hit_emitter.area_entered.connect(on_hit)
	hit_emitter.body_entered.connect(on_hit)
	target.action = func() -> void:
		fired.append(true)
		match victim:
			"volley":
				_free_now(volley, how)
			"factory":
				_free_now(f, how)
			"spawner":
				_free_now(spawner, how)
			"clear":
				volley.clear_all_bullets()
	for k in 30:
		if not fired.is_empty():
			break
		await physics(1)
	assert_false(fired.is_empty(), "the hostile read ran")
	await idle(2)
	if victim == "factory":
		assert_false(is_instance_valid(f), "the factory is gone")
	elif victim == "spawner":
		assert_false(is_instance_valid(spawner), "the spawner is gone")
	elif victim == "clear":
		assert_true(volley.is_pooled(), "the cleared volley is back in the pool")
		assert_eq(hits_after, [], "a bullet the getter took out reports no hit")
	elif volley != null:
		assert_false(is_instance_valid(volley), "the volley is gone")
	expect_no_errors()
	if is_instance_valid(f):
		var books: Dictionary = f.debug_assert_no_dangling()
		assert_true(books["ok"], str(books["error"]))


## A game's own Area2D (a player hurtbox) reports the bullet inside the
## physics server's query flush; its handler frees the factory or the volley.
func test_a_game_area_handler_inside_the_flush_frees(p: Array = use_parameters([["factory", "free"], ["factory", "release"], ["volley", "free"], ["volley", "release"]])) -> void:
	var victim: String = p[0]
	var how: String = p[1]
	if victim == "factory":
		check_factory_after = false
	var d := H.make_volley_data(8, 0.0, 5.0)
	d.monitorable = true
	d.collision_shape = H.make_circle_shape(6.0)
	var volley: BulletVolley2D = factory.spawn_volley(d)
	var hurtbox := make_area(Vector2.ZERO, Vector2(60, 60), 0)
	hurtbox.collision_mask = 0xFFFFFFFF
	var f := factory
	var fired: Array = []
	hurtbox.area_shape_entered.connect(func(_rid: RID, _a: Area2D, _s: int, _l: int) -> void:
		if fired.is_empty():
			fired.append(true)
			_free_now(f if victim == "factory" else volley, how))
	for k in 30:
		if not fired.is_empty():
			break
		await physics(1)
	assert_false(fired.is_empty(), "the hurtbox saw the bullets")
	await idle(2)
	assert_false(is_instance_valid(f if victim == "factory" else volley), "the %s is gone" % victim)
	expect_no_errors()


## [route, victim, free kind]: every way the aimed pattern reads its target.
func _aim_cases() -> Array:
	var out: Array = []
	for route in ["collect", "shot", "telegraph", "preview"]:
		for victim in ["spawner", "factory"]:
			for how in ["free", "release"]:
				out.append([route, victim, how])
	return out


func test_an_aimed_target_velocity_that_frees(p: Array = use_parameters(_aim_cases())) -> void:
	var route: String = p[0]
	var victim: String = p[1]
	var how: String = p[2]
	if victim == "factory":
		check_factory_after = false
	var target := Node2D.new()
	target.set_script(_script(HOSTILE_VELOCITY))
	target.position = Vector2(200, 0)
	add(target)
	var sp: BulletSpawner2D
	if route == "preview":
		sp = make_preview_spawner(BulletSpawner2D.PATTERN_FROM_HELPER_AIMED, 3)
	else:
		sp = make_spawner(H.make_volley_data(3, 100.0, 5.0), BulletSpawner2D.PATTERN_FROM_HELPER_AIMED, 3)
	sp.orphaned_volleys = BulletSpawner2D.ORPHANED_VOLLEYS_REMOVE
	sp.helper_aimed_prediction = 1.0
	sp.helper_aimed_prediction_time = 0.5
	var f := factory
	target.action = func() -> void: _free_now(sp if victim == "spawner" else f, how)
	sp.set_helper_aimed_target(target)
	match route:
		"collect":
			sp.collect_spawn_transforms()
		"shot":
			sp.shoot_once()
		"telegraph":
			sp.telegraph_enabled = true
			sp.telegraph_sec = 0.05
			sp.shoot_interval_sec = 0.1
			sp.set_shooting_enabled(true)
	for k in 30:
		if target.calls > 0:
			break
		await idle(1)
	assert_gt(target.calls, 0, "%s read the target's velocity" % route)
	await idle(2)
	assert_false(is_instance_valid(sp if victim == "spawner" else f), "the %s is gone" % victim)
	for err in get_errors(): # a shot whose factory vanished reports why
		if not err.handled and str(err.code).contains("no BulletFactory2D assigned"):
			err.handled = true
	expect_no_errors()


## The volley's OWN bullet_homing_target_reached handler frees the target node.
## The forward to the owner spawner runs AFTER that handler, so it must
## re-resolve the target by id and never pass the freed pointer (godot-cpp
## boxes the argument into a Variant). It arrives with a null target instead.
## tools/run_sanitizer_tests.py runs this under AddressSanitizer.
func test_a_homing_target_freed_by_the_volleys_own_handler_is_forwarded_null() -> void:
	make_graze_target(Vector2(40, 0), &"signal_targets")
	var d := H.make_volley_data(1, 300.0, 5.0)
	d.homing_distance_before_reached = 10.0
	var sp := make_spawner(d, BulletSpawner2D.PATTERN_FROM_SELF, 1)
	sp.homing_node_group = &"signal_targets"
	sp.set_homing_enabled(true)
	var seen: Array = []
	sp.connect("volley_bullet_homing_target_reached", func(_v, _i, t, _p) -> void:
		seen.append(t))
	var shots: Array = [] # a lambda captures locals by value: collect through an Array
	sp.volley_fired.connect(func(v: BulletVolley2D, _n: int) -> void: shots.append(v))
	sp.shoot_once()
	assert_eq(shots.size(), 1, "the shot fired")
	var volley: BulletVolley2D = shots[0]
	volley.bullet_homing_target_reached.connect(func(_vol, _i, t, _p) -> void:
		if is_instance_valid(t):
			t.free())
	for k in 40:
		await physics(1)
		if not seen.is_empty():
			break
	assert_eq(seen.size(), 1, "the spawner forward fired once")
	assert_eq(seen[0], null, "a target freed by the volley's handler reaches the spawner as null")
	expect_no_errors()
