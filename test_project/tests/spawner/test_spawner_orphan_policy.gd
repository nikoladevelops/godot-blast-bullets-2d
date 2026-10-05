extends BlastTest
## What happens to a spawner's bullets when the spawner is FREED
## (orphaned_volleys), and how the plugin explains hits nobody can handle.
## Signal connections live on the emitter: once a spawner is freed, every
## connection on it is gone - even one whose callback lives on a node that is
## still alive - so its in-flight bullets' hits reach no one. The policy picks
## the outcome: Keep Flying (default, one explanatory warning), Hand To
## Factory (BulletFactory2D signals fire), Clear (On Clear effects) or Remove
## (silent). Reparenting never triggers it. A live emitter with no hit handler
## connected warns once too; emit_collision_signals = false silences both.

const ORPHAN_HIT := "hit something after that spawner was freed"
const ORPHAN_EXPIRY := "expired after that spawner was freed"
const NO_HANDLER := "nothing is connected to its area_entered or body_entered signal"

var spawner_hits: Array = []
var factory_hits: Array = []


func _data(lifetime := 8.0, max_hits := 1) -> BulletVolleyData2D:
	var d := H.make_volley_data(1, 300.0, lifetime)
	d.monitorable = true
	d.set_collision_mask_from_array([3])
	d.collision_shape = H.make_circle_shape(6.0)
	d.bullet_max_collision_count = max_hits
	d.effect_layers = [H.make_effect_layer(BulletEffectLayerData2D.EFFECT_ON_CLEAR, 4)]
	return d


## A spawner that is NOT autofreed (the tests free it themselves).
func _spawner(policy: int, data: BulletVolleyData2D = null) -> BulletSpawner2D:
	var sp := BulletSpawner2D.new()
	sp.set_shooting_enabled(false)
	sp.set_homing_enabled(false)
	sp.set_spawn_data(data if data != null else _data())
	sp.pattern_source = BulletSpawner2D.PATTERN_FROM_SELF
	sp.helper_bullets_amount = 1
	sp.orphaned_volleys = policy
	add_child(sp)
	sp.set_bullet_factory(factory)
	sp.body_entered.connect(func(_b, v, i): spawner_hits.append([v, i]))
	return sp


func _shoot(sp: BulletSpawner2D) -> BulletVolley2D:
	var got: Array = []
	sp.volley_fired.connect(func(v, _n): got.append(v), CONNECT_ONE_SHOT)
	assert_true(sp.shoot_once(), "fired")
	return got[0]


func before_each() -> void:
	await super()
	spawner_hits.clear()
	factory_hits.clear()
	factory.body_entered.connect(func(_b, v, i): factory_hits.append([v, i]))
	factory.debug_set_effect_log_enabled(true)


func after_each() -> void:
	factory.debug_set_effect_log_enabled(false)
	await super()


func _clears() -> int:
	var n := 0
	for e in factory.debug_get_effect_log():
		if int(e["trigger"]) == BulletEffectLayerData2D.EFFECT_ON_CLEAR:
			n += 1
	return n


func _wait_dead(v: BulletVolley2D, frames := 90) -> void:
	for i in frames:
		await physics()
		if not v.is_bullet_status_enabled(0):
			break


func test_keep_flying_bullets_still_die_and_one_warning_explains_the_lost_hits() -> void:
	var sp := _spawner(BulletSpawner2D.ORPHANED_VOLLEYS_KEEP_FLYING)
	var v := _shoot(sp)
	var second := _shoot(sp)
	second.teleport_bullet(0, Vector2(0, 100))
	sp.free()
	make_wall(Vector2(200, 0))
	await _wait_dead(v)
	await _wait_dead(second)
	assert_false(v.is_bullet_status_enabled(0), "the orphaned bullet still dies on the wall")
	assert_eq(spawner_hits, [], "no handler can run: the connections died with the spawner")
	assert_eq(factory_hits, [], "the factory is not involved under Keep Flying")
	expect_errors_containing(ORPHAN_HIT, 1, "one warning for every orphaned hit of that spawner")
	assert_eq(_clears(), 0, "no clear effects")


func test_keep_flying_expiry_warns_once() -> void:
	var d := _data(0.2)
	d.is_life_time_over_signal_enabled = true
	var sp := _spawner(BulletSpawner2D.ORPHANED_VOLLEYS_KEEP_FLYING, d)
	var v := _shoot(sp)
	sp.free()
	await _wait_dead(v)
	assert_false(v.is_bullet_status_enabled(0), "expired")
	expect_errors_containing(ORPHAN_EXPIRY, 1, "the lost expiry is explained once")


func test_hand_to_factory_routes_the_orphans_to_the_factory() -> void:
	var sp := _spawner(BulletSpawner2D.ORPHANED_VOLLEYS_HAND_TO_FACTORY)
	var v := _shoot(sp)
	sp.free()
	assert_eq(int(v.debug_get_volley_info().get("owner_spawner_id", -1)), 0, "factory-owned now")
	make_wall(Vector2(200, 0))
	await _wait_dead(v)
	assert_eq(factory_hits.size(), 1, "BulletFactory2D.body_entered handles the orphan's hit")
	assert_same(factory_hits[0][0], v, "with its volley")
	expect_errors_containing(ORPHAN_HIT, 0, "nothing to warn about")


func test_clear_removes_every_live_bullet_with_on_clear() -> void:
	var sp := _spawner(BulletSpawner2D.ORPHANED_VOLLEYS_CLEAR)
	var v1 := _shoot(sp)
	var v2 := _shoot(sp)
	var other := _spawner(BulletSpawner2D.ORPHANED_VOLLEYS_CLEAR)
	var survivor := _shoot(other)
	var direct: BulletVolley2D = factory.spawn_volley(_data())
	factory.debug_clear_effect_log()
	sp.queue_free()
	await idle(2)
	assert_false(v1.is_bullet_status_enabled(0) or v2.is_bullet_status_enabled(0), "the freed spawner's bullets are gone")
	assert_true(v1.is_pooled() and v2.is_pooled(), "their volleys went back to the pool")
	assert_eq(_clears(), 2, "On Clear once per cleared bullet")
	assert_true(survivor.is_bullet_status_enabled(0), "another spawner's bullets are untouched")
	assert_true(direct.is_bullet_status_enabled(0), "factory-owned bullets are untouched")
	other.free()


func test_remove_is_silent() -> void:
	var sp := _spawner(BulletSpawner2D.ORPHANED_VOLLEYS_REMOVE)
	var v := _shoot(sp)
	factory.debug_clear_effect_log()
	sp.free()
	assert_false(v.is_bullet_status_enabled(0), "removed at once")
	assert_true(v.is_pooled(), "pooled")
	assert_eq(_clears(), 0, "no effects")


func test_reparenting_never_applies_the_policy() -> void:
	var sp := _spawner(BulletSpawner2D.ORPHANED_VOLLEYS_REMOVE)
	var v := _shoot(sp)
	var holder := Node2D.new()
	add(holder)
	sp.get_parent().remove_child(sp)
	holder.add_child(sp)
	await idle(1)
	assert_true(v.is_bullet_status_enabled(0), "a reparent is not a free")
	assert_eq(int(v.debug_get_volley_info().get("owner_spawner_id", 0)), sp.get_instance_id(), "still owned")
	sp.free()
	assert_false(v.is_bullet_status_enabled(0), "the real free applies the policy")


func test_freeing_the_spawner_from_a_hit_handler_mid_tick_is_safe() -> void:
	var sp := _spawner(BulletSpawner2D.ORPHANED_VOLLEYS_CLEAR)
	var owned := _shoot(sp)
	owned.teleport_bullet(0, Vector2(0, 300)) # far from the wall
	var trigger: BulletVolley2D = factory.spawn_volley(_data())
	factory.body_entered.connect(func(_b, v, _i):
		if v == trigger and is_instance_valid(sp):
			sp.free())
	make_wall(Vector2(200, 0), Vector2(20, 200))
	await _wait_dead(trigger)
	await physics(2)
	assert_false(is_instance_valid(sp), "spawner freed inside the factory tick")
	assert_false(owned.is_bullet_status_enabled(0), "its bullets were cleared mid-tick")
	assert_true(factory.debug_assert_no_dangling().get("ok", false), "factory consistent")


func test_scene_teardown_in_both_orders_is_clean() -> void:
	for spawner_first in [true, false]:
		var root := Node2D.new()
		add_child(root)
		var f := BulletFactory2D.new()
		var sp := BulletSpawner2D.new()
		sp.set_shooting_enabled(false)
		sp.set_homing_enabled(false)
		sp.set_spawn_data(_data())
		sp.pattern_source = BulletSpawner2D.PATTERN_FROM_SELF
		sp.helper_bullets_amount = 1
		sp.orphaned_volleys = BulletSpawner2D.ORPHANED_VOLLEYS_CLEAR
		if spawner_first:
			root.add_child(sp)
			root.add_child(f)
		else:
			root.add_child(f)
			root.add_child(sp)
		await idle(2)
		sp.set_bullet_factory(f)
		assert_true(sp.shoot_once(), "fired")
		root.free()
		expect_no_errors("teardown (spawner first = %s) prints nothing" % spawner_first)


func test_a_live_emitter_with_no_hit_handler_warns_once() -> void:
	var sp := _spawner(BulletSpawner2D.ORPHANED_VOLLEYS_KEEP_FLYING)
	for c in sp.body_entered.get_connections():
		sp.body_entered.disconnect(c["callable"])
	var v1 := _shoot(sp)
	var v2 := _shoot(sp)
	v2.teleport_bullet(0, Vector2(0, 100))
	make_wall(Vector2(200, 0))
	await _wait_dead(v1)
	await _wait_dead(v2)
	expect_errors_containing(NO_HANDLER, 1, "warned once for that spawner")
	sp.free()


func test_a_freed_receiver_drops_its_connection_and_the_hit_is_explained() -> void:
	var sp := _spawner(BulletSpawner2D.ORPHANED_VOLLEYS_KEEP_FLYING)
	for c in sp.body_entered.get_connections():
		sp.body_entered.disconnect(c["callable"])
	# The handler belongs to a node (an "enemy") that is freed first.
	var enemy := BodyCounter.new()
	add_child(enemy)
	sp.body_entered.connect(enemy.on_hit)
	enemy.free()
	assert_false(sp.has_connections("body_entered"), "Godot dropped the freed receiver's connection")
	var v := _shoot(sp)
	make_wall(Vector2(200, 0))
	await _wait_dead(v)
	expect_errors_containing(NO_HANDLER, 1, "the lost hit is explained")
	sp.free()


class BodyCounter extends Node:
	var hits := 0

	func on_hit(_b, _v, _i) -> void:
		hits += 1


func test_emit_collision_signals_off_silences_signals_and_warnings() -> void:
	var d := _data()
	d.emit_collision_signals = false
	var sp := _spawner(BulletSpawner2D.ORPHANED_VOLLEYS_KEEP_FLYING, d)
	for c in sp.body_entered.get_connections():
		sp.body_entered.disconnect(c["callable"])
	var heard: Array = []
	sp.body_entered.connect(func(_b, _v, _i): heard.append(1))
	var v := _shoot(sp)
	assert_false(v.get_emit_collision_signals(), "seeded from the data")
	make_wall(Vector2(200, 0))
	await _wait_dead(v)
	assert_false(v.is_bullet_status_enabled(0), "the bullet still dies on contact")
	assert_eq(heard, [], "no signal")
	expect_errors_containing(NO_HANDLER, 0, "no warning")
	sp.free()


func test_spawner_clear_active_bullets_counts_and_effects() -> void:
	var sp := _spawner(BulletSpawner2D.ORPHANED_VOLLEYS_KEEP_FLYING)
	var v1 := _shoot(sp)
	var parked := _shoot(sp)
	parked.set_is_auto_pooling_enabled(false)
	parked.disable_bullet(0)
	assert_true(parked.is_parked(), "a parked volley of the spawner")
	var direct: BulletVolley2D = factory.spawn_volley(_data())
	factory.debug_clear_effect_log()
	assert_eq(sp.clear_active_bullets(), 1, "one live bullet cleared")
	assert_eq(_clears(), 1, "On Clear fired for it")
	assert_false(v1.is_bullet_status_enabled(0), "cleared")
	assert_true(parked.is_pooled(), "the spawner's parked volley went back to the pool")
	assert_true(direct.is_bullet_status_enabled(0), "factory bullets untouched")
	sp.free()


func test_orphaned_volleys_enum_is_locked() -> void:
	assert_eq(BulletSpawner2D.ORPHANED_VOLLEYS_KEEP_FLYING, 0)
	assert_eq(BulletSpawner2D.ORPHANED_VOLLEYS_HAND_TO_FACTORY, 1)
	assert_eq(BulletSpawner2D.ORPHANED_VOLLEYS_CLEAR, 2)
	assert_eq(BulletSpawner2D.ORPHANED_VOLLEYS_REMOVE, 3)
	var sp := make_spawner()
	var hint := ""
	for p in sp.get_property_list():
		if p.name == "orphaned_volleys":
			hint = p.hint_string
	assert_eq(hint, "Keep Flying,Hand To Factory,Clear,Remove", "inspector labels in id order")
	assert_eq(sp.orphaned_volleys, BulletSpawner2D.ORPHANED_VOLLEYS_KEEP_FLYING, "default keeps flying")
	sp.orphaned_volleys = 9
	expect_error_sequence(["BulletSpawner2D: orphaned_volleys out of range, keeping the old value."])
	assert_eq(sp.orphaned_volleys, 0, "old value kept")
