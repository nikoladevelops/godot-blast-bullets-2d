extends SceneTree
## Disable-reentrancy suite: the latch disable_bullet() was missing.
##
## The bug: enable_bullet() acquired the _bullet_enable_depth latch, but
## disable_bullet() only checked it. A user's on_bullet_disable handler
## disabling a second slot therefore proceeded: the counter decremented
## twice, disable_multimesh() ran twice, and the instance pooled twice
## (duplicate pool bucket entry, use-after-free on later pops).
##
## Covers: T1 nested disable from on_bullet_disable is rejected and the
## volley stays exact (counter, live set, single pool entry), T2 the outer
## disable still completes and the volley pools exactly once, T3 a later
## spawn reuses cleanly with no dangling, T4 plain sequential disables are
## unaffected by the latch.
##
## Run: godot --headless --path test_project --script tests/volley/test_volley_disable_reentrancy.gd
## Exit code 0 = all pass. Any FAIL = behavior drift or a real bug.

const Probe := preload("res://tests/scenes/reentrant_probe.gd")

var failures := 0

func _check(cond: bool, label: String) -> void:
	if cond:
		print("PASS  ", label)
	else:
		failures += 1
		printerr("FAIL  ", label)

func _probe_scene() -> PackedScene:
	var probe = Probe.new()
	var ps := PackedScene.new()
	var err: int = ps.pack(probe)
	probe.queue_free()
	if err != OK:
		printerr("probe pack failed")
	return ps

func _data() -> DirectionalBulletsData2D:
	var d := DirectionalBulletsData2D.new()
	d.transforms = [Transform2D(0.0, Vector2.ZERO), Transform2D(0.0, Vector2(16, 0))]
	var speeds: Array = []
	for i in 2:
		var s := BulletSpeedData2D.new()
		s.speed = 0.0
		s.max_speed = 3000.0
		speeds.append(s)
	d.all_bullet_speed_data = speeds
	d.max_life_time = 60.0
	d.texture_size = Vector2(16, 16)
	d.set_collision_layer_from_array([2])
	d.set_collision_mask_from_array([3])
	var c := CircleShape2D.new()
	c.radius = 6.0
	d.collision_shape = c
	d.bullet_max_collision_count = 0
	return d

func _initialize() -> void:
	var factory := BulletFactory2D.new()
	get_root().add_child(factory)
	await process_frame
	await process_frame
	Probe.reset_state()

	# ---------------------------------------------------------------
	printerr("REENTRANCY T1 nested disable from on_bullet_disable rejected")
	var v: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(_data())
	v.bullet_set_attachment(0, _probe_scene(), Vector2.ZERO, true)
	await process_frame
	Probe.volley = v
	Probe.victim = 1
	Probe.attempts = 0
	v.disable_bullet(0)
	_check(Probe.attempts == 1, "T1 handler ran exactly once (got %d)" % Probe.attempts)
	_check(v.is_bullet_status_enabled(1), "T1 nested disable rejected: victim still live")
	_check(not v.is_bullet_status_enabled(0), "T1 outer disable completed")
	_check(factory.debug_get_bullets_pool_amount(0) == 0, "T1 volley NOT pooled (one bullet still live, pooled=%d)" % factory.debug_get_bullets_pool_amount(0))

	# ---------------------------------------------------------------
	printerr("REENTRANCY T2 outer disable completes, pools exactly once")
	Probe.volley = null
	Probe.victim = -1
	v.disable_bullet(1)
	await process_frame
	_check(factory.debug_get_bullets_pool_amount(0) == 1, "T2 volley pooled exactly once (pooled=%d)" % factory.debug_get_bullets_pool_amount(0))

	# ---------------------------------------------------------------
	printerr("REENTRANCY T3 reuse after the rejected nest is clean")
	factory.debug_reset_pool_stats()
	var w: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(_data())
	var stats: Dictionary = factory.debug_get_pool_hit_stats()
	_check(int(stats.get("directional_hits", 0)) >= 1, "T3 reuse is a pool hit")
	_check(w.is_bullet_status_enabled(0) and w.is_bullet_status_enabled(1), "T3 reused volley fully live")
	_check(w.debug_get_attachment_info(0).get("has_attachment", true) == false, "T3 no attachment leaked across")
	factory.free_active_bullets()
	await process_frame

	# ---------------------------------------------------------------
	printerr("REENTRANCY T4 plain sequential disables unaffected by latch")
	var v4: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(_data())
	v4.disable_bullet(0)
	v4.disable_bullet(1)
	await process_frame
	_check(factory.debug_get_bullets_pool_amount(0) >= 1, "T4 sequential disables pool normally")

	_check(factory.debug_assert_no_dangling().get("ok", false) == true, "no dangling at end")
	Probe.reset_state()

	factory.reset()
	factory.queue_free()
	await process_frame
	print("----")
	if failures == 0:
		print("ALL DISABLE-REENTRANCY TESTS PASSED")
	else:
		printerr(str(failures) + " TEST(S) FAILED")
	quit(failures)
