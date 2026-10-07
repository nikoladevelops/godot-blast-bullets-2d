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
