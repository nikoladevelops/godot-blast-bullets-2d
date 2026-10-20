extends BlastTest
## Teleporting mixes with every feature that rides or reads a bullet's pose.
## A teleport (teleport_bullet, teleport_shift_bullet, teleport_shift_all_
## bullets on the volley and the factory, set_bullet_transform) moves the
## bullet without travel: its trail layers and attachment move WITH it at
## once (running or paused, interpolation on or off), the interpolation
## history starts at the new pose (no streak), the jump is never a swept
## segment (no graze, no homing reach, no wall hit along it), a locked orbit
## keeps the new slot, homing steers from the new spot, a frozen bullet wakes
## where it was teleported, and a pooled handle refuses it.

const MOVES := ["teleport_bullet", "teleport_shift_bullet", "volley_shift_all", "factory_shift_all", "set_bullet_transform"]


func _trail_data(n := 2, speed := 100.0) -> BulletVolleyData2D:
	var d := H.make_volley_data(n, speed, 30.0)
	d.effect_layers = [H.make_effect_layer(BulletEffectLayerData2D.EFFECT_TRAIL_FOLLOW)]
	return d


## Moves bullet 0 of `v` by (40, 30) through `how`.
func _move(v: BulletVolley2D, how: String) -> void:
	var at: Vector2 = v.get_bullet_transform(0).origin
	match how:
		"teleport_bullet":
			v.teleport_bullet(0, at + Vector2(40, 30))
		"teleport_shift_bullet":
			v.teleport_shift_bullet(0, Vector2(40, 30))
		"volley_shift_all":
			v.teleport_shift_all_bullets(Vector2(40, 30))
		"factory_shift_all":
			factory.teleport_shift_all_bullets(Vector2(40, 30))
		"set_bullet_transform":
			v.set_bullet_transform(0, Transform2D(0.0, at + Vector2(40, 30)), false)


func _trail_problem(v: BulletVolley2D, label: String) -> String:
	for i in v.get_amount_bullets():
		var trail: Vector2 = v.debug_get_trail_transform(0, i).origin
		var at: Vector2 = v.get_bullet_transform(i).origin
		if trail.distance_to(at) > 0.01:
			return "%s: bullet %d at %s, its trail at %s" % [label, i, at, trail]
	return ""


func test_the_trail_moves_with_the_teleport(how: String = use_parameters(MOVES)) -> void:
	var problems: Array = []
	for paused in [false, true]:
		for interpolation in [false, true]:
			factory.use_physics_interpolation = interpolation
			var v: BulletVolley2D = factory.spawn_volley(_trail_data())
			await physics(3)
			await idle(1)
			factory.set_is_factory_processing_bullets(not paused)
			_move(v, how)
			var label := "%s paused=%s interpolation=%s" % [how, paused, interpolation]
			var p := _trail_problem(v, label)
			if p != "":
				problems.append(p)
			if interpolation and v.debug_get_previous_origin(0).distance_to(v.get_bullet_transform(0).origin) > 0.01:
				problems.append("%s: the interpolation history did not restart (streak from %s)" % [label, v.debug_get_previous_origin(0)])
			factory.set_is_factory_processing_bullets(true)
			v.clear_all_bullets()
			await idle(1)
	assert_eq("\n".join(problems), "", how)
