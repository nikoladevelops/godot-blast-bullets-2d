extends BlastTest
## Velocity composition contract (get_bullet_velocity docs): direction x speed
## + inherited offset + the integrated gravity fall speed whenever gravity is
## active, for shared AND per-bullet gravity, under plain acceleration, drag
## and speed curves (shared or per-bullet). Proof: a ballistic bullet's next
## tick moves it by exactly (reported velocity + gravity x delta) x delta
## (semi-implicit Euler: the fall speed grows first, then the bullet moves).

const DT := 1.0 / 60.0
const CASES := [
	"shared_gravity",
	"per_bullet_gravity",
	"per_bullet_gravity_drag",
	"per_bullet_gravity_shared_speed_curve",
	"per_bullet_gravity_per_bullet_speed_curve",
	"per_bullet_gravity_inherited",
	"shared_gravity_accel",
]


func _speed_curve() -> BulletCurvesData2D:
	var c := BulletCurvesData2D.new()
	c.movement_speed_curve = H.make_flat_curve(150.0)
	return c


func _data(case_name: String) -> BulletVolleyData2D:
	var d := H.make_volley_data(2, 200.0, 30.0)
	match case_name:
		"shared_gravity":
			d.gravity = Vector2(0, 400)
		"shared_gravity_accel":
			d.gravity = Vector2(0, 400)
			d.all_bullet_speed_data = [H.make_speed(100.0, 900.0, 300.0), H.make_speed(100.0, 900.0, 300.0)]
		"per_bullet_gravity_drag":
			d.all_bullet_gravity = [Vector2(0, 400), Vector2(250, 0)]
			d.linear_drag = 0.5
		"per_bullet_gravity_shared_speed_curve":
			d.all_bullet_gravity = [Vector2(0, 400), Vector2(250, 0)]
			d.shared_bullet_curves_data = _speed_curve()
		"per_bullet_gravity_per_bullet_speed_curve":
			d.all_bullet_gravity = [Vector2(0, 400), Vector2(250, 0)]
			d.all_bullet_curves_data = [_speed_curve(), _speed_curve()]
		_:
			d.all_bullet_gravity = [Vector2(0, 400), Vector2(250, 0)]
	return d


func test_reported_velocity_matches_the_next_tick_displacement(case_index: int = use_parameters(range(CASES.size()))) -> void:
	var case_name: String = CASES[case_index]
	var v: BulletVolley2D = factory.spawn_volley(_data(case_name))
	if case_name == "per_bullet_gravity_inherited":
		v.set_inherited_velocity_offset(Vector2(30, -20))
	await idle(1)
	for i in 20: # fall for a while: fall speed well above float noise
		factory.debug_advance_time(DT)
	var problems := PackedStringArray()
	for b in v.get_amount_bullets():
		var p0: Vector2 = v.get_bullet_transform(b).origin
		var reported: Vector2 = v.get_bullet_velocity(b)
		factory.debug_advance_time(DT)
		var moved: Vector2 = (v.get_bullet_transform(b).origin - p0) / DT
		var expected: Vector2 = reported + v.bullet_get_gravity(b) * DT
		if expected.distance_to(moved) > 0.05:
			problems.append("%s bullet %d: reported %s (+g dt = %s), moved %s (fall %.2f)" % [case_name, b, reported, expected, moved, v.debug_get_gravity_info(b)["fall_speed"]])
	assert_eq(", ".join(problems), "", "reported velocity includes the fall speed")


## Setters that change direction or speed report the same composition right
## away (not only after the next tick), and bullet_set_velocity sets the
## EXACT total velocity: the next tick moves the bullet by (set + g dt) dt.
const SETTERS := ["set_bullet_direction", "towards_position", "towards_node2d", "set_bullet_speed_data", "set_bullet_speed", "bullet_set_velocity"]


func test_setters_keep_the_fall_speed_in_the_reported_velocity(setter_index: int = use_parameters(range(SETTERS.size()))) -> void:
	var setter: String = SETTERS[setter_index]
	var d := H.make_volley_data(1, 200.0, 30.0)
	d.gravity = Vector2(0, 400)
	var v: BulletVolley2D = factory.spawn_volley(d)
	var target := Node2D.new()
	target.position = Vector2(-300, -300)
	add(target)
	await idle(1)
	for i in 20:
		factory.debug_advance_time(DT)
	var fall: Vector2 = v.get_bullet_velocity(0) - v.get_bullet_direction(0) * 200.0
	assert_gt(fall.length(), 100.0, "the bullet is falling")
	match setter:
		"set_bullet_direction":
			v.set_bullet_direction(0, Vector2(-1, 0))
		"towards_position":
			v.set_bullet_direction_towards_position(0, Vector2(-500, 0))
		"towards_node2d":
			v.set_bullet_direction_towards_node2d(0, target)
		"set_bullet_speed_data":
			v.set_bullet_speed_data(0, H.make_speed(120.0, 3000.0, 0.0))
		"set_bullet_speed":
			v.set_bullet_speed(0, 120.0)
		"bullet_set_velocity":
			v.bullet_set_velocity(0, Vector2(150, -60))
	var reported: Vector2 = v.get_bullet_velocity(0)
	if setter == "bullet_set_velocity":
		assert_almost_eq(reported, Vector2(150, -60), Vector2(0.01, 0.01), "the exact velocity that was set")
	else:
		var own: Vector2 = v.get_bullet_direction(0) * v.debug_get_bullet_info(0)["speed"]
		assert_almost_eq(reported, own + fall, Vector2(0.05, 0.05), "%s: direction x speed + the current fall speed" % setter)
	var p0: Vector2 = v.get_bullet_transform(0).origin
	factory.debug_advance_time(DT)
	var moved: Vector2 = (v.get_bullet_transform(0).origin - p0) / DT
	assert_almost_eq(moved, reported + Vector2(0, 400) * DT, Vector2(0.05, 0.05), "%s: the next tick flies the reported velocity (+ g dt)" % setter)


## max_speed 0 means unlimited: a velocity write keeps it unlimited (the
## bullet keeps accelerating past the speed that was set), while a capped
## bullet's cap rises to the written speed so the write sticks.
func test_a_velocity_write_keeps_an_unlimited_max_speed_unlimited(setter: String = use_parameters(["bullet_set_velocity", "set_bullet_speed"])) -> void:
	var d := H.make_volley_data(2, 100.0, 30.0)
	d.all_bullet_speed_data = [H.make_speed(100.0, 0.0, 60.0), H.make_speed(100.0, 150.0, 0.0)]
	var v: BulletVolley2D = factory.spawn_volley(d)
	await idle(1)
	for i in 2:
		if setter == "bullet_set_velocity":
			v.bullet_set_velocity(i, Vector2(200, 0))
		else:
			v.set_bullet_speed(i, 200.0)
	for i in 60:
		factory.debug_advance_time(DT)
	assert_eq(v.get_bullet_speed_data(0).max_speed, 0.0, "%s: still unlimited" % setter)
	assert_almost_eq(v.get_bullet_velocity(0).length(), 260.0, 0.5, "%s: one second at 60 px/s² on top of the 200 that was set" % setter)
	assert_almost_eq(v.get_bullet_velocity(1).length(), 200.0, 0.01, "%s: the 150 cap rose to the written 200" % setter)


## set_bullet_speed / get_bullet_speed: the signed speed along the direction.
## A write keeps the direction, the acceleration and the drift, and the next
## tick flies it; a negative speed flies backwards.
func test_set_bullet_speed_is_the_speed_the_next_tick_flies() -> void:
	assert_true(ClassDB.class_has_method("BulletVolley2D", "set_bullet_speed"), "bound")
	var d := H.make_volley_data(3, 100.0, 30.0)
	d.all_bullet_speed_data = [H.make_speed(100.0), H.make_speed(100.0, 3000.0, 120.0), H.make_speed(100.0)]
	var v: BulletVolley2D = factory.spawn_volley(d)
	v.set_inherited_velocity_offset(Vector2(0, 30))
	await idle(1)
	v.set_bullet_speed(0, 250.0)
	v.set_bullet_speed(1, 10.0)
	v.set_bullet_speed(2, -50.0)
	assert_eq(Array(v.all_bullets_get_speed()), [250.0, 10.0, -50.0], "read back")
	assert_eq(v.get_bullet_direction(2), Vector2(1, 0), "the direction is kept")
	var p0: Array = []
	for b in 3:
		p0.append(v.get_bullet_transform(b).origin)
	factory.debug_advance_time(DT)
	var moved: Array = []
	for b in 3:
		moved.append((v.get_bullet_transform(b).origin - p0[b]) / DT)
	assert_almost_eq(moved[0], Vector2(250, 30), Vector2(0.05, 0.05), "the written speed + the drift")
	assert_almost_eq(moved[2], Vector2(-50, 30), Vector2(0.05, 0.05), "a negative speed flies backwards")
	assert_almost_eq(v.get_bullet_speed(1), 12.0, 0.001, "the acceleration applies from the written speed")


func test_set_bullet_speed_rejects_and_keeps_the_old_speed() -> void:
	var v: BulletVolley2D = factory.spawn_volley(H.make_volley_data(2, 100.0, 30.0))
	await idle(1)
	v.set_bullet_speed(0, NAN)
	expect_error_sequence(["set_bullet_speed: new_speed must be finite, keeping the old speed."])
	v.set_bullet_speed(0, INF)
	expect_error_sequence(["set_bullet_speed: new_speed must be finite, keeping the old speed."])
	v.set_bullet_speed(2, 10.0)
	expect_error_sequence(["Invalid bullet index in set_bullet_speed: 2 (amount_bullets: 2)"])
	assert_eq(v.get_bullet_speed(-1), 0.0, "an invalid read is 0")
	expect_error_sequence(["Invalid bullet index in get_bullet_speed: -1 (amount_bullets: 2)"])
	v.all_bullets_set_speed(10.0, 1, 5)
	expect_error_sequence(["Invalid index range in all_bullets_set_speed (1..5 outside 0..1; use -1 as the end for \"through the last bullet\"). Nothing was applied."])
	assert_eq(Array(v.all_bullets_get_speed()), [100.0, 100.0], "nothing changed")
	v.all_bullets_set_speed(40.0, 1)
	assert_eq(Array(v.all_bullets_get_speed()), [100.0, 40.0], "a range writes only its bullets")


func test_a_speed_curve_owns_the_speed() -> void:
	var d := H.make_volley_data(2, 100.0, 30.0)
	var v: BulletVolley2D = factory.spawn_volley(d)
	await idle(1)
	v.bullet_set_curves_data(1, _speed_curve())
	v.set_bullet_speed(1, 10.0)
	expect_warning_sequence(["You are trying to set bullet speed directly while having a movement speed curve assigned as an individual bullet curves data. The curve will override any direct speed changes. Set the curve to null first if you want to set speed directly."])
	v.set_bullet_speed(0, 10.0)
	expect_no_errors("bullet 0 has no curve")
	v.shared_bullet_curves_data = _speed_curve()
	var curved := v.get_bullet_speed(0)
	v.set_bullet_speed(0, 20.0)
	expect_warning_sequence(["You are trying to set bullet speed directly while having a movement speed curve assigned to the shared curves data. The curve will override any direct speed changes. Set the curve to null first if you want to set speed directly."])
	assert_eq(v.get_bullet_speed(0), curved, "the refused write changed nothing")


func test_a_written_speed_never_reaches_the_next_life() -> void:
	var v: BulletVolley2D = factory.spawn_volley(H.make_volley_data(2, 100.0, 0.1))
	await idle(1)
	v.all_bullets_set_speed(-700.0)
	for f in 20:
		factory.debug_advance_time(DT)
	await idle(1)
	assert_true(v.is_pooled(), "expired into the pool")
	var again: BulletVolley2D = factory.spawn_volley(H.make_volley_data(2, 100.0, 0.1))
	assert_eq(again, v, "reused")
	assert_eq(Array(again.all_bullets_get_speed()), [100.0, 100.0], "the spawn data's speed")
	assert_eq(again.get_bullet_speed_data(0).max_speed, 3000.0, "and its cap")
