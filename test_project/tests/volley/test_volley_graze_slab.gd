extends BlastTest
## Many-target graze zones: past 8 live targets a bullet is tested only
## against the targets near its motion in x (the slab, sorted by x once per
## tick); up to 8 sit inline and are all tested. Seeded scenes (fast bullets
## in every direction, resting bullets, targets weaving across each other,
## rings entered, left and re-grazed) produce exactly the same grazes and
## exits with the slab forced on as with every target tested, for few
## (inline) and many targets; equal distances still go to the first target
## in tree order; the threshold knob rejects bad values.

const GROUP := &"graze_slab"


func after_each() -> void:
	factory.debug_set_graze_slab_min_targets(8)
	await super()


## One seeded scene: `target_count` targets weaving around the origin, 48
## bullets (fast in every direction, slow, resting), two-ring After Exit
## zone, 40 ticks. Returns every event as [kind, target index, bullet, ring].
func _scene(scene_seed: int, slab_min: int, target_count: int = 16) -> Array:
	factory.debug_set_graze_slab_min_targets(slab_min)
	var rng := RandomNumberGenerator.new()
	rng.seed = scene_seed
	var targets: Array = []
	var homes: Array = []
	for i in target_count:
		var home := Vector2(rng.randf_range(-300, 300), rng.randf_range(-300, 300))
		targets.append(make_graze_target(home, GROUP))
		homes.append(home)
	var transforms: Array = []
	for i in 48:
		transforms.append(Transform2D(rng.randf_range(-PI, PI), Vector2(rng.randf_range(-400, 400), rng.randf_range(-400, 400))))
	var d := H.make_volley_data(48, 0.0, 60.0)
	d.transforms = transforms
	for i in 48:
		var sp := d.all_bullet_speed_data[i] as BulletSpeedData2D
		sp.max_speed = 100000.0
		sp.speed = [0.0, 300.0, 1800.0, 4200.0][i % 4] # resting, slow, fast, faster than a ring per tick
	d.collision_shape = H.make_circle_shape(4.0)
	var v: BulletVolley2D = factory.spawn_volley(d)
	var zone := H.make_graze_zone([14.0, 36.0], GROUP)
	zone.regraze = BulletGrazeZone2D.REGRAZE_AFTER_EXIT
	v.graze_set_zones([zone])
	var log := H.record_graze(factory)
	for tick in 40:
		for i in targets.size():
			(targets[i] as Node2D).position = homes[i] + Vector2(cos(tick * 0.3 + i), sin(tick * 0.2 + 2 * i)) * 90.0
		step_factory()
	var out: Array = []
	for e in log:
		out.append([e[0], targets.find(e[1]), e[3], e[5]])
	for t in targets:
		(t as Node).free()
	v.graze_clear()
	return out


func test_the_slab_finds_exactly_what_testing_every_target_finds() -> void:
	# 16 targets: every one tested from the volley's buffers vs the slab;
	# 6 targets: every one tested inline vs the slab.
	for target_count in [16, 6]:
		for scene_seed in [11, 23, 47]:
			var every := _scene(scene_seed, 64, target_count)
			var slab := _scene(scene_seed, 0, target_count)
			var what := "%d targets, seed %d" % [target_count, scene_seed]
			assert_gt(every.size(), 10, what + ": a busy scene")
			var exits := every.filter(func(e: Array) -> bool: return e[0] == "exit")
			assert_gt(exits.size(), 0, what + ": visits end too")
			assert_eq(slab, every, what + ": the same events in the same order")


func test_equal_distances_go_to_the_first_target_in_tree_order() -> void:
	for slab_min in [64, 0]: # inline (every target), then the x-sorted slab
		factory.debug_set_graze_slab_min_targets(slab_min)
		var group := StringName("graze_tie_%d" % slab_min)
		var right := make_graze_target(Vector2(10, 0), group) # first in tree order
		var left := make_graze_target(Vector2(-10, 0), group) # first in x order
		var v := graze_volley(H.transforms_at([Vector2.ZERO]), 0.0)
		v.graze_set_zones([H.make_graze_zone([20.0], group)])
		var log := H.record_graze(factory)
		step_factory()
		assert_eq(H.graze_kinds(log), ["enter:0:0"], "slab_min %d: one graze" % slab_min)
		assert_eq(log[0][1], right, "slab_min %d: the tie goes to the first in tree order" % slab_min)
		right.position = Vector2(300, 0)
		left.position = Vector2(-300, 0)
		step_factory()
		assert_eq(H.graze_kinds(log), ["enter:0:0", "exit:0:0"], "slab_min %d: one exit" % slab_min)
		assert_eq(log[1][1], right, "slab_min %d: naming the same target" % slab_min)
		assert_ne(log[1][1], left, "slab_min %d: not the other" % slab_min)
		v.graze_clear()


func test_the_threshold_knob_rejects_bad_values() -> void:
	assert_true(factory.has_method("debug_set_graze_slab_min_targets"), "bound")
	assert_eq(factory.debug_set_graze_slab_min_targets(3), 8, "returns the previous value (default 8)")
	assert_eq(factory.debug_set_graze_slab_min_targets(-1), 3, "a bad value changes nothing")
	assert_eq(factory.debug_set_graze_slab_min_targets(65), 3, "a bad value changes nothing")
	expect_error_sequence([
		"BulletFactory2D::debug_set_graze_slab_min_targets: value must be between 0 and 64, nothing changed.",
		"BulletFactory2D::debug_set_graze_slab_min_targets: value must be between 0 and 64, nothing changed.",
	])
