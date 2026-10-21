extends BlastTest
## Patterns fire lasers like bullets: for EVERY pattern source (read from
## ClassDB, so new shapes are covered) a LaserSpawner2D fires exactly one beam
## per slot of collect_spawn_transforms(), each beam starting on its slot
## (plus the spawn position offset) and pointing along it, also under a
## rotated spawner, pattern scale, the offset and spin; the scene sources get
## their inputs (children, aimed target, Path2D, custom transforms), random
## ones a seed; the bake-cache verifier (on in every test) stays silent.

const DT := 0.0625


func _spawner(src: int) -> LaserSpawner2D:
	var sp := LaserSpawner2D.new()
	sp.set_shooting_enabled(false)
	sp.laser_data = H.make_laser(0.0, 0.0, 0.0, 0.0, 100.0)
	sp.pattern_source = src
	sp.helper_bullets_amount = 6
	sp.position = Vector2(50, 30)
	for p in sp.get_property_list():
		if String(p["name"]).ends_with("_seed") and int(p["type"]) == TYPE_INT:
			sp.set(p["name"], 7) # seed 0 randomizes from the clock
	for k in 3:
		var marker := Node2D.new()
		marker.position = Vector2(20 * k, -10 * k)
		marker.rotation = 0.3 * k
		sp.add_child(marker)
	if src == LaserSpawner2D.PATTERN_FROM_HELPER_AIMED:
		var target := Node2D.new()
		target.position = Vector2(400, 60)
		add(target)
		sp.set_helper_aimed_target(target)
		sp.helper_aimed_step_offset = 24.0
	if src == LaserSpawner2D.PATTERN_FROM_HELPER_PATH2D:
		var path := Path2D.new()
		path.curve = Curve2D.new()
		for pt in [Vector2(-300, 0), Vector2(0, -200), Vector2(300, 0)]:
			path.curve.add_point(pt)
		add(path)
		sp.set_helper_path2d_node(path)
	if src == LaserSpawner2D.PATTERN_FROM_HELPER_CUSTOM:
		sp.helper_custom_transforms = [Transform2D(0.2, Vector2(10, 0)), Transform2D(-1.0, Vector2(0, 25)), Transform2D(2.5, Vector2(-30, -5))]
	add(sp)
	sp.set_bullet_factory(factory)
	return sp


func _sources() -> Array:
	var out: Array = []
	for c in ClassDB.class_get_integer_constant_list("LaserSpawner2D"):
		if String(c).begins_with("PATTERN_FROM_") and String(c) != "PATTERN_FROM_LAST":
			out.append([String(c), ClassDB.class_get_integer_constant("LaserSpawner2D", c)])
	return out


func test_every_pattern_source_fires_one_beam_on_each_slot(variant: String = use_parameters(["plain", "transformed"])) -> void:
	var problems: Array = []
	var sources := _sources()
	assert_gt(sources.size(), 30, "every source")
	for entry in sources:
		var sp := _spawner(entry[1])
		var offset := Vector2.ZERO
		if variant == "transformed":
			sp.rotation = 0.4
			sp.pattern_scale = 1.5
			offset = Vector2(5, -7)
			sp.spawn_position_offset = offset
			sp.set_spin_angle_deg(25.0)
		var slots: Array = sp.collect_spawn_transforms()
		var fired: Array = []
		sp.laser_fired.connect(func(l: LaserVolley2D, _n: int) -> void: fired.append(l))
		sp.shoot_once()
		if slots.is_empty():
			problems.append("%s: no slots" % entry[0])
		elif fired.size() != 1:
			problems.append("%s: %d lasers fired" % [entry[0], fired.size()])
		else:
			var l: LaserVolley2D = fired[0]
			if l.get_beam_count() != slots.size():
				problems.append("%s: %d beams for %d slots" % [entry[0], l.get_beam_count(), slots.size()])
			else:
				for i in slots.size():
					var t: Transform2D = slots[i]
					if l.get_beam_start(i).distance_to(t.origin + offset) > 0.01:
						problems.append("%s beam %d: starts at %s, the slot is %s" % [entry[0], i, l.get_beam_start(i), t.origin + offset])
						break
					if absf(angle_difference(t.x.angle(), l.get_beam_angle(i))) > 0.0001:
						problems.append("%s beam %d: points %s, the slot %s" % [entry[0], i, l.get_beam_angle(i), t.x.angle()])
						break
		sp.free()
		factory.clear_active_lasers(false)
		step_factory(1, DT)
	assert_eq("\n".join(problems), "", "one beam per slot, on its slot")
