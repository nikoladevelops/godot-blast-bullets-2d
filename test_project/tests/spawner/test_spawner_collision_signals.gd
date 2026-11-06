extends BlastTest
## Spawner-owned volleys route their collision signals to the SPAWNER:
## area_entered, life_time_over and bounce_area_entered fire on the spawner,
## and the factory's same-named signals stay silent for those volleys.


func _data(mask_layers: Array, bounce_layers: Array = [], lifetime: float = 5.0) -> BulletVolleyData2D:
	var d := H.make_volley_data(1, 600.0, lifetime)
	d.monitorable = true
	d.set_collision_mask_from_array(mask_layers)
	if not bounce_layers.is_empty():
		d.set_bounce_mask_from_array(bounce_layers)
	return d


func _spawner(data: BulletVolleyData2D) -> BulletSpawner2D:
	var sp := make_spawner(data, BulletSpawner2D.PATTERN_FROM_SELF, 1) # fires along +X
	watch_signals(sp)
	watch_signals(factory)
	return sp


func _until(obj: Object, sig: String, frames: int = 60) -> void:
	for i in frames:
		await physics()
		if get_signal_emit_count(obj, sig) > 0:
			return


func test_area_entered_goes_to_the_spawner() -> void:
	make_area(Vector2(120, 0), Vector2(20, 400), 16) # layer 5
	var sp := _spawner(_data([5]))
	assert_true(sp.shoot_once(), "shot")
	await _until(sp, "area_entered")
	await idle(2)
	assert_signal_emit_count(sp, "area_entered", 1, "the spawner hears the hit once")
	assert_signal_emit_count(factory, "area_entered", 0, "the factory stays silent")


func test_life_time_over_goes_to_the_spawner() -> void:
	var data := _data([5], [], 0.1)
	data.is_life_time_over_signal_enabled = true
	var sp := _spawner(data)
	assert_true(sp.shoot_once(), "shot")
	for i in 60:
		await idle(1)
		if get_signal_emit_count(sp, "life_time_over") > 0:
			break
	assert_signal_emit_count(sp, "life_time_over", 1, "the spawner hears the expiry once")
	assert_eq(get_signal_parameters(sp, "life_time_over", 0)[1], [0], "with the expired bullet index")
	assert_signal_emit_count(factory, "life_time_over", 0, "the factory stays silent")


func test_bounce_area_entered_goes_to_the_spawner() -> void:
	make_area(Vector2(120, 0), Vector2(20, 400), 8) # layer 4, bounce layer
	var sp := _spawner(_data([4], [4]))
	assert_true(sp.shoot_once(), "shot")
	await _until(sp, "bounce_area_entered")
	await idle(2)
	assert_signal_emit_count(sp, "bounce_area_entered", 1, "the spawner hears the bounce once")
	assert_signal_emit_count(factory, "bounce_area_entered", 0, "the factory stays silent")


## Waits until the spawner's bullets have expired (none live).
func _wait_expired(sp: BulletSpawner2D, frames: int = 60) -> void:
	for i in frames:
		await physics()
		if sp.get_active_live_bullet_count() == 0:
			return


## An expiry nobody handles: ONE warning on the emitter (the spawner, naming the
## factory fall-through), never a push_error, and the expiries that happened
## while nothing was connected are never replayed to a handler connected later -
## it hears exactly the next life.
func test_life_time_over_without_a_handler_warns_once_and_is_never_replayed() -> void:
	var data := _life_data()
	assert_false(data.is_life_time_over_signal_enabled == false, "the expiry signal is on in the data")
	var sp := make_spawner(data, BulletSpawner2D.PATTERN_FROM_SELF, 1)
	assert_false(sp.has_connections(&"life_time_over"), "nobody is connected on the spawner")
	assert_false(factory.has_connections(&"life_time_over"), "nor on the factory")
	assert_true(sp.shoot_once(), "first shot")
	await _wait_expired(sp)
	assert_true(sp.shoot_once(), "second shot")
	await _wait_expired(sp)
	expect_warning("is_life_time_over_signal_enabled is on, but nothing is connected to its life_time_over signal (on it or on BulletFactory2D)", "one warning per emitter", "BulletSpawner2D '")
	expect_no_errors("the explanation is a warning, never a push_error")
	var heard: Array = []
	sp.life_time_over.connect(func(_v: BulletVolley2D, _idx: Array): heard.append(1))
	assert_true(sp.shoot_once(), "third shot")
	await _wait_expired(sp)
	assert_eq(heard.size(), 1, "only the new life's expiry reached the new handler (the old ones were not replayed)")
	expect_no_errors()


## With the flag off the same lifetime expires silently: no warning at all
## (every warning is an API a game must claim) and the bullets still die.
func test_life_time_over_signal_off_expires_silently() -> void:
	var d := H.make_volley_data(1, 600.0, 0.1)
	d.monitorable = true
	d.set_collision_mask_from_array([5])
	assert_false(d.is_life_time_over_signal_enabled, "off by default")
	var sp := make_spawner(d, BulletSpawner2D.PATTERN_FROM_SELF, 1)
	assert_true(sp.shoot_once(), "shot")
	assert_eq(sp.get_active_live_bullet_count(), 1, "the bullet lives")
	await _wait_expired(sp)
	assert_eq(sp.get_active_live_bullet_count(), 0, "the bullet expired anyway")
	assert_eq(factory.debug_get_active_volleys_amount(), 0, "and its volley went back to the pool")
	expect_warning_sequence([], "no warning with the signal off")


func _life_data() -> BulletVolleyData2D:
	var d := H.make_volley_data(1, 600.0, 0.1)
	d.monitorable = true
	d.set_collision_mask_from_array([5])
	d.is_life_time_over_signal_enabled = true
	return d


## Two spawners, both watching: only the shooter's volley reaches its own
## spawner (never the other one, never the factory) - and a volley the factory
## spawned but B adopted counts as B's.
func test_two_watching_spawners_hear_only_their_own_volleys() -> void:
	make_wall(Vector2(120, 0))
	await physics()
	var a := _spawner(_data([3]))
	var b := _spawner(_data([3]))
	assert_true(a.shoot_once(), "A shot")
	await _until(a, "body_entered")
	await idle(2)
	assert_signal_emit_count(a, "body_entered", 1, "A heard its own volley's hit")
	assert_signal_emit_count(b, "body_entered", 0, "B heard nothing")
	assert_signal_emit_count(factory, "body_entered", 0, "the factory stayed silent")
	var v: BulletVolley2D = factory.spawn_volley(_data([3]))
	assert_true(b.adopt_live_volley(v), "B adopted the script volley")
	assert_eq(int(v.debug_get_volley_info()["owner_spawner_id"]), int(b.get_instance_id()), "the volley is B's before it hits")
	await _until(b, "body_entered")
	await idle(2)
	assert_signal_emit_count(b, "body_entered", 1, "B heard the adopted volley's hit")
	assert_signal_emit_count(a, "body_entered", 1, "A still heard only its own")
	assert_signal_emit_count(factory, "body_entered", 0, "the factory never took over")
