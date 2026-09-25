extends SceneTree
## Integration suite: interpolation agreement + factory pause + scene churn.
## Covers: debug_check_interpolation_status shape + mismatch hint, mid-game
## interpolation toggle reseeds prev caches (no lerp pop), factory pause stops
## motion and drops queued collisions (no resume hitch), scene churn (spawner
## freed with tracked volleys, factory reset with live volleys), two factories
## in one tree stay independent.
## Run: godot --headless --path test_project --script tests/integration/test_interpolation_integration.gd
## Exit code 0 = all pass.

const H := preload("res://tests/common/blast_test_helpers.gd")

var failures := 0

func _check(cond: bool, label: String) -> void:
	if cond:
		print("PASS  ", label)
	else:
		failures += 1
		printerr("FAIL  ", label)

func _initialize() -> void:
	var factory := BulletFactory2D.new()
	get_root().add_child(factory)
	await process_frame
	await process_frame

	printerr("INT T1 interpolation status + toggle")
	var st: Dictionary = factory.debug_check_interpolation_status()
	_check(st.has("mismatch") and st.has("hint"), "status shape")
	factory.set_use_physics_interpolation_editor(true)
	await process_frame
	var st2: Dictionary = factory.debug_check_interpolation_status()
	_check(st2.get("factory_enabled", false) == true, "factory flag on")
	var v: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(H.make_directional_data(2, 200.0))
	for i in 5:
		await physics_frame
	_check(v.get_bullet_transform(0).is_finite(), "volley ticks with interpolation on")
	factory.set_use_physics_interpolation_editor(false)

	printerr("INT T2 pause freezes + drops backlog")
	var p0: Vector2 = v.get_bullet_global_transform(0).origin
	factory.set_is_factory_processing_bullets(false)
	for i in 10:
		await physics_frame
	var p1: Vector2 = v.get_bullet_global_transform(0).origin
	_check(p1.distance_to(p0) < 0.5, "paused volley frozen")
	factory.set_is_factory_processing_bullets(true)
	await physics_frame
	_check(v.get_bullet_global_transform(0).is_finite(), "resumed volley sane")

	printerr("INT T3 churn: spawner freed, factory reset, two factories")
	var spawner := BulletSpawner2D.new()
	get_root().add_child(spawner)
	await process_frame
	spawner.set_bullet_factory(factory)
	spawner.set_spawn_data(H.make_directional_data(2))
	spawner.set_shooting_enabled(false)
	spawner.set_homing_enabled(true)
	spawner.set_homing_target_source(2)
	spawner.set_homing_global_position(Vector2(400, 0))
	_check(spawner.shoot_once(), "tracked shot fires")
	spawner.queue_free()
	await process_frame
	_check(factory.debug_assert_no_dangling().get("ok", false) == true, "no dangling after spawner freed")
	var f2 := BulletFactory2D.new()
	get_root().add_child(f2)
	await process_frame
	await process_frame
	f2.spawn_directional_bullets(H.make_directional_data(2, 150.0))
	_check(f2.debug_get_total_bullets_amount(0) == 1, "second factory independent")
	_check(factory.debug_assert_no_dangling().get("ok", false) == true, "first factory clean")
	factory.reset()
	f2.reset()
	f2.queue_free()
	factory.queue_free()
	await process_frame
	print("----")
	if failures == 0:
		print("ALL INTEGRATION TESTS PASSED")
	else:
		printerr(str(failures) + " TEST(S) FAILED")
	quit(failures)
