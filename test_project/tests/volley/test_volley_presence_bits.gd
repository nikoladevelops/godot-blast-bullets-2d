extends SceneTree
## Per-bullet seed PRESENCE suite: proves the shared fallbacks no longer use a
## "is the triple all zero?" heuristic to decide whether a slot is a gap.
##
## The bug: a valid all-zero BulletSpeedData2D / BulletRotationData2D entry
## means "this bullet does not move / does not spin". The fallback compared
## (speed, max_speed, acceleration) against 0 and treated the all-zero triple
## as "no per-bullet entry here", so shared data silently overwrote an
## intentional freeze and the frozen bullet started moving.
##
## The fix stores an explicit presence bit per slot, set only when the seed read
## a VALID entry. Bit 0 means "genuine gap (invalid entry), shared may fill".
## Because the fallback also sets the bit when it fills a slot, the documented
## fill-once order rule still holds (a later set_shared_* must not re-apply).
##
## Covers: T1 speed deliberate-zero survives, T2 speed gap still falls back,
## T3 speed NaN entry is a gap, T4 rotation deliberate-zero survives, T5
## rotation gap still falls back, T6 rotation full-seed (no per-bullet array)
## fans out, T7 fill-once order both ways, T8 rotation fill-once, T9 re-seed
## clears the bit, T10 live order independence, T11 pool-reuse neutrality.
##
## Run: godot --headless --path test_project --script tests/volley/test_volley_presence_bits.gd
## Exit code 0 = all pass. Any FAIL = behavior drift or a real bug.

var failures := 0

func _check(cond: bool, label: String) -> void:
	if cond:
		print("PASS  ", label)
	else:
		failures += 1
		printerr("FAIL  ", label)

# n bullets, all seeded at the origin. The caller supplies per-bullet data.
func _bare_data(n: int = 2) -> DirectionalBulletsData2D:
	var d := DirectionalBulletsData2D.new()
	var arr: Array = []
	for i in n:
		arr.append(Transform2D(0.0, Vector2(0.0, 0.0)))
	d.transforms = arr
	d.max_life_time = 5.0
	d.texture_size = Vector2(16, 16)
	d.set_collision_layer_from_array([2])
	d.set_collision_mask_from_array([4])
	return d

func _speed_entry(speed: float, max_speed: float, acc: float) -> BulletSpeedData2D:
	var s := BulletSpeedData2D.new()
	s.speed = speed
	s.max_speed = max_speed
	s.acceleration = acc
	return s

func _rot_entry(speed: float, max_speed: float, acc: float) -> BulletRotationData2D:
	var r := BulletRotationData2D.new()
	r.rotation_speed = speed
	r.max_rotation_speed = max_speed
	r.rotation_acceleration = acc
	return r

func _shared_speed(speed: float) -> BulletSpeedData2D:
	return _speed_entry(speed, 3000.0, 0.0)

func _shared_rot(speed: float) -> BulletRotationData2D:
	return _rot_entry(speed, 3000.0, 0.0)

func _speed_of(v: DirectionalBullets2D, i: int) -> float:
	return float(v.debug_get_bullet_info(i)["speed"])

func _rot_of(v: DirectionalBullets2D, i: int) -> float:
	return float(v.debug_get_bullet_info(i)["rotation_speed"])

func _origin_of(v: DirectionalBullets2D, i: int) -> Vector2:
	return v.get_bullet_transform(i).get_origin()

func _angle_of(v: DirectionalBullets2D, i: int) -> float:
	return v.get_bullet_transform(i).get_rotation()

func _initialize() -> void:
	var factory := BulletFactory2D.new()
	get_root().add_child(factory)
	await process_frame
	await process_frame

	# ---------------------------------------------------------------
	printerr("PRES T1 speed: a deliberate all-zero entry is NOT a gap")
	# Bullet 0 moves (200), bullet 1 is an explicit "don't move" (0/0/0).
	# Shared says 150. Bullet 1 must stay frozen.
	var d1 := _bare_data(2)
	d1.all_bullet_speed_data = [_speed_entry(200.0, 3000.0, 0.0), _speed_entry(0.0, 0.0, 0.0)]
	var v1: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d1)
	_check(v1 != null, "T1 volley spawned")
	v1.set_shared_bullet_speed_data(_shared_speed(150.0))
	_check(absf(_speed_of(v1, 0) - 200.0) < 0.01, "T1 bullet 0 keeps its own speed (200), got %.3f" % _speed_of(v1, 0))
	_check(absf(_speed_of(v1, 1) - 0.0) < 0.01, "T1 bullet 1 stays frozen (0), not overwritten with 150 - got %.3f" % _speed_of(v1, 1))
	# And prove it does not move after a tick, not just that the field reads 0.
	var before1 := _origin_of(v1, 1)
	await physics_frame
	await physics_frame
	var travelled := before1.distance_to(_origin_of(v1, 1))
	_check(travelled < 0.5, "T1 frozen bullet does not travel (%.4f px)" % travelled)
	_check(absf(_speed_of(v1, 0) - 200.0) < 0.01, "T1 bullet 0 still at 200 after ticks")

	# ---------------------------------------------------------------
	printerr("PRES T2 speed: an INVALID entry is still a real gap")
	# Bullet 0 has data, bullet 1 is null -> shared must fill slot 1 only.
	var d2 := _bare_data(2)
	d2.all_bullet_speed_data = [_speed_entry(200.0, 3000.0, 0.0), null]
	var v2: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d2)
	v2.set_shared_bullet_speed_data(_shared_speed(150.0))
	_check(absf(_speed_of(v2, 0) - 200.0) < 0.01, "T2 bullet 0 keeps per-bullet 200, got %.3f" % _speed_of(v2, 0))
	_check(absf(_speed_of(v2, 1) - 150.0) < 0.01, "T2 null-entry bullet 1 falls back to shared 150, got %.3f" % _speed_of(v2, 1))

	# ---------------------------------------------------------------
	printerr("PRES T3 a rejected NaN becomes a deliberate zero, not a gap")
	# BulletSpeedData2D refuses to store NaN, so the resource silently keeps
	# its default 0. That is a VALID all-zero entry ("don't move"), so the
	# shared fallback must NOT fill it. This is the important distinction: a
	# resource that exists and is all-zero is intent, not absence.
	var nan_entry := BulletSpeedData2D.new()
	nan_entry.speed = NAN
	_check(not is_nan(nan_entry.speed), "T3a BulletSpeedData2D refuses to store NaN (keeps default 0)")
	var d3 := _bare_data(2)
	d3.all_bullet_speed_data = [_speed_entry(200.0, 3000.0, 0.0), nan_entry]
	var v3: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d3)
	v3.set_shared_bullet_speed_data(_shared_speed(150.0))
	_check(absf(_speed_of(v3, 0) - 200.0) < 0.01, "T3b sibling keeps its own 200, got %.3f" % _speed_of(v3, 0))
	_check(absf(_speed_of(v3, 1) - 0.0) < 0.01, "T3c rejected-NaN entry is a valid zero, stays frozen (not filled), got %.3f" % _speed_of(v3, 1))
	_check((v3.debug_get_bullet_info(0)["velocity"] as Vector2).is_finite(), "T3d sibling velocity stays finite")

	# ---------------------------------------------------------------
	printerr("PRES T4 rotation: a deliberate all-zero entry is NOT a gap")
	var d4 := _bare_data(2)
	d4.all_bullet_rotation_data = [_rot_entry(4.0, 10.0, 0.0), _rot_entry(0.0, 0.0, 0.0)]
	var v4: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d4)
	_check(v4 != null, "T4 volley spawned")
	v4.set_shared_bullet_rotation_data(_shared_rot(7.0))
	_check(absf(_rot_of(v4, 0) - 4.0) < 0.01, "T4 bullet 0 keeps its own spin (4), got %.3f" % _rot_of(v4, 0))
	_check(absf(_rot_of(v4, 1) - 0.0) < 0.01, "T4 bullet 1 stays no-spin (0), not overwritten with 7 - got %.3f" % _rot_of(v4, 1))
	# Prove the sprite angle of bullet 1 never moved, while bullet 0 did.
	var a1_before := _angle_of(v4, 1)
	var a0_before := _angle_of(v4, 0)
	await physics_frame
	await physics_frame
	var d1_angle := absf(angle_difference(a1_before, _angle_of(v4, 1)))
	var d0_angle := absf(angle_difference(a0_before, _angle_of(v4, 0)))
	_check(d1_angle < 0.0001, "T4 no-spin bullet angle unchanged (%.6f rad)" % d1_angle)
	_check(d0_angle > 0.0001, "T4 spinning bullet angle advanced (%.6f rad)" % d0_angle)

	# ---------------------------------------------------------------
	printerr("PRES T5 rotation: an INVALID entry is still a real gap")
	var d5 := _bare_data(2)
	d5.all_bullet_rotation_data = [_rot_entry(4.0, 10.0, 0.0), null]
	var v5: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d5)
	v5.set_shared_bullet_rotation_data(_shared_rot(7.0))
	_check(absf(_rot_of(v5, 0) - 4.0) < 0.01, "T5 bullet 0 keeps per-bullet 4")
	_check(absf(_rot_of(v5, 1) - 7.0) < 0.01, "T5 null-entry bullet 1 falls back to shared 7, got %.3f" % _rot_of(v5, 1))

	# ---------------------------------------------------------------
	printerr("PRES T6 rotation: no per-bullet array = full shared fan-out")
	# With rotation never seeded, shared must seed EVERY slot (not just 0).
	var v6: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(_bare_data(3))
	v6.set_shared_bullet_rotation_data(_shared_rot(3.0))
	var all_fanned := true
	for i in 3:
		if absf(_rot_of(v6, i) - 3.0) >= 0.01:
			all_fanned = false
	_check(all_fanned, "T6 shared rotation fans out to all 3 slots")

	# ---------------------------------------------------------------
	printerr("PRES T7 fill-once: shared set twice must not re-apply")
	# Documented order rule: set per-bullet, then shared fills the gaps, then
	# changing shared again must NOT overwrite what per-bullet seeded or what
	# the first shared call filled.
	var d7 := _bare_data(2)
	d7.all_bullet_speed_data = [_speed_entry(200.0, 3000.0, 0.0), null]
	var v7: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d7)
	v7.set_shared_bullet_speed_data(_shared_speed(150.0))
	v7.set_shared_bullet_speed_data(_shared_speed(999.0))
	_check(absf(_speed_of(v7, 0) - 200.0) < 0.01, "T7 second shared call leaves per-bullet slot alone")
	_check(absf(_speed_of(v7, 1) - 150.0) < 0.01, "T7 second shared call does not re-fill (150 stays, not 999), got %.3f" % _speed_of(v7, 1))
	# Reverse order: shared first, then per-bullet seeding wins for its slots.
	var v7b: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(_bare_data(2))
	v7b.set_shared_bullet_speed_data(_shared_speed(150.0))
	v7b.set_bullet_speed_data(0, _speed_entry(50.0, 3000.0, 0.0))
	v7b.set_bullet_speed_data(1, _speed_entry(0.0, 0.0, 0.0))
	_check(absf(_speed_of(v7b, 0) - 50.0) < 0.01, "T7b per-bullet seeded after shared wins on slot 0, got %.3f" % _speed_of(v7b, 0))
	_check(absf(_speed_of(v7b, 1) - 0.0) < 0.01, "T7b deliberate zero seeded after shared survives, got %.3f" % _speed_of(v7b, 1))

	# ---------------------------------------------------------------
	printerr("PRES T8 rotation fill-once both ways")
	var d8 := _bare_data(2)
	d8.all_bullet_rotation_data = [_rot_entry(4.0, 10.0, 0.0), null]
	var v8: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d8)
	v8.set_shared_bullet_rotation_data(_shared_rot(7.0))
	v8.set_shared_bullet_rotation_data(_shared_rot(999.0))
	_check(absf(_rot_of(v8, 0) - 4.0) < 0.01, "T8 second shared rotation leaves per-bullet slot alone")
	_check(absf(_rot_of(v8, 1) - 7.0) < 0.01, "T8 second shared rotation does not re-fill (7 stays), got %.3f" % _rot_of(v8, 1))
	# And the same for a deliberate zero seeded AFTER shared.
	var v8b: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(_bare_data(2))
	v8b.set_shared_bullet_rotation_data(_shared_rot(7.0))
	v8b.set_bullet_rotation_data(0, _rot_entry(2.0, 10.0, 0.0))
	v8b.set_bullet_rotation_data(1, _rot_entry(0.0, 0.0, 0.0))
	_check(absf(_rot_of(v8b, 0) - 2.0) < 0.01, "T8b per-bullet rotation seeded after shared wins, got %.3f" % _rot_of(v8b, 0))
	_check(absf(_rot_of(v8b, 1) - 0.0) < 0.01, "T8b deliberate no-spin seeded after shared survives, got %.3f" % _rot_of(v8b, 1))

	# ---------------------------------------------------------------
	printerr("PRES T9 a direct per-bullet write claims presence")
	# set_bullet_speed_data / set_bullet_rotation_data are as authoritative as
	# a seeded entry: a later shared set must not undo them, and their all-zero
	# form ("freeze this bullet") must survive shared exactly like a seeded one.
	var v9: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(_bare_data(2))
	v9.set_shared_bullet_speed_data(_shared_speed(150.0))
	_check(absf(_speed_of(v9, 0) - 150.0) < 0.01, "T9 shared filled both slots first, got %.3f" % _speed_of(v9, 0))
	v9.set_bullet_speed_data(0, _speed_entry(0.0, 0.0, 0.0))
	v9.set_bullet_speed_data(1, _speed_entry(0.0, 0.0, 0.0))
	v9.set_shared_bullet_speed_data(_shared_speed(999.0))
	_check(absf(_speed_of(v9, 0) - 0.0) < 0.01, "T9a direct zero-write survives a later shared set, got %.3f" % _speed_of(v9, 0))
	_check(absf(_speed_of(v9, 1) - 0.0) < 0.01, "T9b direct zero-write survives a later shared set, got %.3f" % _speed_of(v9, 1))
	# Rotation equivalent.
	var v9r: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(_bare_data(2))
	v9r.set_shared_bullet_rotation_data(_shared_rot(7.0))
	v9r.set_bullet_rotation_data(0, _rot_entry(0.0, 0.0, 0.0))
	v9r.set_bullet_rotation_data(1, _rot_entry(0.0, 0.0, 0.0))
	v9r.set_shared_bullet_rotation_data(_shared_rot(999.0))
	_check(absf(_rot_of(v9r, 0) - 0.0) < 0.01, "T9c direct no-spin write survives a later shared set, got %.3f" % _rot_of(v9r, 0))
	_check(absf(_rot_of(v9r, 1) - 0.0) < 0.01, "T9d direct no-spin write survives a later shared set, got %.3f" % _rot_of(v9r, 1))

	# ---------------------------------------------------------------
	printerr("PRES T10 live order independence (shared set before per-bullet)")
	# A user who seeds per-bullet AFTER shared must not have the shared value
	# baked into their deliberate zero.
	var v10: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(_bare_data(2))
	v10.set_shared_bullet_speed_data(_shared_speed(150.0))
	v10.set_bullet_speed_data(0, _speed_entry(0.0, 0.0, 0.0))
	v10.set_bullet_speed_data(1, _speed_entry(0.0, 0.0, 0.0))
	_check(absf(_speed_of(v10, 0) - 0.0) < 0.01, "T10 explicit re-seed to zero wins over earlier shared (slot 0), got %.3f" % _speed_of(v10, 0))
	_check(absf(_speed_of(v10, 1) - 0.0) < 0.01, "T10 explicit re-seed to zero wins over earlier shared (slot 1), got %.3f" % _speed_of(v10, 1))

	# ---------------------------------------------------------------
	printerr("PRES T11 pool-reuse neutrality across a new life")
	# A volley returned to the pool and handed to a new owner must not inherit
	# the previous owner's presence decisions.
	var d11 := _bare_data(2)
	d11.all_bullet_speed_data = [_speed_entry(200.0, 3000.0, 0.0), _speed_entry(0.0, 0.0, 0.0)]
	var v11: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d11)
	v11.set_shared_bullet_speed_data(_shared_speed(150.0))
	_check(absf(_speed_of(v11, 1) - 0.0) < 0.01, "T11 first life slot 1 frozen by a valid zero")
	# Free every bullet and let the factory pool the volley.
	v11.clear_all_bullets()
	await process_frame
	await process_frame
	# New life with a genuine gap in slot 1: shared must fill it.
	var d11b := _bare_data(2)
	d11b.all_bullet_speed_data = [_speed_entry(200.0, 3000.0, 0.0), null]
	var v11b: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d11b)
	v11b.set_shared_bullet_speed_data(_shared_speed(160.0))
	_check(absf(_speed_of(v11b, 1) - 160.0) < 0.01, "T11 new life fills its own gap (no inherited bit), got %.3f" % _speed_of(v11b, 1))

	_check(factory.debug_assert_no_dangling().get("ok", false) == true, "no dangling at end")

	factory.reset()
	factory.queue_free()
	await process_frame
	print("----")
	if failures == 0:
		print("ALL PRESENCE-BITS TESTS PASSED")
	else:
		printerr(str(failures) + " TEST(S) FAILED")
	quit(failures)
