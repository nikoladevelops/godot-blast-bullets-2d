extends BlastTest
## Pattern bake cache: raw transforms are generated once per geometry change
## and re-posed per shot when the generator moves rigidly (or translates, for
## world-direction patterns). Classification is MEASURED (probe markers), the
## verifier (on for every suite via BlastTest.before_all) regenerates every
## cached result, and these tests prove: cached == uncached for every source
## under random generator motion, hits really happen, every inspector
## property invalidates, OFF disables, mirror bursts get their own bake.

const SOURCES_NEEDING_INPUT := [
	BulletSpawner2D.PATTERN_FROM_HELPER_AIMED,
	BulletSpawner2D.PATTERN_FROM_HELPER_CUSTOM,
	BulletSpawner2D.PATTERN_FROM_HELPER_PATH2D,
]

var sp: BulletSpawner2D
var gen: Node2D
var rng := RandomNumberGenerator.new()


func before_each() -> void:
	await super()
	rng.seed = 424242
	gen = add(Node2D.new())
	gen.position = Vector2(300, 200)
	sp = make_spawner(H.make_volley_data(4), BulletSpawner2D.PATTERN_FROM_HELPER_RING, 24)
	sp.set_transforms_generator(gen)


func _assert_same(a: Array, b: Array, label: String) -> void:
	assert_eq(a.size(), b.size(), label + ": size")
	for i in mini(a.size(), b.size()):
		var x: Transform2D = a[i]
		var y: Transform2D = b[i]
		if x.origin.distance_to(y.origin) > 0.01 or x.x.distance_to(y.x) > 0.001 or x.y.distance_to(y.y) > 0.001:
			assert_true(false, "%s: slot %d cached %s vs uncached %s" % [label, i, x, y])
			return


func _random_pose() -> void:
	gen.position = Vector2(rng.randf_range(-500, 500), rng.randf_range(-500, 500))
	gen.rotation = rng.randf_range(-PI, PI)


## Seeds every helper RNG so random layouts are reproducible: an unseeded
## generator re-rolls per call, so two collects can never be compared.
func _seed_all() -> void:
	for p in sp.get_property_list():
		var name := str(p["name"])
		if name.begins_with("helper_") and name.ends_with("_seed"):
			sp.set(name, 1234)


func _configure_input(src: int) -> void:
	if src == BulletSpawner2D.PATTERN_FROM_HELPER_AIMED or src == BulletSpawner2D.PATTERN_FROM_HELPER_CORRIDOR:
		var tgt: Node2D = add(Node2D.new())
		tgt.position = Vector2(900, -300)
		sp.set_helper_aimed_target(tgt)
	elif src == BulletSpawner2D.PATTERN_FROM_HELPER_CUSTOM:
		sp.set_helper_custom_transforms([Transform2D(0.3, Vector2(10, 0)), Transform2D(-0.2, Vector2(0, 25)), Transform2D(1.0, Vector2(-15, -5))])
	elif src == BulletSpawner2D.PATTERN_FROM_HELPER_PATH2D:
		var path := Path2D.new()
		var curve := Curve2D.new()
		for p in [Vector2(0, 0), Vector2(100, 40), Vector2(200, -20), Vector2(320, 30)]:
			curve.add_point(p)
		path.curve = curve
		add(path)
		sp.set_helper_path2d_node(path)
	elif src == BulletSpawner2D.PATTERN_FROM_CHILDREN:
		for i in 3:
			var m := Node2D.new()
			m.position = Vector2(20 * i, -10 * i)
			m.rotation = 0.3 * i
			gen.add_child(m)


func test_every_source_cached_equals_uncached_under_motion(src: int = use_parameters(range(BulletSpawner2D.PATTERN_FROM_LAST))) -> void:
	sp.pattern_source = src
	_seed_all()
	_configure_input(src)
	for step in 6:
		_random_pose()
		var cached: Array = sp.collect_spawn_transforms()
		var fresh: Array = sp.debug_collect_spawn_transforms_uncached()
		_assert_same(cached, fresh, "source %d pose %d" % [src, step])
	assert_true(H.finite_volley(sp.collect_spawn_transforms()), "source %d finite" % src)


func test_rigid_ring_hits_cache_while_moving() -> void:
	sp.collect_spawn_transforms()
	var info: Dictionary = sp.debug_get_pattern_cache_info()
	assert_eq(int(info["shot_class"]), 2, "default ring is classified RIGID (2)")
	var hits0: int = info["hits"]
	var bakes0: int = info["bakes"]
	for i in 10:
		_random_pose()
		sp.collect_spawn_transforms()
	info = sp.debug_get_pattern_cache_info()
	assert_eq(int(info["hits"]) - hits0, 10, "every rigid move is served from the bake")
	assert_eq(int(info["bakes"]), bakes0, "no re-bake while only the generator moves")


func test_scaled_generator_rebakes_then_hits() -> void:
	sp.collect_spawn_transforms()
	gen.scale = Vector2(2, 2)
	sp.collect_spawn_transforms()
	var bakes1: int = sp.debug_get_pattern_cache_info()["bakes"]
	var hits1: int = sp.debug_get_pattern_cache_info()["hits"]
	gen.rotation = 1.0
	gen.position += Vector2(50, 0)
	sp.collect_spawn_transforms()
	var info: Dictionary = sp.debug_get_pattern_cache_info()
	assert_eq(int(info["hits"]), hits1 + 1, "rigid move of the scaled generator hits")
	assert_eq(int(info["bakes"]), bakes1, "no extra bake")


func test_translation_class_world_direction_rain() -> void:
	sp.pattern_source = BulletSpawner2D.PATTERN_FROM_HELPER_RAIN
	sp.helper_rain_seed = 7
	sp.collect_spawn_transforms()
	var cls: int = sp.debug_get_pattern_cache_info()["shot_class"]
	assert_true(cls >= 1, "seeded rain is cacheable (class %d)" % cls)
	var hits0: int = sp.debug_get_pattern_cache_info()["hits"]
	gen.position += Vector2(123, -45)
	sp.collect_spawn_transforms()
	assert_eq(int(sp.debug_get_pattern_cache_info()["hits"]), hits0 + 1, "translation is served from the bake")


func test_unseeded_scatter_never_cached_for_shots() -> void:
	sp.pattern_source = BulletSpawner2D.PATTERN_FROM_HELPER_SCATTER
	sp.helper_scatter_seed = 0
	var a: Array = sp.collect_spawn_transforms()
	var b: Array = sp.collect_spawn_transforms()
	assert_eq(int(sp.debug_get_pattern_cache_info()["shot_class"]), 0, "unseeded randomness is class NONE")
	var differs := false
	for i in mini(a.size(), b.size()):
		if (a[i] as Transform2D).origin.distance_to((b[i] as Transform2D).origin) > 0.01:
			differs = true
	assert_true(differs, "unseeded scatter still re-rolls every shot")


func test_external_sources_are_never_cached(src: int = use_parameters([BulletSpawner2D.PATTERN_FROM_CHILDREN, BulletSpawner2D.PATTERN_FROM_HELPER_AIMED, BulletSpawner2D.PATTERN_FROM_HELPER_CORRIDOR, BulletSpawner2D.PATTERN_FROM_HELPER_CUSTOM, BulletSpawner2D.PATTERN_FROM_HELPER_PATH2D])) -> void:
	sp.pattern_source = src
	_configure_input(src)
	sp.collect_spawn_transforms()
	sp.collect_spawn_transforms()
	var info: Dictionary = sp.debug_get_pattern_cache_info()
	assert_eq(int(info["shot_class"]), 0, "source %d reads other nodes: class NONE" % src)
	assert_eq(int(info["hits"]), 0, "source %d never served from cache" % src)


func test_cache_off_never_hits() -> void:
	sp.pattern_cache_mode = BulletSpawner2D.PATTERN_CACHE_OFF
	for i in 5:
		_random_pose()
		sp.collect_spawn_transforms()
	assert_eq(int(sp.debug_get_pattern_cache_info()["hits"]), 0, "OFF regenerates every time")
	sp.pattern_cache_mode = 7
	expect_error_sequence(["pattern_cache_mode must be 0 (Auto) or 1 (Off)"])
	assert_eq(sp.pattern_cache_mode, BulletSpawner2D.PATTERN_CACHE_OFF, "invalid mode rejected, old value kept")


func test_burst_mirror_bakes_separately() -> void:
	sp.pattern_source = BulletSpawner2D.PATTERN_FROM_HELPER_SPIRAL
	sp.helper_bullets_amount = 12
	sp.burst_alternate_mirror = true
	for i in 6:
		_random_pose()
		var cached: Array = sp.collect_spawn_transforms()
		_assert_same(cached, sp.debug_collect_spawn_transforms_uncached(), "spiral (mirror state %d)" % i)


## Value that differs from `v` for a property, guided by its hint.
func _perturb(p: Dictionary, v: Variant) -> Variant:
	match typeof(v):
		TYPE_BOOL:
			return not v
		TYPE_INT:
			if int(p.get("hint", 0)) == PROPERTY_HINT_ENUM:
				var opts: PackedStringArray = str(p.get("hint_string", "")).split(",")
				return (int(v) + 1) % maxi(1, opts.size())
			return int(v) + 1
		TYPE_FLOAT:
			return float(v) * 1.25 + 3.0
		TYPE_VECTOR2:
			return (v as Vector2) * 1.25 + Vector2(3, 2)
	return null


func test_every_pattern_property_invalidates_the_bake() -> void:
	# Generic sweep: bake, change ONE property, collect again. A setter that
	# forgot mark_pattern_dirty() would serve the stale bake and the verifier
	# push_errors "pattern cache mismatch" (strict mode fails the test).
	var swept := 0
	_seed_all()
	for src in [BulletSpawner2D.PATTERN_FROM_HELPER_RING, BulletSpawner2D.PATTERN_FROM_HELPER_RECTANGLE, BulletSpawner2D.PATTERN_FROM_HELPER_SPIRAL, BulletSpawner2D.PATTERN_FROM_HELPER_FLOWER, BulletSpawner2D.PATTERN_FROM_HELPER_GRID]:
		sp.pattern_source = src
		for p in sp.get_property_list():
			var name := str(p["name"])
			if not (name.begins_with("helper_") or name in ["pattern_scale", "transforms_scale"]):
				continue
			if name == "helper_bullets_amount" or name.ends_with("_path") or name.ends_with("_target") or name.ends_with("_seed") or name == "helper_custom_transforms" or name == "helper_skip_indices":
				continue
			var old: Variant = sp.get(name)
			var nv: Variant = _perturb(p, old)
			if nv == null:
				continue
			sp.collect_spawn_transforms() # bake at the old value
			_random_pose()
			sp.collect_spawn_transforms()
			sp.set(name, nv)
			swallow_rejections_for(name)
			_random_pose()
			var cached: Array = sp.collect_spawn_transforms()
			swallow_rejections_for(name)
			_assert_same(cached, sp.debug_collect_spawn_transforms_uncached(), "after changing %s (source %d)" % [name, src])
			swallow_rejections_for(name)
			sp.set(name, old)
			swallow_rejections_for(name)
			swept += 1
	assert_gt(swept, 100, "sweep covered the pattern inspector (%d properties)" % swept)


## A perturbed value may be legitimately rejected by its setter or make the
## generator refuse (e.g. a negative count): that is fine for THIS test (the
## contract here is cache coherence, pinned by the verifier's error text,
## which is never swallowed).
func swallow_rejections_for(_prop: String) -> void:
	for err in get_errors():
		if not err.handled and not err.contains_text("pattern cache mismatch"):
			err.handled = true


func test_skip_indices_and_scales_apply_after_the_bake() -> void:
	var plain: Array = sp.collect_spawn_transforms()
	sp.helper_skip_indices = PackedInt32Array([0, 5])
	sp.pattern_scale = 1.5
	sp.transforms_scale = 2.0
	_random_pose()
	_assert_same(sp.collect_spawn_transforms(), sp.debug_collect_spawn_transforms_uncached(), "skip + scales on a cached pattern")
	assert_eq(sp.collect_spawn_transforms().size(), plain.size() - 2, "skip carves exactly two slots")
