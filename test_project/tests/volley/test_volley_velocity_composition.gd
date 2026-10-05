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
		var p0: Vector2 = v.get_bullet_global_transform(b).origin
		var reported: Vector2 = v.get_bullet_velocity(b)
		factory.debug_advance_time(DT)
		var moved: Vector2 = (v.get_bullet_global_transform(b).origin - p0) / DT
		var expected: Vector2 = reported + v.bullet_get_gravity(b) * DT
		if expected.distance_to(moved) > 0.05:
			problems.append("%s bullet %d: reported %s (+g dt = %s), moved %s (fall %.2f)" % [case_name, b, reported, expected, moved, v.debug_get_gravity_info(b)["fall_speed"]])
	assert_eq(", ".join(problems), "", "reported velocity includes the fall speed")
