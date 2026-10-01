extends SceneTree
## Collision body-matrix suite: bullets against every Godot physics body
## kind, plus Area2D routing and spawner-vs-factory signal ownership.
##
## Contract under test: a bullet overlapping any physics body counts the hit
## and emits exactly one body_entered (bodies) or area_entered (areas);
## spawner-owned volleys emit on the spawner and never on the factory;
## factory-owned volleys emit on the factory. Block volleys report through
## the block_* signals.
##
## Covers: T1 StaticBody2D counts + factory body signal, T2 CharacterBody2D,
## T3 RigidBody2D, T4 AnimatableBody2D, T5 Area2D routes to area_entered
## (never body_entered), T6 spawner-owned volley routes to spawner only,
## T7 block volley routes to block_body_entered, T8 no dangling.
##
## Run: godot --headless --path test_project --script tests/volley/test_volley_collision_bodies.gd
## Exit code 0 = all pass. Any FAIL = behavior drift or a real bug.

var failures := 0
var _factory_body := 0
var _factory_area := 0
var _factory_block_body := 0
var _spawner_body := 0
var _spawner_vols: Array = []

func _check(cond: bool, label: String) -> void:
	if cond:
		print("PASS  ", label)
	else:
		failures += 1
		printerr("FAIL  ", label)

func _on_factory_body(_b: Object, _v: Object, _i: int) -> void:
	_factory_body += 1

func _on_factory_area(_b: Object, _v: Object, _i: int) -> void:
	_factory_area += 1

func _on_factory_block_body(_b: Object, _v: Object, _i: int) -> void:
	_factory_block_body += 1

func _on_spawner_body(_b: Object, _v: Object, _i: int) -> void:
	_spawner_body += 1

func _on_spawner_volley(volley: Object, _idx: int) -> void:
	if volley is DirectionalBullets2D:
		_spawner_vols.append(volley)

func _data() -> DirectionalBulletsData2D:
	var d := DirectionalBulletsData2D.new()
	d.transforms = [Transform2D(0.0, Vector2.ZERO)]
	var s := BulletSpeedData2D.new()
	s.speed = 900.0
	s.max_speed = 3000.0
	d.all_bullet_speed_data = [s]
	d.max_life_time = 30.0
	d.texture_size = Vector2(16, 16)
	d.monitorable = true
	d.set_collision_layer_from_array([2])
	d.set_collision_mask_from_array([4])
	var c := CircleShape2D.new()
	c.radius = 6.0
	d.collision_shape = c
	d.bullet_max_collision_count = 0
	return d

func _block_data() -> BlockBulletsData2D:
	var d := BlockBulletsData2D.new()
	d.transforms = [Transform2D(0.0, Vector2.ZERO)]
	var s := BulletSpeedData2D.new()
	s.speed = 900.0
	s.max_speed = 3000.0
	s.acceleration = 0.0
	d.block_speed = s
	d.block_rotation_radians = 0.0
	d.max_life_time = 30.0
	d.texture_size = Vector2(16, 16)
	d.monitorable = true
	d.set_collision_layer_from_array([2])
	d.set_collision_mask_from_array([4])
	var c := CircleShape2D.new()
	c.radius = 6.0
	d.collision_shape = c
	return d

func _shape() -> CircleShape2D:
	var c := CircleShape2D.new()
	c.radius = 12.0
	return c

func _add_shape(parent: CollisionObject2D) -> void:
	var cs := CollisionShape2D.new()
	cs.shape = _shape()
	parent.add_child(cs)

func _settle() -> void:
	await physics_frame
	await physics_frame

# Spawns one bullet at the origin flying +X and waits until it registers a
# hit on v (or the budget runs out). Returns the collision count observed.
func _fire_and_wait(factory: BulletFactory2D, target: Node) -> int:
	await _settle()
	var v: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(_data())
	var count := 0
	for i in 40:
		await physics_frame
		if not is_instance_valid(v):
			break
		count = v.get_bullet_collision_count(0)
		if count >= 1:
			break
	target.queue_free()
	factory.free_active_bullets()
	await process_frame
	return count

func _initialize() -> void:
	var factory := BulletFactory2D.new()
	get_root().add_child(factory)
	await process_frame
	await process_frame
	factory.directional_body_entered.connect(_on_factory_body)
	factory.directional_area_entered.connect(_on_factory_area)
	factory.block_body_entered.connect(_on_factory_block_body)

	# ---------------------------------------------------------------
	printerr("BODIES T1 StaticBody2D counts and signals body_entered")
	var st := StaticBody2D.new()
	st.position = Vector2(250, 0)
	st.collision_layer = 8
	st.collision_mask = 2
	_add_shape(st)
	get_root().add_child(st)
	_factory_body = 0
	_check(await _fire_and_wait(factory, st) >= 1, "T1 static body registers the hit")
	_check(_factory_body >= 1, "T1 factory directional_body_entered fired (%d)" % _factory_body)

	# ---------------------------------------------------------------
	printerr("BODIES T2 CharacterBody2D counts and signals body_entered")
	var ch := CharacterBody2D.new()
	ch.position = Vector2(250, 0)
	ch.collision_layer = 8
	ch.collision_mask = 2
	_add_shape(ch)
	get_root().add_child(ch)
	_factory_body = 0
	_check(await _fire_and_wait(factory, ch) >= 1, "T2 character body registers the hit")
	_check(_factory_body >= 1, "T2 factory directional_body_entered fired (%d)" % _factory_body)

	# ---------------------------------------------------------------
	printerr("BODIES T3 RigidBody2D counts and signals body_entered")
	var rg := RigidBody2D.new()
	rg.position = Vector2(250, 0)
	rg.collision_layer = 8
	rg.collision_mask = 2
	rg.gravity_scale = 0.0
	rg.linear_damp = 0.0
	rg.linear_damp_mode = RigidBody2D.DAMP_MODE_REPLACE
	rg.can_sleep = false
	_add_shape(rg)
	get_root().add_child(rg)
	_factory_body = 0
	_check(await _fire_and_wait(factory, rg) >= 1, "T3 rigid body registers the hit")
	_check(_factory_body >= 1, "T3 factory directional_body_entered fired (%d)" % _factory_body)

	# ---------------------------------------------------------------
	printerr("BODIES T4 AnimatableBody2D counts and signals body_entered")
	var an := AnimatableBody2D.new()
	an.position = Vector2(250, 0)
	an.collision_layer = 8
	an.collision_mask = 2
	_add_shape(an)
	get_root().add_child(an)
	_factory_body = 0
	_check(await _fire_and_wait(factory, an) >= 1, "T4 animatable body registers the hit")
	_check(_factory_body >= 1, "T4 factory directional_body_entered fired (%d)" % _factory_body)

	# ---------------------------------------------------------------
	printerr("BODIES T5 Area2D routes to area_entered, never body_entered")
	var ar := Area2D.new()
	ar.position = Vector2(250, 0)
	ar.collision_layer = 8
	ar.collision_mask = 2
	ar.monitoring = true
	ar.monitorable = true
	_add_shape(ar)
	get_root().add_child(ar)
	_factory_body = 0
	_factory_area = 0
	await _settle()
	var va: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(_data())
	var area_count := 0
	for i in 40:
		await physics_frame
		if not is_instance_valid(va):
			break
		area_count = va.get_bullet_collision_count(0)
		if area_count >= 1:
			break
	_check(area_count >= 1, "T5 area registers the hit")
	_check(_factory_area >= 1, "T5 factory directional_area_entered fired (%d)" % _factory_area)
	_check(_factory_body == 0, "T5 no body signal for an area overlap (%d)" % _factory_body)
	ar.queue_free()
	factory.free_active_bullets()
	await process_frame

	# ---------------------------------------------------------------
	printerr("BODIES T6 spawner-owned volley routes to spawner only")
	var spawner := BulletSpawner2D.new()
	get_root().add_child(spawner)
	spawner.set_bullet_factory(factory)
	spawner.set_spawn_data(_data())
	spawner.set_shooting_enabled(false)
	spawner.volley_fired.connect(_on_spawner_volley)
	spawner.body_entered.connect(_on_spawner_body)
	var wall := StaticBody2D.new()
	wall.position = Vector2(250, 0)
	wall.collision_layer = 8
	wall.collision_mask = 2
	_add_shape(wall)
	get_root().add_child(wall)
	await physics_frame
	_factory_body = 0
	_spawner_body = 0
	_spawner_vols.clear()
	_check(spawner.shoot_once(), "T6 spawner shot fires")
	for i in 40:
		await physics_frame
		if _spawner_body >= 1:
			break
	_check(_spawner_body >= 1, "T6 spawner body_entered fired")
	_check(_factory_body == 0, "T6 factory stayed silent for a spawner volley (%d)" % _factory_body)
	spawner.body_entered.disconnect(_on_spawner_body)
	wall.queue_free()
	factory.free_active_bullets()
	await process_frame

	# ---------------------------------------------------------------
	printerr("BODIES T7 block volley routes to block_body_entered")
	var wall7 := StaticBody2D.new()
	wall7.position = Vector2(250, 0)
	wall7.collision_layer = 8
	wall7.collision_mask = 2
	_add_shape(wall7)
	get_root().add_child(wall7)
	await physics_frame
	_factory_block_body = 0
	_factory_body = 0
	factory.spawn_block_bullets(_block_data())
	for i in 40:
		await physics_frame
		if _factory_block_body >= 1:
			break
	_check(factory.debug_get_total_bullets_amount(1) >= 1, "T7 block volley spawned")
	_check(_factory_block_body >= 1, "T7 factory block_body_entered fired (%d)" % _factory_block_body)
	wall7.queue_free()
	factory.free_active_bullets()
	await process_frame

	_check(factory.debug_assert_no_dangling().get("ok", false) == true, "no dangling at end")

	factory.directional_body_entered.disconnect(_on_factory_body)
	factory.directional_area_entered.disconnect(_on_factory_area)
	factory.block_body_entered.disconnect(_on_factory_block_body)
	spawner.queue_free()
	factory.reset()
	factory.queue_free()
	await process_frame
	print("----")
	if failures == 0:
		print("ALL COLLISION-BODIES TESTS PASSED")
	else:
		printerr(str(failures) + " TEST(S) FAILED")
	quit(failures)
