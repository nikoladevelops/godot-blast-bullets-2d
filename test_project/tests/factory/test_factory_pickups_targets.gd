extends BlastTest
## Who collects a pickup and how: whatever the "player" is (Node2D, Area2D,
## CharacterBody2D, RigidBody2D, StaticBody2D, Sprite2D, Marker2D...), by
## distance or by touching it (collect_on_collision), the signal and the
## callback with the same arguments, moving and vanishing targets, and the
## option to follow the source spawner's time scale.

const P := preload("res://tests/common/pickup_test_helpers.gd")

var collected: Array = []
var order: Array = []


func _on_collected(target: Variant, position: Vector2, volley: BulletVolley2D, index: int, value: Variant) -> void:
	collected.append({ "target": target, "position": position, "volley": volley, "index": index, "value": value })
	order.append("signal")


func before_each() -> void:
	await super()
	collected.clear()
	order.clear()
	factory.pickup_collected.connect(_on_collected)


func _wait(n: int, frames := 400) -> int:
	var f := 0
	while collected.size() < n and f < frames:
		await physics(1)
		f += 1
	return f


## A player node of the given class at `pos`, on physics `layer`, with a
## 16 px circle shape where the class has a body.
func _player(kind: String, pos: Vector2, layer := 4) -> Node2D:
	var node: Node2D
	match kind:
		"Node2D": node = Node2D.new()
		"Marker2D": node = Marker2D.new()
		"Sprite2D": node = Sprite2D.new()
		"Area2D": node = Area2D.new()
		"CharacterBody2D": node = CharacterBody2D.new()
		"RigidBody2D":
			var rb := RigidBody2D.new()
			rb.gravity_scale = 0.0
			rb.freeze = true
			node = rb
		"StaticBody2D": node = StaticBody2D.new()
		_: node = Node2D.new()
	node.position = pos
	if node is CollisionObject2D:
		(node as CollisionObject2D).collision_layer = layer
		(node as CollisionObject2D).collision_mask = 0
		var shape := CollisionShape2D.new()
		var circle := CircleShape2D.new()
		circle.radius = 16.0
		shape.shape = circle
		node.add_child(shape)
	add(node)
	return node


func _coin_for_touch() -> BulletPickupData2D:
	var coin := P.coin()
	coin.collect_on_collision = true
	coin.collision_mask = 4
	coin.collision_layer = 0
	coin.collision_radius = 10.0
	coin.collect_radius = 1.0 # distance collection effectively off: only touch counts
	return coin


# --- whatever the player is ---------------------------------------------------------------------------------

func test_every_kind_of_node2d_can_be_the_target() -> void:
	for kind in ["Node2D", "Marker2D", "Sprite2D", "Area2D", "CharacterBody2D", "RigidBody2D", "StaticBody2D"]:
		collected.clear()
		P.still(factory, [Vector2.ZERO])
		var player := _player(kind, Vector2(300, 0))
		factory.convert_all_bullets_to_pickups(P.coin(kind), player)
		await _wait(1)
		assert_eq(collected.size(), 1, "%s: collected" % kind)
		if not collected.is_empty():
			assert_same(collected[0]["target"], player, "%s: the signal names the node itself" % kind)
			assert_eq(collected[0]["value"], kind, "%s: with its value" % kind)
			assert_true(collected[0]["target"].get_class() == kind, "%s: the Variant keeps the real class" % kind)


func test_touching_collects_for_every_physics_player() -> void:
	for kind in ["Area2D", "CharacterBody2D", "RigidBody2D", "StaticBody2D"]:
		collected.clear()
		P.still(factory, [Vector2.ZERO])
		var player := _player(kind, Vector2(300, 0))
		# The pickup is pulled to a point beyond the player: it only stops at the touch.
		factory.convert_all_bullets_to_pickups(_coin_for_touch(), Vector2(600, 0))
		await _wait(1)
		assert_eq(collected.size(), 1, "%s: collected by touch" % kind)
		if not collected.is_empty():
			assert_same(collected[0]["target"], player, "%s: the collector is the node touched" % kind)
			assert_lt(collected[0]["position"].distance_to(player.position), 40.0, "%s: collected at the player" % kind)
		player.free()
		await idle(1)


func test_without_the_flag_touching_only_reports_the_normal_collision_signal() -> void:
	var player := _player("Area2D", Vector2(300, 0))
	P.still(factory, [Vector2.ZERO])
	var coin := _coin_for_touch()
	coin.collect_on_collision = false
	watch_signals(factory)
	factory.convert_all_bullets_to_pickups(coin, Vector2(1000, 0))
	for i in 200:
		await physics(1)
		if get_signal_emit_count(factory, "area_entered") > 0:
			break
	assert_signal_emitted(factory, "area_entered", "the factory's area_entered reports the touch")
	assert_eq(collected.size(), 0, "but a pickup that does not collect by touch is not collected")
	assert_not_null(player)


func test_touch_and_distance_together_collect_exactly_once() -> void:
	var player := _player("Area2D", Vector2(200, 0))
	P.still(factory, [Vector2(150, 0)])
	var coin := P.coin()
	coin.collect_on_collision = true
	coin.collision_mask = 4
	coin.collision_radius = 20.0
	coin.collect_radius = 60.0
	factory.convert_all_bullets_to_pickups(coin, player)
	await _wait(1)
	for i in 30:
		await physics(1)
	assert_eq(collected.size(), 1, "one collection even though both rules apply")
	assert_eq(factory.get_active_bullet_count(), 0, "and the pickup is gone")


func test_touching_something_else_on_the_mask_collects_for_that_node() -> void:
	var wall := _player("StaticBody2D", Vector2(200, 0))
	P.still(factory, [Vector2.ZERO])
	factory.convert_all_bullets_to_pickups(_coin_for_touch(), Vector2(1000, 0))
	await _wait(1)
	assert_eq(collected.size(), 1, "any collider on the mask collects")
	assert_same(collected[0]["target"], wall, "and is reported as the collector")


# --- the callback ------------------------------------------------------------------------------------------------------

func test_the_callback_gets_the_signals_arguments_after_the_signal() -> void:
	var calls: Array = []
	var coin := P.coin("gem")
	coin.collect_callback = func(target, position, volley, index, value):
		order.append("callback")
		calls.append([target, position, volley, index, value])
	var player := _player("CharacterBody2D", Vector2(250, 0))
	P.still(factory, [Vector2.ZERO, Vector2(0, 10)])
	factory.convert_all_bullets_to_pickups(coin, player)
	await _wait(2)
	await idle(2)
	assert_eq(calls.size(), 2, "once per pickup")
	assert_eq(order.slice(0, 2), ["signal", "callback"], "the signal first, then the callback")
	for i in 2:
		assert_same(calls[i][0], collected[i]["target"], "same target")
		assert_eq(calls[i][1], collected[i]["position"], "same position")
		assert_same(calls[i][2], collected[i]["volley"], "same volley")
		assert_eq(calls[i][3], collected[i]["index"], "same index")
		assert_eq(calls[i][4], "gem", "same value")


func test_the_callback_works_with_a_method_callable_and_a_dead_one_is_ignored() -> void:
	var coin := P.coin()
	coin.collect_callback = _on_collect_method
	P.still(factory, [Vector2.ZERO])
	factory.convert_all_bullets_to_pickups(coin, Vector2(200, 0))
	await _wait(1)
	assert_eq(method_calls, 1, "a method callable")
	method_calls = 0
	var temp := Node.new()
	coin.collect_callback = Callable(temp, "queue_free")
	temp.free() # the callable's object is gone: invalid, skipped
	P.still(factory, [Vector2.ZERO])
	collected.clear()
	factory.convert_all_bullets_to_pickups(coin, Vector2(200, 0))
	await _wait(1)
	assert_eq(collected.size(), 1, "an invalid callable does not break the collection")
	expect_no_errors("and logs nothing")


var method_calls := 0


func _on_collect_method(_target, _position, _volley, _index, _value) -> void:
	method_calls += 1


func test_the_callback_is_runtime_state_and_never_saved() -> void:
	var coin := P.coin()
	coin.collect_callback = func(): pass
	var path := "user://blast_pickup_callback.tres"
	assert_eq(ResourceSaver.save(coin, path), OK, "saves")
	var loaded: BulletPickupData2D = ResourceLoader.load(path, "", ResourceLoader.CACHE_MODE_IGNORE)
	assert_false(loaded.collect_callback.is_valid(), "the callable did not travel")
	DirAccess.remove_absolute(ProjectSettings.globalize_path(path))


func test_a_callback_may_free_the_target_convert_again_or_disable_the_bullet() -> void:
	var player := _player("Node2D", Vector2(250, 0))
	var coin := P.coin()
	var again := []
	coin.collect_callback = func(target, _p, volley, index, _v):
		if is_instance_valid(target):
			target.free()
		volley.disable_bullet(index) # the bullet is alive in the callback; we take it out ourselves
		P.still(factory, [Vector2(0, 40)])
		again.append(factory.convert_all_bullets_to_pickups(P.coin(), Vector2(0, 500)))
	P.still(factory, [Vector2.ZERO])
	factory.convert_all_bullets_to_pickups(coin, player)
	await _wait(1)
	await idle(3)
	assert_eq(collected.size(), 1, "collected once")
	assert_eq(again.size(), 1, "the callback converted a second wave")
	assert_true(factory.debug_assert_no_dangling().get("ok", false), "no dangling state")


# --- moving and vanishing players ---------------------------------------------------------------------------------------------

func test_a_moving_player_is_followed_by_physics_and_by_distance() -> void:
	var player := _player("CharacterBody2D", Vector2(200, 0))
	P.still(factory, [Vector2.ZERO])
	var coin := P.coin()
	coin.magnet_max_speed = 900.0
	factory.convert_all_bullets_to_pickups(coin, player)
	for i in 300:
		player.position += Vector2(0, 2.5)
		await physics(1)
		if not collected.is_empty():
			break
	assert_eq(collected.size(), 1, "caught the moving CharacterBody2D")
	assert_lt(collected[0]["position"].distance_to(player.position), 30.0, "next to where it is now")


func test_a_player_freed_midflight_leaves_the_other_players_pickups_working() -> void:
	var a := _player("Node2D", Vector2(-300, 0))
	var b := _player("Node2D", Vector2(300, 0))
	a.add_to_group("players")
	b.add_to_group("players")
	P.still(factory, [Vector2(-250, 0), Vector2(250, 0)])
	factory.convert_all_bullets_to_pickups(P.coin(), "players")
	await idle(2)
	a.free()
	await _wait(1)
	await idle(30)
	assert_gte(collected.size(), 1, "the surviving player's pickup was collected")
	assert_same(collected[0]["target"], b, "by the survivor")
	assert_true(factory.debug_assert_no_dangling().get("ok", false), "clean")


func test_targets_that_are_not_node2d_or_not_usable_are_refused() -> void:
	P.still(factory, [Vector2.ZERO])
	var control := Control.new()
	add(control)
	assert_eq(factory.convert_all_bullets_to_pickups(P.coin(), control), [], "a Control is no Node2D")
	expect_error_sequence(["BulletFactory2D.convert_all_bullets_to_pickups: target must be a Node2D, a Vector2, a group name or null (the mouse), nothing converted."])
	var queued := _player("Node2D", Vector2(10, 0))
	queued.queue_free()
	assert_eq(factory.convert_all_bullets_to_pickups(P.coin(), queued), [], "a node queued for deletion")
	expect_error_sequence(["BulletFactory2D.convert_all_bullets_to_pickups: target node cannot be used (it must be a live Node2D of this factory's world and not part of a BulletFactory2D), nothing converted."])
	assert_eq(factory.convert_all_bullets_to_pickups(P.coin(), factory), [], "the factory itself")
	expect_error_sequence(["BulletFactory2D.convert_all_bullets_to_pickups: target node cannot be used (it must be a live Node2D of this factory's world and not part of a BulletFactory2D), nothing converted."])
	assert_eq(factory.get_active_bullet_count(), 1, "the bullet was never touched")


func test_a_group_mixing_node_kinds_only_uses_node2ds() -> void:
	var area := _player("Area2D", Vector2(400, 0))
	var body := _player("CharacterBody2D", Vector2(-400, 0))
	var ctrl := Control.new()
	add(ctrl)
	for n in [area, body, ctrl]:
		n.add_to_group("mixed")
	P.still(factory, [Vector2(380, 0), Vector2(-380, 0)])
	factory.convert_all_bullets_to_pickups(P.coin(), "mixed")
	await _wait(2)
	assert_eq(collected.size(), 2, "both pickups collected")
	var targets := collected.map(func(c): return c["target"])
	assert_true(area in targets and body in targets, "each went to the nearest Node2D of the group")


# --- time scale option -------------------------------------------------------------------------------------------------------------

func _spawner_with_bullets(scale: float) -> BulletSpawner2D:
	var sp := make_spawner(H.make_volley_data(1, 0.0, 60.0), BulletSpawner2D.PATTERN_FROM_HELPER_RING, 1)
	sp.helper_ring_radius = 0.0
	sp.time_scale = scale
	assert_true(sp.shoot_once(), "shot")
	return sp


func _frames_to_collect(sp: BulletSpawner2D, follow: bool) -> int:
	collected.clear()
	var coin := P.coin()
	coin.follow_source_time_scale = follow
	sp.convert_live_bullets_to_pickups(coin, Vector2(500, 0))
	return await _wait(1, 900)


func test_pickups_follow_the_source_spawners_time_scale_only_when_asked() -> void:
	var sp := _spawner_with_bullets(0.5)
	var off := await _frames_to_collect(sp, false)
	assert_eq(collected.size(), 1, "collected (option off)")
	assert_true(sp.shoot_once(), "another shot")
	var on := await _frames_to_collect(sp, true)
	assert_eq(collected.size(), 1, "collected (option on)")
	assert_gt(float(on), float(off) * 1.4, "with the option the slowed spawner slows its pickups (%d vs %d frames)" % [on, off])


func test_the_option_is_off_by_default_and_ignored_for_mixed_sources() -> void:
	assert_false(BulletPickupData2D.new().follow_source_time_scale, "off by default")
	var slow := _spawner_with_bullets(0.25)
	var fast := _spawner_with_bullets(1.0)
	collected.clear()
	var coin := P.coin()
	coin.follow_source_time_scale = true
	var base := _spawner_with_bullets(1.0)
	var out: Array = factory.convert_all_bullets_to_pickups(coin, Vector2(500, 0))
	var frames := await _wait(out[0].get_amount_bullets(), 600)
	assert_lt(frames, 120, "sources of different spawners: no single scale applies, pickups run on the factory scale")
	assert_not_null(slow)
	assert_not_null(fast)
	assert_not_null(base)


func test_a_pickup_whose_source_spawner_is_freed_runs_on_the_factory_scale() -> void:
	var sp := _spawner_with_bullets(0.1)
	var coin := P.coin()
	coin.follow_source_time_scale = true
	collected.clear()
	sp.convert_live_bullets_to_pickups(coin, Vector2(500, 0))
	sp.get_parent().remove_child(sp)
	sp.free()
	var frames := await _wait(1, 900)
	assert_eq(collected.size(), 1, "collected")
	assert_lt(frames, 120, "no spawner left: no slowdown (%d frames)" % frames)


func test_the_spawner_opt_out_is_honored_by_following_pickups() -> void:
	var sp := _spawner_with_bullets(0.1)
	sp.time_scale_affects_volleys = false
	var coin := P.coin()
	coin.follow_source_time_scale = true
	collected.clear()
	sp.convert_live_bullets_to_pickups(coin, Vector2(500, 0))
	var frames := await _wait(1, 900)
	assert_lt(frames, 120, "time_scale_affects_volleys off keeps the pickups at the factory scale (%d frames)" % frames)
