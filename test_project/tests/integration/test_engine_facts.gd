extends BlastTest
## Engine facts the plugin's design relies on, pinned so an engine upgrade
## that changes one of them fails loudly here instead of silently breaking a
## contract elsewhere:
## - call_deferred() queued from _physics_process still flushes INSIDE the
##   physics frame (so deferring a signal never moves its handler out of the
##   physics step; only the *_deferred idle queue does);
## - Area2D overlap callbacks for a physics frame arrive BEFORE any node's
##   _physics_process of that frame (records drained at the start of the
##   factory tick describe the pose the server just tested);
## - signal connections live on the EMITTER: freeing the receiver drops the
##   connection (has_connections() turns false), so a volley's emit then
##   reaches nobody.
## - a MultiMeshInstance2D under a plain Node2D follows the node mode: with
##   tree physics_interpolation on it reports interpolated-and-enabled, and
##   PHYSICS_INTERPOLATION_MODE_OFF opts it back out (the shard opt-out
##   relies on exactly this).
## - an area shape transform scales its rectangle or capsule non-uniformly
##   (laser beams stretch one unit shape per beam);
## - a MultiMesh refuses a use_colors change while it has instances (a volley
##   switching color format must empty it first: the pool keys apart);
## - Node2D.reset_physics_interpolation() exists (teleports call it);
## - intersect_ray answers inside the physics frame (beams stop at walls).


class DeferredProbe extends Node:
	var seen_in_physics := -1 # -1 = never ran, 0 = idle, 1 = physics
	var queued := false

	func _physics_process(_delta: float) -> void:
		if not queued:
			queued = true
			call_deferred("_probe")

	func _probe() -> void:
		seen_in_physics = 1 if Engine.is_in_physics_frame() else 0


class OrderProbe extends Node:
	var frames_processed: Array = []

	func _physics_process(_delta: float) -> void:
		frames_processed.append(Engine.get_physics_frames())


class Receiver extends Node:
	var calls := 0

	func on_ping() -> void:
		calls += 1


class Emitter extends Node:
	signal ping


func test_call_deferred_from_physics_flushes_inside_the_physics_frame() -> void:
	var probe := DeferredProbe.new()
	add(probe)
	for i in 10:
		await physics()
		if probe.seen_in_physics != -1:
			break
	assert_eq(probe.seen_in_physics, 1, "a call_deferred queued in _physics_process runs while Engine.is_in_physics_frame() is true")


var _overlap_frame := -1
var _overlap_before_physics_process := false
var _order_probe: OrderProbe = null


func _on_area_body_entered(_body: Node) -> void:
	if _overlap_frame != -1:
		return
	_overlap_frame = Engine.get_physics_frames()
	_overlap_before_physics_process = not _order_probe.frames_processed.has(_overlap_frame)


func test_overlap_callbacks_arrive_before_physics_process() -> void:
	_order_probe = OrderProbe.new()
	_order_probe.process_priority = -1000 # runs early in the physics step
	_order_probe.process_physics_priority = -1000
	add(_order_probe)
	var area := make_area(Vector2(0, 0), Vector2(40, 40))
	area.collision_mask = 4
	area.body_entered.connect(_on_area_body_entered)
	await physics(2)
	# A CharacterBody2D (a static area never pairs with a static body).
	var mover := CharacterBody2D.new()
	mover.collision_layer = 4
	var col := CollisionShape2D.new()
	col.shape = H.make_circle_shape(4.0)
	mover.add_child(col)
	add(mover)
	for i in 20:
		await physics()
		if _overlap_frame != -1:
			break
	assert_ne(_overlap_frame, -1, "overlap reported")
	assert_true(_overlap_before_physics_process, "the overlap callback of frame N runs before any _physics_process of frame N")


func test_connections_live_on_the_emitter() -> void:
	var emitter := Emitter.new()
	add(emitter)
	var receiver := Receiver.new()
	add_child(receiver)
	emitter.ping.connect(receiver.on_ping)
	assert_true(emitter.has_connections("ping"), "connected")
	emitter.ping.emit()
	assert_eq(receiver.calls, 1, "receiver called while alive")
	receiver.free()
	assert_false(emitter.has_connections("ping"), "freeing the receiver drops the connection on the emitter")
	emitter.ping.emit() # reaches nobody, no error
	expect_no_errors()


func test_multimesh_instance_follows_node_interpolation() -> void:
	get_tree().physics_interpolation = true
	var holder := Node2D.new()
	add(holder)
	var mmi := MultiMeshInstance2D.new()
	var mm := MultiMesh.new()
	mm.transform_format = MultiMesh.TRANSFORM_2D
	mm.mesh = QuadMesh.new()
	mm.instance_count = 1
	mmi.multimesh = mm
	holder.add_child(mmi)
	assert_true(mmi.is_physics_interpolated_and_enabled(), "a MultiMeshInstance2D under a plain node is interpolated when the tree flag is on")
	mmi.physics_interpolation_mode = Node.PHYSICS_INTERPOLATION_MODE_OFF
	assert_false(mmi.is_physics_interpolated_and_enabled(), "PHYSICS_INTERPOLATION_MODE_OFF opts it back out")
	get_tree().physics_interpolation = false


## An area with one shape of half extents (5, 5) / a capsule r5 h10 at the
## origin, its transform scaled by `scale`; counts AREA_BODY_ADDED.
func _scaled_area(kind: String, scale: Vector2) -> Array:
	var ps := PhysicsServer2D
	var area := ps.area_create()
	ps.area_set_space(area, factory.get_world_2d().space)
	var shape := ps.rectangle_shape_create() if kind == "rect" else ps.capsule_shape_create()
	ps.shape_set_data(shape, Vector2(5, 5) if kind == "rect" else Vector2(5, 10))
	ps.area_add_shape(area, shape, Transform2D(0.0, scale, 0.0, Vector2.ZERO))
	ps.area_set_collision_mask(area, 4) # make_wall's layer
	ps.area_set_monitorable(area, true)
	var hits := [0]
	ps.area_set_monitor_callback(area, func(status, _rid, _id, _shape, _self_shape) -> void:
		if status == PhysicsServer2D.AREA_BODY_ADDED:
			hits[0] += 1)
	return [area, shape, hits]


func test_area_shapes_scale_non_uniformly() -> void:
	make_wall(Vector2(40, 0), Vector2(2, 2)) # inside a 10x stretch only
	make_wall(Vector2(0, 30), Vector2(2, 2)) # outside both: y is not stretched
	var got: Array = []
	for kind in ["rect", "capsule"]:
		for sc in [Vector2(1, 1), Vector2(10, 1)]:
			var a: Array = _scaled_area(kind, sc)
			await physics(4)
			got.append("%s %s: %d" % [kind, sc, a[2][0]])
			PhysicsServer2D.free_rid(a[0])
			PhysicsServer2D.free_rid(a[1])
	assert_eq(got, ["rect (1.0, 1.0): 0", "rect (10.0, 1.0): 1", "capsule (1.0, 1.0): 0", "capsule (10.0, 1.0): 1"], "the scale stretches x only")


func test_multimesh_refuses_a_color_format_change_with_instances() -> void:
	var mm := MultiMesh.new()
	mm.transform_format = MultiMesh.TRANSFORM_2D
	mm.instance_count = 4
	mm.use_colors = true
	expect_errors_containing('Condition "instance_count > 0" is true.', 1)
	assert_false(mm.use_colors, "refused while instances exist")
	mm.instance_count = 0
	mm.use_colors = true
	mm.instance_count = 4
	assert_true(mm.use_colors, "accepted on an empty MultiMesh")


func test_reset_physics_interpolation_exists() -> void:
	assert_true(ClassDB.class_has_method("Node", "reset_physics_interpolation"), "Node.reset_physics_interpolation")


func test_intersect_ray_answers_inside_the_physics_frame() -> void:
	make_wall(Vector2(40, 0), Vector2(2, 2))
	await physics(2)
	assert_true(Engine.is_in_physics_frame(), "inside the physics step")
	var hit := factory.get_world_2d().direct_space_state.intersect_ray(PhysicsRayQueryParameters2D.create(Vector2(-100, 0), Vector2(100, 0), 4))
	assert_eq(hit.get("position"), Vector2(39, 0), "the near face of the 2 px box")
