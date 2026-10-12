extends BlastTest
## Flocking across the pool: a pooled volley releases its flock (weakref
## proof) and the factory's armed count; reused for plain data it flies
## exactly like a cold volley and reports no flock; a PARKED volley (auto
## pooling off) keeps its flock and a wake resumes flocking.

const DT := 1.0 / 60.0


func _data(lifetime := 1000.0) -> BulletVolleyData2D:
	return H.make_flight_data([Transform2D(0.0, Vector2(0, 0)), Transform2D(0.5, Vector2(0, 30)), Transform2D(-0.5, Vector2(20, 10))], 100.0, lifetime)


func test_a_pooled_volley_releases_its_flock() -> void:
	var v: BulletVolley2D = factory.spawn_volley(_data())
	var f := H.make_flock(1.0, 1.0, 1.0)
	var ref: WeakRef = weakref(f)
	v.flock_join(f)
	f = null
	step_factory()
	assert_not_null(ref.get_ref(), "the armed volley holds the flock")
	for i in 3:
		v.disable_bullet(i)
	assert_eq(v.debug_get_life_state(), "pooled", "pooled")
	assert_null(ref.get_ref(), "the pooled volley released it")
	assert_eq(factory.debug_get_flock_stats()["armed_volleys"], 0, "disarmed")


func test_reuse_for_plain_data_flies_like_a_cold_volley() -> void:
	var v: BulletVolley2D = factory.spawn_volley(_data())
	v.flock_join(H.make_flock(3.0, 2.0, 2.0))
	step_factory(10)
	for i in 3:
		v.disable_bullet(i)
	var reused: BulletVolley2D = factory.spawn_volley(_data())
	assert_eq(reused, v, "the pool handed it back")
	var cold: BulletVolley2D = factory.spawn_volley(_data())
	assert_ne(cold, reused, "a fresh volley")
	assert_null(reused.flock_get(), "no flock")
	assert_eq(reused.flock_get_scope(), -1, "no scope")
	step_factory(30)
	for i in 3:
		assert_eq(reused.get_bullet_transform(i), cold.get_bullet_transform(i), "bullet %d flies like the cold volley" % i)
		assert_eq(reused.debug_get_bullet_flock_neighbors(i), 0, "bullet %d reads no flock" % i)


func test_a_parked_volley_keeps_its_flock_and_a_wake_resumes_flocking() -> void:
	var v: BulletVolley2D = factory.spawn_volley(_data())
	var f := H.make_flock(0.0, 0.0, 1.0)
	v.flock_join(f)
	v.set_is_auto_pooling_enabled(false)
	step_factory()
	for i in 3:
		v.disable_bullet(i)
	assert_eq(v.debug_get_life_state(), "parked", "parked, still owned")
	assert_eq(v.flock_get(), f, "a parked volley keeps its flock")
	assert_eq(factory.debug_get_flock_stats()["armed_volleys"], 1, "still armed")
	step_factory(3)
	assert_eq(factory.debug_get_flock_stats()["grid_members"], 0, "nothing gathered while parked")
	for i in 3:
		v.enable_bullet(i)
	step_factory()
	assert_eq(v.debug_get_bullet_flock_neighbors(0), 2, "the wake flocks again")
	assert_ne(v.debug_get_bullet_flock_steering(0), Vector2(), "with a pull")
