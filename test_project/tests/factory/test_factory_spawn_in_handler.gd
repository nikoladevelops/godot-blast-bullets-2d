extends BlastTest
## Spawning from a collision handler (split-on-hit, debris) works no matter
## what the pool holds. The killing blow pools the dying volley BEFORE the
## signal fires; a same-key spawn from the handler must never get that
## draining volley back, and other pooled instances stay reusable mid-sweep.

var _spawned: Array = []
var _wall: StaticBody2D


func before_each() -> void:
	await super()
	_spawned.clear()
	_wall = StaticBody2D.new()
	_wall.position = Vector2(200, 0)
	_wall.collision_layer = 4
	_wall.collision_mask = 2
	var col := CollisionShape2D.new()
	var box := RectangleShape2D.new()
	box.size = Vector2(20, 400)
	col.shape = box
	_wall.add_child(col)
	add(_wall)
	await physics()
	await idle()
	factory.directional_body_entered.connect(_on_body)


func _bodied_data() -> DirectionalBulletsData2D:
	var d := H.make_directional_data(1, 300.0, 10.0)
	d.monitorable = true
	d.set_collision_mask_from_array([3])
	d.collision_shape = H.make_circle_shape(6.0)
	return d


func _debris_data() -> DirectionalBulletsData2D:
	var d := _bodied_data()
	d.transforms = [Transform2D(0.0, Vector2(-400, 0))]
	var sp := BulletSpeedData2D.new()
	sp.speed = 0.0
	d.all_bullet_speed_data = [sp]
	return d


func _on_body(_body: Object, _volley: DirectionalBullets2D, _idx: int) -> void:
	_spawned.append(factory.spawn_controllable_directional_bullets(_debris_data()))


func _wait_for_handler() -> void:
	for i in 90:
		await physics()
		if not _spawned.is_empty():
			return


func test_same_key_spawn_from_killing_blow() -> void:
	var v: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(_bodied_data())
	await _wait_for_handler()
	assert_eq(_spawned.size(), 1, "handler ran once")
	var s: DirectionalBullets2D = _spawned[0] if _spawned.size() > 0 else null
	assert_not_null(s, "spawn inside the handler returned a volley")
	assert_ne(s, v, "handler never gets the dying volley back")
	if s != null:
		assert_true(s.debug_get_volley_info().get("is_active", false), "spawned volley is live")


func test_prepopulated_pool_reused_from_handler() -> void:
	factory.populate_bullets_pool(MultiMeshPoolKey2D.make(1, 3), _bodied_data(), 3)
	factory.debug_reset_pool_stats()
	var v: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(_bodied_data())
	await _wait_for_handler()
	assert_eq(_spawned.size(), 1)
	assert_not_null(_spawned[0] if _spawned.size() > 0 else null, "pool-hit spawn inside the handler succeeded")
	assert_ne(_spawned[0] if _spawned.size() > 0 else null, v, "pooled reuse is not the draining volley")
	assert_gte(int(factory.debug_get_pool_hit_stats().get("directional_hits", 0)), 2, "handler spawn came from the pool")
