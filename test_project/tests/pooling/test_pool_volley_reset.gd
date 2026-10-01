extends SceneTree
## Pooled-volley reset suite: a reused volley must start as a new life.
##
## Contract under test: disabling every bullet parks the volley pooled; the
## next spawn of the same bucket reuses it (pool hit), and the new life has
## zeroed collision counts, a fresh dedup window (the same wall counts
## again), no live custom timers from the old life, no live attachments, and
## the reseeded ballistics (not the previous owner's per-bullet edits).
##
## Covers: T1 reuse is a pool hit, T2 collision counts reset, T3 the dedup
## window resets (same wall hits again), T4 old custom timers never fire in
## the new life, T5 no attachments leak across, T6 per-bullet ballistics edits
## do not survive, T7 no dangling.
##
## Run: godot --headless --path test_project --script tests/pooling/test_pool_volley_reset.gd
## Exit code 0 = all pass. Any FAIL = behavior drift or a real bug.

var failures := 0
var _timer_fired := false

func _check(cond: bool, label: String) -> void:
	if cond:
		print("PASS  ", label)
	else:
		failures += 1
		printerr("FAIL  ", label)

func _on_old_timer() -> void:
	_timer_fired = true

func _data() -> DirectionalBulletsData2D:
	var d := DirectionalBulletsData2D.new()
	d.transforms = [Transform2D(0.0, Vector2.ZERO)]
	var s := BulletSpeedData2D.new()
	s.speed = 900.0
	s.max_speed = 3000.0
	d.all_bullet_speed_data = [s]
	d.max_life_time = 60.0
	d.texture_size = Vector2(16, 16)
	d.monitorable = true
	d.set_collision_layer_from_array([2])
	d.set_collision_mask_from_array([4])
	var c := CircleShape2D.new()
	c.radius = 6.0
	d.collision_shape = c
	d.bullet_max_collision_count = 0
	return d

func _make_wall(parent: Node, pos: Vector2) -> StaticBody2D:
	var wall := StaticBody2D.new()
	wall.position = pos
	wall.collision_layer = 8
	wall.collision_mask = 2
	var col := CollisionShape2D.new()
	var box := RectangleShape2D.new()
	box.size = Vector2(20, 400)
	col.shape = box
	wall.add_child(col)
	parent.add_child(wall)
	return wall

func _initialize() -> void:
	var factory := BulletFactory2D.new()
	get_root().add_child(factory)
	await process_frame
	await process_frame
	var wall := _make_wall(get_root(), Vector2(200, 0))
	await physics_frame

	# First life: hit the wall once, arm a long timer, edit ballistics.
	printerr("RESET T1 first life hits, then parks pooled")
	var a: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(_data())
	for i in 40:
		await physics_frame
		if a.get_bullet_collision_count(0) >= 1:
			break
	_check(a.get_bullet_collision_count(0) >= 1, "T1 first life registered the hit")
	a.multimesh_attach_time_based_function(0.5, _on_old_timer, false, true)
	await process_frame
	await physics_frame
	_check(a.debug_get_timer_count() >= 1, "T1 old-life timer attached")
	var spd := BulletSpeedData2D.new()
	spd.speed = 111.0
	spd.max_speed = 222.0
	a.set_bullet_speed_data(0, spd)
	for i in a.get_amount_bullets():
		a.disable_bullet(i)
	await process_frame
	_check(factory.debug_get_bullets_pool_amount(0) >= 1, "T1 emptied volley parked pooled")

	# ---------------------------------------------------------------
	printerr("RESET T2 second life reuses the pool and resets counts")
	factory.debug_reset_pool_stats()
	var b: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(_data())
	await process_frame
	var stats: Dictionary = factory.debug_get_pool_hit_stats()
	_check(int(stats.get("directional_hits", 0)) >= 1, "T2 second life is a pool hit (hits=%s)" % str(stats.get("directional_hits", 0)))
	_check(b.get_bullet_collision_count(0) == 0, "T2 collision count reset (got %d)" % b.get_bullet_collision_count(0))

	# ---------------------------------------------------------------
	printerr("RESET T3 dedup window is fresh: the same wall hits again")
	var rehit := false
	for i in 40:
		await physics_frame
		if b.get_bullet_collision_count(0) >= 1:
			rehit = true
			break
	_check(rehit, "T3 same wall registers again in the new life")

	# ---------------------------------------------------------------
	printerr("RESET T4 old-life timers never fire in the new life")
	_timer_fired = false
	_check(b.debug_get_timer_count() == 0, "T4 new life holds no timers (got %d)" % b.debug_get_timer_count())
	# The old 0.5s timer would have fired long ago had the disable leaked it
	# into the new life: wait ~1.5s of physics and require silence.
	for i in 90:
		await physics_frame
		if _timer_fired:
			break
	_check(not _timer_fired, "T4 old 0.5s timer stayed dead after pooling")

	# ---------------------------------------------------------------
	printerr("RESET T5 no attachments leak across lives")
	_check(b.get_amount_active_attachments() == 0, "T5 new life has no live attachments")

	# ---------------------------------------------------------------
	printerr("RESET T6 reseeded ballistics win over old per-bullet edits")
	_check(absf(b.get_bullet_speed_data(0).speed - 900.0) < 0.01, "T6 speed reseeded to 900 (got %.1f)" % b.get_bullet_speed_data(0).speed)

	wall.queue_free()
	factory.free_active_bullets()
	await process_frame

	_check(factory.debug_assert_no_dangling().get("ok", false) == true, "no dangling at end")

	factory.reset()
	factory.queue_free()
	await process_frame
	print("----")
	if failures == 0:
		print("ALL POOL-RESET TESTS PASSED")
	else:
		printerr(str(failures) + " TEST(S) FAILED")
	quit(failures)
