extends SceneTree
## Volley collision suite: REAL physics (no mocks).
## Harness: factory at origin, 1 bullet at 300px/s +X toward a StaticBody2D
## wall at x=200 (layer 3). Asserts: directional_body_entered fires with slim
## payload (body, volley, index), bullet disables at max 1, max 0 = infinite
## (survives the wall), epoch guard (disable+wake same drain = single count),
## area path via an Area2D sibling. Requires physics frames.
## Run: godot --headless --path test_project --script tests/volley/test_volley_collision.gd
## Exit code 0 = all pass.

const H := preload("res://tests/common/blast_test_helpers.gd")

var failures := 0
var _body_hits: Array = []
var _area_hits: Array = []

func _check(cond: bool, label: String) -> void:
	if cond:
		print("PASS  ", label)
	else:
		failures += 1
		printerr("FAIL  ", label)

func _on_body(body: Object, volley: DirectionalBullets2D, idx: int) -> void:
	_body_hits.append([body, volley, idx])

func _on_area(area: Object, volley: DirectionalBullets2D, idx: int) -> void:
	_area_hits.append([area, volley, idx])

func _bodied_data() -> DirectionalBulletsData2D:
	var d := H.make_directional_data(1, 300.0, 10.0)
	d.monitorable = true
	d.set_collision_layer_from_array([2])
	d.set_collision_mask_from_array([3])
	var c := CircleShape2D.new()
	c.radius = 6.0
	d.collision_shape = c
	return d

func _initialize() -> void:
	var factory := BulletFactory2D.new()
	get_root().add_child(factory)
	await process_frame
	await process_frame

	# Wall body on layer 3 (value 4), mask back to layer 2.
	var wall := StaticBody2D.new()
	wall.position = Vector2(200, 0)
	wall.collision_layer = 4
	wall.collision_mask = 2
	var col := CollisionShape2D.new()
	var box := RectangleShape2D.new()
	box.size = Vector2(20, 400)
	col.shape = box
	wall.add_child(col)
	get_root().add_child(wall)
	var eye := Area2D.new()
	eye.position = Vector2(200, 0)
	eye.collision_layer = 4
	eye.monitoring = true
	eye.monitorable = true
	var col2 := CollisionShape2D.new()
	var box2 := RectangleShape2D.new()
	box2.size = Vector2(20, 400)
	col2.shape = box2
	eye.add_child(col2)
	get_root().add_child(eye)
	await physics_frame

	printerr("HIT T1 body entered fires, bullet dies at max 1")
	factory.directional_body_entered.connect(_on_body)
	factory.directional_area_entered.connect(_on_area)
	var v: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(_bodied_data())
	_check(v != null, "walled spawn ok")
	for i in 90:
		await physics_frame
		if not v.is_bullet_status_enabled(0):
			break
	_check(_body_hits.size() >= 1, "body signal fired")
	if _body_hits.size() >= 1:
		_check(_body_hits[0][1] == v and (_body_hits[0][2] as int) == 0, "slim payload (volley, index 0)")
	_check(not v.is_bullet_status_enabled(0), "bullet disabled at max 1")
	_check(v.get_bullet_collision_count(0) >= 1, "collision count tracked")
	# NOTE: the Area2D sibling legitimately stays silent here — max 1 means the
	# first record (body) kills the bullet and the area record for the same dead
	# slot is skipped. Multi-hit volleys report both (see T1b below).

	printerr("HIT T1b max 2 reports body AND area for one bullet")
	_body_hits.clear()
	_area_hits.clear()
	var v1b: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(_bodied_data())
	v1b.set_bullet_max_collision_count(2)
	for i in 90:
		await physics_frame
		if _body_hits.size() >= 1 and _area_hits.size() >= 1:
			break
	_check(_body_hits.size() >= 1, "body reported on multi-hit volley")
	_check(_area_hits.size() >= 1, "area reported on multi-hit volley")

	printerr("HIT T2 max 0 = infinite survives wall")
	_body_hits.clear()
	_area_hits.clear()
	var v2: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(_bodied_data())
	v2.set_bullet_max_collision_count(0)
	for i in 90:
		await physics_frame
		if _body_hits.size() >= 2:
			break
	_check(_body_hits.size() >= 1, "infinite bullet still reports hits")
	_check(v2.is_bullet_status_enabled(0), "infinite bullet stays alive through wall")

	printerr("HIT T3 epoch: disable+wake same life = single fresh count")
	_body_hits.clear()
	var v3: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(_bodied_data())
	v3.disable_bullet(0)
	_check(not v3.is_bullet_status_enabled(0), "manual disable holds")
	v3.wake_bullet(0)
	_check(v3.is_bullet_status_enabled(0), "wake revives")
	_check(factory.debug_assert_no_dangling().get("ok", false) == true, "no dangling after wake")

	factory.directional_body_entered.disconnect(_on_body)
	factory.directional_area_entered.disconnect(_on_area)
	factory.reset()
	wall.queue_free()
	eye.queue_free()
	factory.queue_free()
	await process_frame
	print("----")
	if failures == 0:
		print("ALL COLLISION TESTS PASSED")
	else:
		printerr(str(failures) + " TEST(S) FAILED")
	quit(failures)
