extends SceneTree
## Path2D singular-base suite: the legacy AT_PATH2D linker must fall back to
## raw curve points when the generator is singular.
##
## The gap: the linker guarded base_inv/node_global on finite-only. A
## singular generator passes finite checks while its affine_inverse() is
## garbage, warping the whole curve. Now the linker requires an invertible
## base and otherwise uses the raw baked points (documented fallback).
##
## Covers: T1 singular generator still volleys with finite slots, T2 sane
## generator links normally, T3 no dangling.
##
## Run: godot --headless --path test_project --script tests/spawner/test_spawner_path2d_singular_base.gd
## Exit code 0 = all pass. Any FAIL = behavior drift or a real bug.

var failures := 0

func _check(cond: bool, label: String) -> void:
	if cond:
		print("PASS  ", label)
	else:
		failures += 1
		printerr("FAIL  ", label)

func _data() -> DirectionalBulletsData2D:
	var d := DirectionalBulletsData2D.new()
	d.transforms = [Transform2D(0.0, Vector2.ZERO)]
	var s := BulletSpeedData2D.new()
	s.speed = 0.0
	s.max_speed = 3000.0
	d.all_bullet_speed_data = [s]
	d.max_life_time = 60.0
	d.texture_size = Vector2(16, 16)
	d.set_collision_layer_from_array([2])
	d.set_collision_mask_from_array([3])
	return d

func _path() -> Path2D:
	var path := Path2D.new()
	var curve := Curve2D.new()
	curve.add_point(Vector2(-100, 0))
	curve.add_point(Vector2(0, -60), Vector2.ZERO, Vector2.ZERO)
	curve.add_point(Vector2(100, 0))
	path.curve = curve
	get_root().add_child(path)
	return path

func _finite_volley(volley: Array) -> bool:
	for t in volley:
		if not (t as Transform2D).is_finite():
			return false
	return true

func _initialize() -> void:
	var factory := BulletFactory2D.new()
	get_root().add_child(factory)
	await process_frame
	await process_frame
	var path := _path()

	# ---------------------------------------------------------------
	printerr("PATH2DSING T1 singular generator falls back, stays finite")
	var gen := Node2D.new()
	gen.scale = Vector2(0, 1) # determinant 0, finite
	get_root().add_child(gen)
	await process_frame
	var sp := BulletSpawner2D.new()
	get_root().add_child(sp)
	sp.set_bullet_factory(factory)
	sp.set_spawn_data(_data())
	sp.set_shooting_enabled(false)
	sp.set_helper_path2d_node(path)
	sp.set_helper_path2d_space(BulletSpawner2D.PATH2D_SPACE_AT_PATH2D)
	sp.set_transforms_generator(gen)
	sp.pattern_source = BulletSpawner2D.PATTERN_FROM_HELPER_PATH2D
	sp.helper_bullets_amount = 8
	var volley = sp.collect_spawn_transforms()
	_check(volley.size() == 8, "T1 volley still emits 8 slots (got %d)" % volley.size())
	_check(_finite_volley(volley), "T1 all slots finite (raw-point fallback, no inverse garbage)")

	# ---------------------------------------------------------------
	printerr("PATH2DSING T2 sane generator links normally")
	gen.scale = Vector2.ONE
	var volley2 = sp.collect_spawn_transforms()
	_check(volley2.size() == 8, "T2 sane volley emits 8 slots")
	_check(_finite_volley(volley2), "T2 sane slots finite")

	_check(factory.debug_assert_no_dangling().get("ok", false) == true, "no dangling at end")

	path.queue_free()
	gen.queue_free()
	sp.queue_free()
	factory.reset()
	factory.queue_free()
	await process_frame
	print("----")
	if failures == 0:
		print("ALL PATH2D-SINGULAR TESTS PASSED")
	else:
		printerr(str(failures) + " TEST(S) FAILED")
	quit(failures)
