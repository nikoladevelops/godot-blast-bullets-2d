extends BlastTest
## AT_PATH2D linker: a singular generator (finite, det 0) falls back to the
## raw curve points instead of inverting garbage; a sane generator links.


func test_singular_then_sane_generator() -> void:
	var path: Path2D = add(Path2D.new())
	var curve := Curve2D.new()
	curve.add_point(Vector2(-100, 0))
	curve.add_point(Vector2(0, -60))
	curve.add_point(Vector2(100, 0))
	path.curve = curve
	var gen: Node2D = add(Node2D.new())
	gen.scale = Vector2(0, 1)
	await idle(1)
	var sp := make_spawner(H.make_directional_data(1, 0.0, 60.0), BulletSpawner2D.PATTERN_FROM_HELPER_PATH2D, 8)
	sp.set_helper_path2d_node(path)
	sp.set_helper_path2d_space(BulletSpawner2D.PATH2D_SPACE_AT_PATH2D)
	sp.set_transforms_generator(gen)
	var volley = sp.collect_spawn_transforms()
	assert_eq(volley.size(), 8, "singular generator still emits 8 slots")
	assert_true(H.finite_volley(volley), "raw-point fallback stays finite")
	gen.scale = Vector2.ONE
	var volley2 = sp.collect_spawn_transforms()
	assert_eq(volley2.size(), 8, "sane generator emits 8 slots")
	assert_true(H.finite_volley(volley2))
