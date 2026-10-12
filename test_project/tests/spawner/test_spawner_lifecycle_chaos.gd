extends BlastTest
## Lifecycle chaos for spawners over real frames: spawners created under the
## test or under holder nodes, removed and re-added, reparented, detached,
## switched between up to three factories, freed one by one or with their
## holder, factories freed and recreated while their spawners and bullets
## fly, auto-fire with bursts and telegraphs, Path2D movement, live
## previews, pattern lists, pause/resume/reset, flocking toggled with
## flock_live_volleys, clears/resets/frees. Every step: only the documented
## refusals (a detached or factory-less spawner cannot shoot), finite
## bullets, every live factory's books balanced; no leak at exit.

const SEEDS := 10
const STEPS := 120
const KNOWN := ["spawner is outside the scene tree", "produced no transforms, volley skipped", "no BulletFactory2D assigned"]

var rng := RandomNumberGenerator.new()
var problems: Array = []
var factories: Array = []
var spawners: Array = []
var holders: Array = []
var path: Path2D
var volleys: Array = []


func _collect(where: String) -> void:
	for err in get_errors():
		if err.handled:
			continue
		err.handled = true
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


func _new_spawner() -> void:
	var fs := _live_factories()
	var data := H.make_volley_data(4, rng.randf_range(0, 200), rng.randf_range(0.3, 3.0))
	data.all_bullet_speed_data = []
	data.shared_bullet_speed_data = H.make_speed(rng.randf_range(0, 200))
	var sp := BulletSpawner2D.new()
	sp.set_shooting_enabled(rng.randf() < 0.5)
	sp.set_homing_enabled(false)
	sp.set_spawn_data(data)
	sp.pattern_source = [BulletSpawner2D.PATTERN_FROM_HELPER_RING, BulletSpawner2D.PATTERN_FROM_HELPER_FAN, BulletSpawner2D.PATTERN_FROM_HELPER_SPIRAL][rng.randi() % 3]
	sp.helper_bullets_amount = rng.randi_range(1, 16)
	sp.shoot_interval_sec = rng.randf_range(0.05, 0.4)
	sp.burst_enabled = rng.randf() < 0.4
	sp.telegraph_enabled = rng.randf() < 0.3
	sp.telegraph_sec = 0.1
	sp.flock_enabled = rng.randf() < 0.5
	sp.flock = BulletFlock2D.new()
	sp.flock_scope = rng.randi() % 3
	sp.show_pattern_preview = rng.randf() < 0.5
	sp.show_preview_during_runtime = rng.randf() < 0.5
	sp.orphaned_volleys = rng.randi() % 4
	sp.position = Vector2(rng.randf_range(0, 400), rng.randf_range(0, 300))
	sp.volley_fired.connect(func(v: BulletVolley2D, _n: int) -> void: volleys.append(v))
	var parent: Node = self if holders.is_empty() or rng.randf() < 0.5 else holders[rng.randi() % holders.size()]
	if not is_instance_valid(parent):
		parent = self
	parent.add_child(sp)
	if not fs.is_empty():
		sp.set_bullet_factory(fs[rng.randi() % fs.size()])
	if rng.randf() < 0.3:
		sp.movement_path = sp.get_path_to(path)
		sp.movement_enabled = true
	spawners.append(sp)


func _op() -> void:
	spawners = spawners.filter(func(s): return is_instance_valid(s))
	holders = holders.filter(func(h): return is_instance_valid(h))
	var sp: BulletSpawner2D = null if spawners.is_empty() else spawners[rng.randi() % spawners.size()]
	match rng.randi() % 14:
		0, 1: _new_spawner()
		2:
			if sp != null:
				sp.shoot_once()
		3:
			if sp != null and sp.is_inside_tree():
				var parent := sp.get_parent()
				parent.remove_child(sp)
				holders.append(Node2D.new())
				add_child(holders.back())
				if rng.randf() < 0.5:
					holders.back().add_child(sp)
				else:
					parent.add_child(sp)
		4:
			if sp != null and sp.is_inside_tree():
				sp.get_parent().remove_child(sp)
				sp.set_meta("detached", true)
				if rng.randf() < 0.5:
					add_child(sp) # re-enter at once
		5:
			for s in spawners:
				if not s.is_inside_tree():
					add_child(s)
		6:
			var fs := _live_factories()
			if sp != null and not fs.is_empty():
				sp.set_bullet_factory(fs[rng.randi() % fs.size()])
		7:
			if sp != null:
				sp.spawn_pattern_list([{"helper_bullets_amount": rng.randi_range(1, 6)}, {"pattern_source": BulletSpawner2D.PATTERN_FROM_HELPER_LINE}], rng.randf() < 0.5, 0.05)
		8:
			if sp != null:
				var which := rng.randi() % 3
				if which == 0:
					sp.pause_shooting()
				elif which == 1:
					sp.resume_shooting()
				else:
					sp.reset_shooting()
		9:
			if sp != null:
				sp.queue_free()
		10:
			var fs := _live_factories()
			if fs.size() > 1 and rng.randf() < 0.3:
				fs[rng.randi() % fs.size()].queue_free()
			elif fs.size() < 3:
				_new_factory()
		11:
			if not holders.is_empty() and rng.randf() < 0.3:
				holders[rng.randi() % holders.size()].queue_free() # takes its spawners along
		12:
			if sp != null:
				sp.flock_enabled = not sp.flock_enabled
				sp.flock_live_volleys()
		13:
			var fs := _live_factories()
			if not fs.is_empty():
				var which := rng.randi() % 3
				if which == 0:
					fs[0].clear_active_bullets()
				elif which == 1:
					fs[0].reset()
				else:
					fs[0].free_active_bullets()


func test_lifecycle_chaos_keeps_every_invariant() -> void:
	path = Path2D.new()
	path.curve = Curve2D.new()
	path.curve.add_point(Vector2(0, 0))
	path.curve.add_point(Vector2(300, 100))
	add_child(path)
	for sd in SEEDS:
		rng.seed = 900 + sd
		seed(900 + sd)
		_new_factory()
		for step in STEPS:
			_op()
			await physics(1)
			await idle(1)
			_collect("seed %d step %d" % [sd, step])
			for f in _live_factories():
				var books: Dictionary = f.debug_assert_no_dangling()
				if not books["ok"]:
					problems.append("seed %d step %d: %s" % [sd, step, books["error"]])
			volleys = volleys.filter(func(v): return is_instance_valid(v))
			for v in volleys:
				if v.debug_get_life_state() == "active":
					for i in v.get_amount_bullets():
						if v.is_bullet_status_enabled(i) and not v.get_bullet_transform(i).is_finite():
							problems.append("seed %d step %d: non-finite bullet" % [sd, step])
		for s in spawners:
			if is_instance_valid(s):
				s.free()
		for h in holders:
			if is_instance_valid(h):
				h.free()
		for f in factories:
			if is_instance_valid(f):
				f.free()
		spawners.clear()
		holders.clear()
		factories.clear()
		volleys.clear()
		await idle(2)
		_collect("seed %d end" % sd)
	assert_eq("\n".join(problems.slice(0, 6)), "", "lifecycle chaos keeps every invariant")
