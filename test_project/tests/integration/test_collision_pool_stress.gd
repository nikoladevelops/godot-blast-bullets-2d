extends BlastTest
## Collisions stay exact through pooling, with real physics. Every bullet
## life that touches a target it can hit gets exactly one hit (the default
## bullet_max_collision_count), whatever happened to the pooled volley before:
## the previous life died on the same target, the reuse came in the same
## physics frame as the kill (a shooter right after the factory's tick), from
## inside a game's own physics callback, or later; big or small shapes of every
## type; spawned inside the target or flying in; Area2D, StaticBody2D,
## CharacterBody2D and RigidBody2D targets (bodies need monitorable, switched
## between lives). Hits go to exactly one emitter after every reuse: the
## spawner that fired this life while it has handlers, else the factory; a
## previous owner never hears them. Seeds: COLLISION_SEEDS=<n> soaks n more.

const SHOTS_PER_CASE := 4


## Fires `data` right after the factory's physics tick (the demo's player
## fires from _physics_process): the reuse lands in the same frame as the kill.
class Shooter extends Node:
	var factory: BulletFactory2D
	var data: BulletVolleyData2D
	var shots_left := 0
	var live: BulletVolley2D
	var fired: Array = []

	func _physics_process(_delta: float) -> void:
		if shots_left > 0 and (live == null or live.is_pooled()):
			live = factory.spawn_volley(data)
			fired.append(live)
			shots_left -= 1


var _hits: Array = []


func _log_factory_hits() -> void:
	factory.area_entered.connect(func(t: Object, v: BulletVolley2D, i: int) -> void: _hits.append(["factory", t, v, i]))
	factory.body_entered.connect(func(t: Object, v: BulletVolley2D, i: int) -> void: _hits.append(["factory", t, v, i]))


func _target(kind: String, pos: Vector2) -> Node2D:
	var node: CollisionObject2D
	match kind:
		"area":
			node = Area2D.new()
		"static":
			node = StaticBody2D.new()
		"character":
			node = CharacterBody2D.new()
		_:
			var rigid := RigidBody2D.new()
			rigid.freeze = true
			node = rigid
	node.collision_layer = 4
	node.collision_mask = 0
	var cs := CollisionShape2D.new()
	var box := RectangleShape2D.new()
	box.size = Vector2(60, 60)
	cs.shape = box
	node.add_child(cs)
	node.position = pos
	add(node)
	return node


func _shape(kind: int, size: float) -> Shape2D:
	match kind:
		0:
			var c := CircleShape2D.new()
			c.radius = size
			return c
		1:
			var r := RectangleShape2D.new()
			r.size = Vector2(size * 2.0, size)
			return r
		_:
			var cap := CapsuleShape2D.new()
			cap.radius = size * 0.5
			cap.height = size * 2.0
			return cap


func _data(shape: Shape2D, from: Vector2, speed: float, monitorable: bool) -> BulletVolleyData2D:
	var d := H.make_volley_data(1, speed, 4.0)
	d.collision_mask = 4
	d.monitorable = monitorable
	d.collision_shape = shape
	d.transforms = [Transform2D(0.0, from)]
	return d


## Runs SHOTS_PER_CASE lives through one pool slot and returns a problem
## string per life that did not get exactly one hit on `target`.
func _run_case(label: String, target: Node2D, data: BulletVolleyData2D) -> Array:
	var shooter := Shooter.new()
	shooter.process_physics_priority = 1000 # after the factory's tick
	shooter.factory = factory
	shooter.data = data
	shooter.shots_left = SHOTS_PER_CASE
	add(shooter)
	for k in 240:
		await physics(1)
		if shooter.shots_left == 0 and shooter.live != null and shooter.live.is_pooled():
			break
	var problems: Array = []
	if shooter.fired.size() != SHOTS_PER_CASE:
		problems.append("%s: fired %d of %d lives" % [label, shooter.fired.size(), SHOTS_PER_CASE])
	var lives := {}
	for v in shooter.fired:
		lives[v.get_instance_id()] = true
	var per_life := {}
	for h in _hits:
		if h[1] == target:
			per_life[h[2].get_instance_id()] = per_life.get(h[2].get_instance_id(), 0) + 1
	var reused := 0
	for k in range(1, shooter.fired.size()):
		reused += int(shooter.fired[k] == shooter.fired[k - 1])
	var total_hits := 0
	for n in per_life.values():
		total_hits += n
	if total_hits != shooter.fired.size():
		problems.append("%s: %d hits for %d lives (reused %d times)" % [label, total_hits, shooter.fired.size(), reused])
	if not shooter.live.is_pooled():
		problems.append("%s: the last life never hit (still alive)" % label)
		shooter.live.disable_bullet(0)
	shooter.shots_left = 0
	shooter.queue_free()
	_hits.clear()
	return problems


func test_every_life_hits_its_target_exactly_once_through_pool_reuse() -> void:
	_log_factory_hits()
	var problems: Array = []
	var rng := RandomNumberGenerator.new()
	var extra := int(OS.get_environment("COLLISION_SEEDS")) if OS.has_environment("COLLISION_SEEDS") else 0
	var cases: Array = []
	for kind in ["area", "static", "character", "rigid"]:
		for shape_kind in 3:
			for size in [6.0, 48.0, 260.0]:
				for inside in [true, false]:
					cases.append([kind, shape_kind, size, inside])
	rng.seed = 7
	for s in extra * 8:
		cases.append([["area", "static", "character", "rigid"][rng.randi() % 4], rng.randi() % 3, rng.randf_range(4.0, 400.0), rng.randf() < 0.5])
	for c in cases:
		var target := _target(c[0], Vector2(400, 0))
		await idle(2) # the space sees the new target
		var from := Vector2(400, 0) if c[3] else Vector2(400 - 120 - c[2] * 2.0, 0)
		var speed := 0.0 if c[3] else 900.0
		var label := "%s shape %d size %.0f %s" % [c[0], c[1], c[2], "inside" if c[3] else "flying in"]
		problems.append_array(await _run_case(label, target, _data(_shape(c[1], c[2]), from, speed, c[0] != "area")))
		target.queue_free()
		await idle(2)
	assert_eq(problems, [], "every life hit exactly once (%d cases x %d lives)" % [cases.size(), SHOTS_PER_CASE])


func test_monitorable_switched_between_lives_decides_body_hits() -> void:
	_log_factory_hits()
	var _wall := _target("static", Vector2(300, 0))
	await idle(2)
	var circle := CircleShape2D.new()
	circle.radius = 20.0
	var seen: Array = []
	for monitorable in [false, true, false, true]:
		var v := factory.spawn_volley(_data(circle, Vector2(300, 0), 0.0, monitorable))
		for k in 6:
			await physics(1)
		seen.append([monitorable, _hits.size(), v.is_pooled()])
		if not v.is_pooled():
			v.disable_bullet(0)
		_hits.clear()
		await idle(2)
	# A static body is hit only by monitorable bullets; the pooled volley takes
	# the new setting every life (it is one pool slot: same count and shape).
	assert_eq(seen, [[false, 0, false], [true, 1, true], [false, 0, false], [true, 1, true]], "monitorable decides, life by life")


func test_hits_reach_only_the_current_owner_after_every_reuse() -> void:
	var log_entries: Array = []
	factory.area_entered.connect(func(_t: Object, v: BulletVolley2D, _i: int) -> void: log_entries.append(["factory", v]))
	var _target_node := _target("area", Vector2(300, 0))
	await idle(2)
	var data := H.make_volley_data(1, 0.0, 4.0)
	data.collision_mask = 4
	var spawners: Array = []
	for label in ["A", "B"]:
		var sp := make_spawner(data, BulletSpawner2D.PATTERN_FROM_HELPER_RING, 1)
		sp.helper_ring_radius = 0.0
		sp.position = Vector2(300, 0)
		sp.area_entered.connect(func(_t: Object, v: BulletVolley2D, _i: int) -> void: log_entries.append([label, v]))
		spawners.append(sp)
	var fired: Array = []
	var shoot := func(who: String) -> BulletVolley2D:
		var got: Array = []
		if who == "factory":
			data.transforms = [Transform2D(0.0, Vector2(300, 0))]
			got.append(factory.spawn_volley(data))
		else:
			var sp: BulletSpawner2D = spawners[0] if who == "A" else spawners[1]
			sp.volley_fired.connect(func(v: BulletVolley2D, _n: int) -> void: got.append(v), CONNECT_ONE_SHOT)
			sp.shoot_once()
		return got[0]
	for who in ["A", "B", "factory", "A", "factory", "B"]:
		log_entries.clear()
		var v: BulletVolley2D = shoot.call(who)
		for k in 6:
			await physics(1)
			if v.is_pooled():
				break
		fired.append(v)
		assert_eq(log_entries, [[who, v]], "%s's life: one hit, heard only by %s" % [who, who])
	var distinct := {}
	for v in fired:
		distinct[v.get_instance_id()] = true
	assert_eq(distinct.size(), 1, "one pooled volley served every owner in turn")
	# A freed spawner's pooled volley serves the factory next, silently for the old owner.
	spawners[0].free()
	log_entries.clear()
	var last: BulletVolley2D = shoot.call("factory")
	for k in 6:
		await physics(1)
	assert_eq(log_entries, [["factory", last]], "after the old owner is gone, the factory hears it")


## Godot's own rule for a shape switched off and on (probed with a plain
## Area2D: area_set_shape_disabled true then false in one frame fires no new
## area_entered; with a physics step between, it enters again): a bullet frozen
## and woken inside its target in the same frame never left it; frozen for a
## step, it enters again.
func test_a_bullet_woken_inside_its_target_enters_again_only_after_a_step() -> void:
	_log_factory_hits()
	var _target_node := _target("area", Vector2(300, 0))
	await idle(2)
	var circle := CircleShape2D.new()
	circle.radius = 20.0
	var d := _data(circle, Vector2(300, 0), 0.0, false)
	d.bullet_max_collision_count = 0 # never dies: only entries count
	var v := factory.spawn_volley(d)
	v.set_is_auto_pooling_enabled(false)
	for k in 4:
		await physics(1)
	assert_eq(_hits.size(), 1, "the first entry")
	await physics(1)
	v.disable_bullet(0)
	v.enable_bullet(0) # same frame: like Godot, it never left
	for k in 4:
		await physics(1)
	assert_eq(_hits.size(), 1, "frozen and woken in one frame: no new entry (Godot parity)")
	v.disable_bullet(0)
	for k in 2:
		await physics(1)
	v.enable_bullet(0)
	for k in 4:
		await physics(1)
	assert_eq(_hits.size(), 2, "frozen for a step: it enters again")
	v.disable_bullet(0)
