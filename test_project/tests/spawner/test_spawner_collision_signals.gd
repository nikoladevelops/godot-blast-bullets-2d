extends BlastTest
## Spawner-owned volleys route their collision signals to the SPAWNER:
## area_entered, life_time_over and bounce_area_entered fire on the spawner,
## and the factory's directional_* twins stay silent for those volleys.


func _data(mask_layers: Array, bounce_layers: Array = [], lifetime: float = 5.0) -> DirectionalBulletsData2D:
	var d := H.make_directional_data(1, 600.0, lifetime)
	d.monitorable = true
	d.set_collision_mask_from_array(mask_layers)
	if not bounce_layers.is_empty():
		d.set_bounce_mask_from_array(bounce_layers)
	return d


func _spawner(data: DirectionalBulletsData2D) -> BulletSpawner2D:
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
	assert_signal_emit_count(factory, "directional_area_entered", 0, "the factory stays silent")


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
	assert_signal_emit_count(factory, "directional_life_time_over", 0, "the factory stays silent")


func test_bounce_area_entered_goes_to_the_spawner() -> void:
	make_area(Vector2(120, 0), Vector2(20, 400), 8) # layer 4, bounce layer
	var sp := _spawner(_data([4], [4]))
	assert_true(sp.shoot_once(), "shot")
	await _until(sp, "bounce_area_entered")
	await idle(2)
	assert_signal_emit_count(sp, "bounce_area_entered", 1, "the spawner hears the bounce once")
	assert_signal_emit_count(factory, "directional_bounce_area_entered", 0, "the factory stays silent")
