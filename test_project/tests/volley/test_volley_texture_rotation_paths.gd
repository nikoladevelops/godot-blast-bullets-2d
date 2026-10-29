extends BlastTest
## texture_rotation_radians only turns the ART (the instance basis carries
## heading + offset): it never changes where a bullet flies, for every
## feature that faces or steers by the basis (homing in transform mode,
## wobble facing, direction-curve facing, movement-pattern facing, the
## flock's face_heading, all mixed). Each case flies twice, without and with
## a texture rotation, far apart (nothing interacts): the paths must match
## and the sprite must face the heading plus the offset.
## Pins: homing turned the basis by MINUS the offset (the heading came out
## off by twice the offset: a homing bullet flew off course), and the
## wobble / curve / pattern facings dropped the offset (with homing, a
## permanent spin).

const DT := 1.0 / 60.0
const OFFSET := 0.7
const FAR := Vector2(0, 5000)


func _data(texture_rotation: float, at: Vector2, case_name: String) -> BulletVolleyData2D:
	var tr: Array = []
	for k in 5:
		tr.append(Transform2D(-0.3 + 0.15 * k, at + Vector2(0, 14 * k)))
	var d := H.make_flight_data(tr, 180.0, 1000.0)
	d.texture_rotation_radians = texture_rotation
	if case_name.contains("homing"):
		d.homing_smoothing = 4.0
		d.homing_take_control_of_texture_rotation = true
	if case_name.contains("wobble"):
		var w := BulletWobbleData2D.new()
		w.enabled = true
		d.shared_bullet_wobble_data = w
	if case_name.contains("curve"):
		var c := BulletCurvesData2D.new()
		var curve := Curve.new()
		curve.min_value = -2.0
		curve.max_value = 2.0
		curve.add_point(Vector2(0, -1))
		curve.add_point(Vector2(0.5, 1))
		curve.add_point(Vector2(1, -1))
		c.y_direction_curve = curve
		c.rotate_towards_adjusted_direction = true
		c.direction_curve_rotation_speed = 6.0
		d.shared_bullet_curves_data = c
		d.max_life_time = 3.0
	return d


func _fly(case_name: String, texture_rotation: float, at: Vector2) -> BulletVolley2D:
	var v: BulletVolley2D = factory.spawn_volley(_data(texture_rotation, at, case_name))
	if case_name.contains("homing"):
		# Out of reach for the measured window: right next to its target a
		# bullet's bearing is singular and float noise grows.
		v.shared_homing_deque_push_back_global_position_target(at + Vector2(600, 300))
	if case_name.contains("pattern"):
		var pc := Curve2D.new()
		pc.add_point(Vector2(0, 0))
		pc.add_point(Vector2(100, 60))
		pc.add_point(Vector2(200, -60))
		pc.add_point(Vector2(300, 0))
		v.set_shared_movement_pattern_curve(pc)
		v.set_shared_movement_pattern_face_movement_direction(true)
	if case_name.contains("flock"):
		v.flock_join(BulletFlock2D.new()) # its own flock: the two runs never meet
	return v


func test_texture_rotation_never_changes_the_flight_path(case_name: String = use_parameters(
		["homing", "homing wobble", "wobble", "curve", "pattern", "flock", "flock homing wobble", "homing curve"])) -> void:
	var plain := _fly(case_name, 0.0, Vector2.ZERO)
	var turned := _fly(case_name, OFFSET, FAR)
	var problems: Array = []
	for tick in 150:
		factory.debug_advance_time(DT)
		if tick % 10 != 9:
			continue
		for i in 5:
			var a: Transform2D = plain.get_bullet_transform(i)
			var b: Transform2D = turned.get_bullet_transform(i)
			var drift := (b.origin - FAR).distance_to(a.origin)
			if drift > 0.05:
				problems.append("tick %d bullet %d: path off by %.2f px" % [tick + 1, i, drift])
			var art := absf(angle_difference(a.x.angle() + OFFSET, b.x.angle()))
			if art > deg_to_rad(0.05):
				problems.append("tick %d bullet %d: art turned %.2f deg off its offset" % [tick + 1, i, rad_to_deg(art)])
	assert_eq("\n".join(problems.slice(0, 6)), "", "%s: the texture rotation is cosmetic" % case_name)


## The texture follows the FIRST channel that steered: a shared y-curve must
## not steal the facing source from the bullet's own x-curve.
func test_the_own_x_curve_keeps_the_facing_source() -> void:
	var own := BulletCurvesData2D.new()
	var cx := Curve.new()
	cx.min_value = -2.0
	cx.max_value = 2.0
	cx.add_point(Vector2(0, -1))
	cx.add_point(Vector2(0.5, 1))
	cx.add_point(Vector2(1, -1))
	own.x_direction_curve = cx
	own.rotate_towards_adjusted_direction = true
	own.direction_curve_rotation_speed = 12.0
	var shared := BulletCurvesData2D.new()
	var cy := Curve.new()
	cy.min_value = -2.0
	cy.max_value = 2.0
	cy.add_point(Vector2(0, -1))
	cy.add_point(Vector2(0.5, 1))
	cy.add_point(Vector2(1, -1))
	shared.y_direction_curve = cy
	shared.rotate_towards_adjusted_direction = false # the shared resource never turns the art
	var d := H.make_flight_data([Transform2D(0.0, Vector2.ZERO)], 180.0, 1000.0)
	d.shared_bullet_curves_data = shared
	var v: BulletVolley2D = factory.spawn_volley(d)
	v.bullet_set_curves_data(0, own)
	var first: float = v.get_bullet_transform(0).x.angle()
	var turned := false
	for tick in 120:
		factory.debug_advance_time(DT)
		if absf(angle_difference(v.get_bullet_transform(0).x.angle(), first)) > 0.2:
			turned = true
			break
	assert_true(turned, "the bullet's own x-curve owns the facing (shared rotate = false)")


func test_a_homing_bullet_with_a_texture_rotation_reaches_its_target() -> void:
	for offset in [0.0, 0.7, -1.5, PI]:
		var d := H.make_flight_data([Transform2D(0.0, Vector2(0, 0))], 300.0, 1000.0)
		d.texture_rotation_radians = offset
		d.homing_smoothing = 5.0
		d.homing_take_control_of_texture_rotation = true
		d.homing_distance_before_reached = 12.0
		var v: BulletVolley2D = factory.spawn_volley(d)
		v.shared_homing_deque_push_back_global_position_target(Vector2(400, 300))
		var reached := false
		for tick in 180:
			factory.debug_advance_time(DT)
			if v.get_bullet_transform(0).origin.distance_to(Vector2(400, 300)) <= 12.0:
				reached = true
				break
		assert_true(reached, "texture rotation %.2f: reached within 3 s" % offset)
		factory.free_active_bullets()
		await idle(1)
