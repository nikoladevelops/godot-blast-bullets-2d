extends SceneTree
## Volley lifetime suite: finite / infinite / zero lifetimes, curve-clock sync,
## expiry pooling + wake top-up, life_time_over deferred signal, max collision
## count interplay (0 = infinite). Uses real physics frames so timers tick.
## Run: godot --headless --path test_project --script tests/volley/test_volley_lifetime.gd
## Exit code 0 = all pass.

const H := preload("res://tests/common/blast_test_helpers.gd")

var failures := 0
var _lifetime_hits: Array = []

func _check(cond: bool, label: String) -> void:
	if cond:
		print("PASS  ", label)
	else:
		failures += 1
		printerr("FAIL  ", label)

func _on_lifetime(_volley, indexes: Array) -> void:
	_lifetime_hits.append(indexes.duplicate())

func _initialize() -> void:
	var factory := BulletFactory2D.new()
	get_root().add_child(factory)
	await process_frame
	await process_frame

	printerr("LIFE T1 short lifetime expires and pools")
	var quick := H.make_directional_data(2, 300.0, 0.15)
	quick.is_life_time_over_signal_enabled = true
	var q: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(quick)
	_check(q != null, "short-life spawn ok")
	if q != null:
		factory.directional_life_time_over.connect(_on_lifetime)
		for i in 30:
			await physics_frame
			if not q.is_bullet_status_enabled(0) and not q.is_bullet_status_enabled(1):
				break
		_check(not q.is_bullet_status_enabled(0), "bullet 0 expired")
		_check(_lifetime_hits.size() >= 1, "life_time_over emitted deferred")
		_check(factory.debug_get_bullets_pool_amount(0) >= 1, "expired volley pooled")
		factory.directional_life_time_over.disconnect(_on_lifetime)
		_lifetime_hits.clear()

	printerr("LIFE T2 infinite lifetime never expires")
	var inf := H.make_directional_data(2, 300.0, 5.0)
	inf.is_life_time_infinite = true
	var w: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(inf)
	for i in 10:
		await physics_frame
	_check(w.is_bullet_status_enabled(0), "infinite volley still alive")
	_check(w.get_curves_elapsed_time() > 0.0, "curve clock advances while infinite")

	printerr("LIFE T3 invalid lifetimes fail safe at the resource setter")
	# The data resource itself rejects non-positive finite lifetimes (keeps the
	# previous value), so an invalid lifetime can never reach the spawn gate
	# through setters — defense in depth, asserted here end to end.
	var zero := H.make_directional_data(2, 100.0, 5.0)
	zero.max_life_time = 0.0
	_check(zero.max_life_time > 0.0, "zero lifetime rejected at setter, old kept")
	var total_before: int = factory.debug_get_total_bullets_amount(0)
	factory.spawn_directional_bullets(zero)
	await process_frame
	_check(factory.debug_get_total_bullets_amount(0) == total_before + 1, "spawn uses kept valid lifetime")
	zero.max_life_time = NAN
	_check(zero.max_life_time > 0.0, "NaN lifetime rejected at setter, old kept")

	printerr("LIFE T4 max collisions: 0 = infinite")
	var tank := H.make_directional_data(1, 0.0, 30.0)
	var t: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(tank)
	t.set_bullet_max_collision_count(0)
	t.set_bullet_collision_count(0, 0)
	_check(t.get_bullet_max_collision_count() == 0, "max 0 stored (infinite)")
	t.set_bullet_collision_count(0, 5)
	_check(t.get_bullet_collision_count(0) == 5, "count tracked even when infinite")
	t.set_bullet_max_collision_count(-1)
	_check(t.get_bullet_max_collision_count() == 0, "negative max rejected")

	printerr("LIFE T5 curves clock rejects NaN")
	var c: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(H.make_directional_data(1, 100.0, 5.0))
	var t_before: float = c.get_curves_elapsed_time()
	c.set_curves_elapsed_time(NAN)
	_check(c.get_curves_elapsed_time() == t_before, "NaN curve time rejected")

	_check(factory.debug_assert_no_dangling().get("ok", false) == true, "no dangling at end")
	factory.reset()
	factory.queue_free()
	await process_frame
	print("----")
	if failures == 0:
		print("ALL LIFETIME TESTS PASSED")
	else:
		printerr(str(failures) + " TEST(S) FAILED")
	quit(failures)
