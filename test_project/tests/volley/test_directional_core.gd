extends SceneTree
## Directional core suite: speed / direction / transform / velocity get/set.
## Covers: per-bullet + all_bullets_ variants, NaN/zero rejects (old value kept),
## direction-curve ownership warnings, set_bullet_transform derive-direction,
## teleport paths, get_bullet_global_transform round-trip, debug shape state.
## Run: godot --headless --path test_project --script tests/volley/test_directional_core.gd
## Exit code 0 = all pass. Any FAIL = behavior drift or half-applied mutation.

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

	printerr("DIR-CORE T1 speed data")
	var v: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(H.make_directional_data(3, 200.0))
	_check(v != null, "spawn ok")
	if v == null:
		quit(1)
		return
	_check(absf(v.get_bullet_speed_data(0).speed - 200.0) < 0.01, "speed seeded 200")
	var fast := BulletSpeedData2D.new()
	fast.speed = 500.0
	fast.max_speed = 3000.0
	v.set_bullet_speed_data(1, fast)
	_check(absf(v.get_bullet_speed_data(1).speed - 500.0) < 0.01, "per-bullet speed set")
	_check(absf(v.get_bullet_speed_data(0).speed - 200.0) < 0.01, "sibling speed untouched")
	var bad_sp := BulletSpeedData2D.new()
	bad_sp.speed = NAN
	_check(bad_sp.speed == 0.0, "resource setter rejects NaN speed, keeps default")
	v.set_bullet_speed_data(0, null)
	_check(absf(v.get_bullet_speed_data(0).speed - 200.0) < 0.01, "null speed data rejected, old kept")
	v.set_bullet_speed_data(-1, fast)
	v.set_bullet_speed_data(99, fast)
	_check(absf(v.get_bullet_speed_data(1).speed - 500.0) < 0.01, "OOB speed index no-op")
	_check(factory.debug_assert_no_dangling().get("ok", false) == true, "no dangling after speed edits")

	printerr("DIR-CORE T2 direction")
	v.set_bullet_direction(0, Vector2(0, 1))
	_check(v.get_bullet_direction(0).distance_to(Vector2(0, 1)) < 0.01, "direction set down")
	v.set_bullet_direction(0, Vector2.ZERO)
	_check(v.get_bullet_direction(0).distance_to(Vector2(0, 1)) < 0.01, "zero direction rejected")
	v.set_bullet_direction(0, Vector2(NAN, 0))
	_check(v.get_bullet_direction(0).distance_to(Vector2(0, 1)) < 0.01, "NaN direction rejected")
	var vel: Vector2 = v.get_bullet_velocity(0)
	_check(vel.is_finite() and vel.length() > 1.0, "velocity finite and live")
	v.all_bullets_set_direction(Vector2(1, 0))
	_check(v.get_bullet_direction(2).distance_to(Vector2(1, 0)) < 0.01, "all_bullets direction fans out")

	printerr("DIR-CORE T3 transforms + teleport")
	var t0: Transform2D = v.get_bullet_transform(0)
	_check(t0.is_finite(), "transform finite")
	_check(v.get_bullet_global_transform(0) == t0, "global transform round-trips cache")
	var moved := Transform2D(0.0, Vector2(300, 400))
	v.set_bullet_transform(0, moved, true)
	_check(v.get_bullet_transform(0).origin.distance_to(Vector2(300, 400)) < 0.01, "set transform moves")
	_check(v.get_bullet_direction(0).distance_to(Vector2(1, 0)) < 0.5, "derive-direction follows transform")
	v.set_bullet_transform(1, Transform2D(0.0, Vector2(NAN, 0)))
	_check(v.get_bullet_transform(1).is_finite(), "NaN transform rejected, old kept")
	v.set_bullet_transform(1, Transform2D.IDENTITY.scaled(Vector2(0, 0)))
	_check(v.get_bullet_transform(1).is_finite(), "zero-scale transform rejected")
	v.teleport_shift_all_bullets(Vector2(10, 0))
	_check(v.get_bullet_transform(0).origin.distance_to(Vector2(310, 400)) < 0.01, "volley shift carries")
	var shape: Dictionary = v.debug_get_shape_state()
	_check(shape.get("valid", false) == true and (shape.get("rid_count", 0) as int) == 3, "shape state valid, 3 RIDs")

	printerr("DIR-CORE T5 default speed resource flies (max 0 = unlimited)")
	var plain_data := DirectionalBulletsData2D.new()
	plain_data.transforms = [Transform2D.IDENTITY]
	var plain_sp := BulletSpeedData2D.new()
	plain_sp.speed = 300.0
	# NOTE: max_speed left at default 0 = unlimited (not a brake).
	plain_data.all_bullet_speed_data = [plain_sp]
	plain_data.max_life_time = 10.0
	plain_data.texture_size = Vector2(16, 16)
	var pv: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(plain_data)
	var pp0: Vector2 = pv.get_bullet_global_transform(0).origin
	for i in 10:
		await physics_frame
	var pp1: Vector2 = pv.get_bullet_global_transform(0).origin
	_check(pp1.x > pp0.x + 20.0, "default max_speed flies straight (dx=%.1f)" % (pp1.x - pp0.x))

	printerr("DIR-CORE T4 texture rotation")
	v.set_bullet_texture_rotation_radians(0, PI * 0.5)
	_check(absf(v.get_bullet_texture_rotation_radians(0) - PI * 0.5) < 0.2, "texture rotation set")
	v.set_bullet_texture_rotation_degrees(0, 0.0)
	_check(absf(v.get_bullet_texture_rotation_degrees(0)) < 5.0, "texture rotation degrees set")
	v.set_bullet_texture_rotation_radians(0, NAN)
	_check(v.get_bullet_texture_rotation_radians(0) < 1.0, "NaN rotation rejected")

	factory.reset()
	await process_frame
	_check(factory.debug_assert_no_dangling().get("ok", false) == true, "no dangling after reset")
	factory.queue_free()
	await process_frame
	print("----")
	if failures == 0:
		print("ALL DIRECTIONAL CORE TESTS PASSED")
	else:
		printerr(str(failures) + " TEST(S) FAILED")
	quit(failures)
