extends BlastTest
## Custom data contract. Every bullet carries its OWN custom data (entry i of
## all_bullets_custom_data; tile_all_bullets_custom_data wraps short arrays)
## and the volley carries shared_bullets_custom_data; the two never mix (a
## bullet without its own entry reads null, never the shared value). Both are
## readable as the very same Resource instance in every callback that hands
## out the volley, together with the bullet's other runtime state. Runtime
## writes persist, survive freeze/wake, and never leak through the pool; a
## pooled volley releases its references.

var seen: Array = [] # [kind, index, custom, shared, extra]


func _data(n: int, customs: Array, shared: Resource = null, tile := false) -> BulletVolleyData2D:
	var d := H.make_volley_data(n, 300.0, 8.0)
	var arr: Array = []
	for i in n:
		arr.append(Transform2D(0.0, Vector2(0, 40 * i)))
	d.transforms = arr
	d.monitorable = true
	d.set_collision_mask_from_array([3])
	d.collision_shape = H.make_circle_shape(6.0)
	d.all_bullets_custom_data = customs
	d.tile_all_bullets_custom_data = tile
	d.shared_bullets_custom_data = shared
	return d


func _record(kind: String, volley: BulletVolley2D, idx: int, extra := {}) -> void:
	seen.append([kind, idx, volley.bullet_get_custom_data(idx), volley.get_shared_bullets_custom_data(), extra])


func _resources(n: int) -> Array:
	var out: Array = []
	for i in n:
		var r := Resource.new()
		r.resource_name = "custom_%d" % i
		out.append(r)
	return out


func before_each() -> void:
	await super()
	seen.clear()


func _wait(cond: Callable, frames := 90) -> void:
	for i in frames:
		await physics()
		if cond.call():
			break


func test_every_bullet_reads_its_own_data_in_hit_handlers_first_middle_and_last() -> void:
	make_wall(Vector2(200, 0), Vector2(20, 600))
	await physics()
	var customs := _resources(3)
	var shared := Resource.new()
	factory.body_entered.connect(func(_b, v: BulletVolley2D, i: int):
		_record("hit", v, i, {"alive": v.is_bullet_status_enabled(i), "count": v.get_bullet_collision_count(i), "velocity": v.get_bullet_velocity(i)}))
	var v: BulletVolley2D = factory.spawn_volley(_data(3, customs, shared))
	await _wait(func(): return seen.size() >= 3)
	assert_eq(seen.size(), 3, "three killing hits (max 1)")
	for s in seen:
		var i: int = s[1]
		assert_same(s[2], customs[i], "bullet %d reads its own custom data inside its killing hit" % i)
		assert_same(s[3], shared, "shared custom data readable")
		assert_true(s[4]["alive"], "alive in the handler")
		assert_eq(s[4]["count"], 1, "the hit is already counted")
		assert_gt(s[4]["velocity"].x, 0.0, "velocity readable")
	assert_true(v.is_pooled(), "the last kill pooled the volley after its handler")


func test_tiled_and_strict_short_arrays() -> void:
	var two := _resources(2)
	var tiled: BulletVolley2D = factory.spawn_volley(_data(4, two, null, true))
	assert_eq([tiled.bullet_get_custom_data(0), tiled.bullet_get_custom_data(1), tiled.bullet_get_custom_data(2), tiled.bullet_get_custom_data(3)], [two[0], two[1], two[0], two[1]], "tiling wraps A,B,A,B")
	var shared := Resource.new()
	var strict: BulletVolley2D = factory.spawn_volley(_data(3, [two[0]], shared))
	expect_errors_containing("all_bullets_custom_data size", 2, "one size warning per mismatched spawn (tiled 2 != 4, strict 1 != 3)")
	assert_same(strict.bullet_get_custom_data(0), two[0], "entry 0 drives bullet 0")
	assert_null(strict.bullet_get_custom_data(1), "uncovered bullets read null")
	assert_null(strict.bullet_get_custom_data(2), "never the shared value")
	assert_same(strict.get_shared_bullets_custom_data(), shared, "shared stays separate")


func test_lifetime_bounce_and_homing_handlers_read_custom_data() -> void:
	var customs := _resources(2)
	# Lifetime
	var d := _data(2, customs)
	d.max_life_time = 0.2
	d.is_life_time_over_signal_enabled = true
	factory.life_time_over.connect(func(v: BulletVolley2D, idx: Array):
		for i in idx:
			_record("lifetime", v, i))
	var lv: BulletVolley2D = factory.spawn_volley(d)
	await _wait(func(): return not lv.is_bullet_status_enabled(0))
	var lifetime_seen: Array = seen.filter(func(s): return s[0] == "lifetime")
	assert_eq(lifetime_seen.size(), 2, "both listed")
	for s in lifetime_seen:
		assert_same(s[2], customs[s[1]], "expiring bullet %d reads its data" % s[1])
	# Bounce
	seen.clear()
	make_wall(Vector2(200, 0), Vector2(20, 600), 8, 2)
	await physics()
	var bd := _data(1, [customs[0]])
	bd.set_collision_mask_from_array([4])
	bd.set_bounce_mask_from_array([4])
	factory.bounce_body_entered.connect(func(_b, v: BulletVolley2D, i: int):
		_record("bounce", v, i, {"bounces": v.bullet_get_bounce_count(i), "velocity": v.get_bullet_velocity(i)}))
	factory.spawn_volley(bd)
	await _wait(func(): return not seen.is_empty())
	assert_eq(seen.size(), 1, "one bounce")
	assert_same(seen[0][2], customs[0], "bouncing bullet reads its data")
	assert_eq(seen[0][4]["bounces"], 1, "bounce already counted in the handler")
	assert_lt(seen[0][4]["velocity"].x, 0.0, "reflected velocity readable in the handler")
	# Homing reached
	seen.clear()
	var hd := H.make_volley_data(1, 0.0, 30.0)
	hd.transforms = [Transform2D()]
	hd.all_bullets_custom_data = [customs[1]]
	hd.homing_distance_before_reached = 400.0
	var hv: BulletVolley2D = factory.spawn_volley(hd)
	hv.bullet_homing_target_reached.connect(func(v: BulletVolley2D, i: int, _t, _p): _record("reached", v, i))
	hv.shared_homing_deque_push_back_global_position_target(Vector2(5, 0))
	await _wait(func(): return not seen.is_empty(), 10)
	assert_eq(seen.size(), 1, "one reach")
	assert_same(seen[0][2], customs[1], "homing bullet reads its data")


func test_spawner_callbacks_read_custom_data() -> void:
	var customs := _resources(2)
	var shared := Resource.new()
	var sp := make_spawner(_data(2, customs, shared), BulletSpawner2D.PATTERN_FROM_HELPER_RING, 2)
	sp.pre_shoot.connect(func(v: BulletVolley2D, _n): _record("pre_shoot", v, 1))
	sp.volley_fired.connect(func(v: BulletVolley2D, _n): _record("volley_fired", v, 0))
	assert_true(sp.shoot_once(), "fired")
	assert_eq(seen.size(), 2, "both spawner callbacks")
	assert_same(seen[0][2], customs[1], "pre_shoot reads bullet 1's data")
	assert_same(seen[1][2], customs[0], "volley_fired reads bullet 0's data")
	assert_same(seen[1][3], shared, "shared data on spawner volleys")


func test_runtime_writes_persist_across_hits_and_freeze() -> void:
	make_wall(Vector2(100, 0), Vector2(10, 600))
	make_wall(Vector2(300, 0), Vector2(10, 600))
	await physics()
	var tag := Resource.new()
	factory.body_entered.connect(func(_b, v: BulletVolley2D, i: int):
		_record("hit", v, i)
		if seen.size() == 1:
			v.bullet_set_custom_data(i, tag))
	var d := _data(1, [])
	d.bullet_max_collision_count = 0
	var v: BulletVolley2D = factory.spawn_volley(d)
	await _wait(func(): return seen.size() >= 2)
	assert_eq(seen.size(), 2, "two walls, two hits")
	assert_null(seen[0][2], "no custom data at the first hit")
	assert_same(seen[1][2], tag, "the value written in the first handler is there at the second hit")
	v.set_is_auto_pooling_enabled(false)
	v.disable_bullet(0)
	assert_same(v.bullet_get_custom_data(0), tag, "kept while frozen (parked)")
	v.enable_bullet(0)
	assert_same(v.bullet_get_custom_data(0), tag, "kept after the wake")


func test_pool_reuse_never_leaks_custom_data() -> void:
	var customs := _resources(2)
	var v: BulletVolley2D = factory.spawn_volley(_data(2, customs, Resource.new()))
	v.bullet_set_custom_data(0, Resource.new())
	v.set_shared_bullets_custom_data(Resource.new())
	v.disable_bullet(0)
	v.disable_bullet(1)
	var reused: BulletVolley2D = factory.spawn_volley(_data(2, []))
	assert_same(reused, v, "pool reuse")
	assert_null(reused.bullet_get_custom_data(0), "plain respawn reads null")
	assert_null(reused.bullet_get_custom_data(1), "plain respawn reads null")
	assert_null(reused.get_shared_bullets_custom_data(), "no shared data")
	var next := _resources(2)
	reused.disable_bullet(0)
	reused.disable_bullet(1)
	var third: BulletVolley2D = factory.spawn_volley(_data(2, next))
	assert_same(third, v, "reused again")
	assert_same(third.bullet_get_custom_data(1), next[1], "the new owner's data, never the old")


func test_a_pooled_volley_releases_its_custom_data() -> void:
	var probe := Resource.new()
	var ref: WeakRef = weakref(probe)
	var d := _data(1, [probe])
	d.shared_bullets_custom_data = probe
	var v: BulletVolley2D = factory.spawn_volley(d)
	d = null
	probe = null
	assert_not_null(ref.get_ref(), "alive while the bullet carries it")
	v.disable_bullet(0)
	assert_true(v.is_pooled(), "pooled")
	assert_null(ref.get_ref(), "the pooled volley released every reference")
