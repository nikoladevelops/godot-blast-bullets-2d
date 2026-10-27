extends BlastTest
## Steady-state retention: once pools are warm, repeated shots/spawns must
## not grow the live Object count (a retention leak survives until exit, so
## the --verbose exit report never sees it). Pins the spawn-warning spam
## found by benchmarks: a short per-bullet array warned on EVERY spawn and
## (under GUT) kept one tracked-error object per line alive. Config warnings
## now fire once per (resource, warning, sizes) via WarnOnce2D.

func _objs() -> int:
	return int(Performance.get_monitor(Performance.OBJECT_COUNT))


## `audio`: the shots play sounds. Headless audio frees a stopped playback
## on its own real-time mix cycles (tests run faster than real time), so
## both counts wait for it first.
func _steady(f: Callable, warm := 30, measured := 200, audio := false) -> int:
	for i in warm:
		f.call()
		await physics()
	await idle(4)
	if audio:
		OS.delay_msec(700)
		await idle(2)
	var before := _objs()
	for i in measured:
		f.call()
		await physics()
	await idle(4)
	if audio:
		OS.delay_msec(700)
		await idle(2)
	return _objs() - before


func test_spawner_shots_retain_nothing() -> void:
	var sp := make_spawner(H.make_volley_data(4, 250.0, 0.05), BulletSpawner2D.PATTERN_FROM_HELPER_HEART, 300)
	var grown: int = await _steady(func():
		sp.rotation += 0.01
		sp.shoot_once())
	assert_eq(grown, 0, "200 warm spawner shots: zero retained objects")


func test_graze_shots_retain_nothing() -> void:
	# Every shot grazes (a target sits on the muzzle) and a handler listens:
	# events, zone references and per-bullet state must all recycle.
	make_graze_target(Vector2(5, 0))
	var sp := make_spawner(H.make_volley_data(4, 250.0, 0.05), BulletSpawner2D.PATTERN_FROM_HELPER_RING, 8)
	sp.graze_zones = [H.make_graze_zone([400.0, 200.0])] # the ring pattern spawns ~100 px out
	sp.graze_enabled = true
	sp.graze_node_group = &"graze_targets"
	var grazes := [0]
	factory.bullet_grazed.connect(func(_t: Node2D, _v: BulletVolley2D, _i: int, _z: GrazeZone2D, _r: int) -> void: grazes[0] += 1)
	var grown: int = await _steady(func(): sp.shoot_once())
	assert_gt(grazes[0], 1000, "the shots really grazed (%d events)" % grazes[0])
	assert_eq(grown, 0, "200 warm grazing shots: zero retained objects")


func test_script_spawns_with_short_arrays_retain_nothing() -> void:
	var d := H.make_volley_data(3, 250.0, 0.05) # 3 speed entries...
	var arr: Array = []
	for i in 200:
		arr.append(Transform2D(0.0, Vector2(i * 4, 0)))
	d.transforms = arr # ...for 200 bullets: a size-mismatch warning case
	d.all_bullet_gravity = [Vector2(0, 10)]
	var grown: int = await _steady(func(): factory.spawn_volley(d))
	assert_eq(grown, 0, "200 warm short-array spawns: zero retained objects")


func test_size_warning_once_per_resource_and_rearms() -> void:
	var d := H.make_volley_data(2, 100.0, 1.0)
	var arr: Array = []
	for i in 10:
		arr.append(Transform2D(0.0, Vector2(i * 20, 0)))
	d.transforms = arr
	for i in 5:
		factory.spawn_volley(d)
	var seen := 0
	for err in get_errors():
		if err.is_push_warning() and err.contains_text("all_bullet_speed_data size (2) != bullets (10)"):
			seen += 1
			err.handled = true
	assert_eq(seen, 1, "5 spawns of one resource: the size warning fires once")
	d.all_bullet_speed_data = [H.make_speed(50.0), H.make_speed(60.0), H.make_speed(70.0)]
	factory.spawn_volley(d)
	seen = 0
	for err in get_errors():
		if not err.handled and err.is_push_warning() and err.contains_text("all_bullet_speed_data size (3) != bullets (10)"):
			seen += 1
	assert_eq(seen, 1, "a new array size re-arms the warning")
	var other := d.duplicate() as BulletVolleyData2D
	factory.spawn_volley(other)
	seen = 0
	for err in get_errors():
		if not err.handled and err.is_push_warning() and err.contains_text("all_bullet_speed_data size (3) != bullets (10)"):
			seen += 1
	assert_eq(seen, 2, "a different resource with the same mistake warns too")


func test_flocking_shots_retain_nothing(scope: int = use_parameters([BulletFlock2D.SCOPE_PER_SPAWNER, BulletFlock2D.SCOPE_SHARED, BulletFlock2D.SCOPE_PER_VOLLEY])) -> void:
	# Every shot flocks (dense ring, every rule on): configs, views and the
	# factory's pass buffers must recycle with the pool.
	var sp := make_spawner(H.make_volley_data(4, 250.0, 0.05), BulletSpawner2D.PATTERN_FROM_HELPER_RING, 24)
	sp.helper_ring_radius = 30.0
	sp.flock_enabled = true
	sp.flock = BulletFlock2D.new()
	sp.flock_scope = scope
	var grown: int = await _steady(func(): sp.shoot_once())
	assert_gt(factory.get_frame_stats()["flock_usec"], -1, "the pass ran")
	assert_eq(grown, 0, "200 warm flocking shots: zero retained objects")
	assert_true(factory.debug_assert_no_dangling()["ok"], "consistent")


func test_joined_factory_spawns_and_swapped_flocks_retain_nothing() -> void:
	# A new flock resource per spawn (the old ones must be released with
	# their volleys), joined by script.
	var d := H.make_volley_data(4, 250.0, 0.05)
	var grown: int = await _steady(func():
		var v: BulletVolley2D = factory.spawn_volley(d)
		v.flock_join(H.make_flock(1.0, 1.0, 1.0), BulletFlock2D.SCOPE_PER_VOLLEY))
	assert_eq(grown, 0, "200 warm joined spawns with fresh flocks: zero retained objects")
	assert_true(factory.debug_assert_no_dangling()["ok"], "consistent")


## A LaserSpawner2D of `type` firing short lasers through a target and a
## wall, graze and sound armed: every life recycles through the laser pool.
func _laser_spawner(type: int) -> LaserSpawner2D:
	var d := H.make_laser(0.0, 0.0, 0.05, 0.0, 200.0)
	d.type = type
	d.speed = 1200.0
	d.wall_mask = 16
	d.hit_interval_sec = 0.02
	var sp := LaserSpawner2D.new()
	sp.set_shooting_enabled(false)
	sp.laser_data = d
	sp.pattern_source = LaserSpawner2D.PATTERN_FROM_HELPER_RING
	sp.helper_bullets_amount = 6
	sp.helper_ring_radius = 10.0
	sp.graze_zones = [H.make_graze_zone([60.0])]
	sp.graze_enabled = true
	sp.graze_node_group = &"graze_targets"
	sp.sound_effects = [H.make_sound(BulletSoundData2D.SOUND_ON_HIT)]
	sp.sound_enabled = true
	add(sp)
	sp.set_bullet_factory(factory)
	return sp


func test_laser_shots_retain_nothing(type: int = use_parameters([LaserData2D.LASER_BEAM, LaserData2D.LASER_BOLT, LaserData2D.LASER_CURVY])) -> void:
	make_area(Vector2(60, 0), Vector2(20, 400))
	make_wall(Vector2(150, 0), Vector2(20, 400), 16)
	make_graze_target(Vector2(40, 30))
	await idle(2)
	var sp := _laser_spawner(type)
	var hits := [0]
	sp.laser_hit.connect(func(_t, _l, _b, _p) -> void: hits[0] += 1)
	var grown: int = await _steady(func():
		sp.rotation += 0.05
		sp.shoot_once(), 30, 200, true)
	assert_gt(hits[0], 100, "the lasers really hit (%d)" % hits[0])
	assert_eq(grown, 0, "200 warm laser shots: zero retained objects")
	assert_lt(factory.debug_get_laser_pool_amount() + factory.get_active_laser_count(), 12, "a bounded laser pool")


func test_emitter_colored_lifetime_shots_retain_nothing() -> void:
	var tpl := make_spawner(H.make_volley_data(3, 200.0, 0.05), BulletSpawner2D.PATTERN_FROM_HELPER_RING, 3)
	var d := H.make_volley_data(4, 150.0, 0.12)
	d.all_bullet_life_times = PackedFloat64Array([0.05, 0.08, 0.1, 0.12])
	d.scale_in_sec = 0.04
	var sp := make_spawner(d, BulletSpawner2D.PATTERN_FROM_HELPER_RING, 4)
	sp.bullet_color_mode = BulletSpawner2D.COLOR_MODE_GRADIENT_ACROSS_PATTERN
	var e := BulletEmitterData2D.new()
	e.interval_sec = 0.03
	sp.emitter = e
	sp.emitter_spawner_path = sp.get_path_to(tpl)
	sp.emitter_enabled = true
	var children := [0]
	tpl.volley_fired.connect(func(_v, _n) -> void: children[0] += 1)
	# The drain also lets an earlier test's sounds finish freeing first.
	var grown: int = await _steady(func(): sp.shoot_once(), 30, 200, true)
	assert_gt(children[0], 100, "the bullets really emitted (%d)" % children[0])
	assert_eq(grown, 0, "200 warm emitter / colored / per-bullet-lifetime shots: zero retained objects")
