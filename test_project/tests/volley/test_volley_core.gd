extends BlastTest
## Volley core API: speed / direction / transform / velocity / texture
## rotation / scale get-set, per-bullet and all_bullets_ variants, NaN / zero /
## null / OOB rejects keep the old value, derive-direction from a transform,
## teleport carry, shape state, default max_speed 0 = unlimited.

var v: BulletVolley2D


func before_each() -> void:
	await super()
	v = quick_volley(3, 200.0)


func test_speed_data() -> void:
	assert_almost_eq(v.get_bullet_speed_data(0).speed, 200.0, 0.01, "speed seeded")
	v.set_bullet_speed_data(1, H.make_speed(500.0))
	assert_almost_eq(v.get_bullet_speed_data(1).speed, 500.0, 0.01, "per-bullet speed set")
	assert_almost_eq(v.get_bullet_speed_data(0).speed, 200.0, 0.01, "sibling untouched")
	var bad_sp := BulletSpeedData2D.new()
	bad_sp.speed = NAN
	expect_error_sequence(["BulletSpeedData2D: speed must be finite (NaN/Inf would poison bullet movement), keeping the old value."])
	assert_eq(bad_sp.speed, 0.0, "resource setter rejects NaN, keeps default")
	v.set_bullet_speed_data(0, null)
	expect_error("is null")
	assert_almost_eq(v.get_bullet_speed_data(0).speed, 200.0, 0.01, "null speed data rejected")
	v.set_bullet_speed_data(-1, H.make_speed(500.0))
	v.set_bullet_speed_data(99, H.make_speed(500.0))
	expect_errors_containing("Invalid bullet index", 2)


func test_direction() -> void:
	v.set_bullet_direction(0, Vector2(0, 1))
	assert_almost_eq(v.get_bullet_direction(0), Vector2(0, 1), Vector2(0.01, 0.01), "direction set")
	v.set_bullet_direction(0, Vector2.ZERO)
	expect_error("direction is zero")
	v.set_bullet_direction(0, Vector2(NAN, 0))
	expect_error("must be finite")
	assert_almost_eq(v.get_bullet_direction(0), Vector2(0, 1), Vector2(0.01, 0.01), "zero and NaN rejected")
	var vel: Vector2 = v.get_bullet_velocity(0)
	assert_true(vel.is_finite() and vel.length() > 1.0, "velocity finite and live")
	v.all_bullets_set_direction(Vector2(1, 0))
	assert_almost_eq(v.get_bullet_direction(2), Vector2(1, 0), Vector2(0.01, 0.01), "all_bullets direction fans out")


func test_transforms_and_teleport() -> void:
	var t0: Transform2D = v.get_bullet_transform(0)
	assert_true(t0.is_finite())
	assert_eq(v.get_bullet_transform(0), t0, "global transform round-trips the cache")
	v.set_bullet_transform(0, Transform2D(0.0, Vector2(300, 400)), true)
	assert_almost_eq(v.get_bullet_transform(0).origin, Vector2(300, 400), Vector2(0.01, 0.01), "set transform moves")
	assert_almost_eq(v.get_bullet_direction(0), Vector2(1, 0), Vector2(0.5, 0.5), "derive-direction follows the transform")
	v.set_bullet_transform(1, Transform2D(0.0, Vector2(NAN, 0)))
	expect_error("must be finite")
	v.set_bullet_transform(1, Transform2D.IDENTITY.scaled(Vector2(0, 0)))
	expect_error_sequence(["set_bullet_transform: scale must be non-zero and non-singular, keeping the old transform."])
	assert_true(v.get_bullet_transform(1).is_finite(), "bad transforms rejected")
	v.teleport_shift_all_bullets(Vector2(10, 0))
	assert_almost_eq(v.get_bullet_transform(0).origin, Vector2(310, 400), Vector2(0.01, 0.01), "volley shift carries")
	var shape: Dictionary = v.debug_get_shape_state()
	assert_true(shape.get("valid", false), "shape state valid")
	# One shared server shape per volley (F1: N RIDs made cold spawns O(N^2)),
	# attached once per bullet.
	assert_eq(int(shape.get("rid_count", 0)), 1, "one shared shape RID per volley")
	assert_eq(int(shape.get("shape_count", 0)), 3, "one area shape slot per bullet")


func test_default_max_speed_is_unlimited() -> void:
	var d := H.make_volley_data(1, 300.0, 10.0)
	var sp := BulletSpeedData2D.new()
	sp.speed = 300.0 # max_speed left at 0 = unlimited, not a brake
	d.all_bullet_speed_data = [sp]
	var pv: BulletVolley2D = factory.spawn_volley(d)
	var x0: float = pv.get_bullet_transform(0).origin.x
	await physics(10)
	assert_gt(pv.get_bullet_transform(0).origin.x, x0 + 20.0, "default max_speed flies")


func test_texture_rotation() -> void:
	v.set_bullet_texture_rotation_radians(0, PI * 0.5)
	assert_almost_eq(v.get_bullet_texture_rotation_radians(0), PI * 0.5, 0.001, "texture rotation set")
	v.set_bullet_texture_rotation_degrees(0, 0.0)
	assert_almost_eq(v.get_bullet_texture_rotation_degrees(0), 0.0, 0.01, "degrees set")
	v.set_bullet_texture_rotation_radians(0, NAN)
	expect_error("must be finite")
	assert_almost_eq(v.get_bullet_texture_rotation_radians(0), 0.0, 0.001, "NaN rejected")


func test_texture_rotation_round_trip_with_offset() -> void:
	var d := H.make_volley_data(1, 0.0)
	d.texture_rotation_radians = 0.7
	var w: BulletVolley2D = factory.spawn_volley(d)
	var r0: float = w.get_bullet_texture_rotation_radians(0)
	assert_almost_eq(r0, 0.0, 0.001, "fresh bullet reads 0 (volley offset excluded)")
	w.set_bullet_texture_rotation_radians(0, w.get_bullet_texture_rotation_radians(0))
	w.set_bullet_texture_rotation_radians(0, w.get_bullet_texture_rotation_radians(0))
	assert_almost_eq(w.get_bullet_texture_rotation_radians(0), r0, 0.001, "set(get()) is a no-op")
	w.set_bullet_texture_rotation_degrees(0, 30.0)
	assert_almost_eq(w.get_bullet_texture_rotation_degrees(0), 30.0, 0.01, "degrees round-trip")
	assert_almost_eq(angle_difference(w.get_bullet_transform(0).get_rotation(), deg_to_rad(30.0) + 0.7), 0.0, 0.001, "instance still carries the offset")


## A direction curve owns the heading: every direction setter refuses the
## write with ONE warning in direction wording (it once said "speed data").
func test_direction_setters_under_a_direction_curve_refuse_with_direction_wording() -> void:
	var target := Node2D.new()
	target.position = Vector2(0, 300)
	add(target)
	for owner in ["shared", "individual"]:
		var curves := BulletCurvesData2D.new()
		curves.x_direction_curve = H.make_flat_curve(0.5)
		var d := H.make_volley_data(2, 100.0, 30.0)
		if owner == "shared":
			d.shared_bullet_curves_data = curves
		else:
			d.all_bullet_curves_data = [curves, curves]
		var v: BulletVolley2D = factory.spawn_volley(d)
		var before: Vector2 = v.get_bullet_direction(0)
		var text := "You are trying to set bullet direction directly while having a direction curve assigned to the %s curves data. The curve will override any direct direction changes. Set the curve to null first if you want to set direction directly." % owner
		v.set_bullet_direction(0, Vector2(0, -1))
		expect_warning_sequence([text])
		v.set_bullet_direction_towards_position(0, Vector2(-400, 0))
		expect_warning_sequence([text])
		v.set_bullet_direction_towards_node2d(0, target)
		expect_warning_sequence([text])
		assert_eq(v.get_bullet_direction(0), before, "%s curve: the direction was left to the curve" % owner)
		v.clear_all_bullets()
		await idle(1)


## A direction whose square overflows a float (1e20 px) still normalizes to
## its direction (normalized() of such a vector used to return (0, 0) and the
## bullet stalled for good), through the setter, a direction curve with a
## huge strength and a huge lateral wobble.
func test_huge_directions_keep_their_heading(how: String = use_parameters(["setter", "curve", "wobble"])) -> void:
	var d := H.make_volley_data(1, 100.0, 30.0)
	if how == "curve":
		var c := BulletCurvesData2D.new()
		c.x_direction_curve = H.make_flat_curve(1.0)
		c.x_direction_curve_strength = 1e20
		d.shared_bullet_curves_data = c
	elif how == "wobble":
		var w := BulletWobbleData2D.new()
		w.enabled = true
		w.mode = BulletWobbleData2D.WOBBLE_LATERAL
		w.amplitude = 1e22
		w.frequency_hz = 2.0
		d.shared_bullet_wobble_data = w
	var v: BulletVolley2D = factory.spawn_volley(d)
	if how == "setter":
		v.set_bullet_direction(0, Vector2(1e20, 0))
		assert_almost_eq(v.get_bullet_direction(0), Vector2(1, 0), Vector2(0.0001, 0.0001), "normalized, not zeroed")
	var start: Vector2 = v.get_bullet_transform(0).origin
	for k in 6:
		factory.debug_advance_time(1.0 / 60.0)
	var dir: Vector2 = v.get_bullet_direction(0)
	assert_true(dir.is_finite() and dir.length() > 0.99, "%s: a unit heading (%s)" % [how, dir])
	assert_gt(v.get_bullet_transform(0).origin.distance_to(start), 1.0, "%s: the bullet keeps moving" % how)


## Integrated motion never overflows into INF/NaN poses: speed with an
## extreme acceleration and no top speed (0 = unlimited), spin the same way,
## a huge gravity, a teleport shift past float range and a fast orbit on a
## tiny radius. Every bullet stays finite (an INF pose reached the physics
## server and the MultiMesh; a NaN one stuck forever).
func test_extreme_motion_stays_finite(how: String = use_parameters(["speed", "spin", "gravity", "teleport", "orbit", "movement_pattern"])) -> void:
	var d := H.make_volley_data(2, 100.0, 60.0)
	if how == "speed" or how == "movement_pattern":
		d.all_bullet_speed_data = [H.make_speed(100.0, 0.0, 3e38), H.make_speed(100.0, 0.0, 3e38)]
	elif how == "spin":
		d.all_bullet_rotation_data = [H.make_rotation(1.0, 0.0, 3e38), H.make_rotation(1.0, 0.0, 3e38)]
	var v: BulletVolley2D = factory.spawn_volley(d)
	match how:
		"gravity":
			v.all_bullets_set_gravity(Vector2(3e38, 0))
		"teleport":
			v.teleport_bullet(0, Vector2(3e38, 0))
			v.teleport_shift_bullet(0, Vector2(3e38, 0))
			expect_error_sequence(["teleport_shift_bullet: the shifted position must be finite (it overflows float range), the bullet stays where it is."])
			assert_eq(v.get_bullet_transform(0).origin, Vector2(3e38, 0), "the refused shift kept the bullet")
		"orbit":
			v.shared_homing_deque_push_back_global_position_target(Vector2(50, 0))
			v.bullet_enable_orbiting(0, 0.5)
			v.set_bullet_speed_data(0, H.make_speed(3e38, 0.0, 0.0))
		"movement_pattern":
			# A step whose length overflows float range (|v dt| of ~1e36
			# components is INF in float32) must not walk the pattern to INF.
			var c := Curve2D.new()
			for p in [Vector2(0, 0), Vector2(30, 10), Vector2(60, -10), Vector2(90, 0)]:
				c.add_point(p)
			v.set_shared_movement_pattern_curve(c)
	for k in 120:
		factory.debug_advance_time(1.0 / 60.0)
	var bad := ""
	for i in 2:
		if not v.get_bullet_transform(i).is_finite():
			bad += "bullet %d pose %s; " % [i, v.get_bullet_transform(i)]
		if not v.get_bullet_velocity(i).is_finite():
			bad += "bullet %d velocity %s; " % [i, v.get_bullet_velocity(i)]
	assert_eq(bad, "", "%s: every pose and velocity stays finite" % how)


func test_bullet_scale_keeps_the_pose_and_round_trips() -> void:
	assert_true(ClassDB.class_has_method("BulletVolley2D", "set_bullet_scale"), "bound")
	v.set_bullet_transform(1, Transform2D(0.7, Vector2(2, 1), 0.0, Vector2(50, 60)))
	v.set_bullet_transform(2, Transform2D(0.3, Vector2(1, -1), 0.0, Vector2(80, 60))) # mirrored
	var before: Array = v.all_bullets_get_transforms()
	var dir: Vector2 = v.get_bullet_direction(1)
	assert_almost_eq(v.get_bullet_scale(1), Vector2(2, 1), Vector2(0.001, 0.001), "read")
	assert_almost_eq(v.get_bullet_scale(2), Vector2(1, -1), Vector2(0.001, 0.001), "a mirrored bullet reads a negative y")
	for b in 3:
		v.set_bullet_scale(b, v.get_bullet_scale(b))
	var problems: Array = []
	for b in 3:
		var t: Transform2D = v.get_bullet_transform(b)
		if not t.is_equal_approx(before[b]):
			problems.append("bullet %d: %s became %s" % [b, before[b], t])
	assert_eq("\n".join(problems), "", "set(get()) is the identity")
	v.set_bullet_scale(1, Vector2(3, 0.5))
	var t1: Transform2D = v.get_bullet_transform(1)
	assert_almost_eq(t1.get_scale(), Vector2(3, 0.5), Vector2(0.001, 0.001), "scaled")
	assert_almost_eq(t1.get_rotation(), 0.7, 0.001, "the rotation is kept")
	assert_eq(t1.origin, Vector2(50, 60), "the origin is kept")
	assert_eq(v.get_bullet_direction(1), dir, "the direction is kept")
	v.all_bullets_set_scale(Vector2(0.5, 0.5), 0, 1)
	for s in v.all_bullets_get_scale(0, 1):
		assert_almost_eq(s, Vector2(0.5, 0.5), Vector2(0.001, 0.001), "a range")
	assert_almost_eq(v.get_bullet_scale(2), Vector2(1, -1), Vector2(0.001, 0.001), "outside the range untouched")


func test_bullet_scale_rejects_and_keeps_the_old_scale() -> void:
	v.set_bullet_scale(0, Vector2(2, 2))
	for bad in [Vector2(NAN, 1), Vector2(1, INF), Vector2(0, 1), Vector2(1, 0.00001), Vector2(1e39, 1)]:
		v.set_bullet_scale(0, bad)
		expect_error_sequence(["set_bullet_scale: new_scale must be finite and non-zero on both axes, keeping the old scale."])
	assert_almost_eq(v.get_bullet_scale(0), Vector2(2, 2), Vector2(0.001, 0.001), "kept")
	v.set_bullet_scale(3, Vector2(2, 2))
	expect_error_sequence(["Invalid bullet index in set_bullet_scale: 3 (amount_bullets: 3)"])
	v.all_bullets_set_scale(Vector2(2, 2), 2, 1)
	expect_error_sequence(["Invalid index range in all_bullets_set_scale (start > end). Nothing was applied."])


func test_a_scaled_bullet_keeps_its_scale_in_flight_and_its_hitbox_scales(rotate_only_textures: bool = use_parameters([true, false])) -> void:
	var d := H.make_still_data(2)
	d.rotate_only_textures = rotate_only_textures
	d.transforms = [Transform2D(0.0, Vector2(0, 0)), Transform2D(0.0, Vector2(0, 100))]
	d.monitorable = true # static bodies need it
	d.all_bullet_rotation_data = [H.make_rotation(4.0, 60.0), H.make_rotation(4.0, 60.0)]
	make_wall(Vector2(40, 0), Vector2(20, 20)) # its left edge at x = 30: out of the r6 circle's reach
	var hits := [0]
	factory.body_entered.connect(func(_b, _v, i) -> void:
		if i == 0:
			hits[0] += 1)
	var w: BulletVolley2D = factory.spawn_volley(d)
	await physics(4)
	assert_eq(hits[0], 0, "rotate_only_textures %s: out of reach unscaled" % rotate_only_textures)
	w.set_bullet_scale(0, Vector2(6, 6)) # r36 reaches past x = 30
	for f in 30:
		await physics(1)
		if hits[0] > 0:
			break
	assert_eq(hits[0], 1, "rotate_only_textures %s: the hitbox scaled with the bullet" % rotate_only_textures)
	assert_almost_eq(w.get_bullet_scale(0), Vector2(6, 6), Vector2(0.001, 0.001), "spinning keeps the scale")
	assert_gt(absf(w.get_bullet_transform(0).get_rotation()), 0.01, "it did spin")


func test_a_written_scale_never_reaches_the_next_life() -> void:
	var d := H.make_volley_data(2, 100.0, 0.1)
	var w: BulletVolley2D = factory.spawn_volley(d)
	await idle(1)
	w.all_bullets_set_scale(Vector2(4, 3))
	for f in 20:
		factory.debug_advance_time(1.0 / 60.0)
	await idle(1)
	assert_true(w.is_pooled(), "expired into the pool")
	var again: BulletVolley2D = factory.spawn_volley(d)
	assert_eq(again, w, "reused")
	assert_eq(Array(again.all_bullets_get_scale()), [Vector2(1, 1), Vector2(1, 1)], "the spawn data's scale")
