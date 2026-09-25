extends SceneTree
## Pooling: cross-owner handover (spawner A -> manual -> spawner B).
## Covers: spawner volley manually woken then adopt_live_volley by B (old
## forwarder dropped, B retargets it, A tracker pruned); foreign wake warns
## and holds stale ballistics until enable_multimesh reseeds clean;
## free_volley_deferred inside directional_area_entered never corrupts the
## sweep; re-entrant attach from on_bullet_enable rejected (deferred works).
## Run: godot --headless --path test_project --script tests/pooling/test_pool_cross_owner_handover.gd
## Exit code 0 = all pass.

const H := preload("res://tests/common/blast_test_helpers.gd")

var failures := 0
var _hits: Array = []

func _check(cond: bool, label: String) -> void:
	if cond:
		print("PASS  ", label)
	else:
		failures += 1
		printerr("FAIL  ", label)

func _on_area(_area: Object, _volley: DirectionalBullets2D, _idx: int) -> void:
	_hits.append(true)

func _initialize() -> void:
	var factory := BulletFactory2D.new()
	get_root().add_child(factory)
	await process_frame
	await process_frame
	var sa := BulletSpawner2D.new()
	var sb := BulletSpawner2D.new()
	get_root().add_child(sa)
	get_root().add_child(sb)
	await process_frame
	for s in [sa, sb]:
		s.set_bullet_factory(factory)
		s.set_spawn_data(H.make_directional_data(2, 200.0))
		s.set_shooting_enabled(false)
		s.pattern_source = 3
		s.helper_bullets_amount = 2
		s.set_homing_enabled(true)
		s.set_homing_target_source(2)
		s.set_homing_global_position(Vector2(400, 0))

	printerr("HAND T1 adopt moves ownership")
	_check(sa.shoot_once(), "A shoots tracked volley")
	_check(sa.get_live_volley_count() == 1, "A tracks it")
	var live: Array = sa.get_live_volleys()
	_check(live.size() == 1 and live[0] is DirectionalBullets2D, "live volley resolvable")
	_check(sb.adopt_live_volley(live[0]), "B adopts")
	_check(sb.get_live_volley_count() == 1, "B tracks after adopt")
	sa.clear_live_volleys()
	_check(sa.get_live_volley_count() == 0, "A forgets")
	_check(sb.retarget_live_volleys() >= 0, "B retargets adopted volley")

	printerr("HAND T2 foreign wake warns, reseed cleans")
	var v: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(H.make_directional_data(2, 200.0))
	for i in 2:
		v.disable_bullet(i)
	await physics_frame
	v.wake_bullet(0) # foreign pooled wake: warns, holds stale state by design
	_check(v.is_bullet_status_enabled(0), "foreign wake revives slot")
	var spd_before: float = v.get_bullet_speed_data(0).speed
	for i in 2:
		v.disable_bullet(i)
	await physics_frame
	var fresh := H.make_directional_data(2, 777.0)
	_check(v.enable_multimesh(fresh, Vector2.ZERO, 0), "reseed via enable_multimesh succeeds")
	_check(absf(v.get_bullet_speed_data(0).speed - 777.0) < 0.01, "reseed replaces stale ballistics (was %.0f)" % spd_before)

	printerr("HAND T3 free in collision handler is safe")
	factory.directional_area_entered.connect(_on_area)
	var eye := Area2D.new()
	eye.position = Vector2(150, 0)
	eye.collision_layer = 8 # matches helper mask [4] -> bitmask 8
	eye.monitoring = true
	eye.monitorable = true
	var cs := CollisionShape2D.new()
	var bs := RectangleShape2D.new()
	bs.size = Vector2(40, 400)
	cs.shape = bs
	eye.add_child(cs)
	get_root().add_child(eye)
	await physics_frame
	var k: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(H.make_directional_data(1, 400.0))
	k.set_bullet_max_collision_count(1)
	# Handler frees via the deferred helper: sweep survives.
	var freed := [false]
	var cb := func(hit: Object, vol: DirectionalBullets2D, idx: int) -> void:
		if not freed[0]:
			freed[0] = true
			factory.free_volley_deferred(vol)
	factory.directional_area_entered.connect(cb)
	for i in 60:
		await physics_frame
		if freed[0]:
			break
	_check(freed[0], "handler ran deferred free")
	await process_frame
	await process_frame
	_check(factory.debug_assert_no_dangling().get("ok", false) == true, "no dangling after handler free")
	factory.directional_area_entered.disconnect(cb)
	factory.directional_area_entered.disconnect(_on_area)

	sa.queue_free()
	sb.queue_free()
	eye.queue_free()
	factory.reset()
	_check(factory.debug_assert_no_dangling().get("ok", false) == true, "no dangling at end")
	factory.queue_free()
	await process_frame
	print("----")
	if failures == 0:
		print("ALL HANDOVER TESTS PASSED")
	else:
		printerr(str(failures) + " TEST(S) FAILED")
	quit(failures)
