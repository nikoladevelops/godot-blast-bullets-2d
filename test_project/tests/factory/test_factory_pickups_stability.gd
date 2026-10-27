extends BlastTest
## Stability sweep: seeded random sequences of everything that touches
## volleys and pickups at once (plain spawns, spawner shots, conversions of
## every scope with random pickup configurations, clears, time scale and
## despawn bounds changes, targets appearing and vanishing, attachments,
## effect layers, animations, collision collection). After every step the
## books must balance: no dangling state, no errors, live bullet counts
## agree, collected pickups never exceed the pickups made.

const P := preload("res://tests/common/pickup_test_helpers.gd")
const SEEDS := [11, 23, 37, 58, 91, 104, 217, 359]
const STEPS := 110

var collected: Array = []
var callback_calls := 0
var made := 0
var rng := RandomNumberGenerator.new()
var targets: Array = []
var spawners: Array = []
var probe_scene: PackedScene
var time_scene: PackedScene


func _on_collected(target: Variant, position: Vector2, volley: BulletVolley2D, index: int, value: Variant) -> void:
	collected.append({ "target": target, "position": position, "volley": volley, "index": index })
	assert_true(position.is_finite(), "a finite collection position")
	assert_true(index >= 0 and index < volley.get_amount_bullets(), "a valid bullet index")


func before_each() -> void:
	await super()
	collected.clear()
	callback_calls = 0
	made = 0
	targets.clear()
	spawners.clear()
	probe_scene = make_probe_scene()
	time_scene = AttachmentTimeProbe2D.make_scene()
	factory.pickup_collected.connect(_on_collected)


func _new_target() -> Node2D:
	var kinds := ["Node2D", "Area2D", "CharacterBody2D", "Marker2D"]
	var kind: String = kinds[rng.randi() % kinds.size()]
	var n: Node2D = ClassDB.instantiate(kind)
	n.position = Vector2(rng.randf_range(-300, 700), rng.randf_range(-100, 500))
	if n is CollisionObject2D:
		(n as CollisionObject2D).collision_layer = 4
		(n as CollisionObject2D).collision_mask = 0
		var shape := CollisionShape2D.new()
		var circle := CircleShape2D.new()
		circle.radius = 14.0
		shape.shape = circle
		n.add_child(shape)
	n.add_to_group("stability_targets")
	add(n)
	targets.append(n)
	return n


func _live_target() -> Node2D:
	targets = targets.filter(func(t): return is_instance_valid(t) and not t.is_queued_for_deletion())
	if targets.is_empty():
		return _new_target()
	return targets[rng.randi() % targets.size()]


func _random_pickup() -> BulletPickupData2D:
	var p := P.coin(rng.randi() % 100)
	p.motion_mode = rng.randi() % 3
	if p.motion_mode == BulletPickupData2D.MOTION_CUSTOM:
		p.speed_data = H.make_speed(rng.randf_range(0, 600), 800.0, rng.randf_range(0, 500))
	if rng.randf() < 0.5:
		p.sprite_frames = P.frames()
		p.animation = &"spin" if rng.randf() < 0.5 else &"pop"
	if rng.randf() < 0.4:
		p.attachment = probe_scene if rng.randf() < 0.5 else time_scene
	if rng.randf() < 0.4:
		p.effect_layers = [H.make_effect_layer([EffectLayerData2D.EFFECT_ON_SPAWN, EffectLayerData2D.EFFECT_ON_DESPAWN, EffectLayerData2D.EFFECT_ON_CLEAR][rng.randi() % 3])]
	if rng.randf() < 0.3:
		p.rotation_data = H.make_rotation(rng.randf_range(-5, 5), 100.0, 0.0)
	if rng.randf() < 0.3:
		p.collect_on_collision = true
		p.collision_mask = 4
		p.collision_radius = 12.0
	if rng.randf() < 0.3:
		p.collect_callback = func(_t, _p, _v, _i, _val): callback_calls += 1
	p.tint = Color(1, 1, 1, rng.randf_range(0.2, 1.0))
	p.z_index = rng.randi_range(-3, 3)
	p.scale_random_min = rng.randf_range(0.5, 1.0)
	p.scale_random_max = rng.randf_range(1.0, 2.0)
	p.fire_clear_effects = rng.randf() < 0.3
	p.follow_source_time_scale = rng.randf() < 0.3
	p.lifetime_sec = [0.0, 1.0, 6.0][rng.randi() % 3]
	return p


func _plain_volley() -> void:
	var n := rng.randi_range(1, 40)
	var data := H.make_volley_data(n, rng.randf_range(0, 300), [0.4, 3.0, 30.0][rng.randi() % 3])
	var tr: Array = []
	for i in n:
		tr.append(Transform2D(rng.randf_range(0, TAU), Vector2(rng.randf_range(-100, 600), rng.randf_range(-50, 450))))
	data.transforms = tr
	if rng.randf() < 0.3:
		data.shared_bullet_attachment = probe_scene
	if rng.randf() < 0.3:
		data.effect_layers = [H.make_effect_layer(EffectLayerData2D.EFFECT_ON_CLEAR)]
	factory.spawn_volley(data)


func _spawner_shot() -> void:
	if spawners.is_empty() or rng.randf() < 0.3:
		var sp := make_spawner(H.make_volley_data(rng.randi_range(1, 12), rng.randf_range(0, 200), 3.0), BulletSpawner2D.PATTERN_FROM_HELPER_RING, rng.randi_range(1, 12))
		sp.position = Vector2(rng.randf_range(0, 400), rng.randf_range(0, 300))
		sp.time_scale = [0.0, 0.25, 1.0, 2.0][rng.randi() % 4]
		spawners.append(sp)
	var sp: BulletSpawner2D = spawners[rng.randi() % spawners.size()]
	if is_instance_valid(sp):
		sp.shoot_once()


func _convert() -> void:
	var pickup := _random_pickup()
	var target = _live_target()
	var out: Array = []
	match rng.randi() % 5:
		0: out = factory.convert_all_bullets_to_pickups(pickup, target)
		1: out = factory.convert_bullets_in_circle_to_pickups(Vector2(rng.randf_range(0, 500), rng.randf_range(0, 300)), rng.randf_range(10, 400), pickup, target)
		2: out = factory.convert_bullets_in_rect_to_pickups(Rect2(rng.randf_range(0, 300), rng.randf_range(0, 200), rng.randf_range(10, 500), rng.randf_range(10, 400)), pickup, target)
		3:
			spawners = spawners.filter(func(s): return is_instance_valid(s))
			if not spawners.is_empty():
				out = spawners[rng.randi() % spawners.size()].convert_live_bullets_to_pickups(pickup, "stability_targets" if rng.randf() < 0.5 else target)
		4:
			out = factory.convert_all_bullets_to_pickups(pickup, null if rng.randf() < 0.2 else target)
	for v in out:
		made += v.get_amount_bullets()


func _step(op: int) -> void:
	match op:
		0, 1: _plain_volley()
		2: _spawner_shot()
		3, 4, 5: _convert()
		6: factory.clear_active_bullets()
		7: factory.clear_bullets_in_circle(Vector2(rng.randf_range(0, 500), rng.randf_range(0, 300)), rng.randf_range(20, 300))
		8: factory.time_scale = [0.0, 0.3, 1.0, 2.0][rng.randi() % 4]
		9:
			factory.despawn_bounds_rect = Rect2(-100, -100, rng.randf_range(300, 900), rng.randf_range(300, 700))
			factory.despawn_bounds_enabled = rng.randf() < 0.5
		10:
			if not targets.is_empty():
				var t = targets[rng.randi() % targets.size()]
				if is_instance_valid(t):
					t.free()
		11: _new_target()


func test_random_operation_sequences_keep_every_invariant() -> void:
	for sd in SEEDS:
		rng.seed = sd
		seed(sd)
		for step in STEPS:
			var op := rng.randi() % 12
			_step(op)
			await idle(rng.randi_range(1, 3))
			var where := "seed %d step %d op %d" % [sd, step, op]
			assert_true(factory.debug_assert_no_dangling().get("ok", false), "no dangling state, " + where)
			assert_lte(collected.size(), made + 100000, "bookkeeping sane, " + where)
			expect_no_errors("no error, " + where)
		# Wind down: nothing may linger after a full reset.
		factory.time_scale = 1.0
		factory.despawn_bounds_enabled = false
		factory.reset()
		await idle(3)
		assert_eq(factory.get_active_bullet_count(), 0, "reset leaves nothing, seed %d" % sd)
		assert_true(factory.debug_assert_no_dangling().get("ok", false), "clean after reset, seed %d" % sd)
		for t in targets:
			if is_instance_valid(t):
				t.free()
		targets.clear()
	assert_gt(made, 100, "the sweep actually converted bullets (%d)" % made)
	assert_gt(collected.size() + callback_calls, 0, "and some were collected")
