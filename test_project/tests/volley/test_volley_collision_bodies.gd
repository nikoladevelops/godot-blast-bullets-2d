extends BlastTest
## Bullets against every physics body kind (Static/Character/Rigid/
## Animatable) count the hit and emit body_entered; Area2D routes to
## area_entered only; spawner-owned volleys emit on the spawner, never the
## factory.


func _data() -> BulletVolleyData2D:
	var d := H.make_volley_data(1, 900.0, 30.0)
	d.transforms = [Transform2D()]
	d.monitorable = true
	d.collision_shape = H.make_circle_shape(6.0)
	d.bullet_max_collision_count = 0
	return d


func _target(obj: CollisionObject2D) -> CollisionObject2D:
	obj.position = Vector2(250, 0)
	obj.collision_layer = 8
	obj.collision_mask = 2
	var cs := CollisionShape2D.new()
	cs.shape = H.make_circle_shape(12.0)
	obj.add_child(cs)
	add(obj)
	return obj


func _fire_and_wait() -> int:
	await physics(2)
	var v: BulletVolley2D = factory.spawn_volley(_data())
	var count := 0
	for i in 40:
		await physics()
		count = v.get_bullet_collision_count(0)
		if count >= 1:
			break
	return count


func _body_case(obj: CollisionObject2D, label: String) -> void:
	watch_signals(factory)
	_target(obj)
	assert_eq(await _fire_and_wait(), 1, "%s registers exactly one hit" % label)
	assert_signal_emit_count(factory, "body_entered", 1, "%s: factory body_entered fired once" % label)


func test_static_body() -> void:
	await _body_case(StaticBody2D.new(), "StaticBody2D")


func test_character_body() -> void:
	await _body_case(CharacterBody2D.new(), "CharacterBody2D")


func test_rigid_body() -> void:
	var rg := RigidBody2D.new()
	rg.gravity_scale = 0.0
	rg.linear_damp = 0.0
	rg.linear_damp_mode = RigidBody2D.DAMP_MODE_REPLACE
	rg.can_sleep = false
	await _body_case(rg, "RigidBody2D")


func test_animatable_body() -> void:
	await _body_case(AnimatableBody2D.new(), "AnimatableBody2D")


func test_area_routes_to_area_entered() -> void:
	watch_signals(factory)
	var ar := Area2D.new()
	ar.monitoring = true
	ar.monitorable = true
	_target(ar)
	assert_eq(await _fire_and_wait(), 1, "area registers the hit")
	assert_signal_emitted(factory, "area_entered", "factory area_entered fired")
	assert_signal_not_emitted(factory, "body_entered", "no body signal for an area overlap")


func test_spawner_volley_routes_to_spawner_only() -> void:
	var spawner := make_spawner(_data(), 1)
	watch_signals(factory)
	watch_signals(spawner)
	_target(StaticBody2D.new())
	await physics()
	assert_true(spawner.shoot_once(), "spawner shot fires")
	for i in 40:
		await physics()
		if get_signal_emit_count(spawner, "body_entered") >= 1:
			break
	assert_signal_emitted(spawner, "body_entered", "spawner body_entered fired")
	assert_signal_not_emitted(factory, "body_entered", "factory silent for a spawner volley")

