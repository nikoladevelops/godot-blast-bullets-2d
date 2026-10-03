extends BlastTest
## Steady-state retention: once pools are warm, repeated shots/spawns must
## not grow the live Object count (a retention leak survives until exit, so
## the --verbose exit report never sees it). Pins the spawn-warning spam
## found by benchmarks: a short per-bullet array warned on EVERY spawn and
## (under GUT) kept one tracked-error object per line alive. Config warnings
## now fire once per (resource, warning, sizes) via WarnOnce2D.

func _objs() -> int:
	return int(Performance.get_monitor(Performance.OBJECT_COUNT))


func _steady(f: Callable, warm := 30, measured := 200) -> int:
	for i in warm:
		f.call()
		await physics()
	await idle(4)
	var before := _objs()
	for i in measured:
		f.call()
		await physics()
	await idle(4)
	return _objs() - before


func test_spawner_shots_retain_nothing() -> void:
	var sp := make_spawner(H.make_volley_data(4, 250.0, 0.05), BulletSpawner2D.PATTERN_FROM_HELPER_HEART, 300)
	var grown: int = await _steady(func():
		sp.rotation += 0.01
		sp.shoot_once())
	assert_eq(grown, 0, "200 warm spawner shots: zero retained objects")


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
