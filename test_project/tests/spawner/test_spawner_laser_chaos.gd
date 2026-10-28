extends BlastTest
## Laser spawner chaos over real frames: seeded LaserSpawner2Ds of every
## laser type, aim and target source (following or not, muzzle offsets in
## both spaces, spin, bursts, telegraphs, Path2D movement, previews, orphan
## policies) fire into up to three factories while the run reparents them
## (also across frames), detaches and re-adds them, teleports them (carry or
## not), switches their sources, retargets and re-applies live settings,
## swaps and edits their laser_data, ends and clears lasers, switches and
## frees factories, frees spawners and their holders. Every step: only the
## documented refusals (a detached or factory-less spawner cannot shoot),
## balanced books in every live factory, every beam of every live laser
## finite; no leak at exit.

const SEEDS := 8
const STEPS := 110
const KNOWN := ["spawner is outside the scene tree", "produced no transforms, volley skipped", "no BulletFactory2D assigned", "BulletFactory2D share its world", "BulletFactory2D is not in the scene tree"]

var rng := RandomNumberGenerator.new()
var problems: Array = []
var factories: Array = []
var spawners: Array = []
var holders: Array = []
var lasers: Array = []
var targets: Array = []
var path: Path2D


func _collect(where: String) -> void:
	for err in get_errors():
		if err.handled:
			continue
		err.handled = true
		if err.is_push_warning():
			continue
		var known := false
		for k in KNOWN:
			if str(err.code).contains(k):
				known = true
		if not known:
			problems.append("%s: %s" % [where, err.code])


func _new_factory() -> BulletFactory2D:
	var f := BulletFactory2D.new()
	add_child(f)
	factories.append(f)
	return f


func _live_factories() -> Array:
	factories = factories.filter(func(f): return is_instance_valid(f))
	return factories


func _laser_data() -> LaserData2D:
	var d := H.make_laser(rng.randf_range(0.0, 0.1), rng.randf_range(0.0, 0.1), rng.randf_range(0.0, 0.6), rng.randf_range(0.0, 0.2), rng.randf_range(20.0, 300.0))
	d.type = rng.randi() % 3
	d.aim = rng.randi() % 3
	d.follow_spawner = rng.randf() < 0.7
	d.speed = rng.randf_range(50.0, 400.0)
	d.turn_rate_degrees = [0.0, 90.0, 720.0][rng.randi() % 3]
	return d


func _new_spawner() -> void:
	var sp := LaserSpawner2D.new()
	sp.set_shooting_enabled(rng.randf() < 0.5)
	sp.laser_data = _laser_data()
	sp.pattern_source = [LaserSpawner2D.PATTERN_FROM_HELPER_RING, LaserSpawner2D.PATTERN_FROM_HELPER_FAN, LaserSpawner2D.PATTERN_FROM_HELPER_SPIRAL][rng.randi() % 3]
	sp.helper_bullets_amount = rng.randi_range(1, 6)
	sp.shoot_interval_sec = rng.randf_range(0.05, 0.4)
	sp.burst_enabled = rng.randf() < 0.3
	sp.telegraph_enabled = rng.randf() < 0.3
	sp.telegraph_sec = 0.05
	sp.spin_enabled = rng.randf() < 0.5
	sp.spin_speed_deg_per_sec = rng.randf_range(-180.0, 180.0)
	sp.spawn_position_offset = Vector2(rng.randf_range(-30, 30), rng.randf_range(-30, 30)) if rng.randf() < 0.4 else Vector2.ZERO
	sp.spawn_position_offset_space = rng.randi() % 2
	sp.show_pattern_preview = rng.randf() < 0.5
	sp.show_preview_during_runtime = rng.randf() < 0.3
	sp.orphaned_lasers = rng.randi() % 4
	sp.max_live_lasers = rng.randi_range(0, 3)
	sp.laser_target_source = rng.randi() % 7
	sp.laser_target_group = &"laser_chaos_foes"
	sp.laser_target_position = Vector2(rng.randf_range(0, 400), rng.randf_range(0, 300))
	sp.laser_target_node_name = "Foe"
	sp.laser_retarget_interval_sec = [0.0, 0.1][rng.randi() % 2]
	sp.position = Vector2(rng.randf_range(0, 400), rng.randf_range(0, 300))
	sp.laser_fired.connect(func(l: LaserVolley2D, _n: int) -> void: lasers.append(l))
	var parent: Node = self if holders.is_empty() or rng.randf() < 0.5 else holders[rng.randi() % holders.size()]
	if not is_instance_valid(parent):
		parent = self
	parent.add_child(sp)
	var fs := _live_factories()
	if not fs.is_empty():
		sp.set_bullet_factory(fs[rng.randi() % fs.size()])
	if rng.randf() < 0.3:
		sp.movement_path = sp.get_path_to(path)
		sp.movement_enabled = true
	spawners.append(sp)


func _op() -> void:
	spawners = spawners.filter(func(s): return is_instance_valid(s))
	holders = holders.filter(func(h): return is_instance_valid(h))
	var sp: LaserSpawner2D = null if spawners.is_empty() else spawners[rng.randi() % spawners.size()]
	match rng.randi() % 18:
		0, 1:
			_new_spawner()
		2, 3:
			if sp != null:
				sp.shoot_once()
		4:
			if sp != null and sp.is_inside_tree():
				var parent := sp.get_parent()
				parent.remove_child(sp)
				holders.append(Node2D.new())
				(holders.back() as Node2D).position = Vector2(rng.randf_range(-50, 50), rng.randf_range(-50, 50))
				add_child(holders.back())
				if rng.randf() < 0.5:
					holders.back().add_child(sp)
				else:
					sp.set_meta("detached", true) # back in by op 5 (frames later)
		5:
			for s in spawners:
				if not s.is_inside_tree():
					add_child(s)
		6:
			var fs := _live_factories()
			if sp != null and not fs.is_empty():
				sp.set_bullet_factory(fs[rng.randi() % fs.size()])
		7:
			if sp != null and sp.is_inside_tree():
				sp.teleport(Vector2(rng.randf_range(0, 400), rng.randf_range(0, 300)), rng.randf() < 0.5)
		8:
			if sp != null:
				sp.laser_target_source = rng.randi() % 7
				if rng.randf() < 0.5:
					sp.retarget_live_lasers()
				else:
					sp.apply_to_live_lasers()
		9:
			if sp != null:
				if rng.randf() < 0.5:
					sp.laser_data = _laser_data()
				elif sp.laser_data != null:
					sp.laser_data.aim = rng.randi() % 3
					sp.laser_data.length = rng.randf_range(10.0, 400.0)
		10:
			if sp != null:
				sp.spawn_pattern_list([{"laser_data": _laser_data()}, {"helper_bullets_amount": rng.randi_range(1, 4)}], rng.randf() < 0.5, 0.05)
		11:
			if sp != null:
				if rng.randf() < 0.5:
					sp.free()
				else:
					sp.queue_free()
		12:
			var fs := _live_factories()
			if fs.size() > 1 and rng.randf() < 0.3:
				fs[rng.randi() % fs.size()].queue_free()
			elif fs.size() < 3:
				_new_factory()
		13:
			if not holders.is_empty() and rng.randf() < 0.3:
				holders[rng.randi() % holders.size()].queue_free() # takes its spawners along
		14:
			if sp != null:
				sp.end_live_lasers(rng.randf() < 0.5)
		15:
			var fs := _live_factories()
			if not fs.is_empty():
				match rng.randi() % 3:
					0:
						fs[0].clear_active_lasers()
					1:
						fs[0].end_lasers_in_circle(Vector2(200, 150), 120.0)
					_:
						fs[0].reset()
		16:
			if sp != null:
				sp.show_preview_during_runtime = not sp.show_preview_during_runtime
				sp.set_spin_angle_deg(rng.randf_range(0, 360))
				sp.spawn_position_offset_space = rng.randi() % 2
		17:
			if not targets.is_empty() and rng.randf() < 0.4:
				var t: Node2D = targets.pop_back()
				if is_instance_valid(t):
					t.queue_free()
			else:
				var t := Node2D.new()
				t.name = "Foe"
				t.position = Vector2(rng.randf_range(0, 400), rng.randf_range(0, 300))
				t.add_to_group(&"laser_chaos_foes")
				add_child(t)
				targets.append(t)


func _check_lasers(where: String) -> void:
	lasers = lasers.filter(func(l): return is_instance_valid(l))
	for l: LaserVolley2D in lasers:
		if l.is_pooled():
			continue
		for i in l.get_beam_count():
			var a := l.get_beam_start(i)
			var b := l.get_beam_end(i)
			if not (a.is_finite() and b.is_finite() and is_finite(l.get_beam_angle(i))):
				problems.append("%s: laser beam %d not finite (%s -> %s)" % [where, i, a, b])


func test_laser_spawner_chaos_keeps_every_invariant() -> void:
	path = Path2D.new()
	path.curve = Curve2D.new()
	path.curve.add_point(Vector2(0, 0))
	path.curve.add_point(Vector2(300, 100))
	add_child(path)
	for sd in SEEDS:
		rng.seed = 7100 + sd
		seed(7100 + sd)
		_new_factory()
		for step in STEPS:
			_op()
			await physics(1)
			await idle(1)
			var where := "seed %d step %d" % [sd, step]
			_collect(where)
			_check_lasers(where)
			for f in _live_factories():
				var books: Dictionary = f.debug_assert_no_dangling()
				if not books["ok"]:
					problems.append("%s: %s" % [where, books["error"]])
		for s in spawners:
			if is_instance_valid(s):
				s.free()
		for h in holders:
			if is_instance_valid(h):
				h.free()
		for t in targets:
			if is_instance_valid(t):
				t.free()
		for f in factories:
			if is_instance_valid(f):
				f.free()
		spawners.clear()
		holders.clear()
		targets.clear()
		factories.clear()
		lasers.clear()
		await idle(2)
		_collect("seed %d end" % sd)
	assert_eq("\n".join(problems.slice(0, 8)), "", "laser spawner chaos keeps every invariant")
