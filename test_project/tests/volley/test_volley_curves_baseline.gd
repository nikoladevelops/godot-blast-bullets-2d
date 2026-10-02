extends BlastTest
## Characterization (pinned, not necessarily ideal): clearing shared or
## per-bullet curves FREEZES the last sampled speed (no baseline restore),
## and a constant Additive direction curve steers cumulatively each tick.
## Any change to these semantics must fail here loudly.


func _data() -> DirectionalBulletsData2D:
	var d := H.make_directional_data(1, 200.0, 60.0)
	d.transforms = [Transform2D(PI / 2.0, Vector2.ZERO)] # fly +Y
	return d


func test_shared_curve_drives_then_clear_freezes() -> void:
	var v: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(_data())
	var cc := BulletCurvesData2D.new()
	cc.movement_speed_curve = H.make_flat_curve(400.0)
	v.set_shared_bullet_curves_data(cc)
	await physics(10)
	var driven: float = v.get_bullet_speed_data(0).speed
	assert_almost_eq(driven, 400.0, 5.0, "curve drives speed to 400")
	v.remove_shared_bullet_curves_data()
	await physics(10)
	assert_almost_eq(v.get_bullet_speed_data(0).speed, driven, 5.0, "speed frozen at the last sample")


func test_per_bullet_clear_freezes() -> void:
	var v: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(_data())
	var c0 := BulletCurvesData2D.new()
	c0.movement_speed_curve = H.make_flat_curve(500.0)
	v.bullet_set_curves_data(0, c0)
	await physics(10)
	var driven: float = v.get_bullet_speed_data(0).speed
	assert_almost_eq(driven, 500.0, 5.0, "per-bullet curve drives")
	v.bullet_set_curves_data(0, null)
	await physics(10)
	assert_almost_eq(v.get_bullet_speed_data(0).speed, driven, 5.0, "per-bullet clear freezes")


func test_additive_direction_steers_cumulatively() -> void:
	var v: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(_data())
	var dc := BulletCurvesData2D.new()
	dc.x_direction_curve = H.make_flat_curve(0.5)
	dc.x_direction_curve_strength = 1.0
	dc.x_direction_curve_mode = 0 # Additive
	v.set_shared_bullet_curves_data(dc)
	await physics(5)
	var early: float = v.get_bullet_direction(0).angle()
	await physics(30)
	assert_gt(absf(angle_difference(early, v.get_bullet_direction(0).angle())), 0.05, "heading steered over ticks")
	assert_true(v.get_bullet_direction(0).is_normalized(), "direction normalized")
	assert_true(v.get_bullet_direction(0).is_finite(), "direction finite")
