extends BlastTest
## Volley collision with REAL physics: 1 bullet at 300 px/s +X toward a wall
## body + Area2D at x=200 (layer 3). body_entered fires with the
## slim payload (body, volley, index), the bullet disables at max 1, max 0 is
## infinite, and disable+wake keeps the factory consistent.


func _bodied_data() -> BulletVolleyData2D:
	var d := H.make_volley_data(1, 300.0, 10.0)
	d.monitorable = true
	d.set_collision_mask_from_array([3])
	d.collision_shape = H.make_circle_shape(6.0)
	return d


func before_each() -> void:
	await super()
	make_wall(Vector2(200, 0))
	make_area(Vector2(200, 0))
	watch_signals(factory)
	await physics()


func _wait_until(cond: Callable, frames := 90) -> void:
	for i in frames:
		await physics()
		if cond.call():
			return


func test_body_entered_kills_at_max_one() -> void:
	var v: BulletVolley2D = factory.spawn_volley(_bodied_data())
	assert_not_null(v, "walled spawn ok")
	await _wait_until(func(): return not v.is_bullet_status_enabled(0))
	assert_signal_emitted(factory, "body_entered", "body signal fired")
	var p = get_signal_parameters(factory, "body_entered", 0)
	assert_eq(p[1], v, "payload volley")
	assert_eq(int(p[2]), 0, "payload index 0")
	assert_false(v.is_bullet_status_enabled(0), "bullet disabled at max 1")
	assert_eq(v.get_bullet_collision_count(0), 1, "collision count tracked")
	# The Area2D sibling legitimately stays silent: max 1 means the first
	# record (body) kills the bullet and the area record for the dead slot is
	# skipped. Multi-hit volleys report both (next test).


func test_max_two_reports_body_and_area() -> void:
	var v: BulletVolley2D = factory.spawn_volley(_bodied_data())
	v.set_bullet_max_collision_count(2)
	await _wait_until(func(): return get_signal_emit_count(factory, "body_entered") >= 1 \
		and get_signal_emit_count(factory, "area_entered") >= 1)
	assert_signal_emitted(factory, "body_entered", "body reported on multi-hit volley")
	assert_signal_emitted(factory, "area_entered", "area reported on multi-hit volley")


func test_max_zero_is_infinite() -> void:
	var v: BulletVolley2D = factory.spawn_volley(_bodied_data())
	v.set_bullet_max_collision_count(0)
	await _wait_until(func(): return get_signal_emit_count(factory, "body_entered") >= 2)
	assert_signal_emitted(factory, "body_entered", "infinite bullet still reports hits")
	assert_true(v.is_bullet_status_enabled(0), "infinite bullet stays alive through the wall")


func test_disable_wake_same_life() -> void:
	var v: BulletVolley2D = factory.spawn_volley(_bodied_data())
	v.set_is_auto_pooling_enabled(false) # a drained 1-bullet volley parks instead of pooling
	v.disable_bullet(0)
	assert_false(v.is_bullet_status_enabled(0), "manual disable holds")
	v.enable_bullet(0)
	assert_true(v.is_bullet_status_enabled(0), "wake revives")
	assert_true(factory.debug_assert_no_dangling().get("ok", false), "no dangling after wake")


## A resting 1-bullet volley at `pos` on `layer_bits`' complement: layer 2 (the
## default volley layer), a mask of 8 (layer 4) so only the walls/areas the
## tests below build can match it.
func _resting_at(pos: Vector2, monitorable: bool, layer_bits := 2, mask_bits := 4) -> BulletVolley2D:
	var d := H.make_volley_data(1, 0.0, 60.0)
	d.transforms = [Transform2D(0.0, pos)]
	d.set_collision_layer_from_array([layer_bits])
	d.set_collision_mask_from_array([mask_bits])
	d.collision_shape = H.make_circle_shape(6.0)
	d.bullet_max_collision_count = 0
	d.monitorable = monitorable
	return factory.spawn_volley(d)


## A flying 1-bullet volley from (0,0) with the given layer/mask, dying on its
## first counted hit (max 1) unless `max_count` says otherwise.
func _flying(mask_bits: int, max_count := 1, speed := 600.0) -> BulletVolley2D:
	var d := H.make_volley_data(1, speed, 10.0)
	d.transforms = [Transform2D(0.0, Vector2.ZERO)]
	d.set_collision_mask_from_array([mask_bits])
	d.collision_shape = H.make_circle_shape(6.0)
	d.bullet_max_collision_count = max_count
	d.monitorable = true
	return factory.spawn_volley(d)


func _fly_until(v: BulletVolley2D, cond: Callable, frames := 120) -> void:
	for i in frames:
		await physics()
		if cond.call():
			return


## Layer/mask arrays are `calculate_bitmask` of the array, on the data AND at
## runtime on the volley; an out-of-range entry is ignored with one exact
## error while the valid entries still apply.
func test_layer_and_mask_arrays_follow_calculate_bitmask() -> void:
	var d := H.make_volley_data(1)
	for arr in [[1, 3], [2], [32]]:
		var want := BulletVolleyData2D.calculate_bitmask(arr)
		d.set_collision_layer_from_array(arr)
		assert_eq(d.get_collision_layer(), want, "layer for %s" % str(arr))
		d.set_collision_mask_from_array(arr)
		assert_eq(d.get_collision_mask(), want, "mask for %s" % str(arr))
	var v := quick_volley(1)
	v.set_collision_layer_from_array([1, 3])
	assert_eq(v.get_collision_layer(), BulletVolleyData2D.calculate_bitmask([1, 3]), "the volley's runtime layer setter uses the same helper")
	v.set_collision_mask_from_array([2])
	assert_eq(v.get_collision_mask(), 2, "and so does its mask setter")
	assert_eq(BulletVolleyData2D.calculate_bitmask([1, 33]), 1, "an invalid entry is dropped, the valid ones stay")
	expect_error_sequence(["Invalid layer number 33 in calculate_bitmask. Valid range is 1..32. Ignoring."])
	d.set_collision_layer_from_array([1, 33])
	assert_eq(d.get_collision_layer(), 1, "the data setter reports and keeps the valid bits")
	expect_error_sequence(["Invalid layer number 33 in calculate_bitmask. Valid range is 1..32. Ignoring."])
	v.set_collision_mask_from_array([33])
	assert_eq(v.get_collision_mask(), 0, "an all-invalid array leaves an empty mask")
	expect_error_sequence(["Invalid layer number 33 in calculate_bitmask. Valid range is 1..32. Ignoring."])


## Layer 32 is bit 31: the volley's layer property reports the same
## (sign-extended) 32-bit value `calculate_bitmask` computes - the bit itself is
## set - and only an observing Area2D whose MASK names layer 32 receives the
## bullet's overlap at all (as one shape-level record; the layer-1 watcher stays
## silent).
func test_a_layer_32_bullet_is_seen_only_by_a_layer_32_area() -> void:
	var p := make_area(Vector2(0, 0), Vector2(200, 200), 0)
	var q := make_area(Vector2(0, 0), Vector2(200, 200), 0)
	p.name = "PlayerLayer1"
	q.name = "PlayerLayer32"
	p.collision_mask = 1
	q.collision_mask = 1 << 31
	assert_eq(q.collision_mask, 1 << 31, "the watcher's mask round-trips layer 32")
	watch_signals(p)
	watch_signals(q)
	var q_records: Array = []
	q.area_shape_entered.connect(func(_rid: RID, area: Object, _si: int, _li: int) -> void: q_records.append(area))
	await physics()
	var v := _resting_at(Vector2.ZERO, true, 32, 30)
	assert_eq(v.get_collision_layer(), BulletVolleyData2D.calculate_bitmask([32]), "the volley's layer is the layer-32 bitmask")
	assert_eq(v.get_collision_layer(), -2147483648, "layer 32 is the sign-extended 32-bit bit (Godot's own int property behavior)")
	assert_ne(v.get_collision_layer() & (1 << 31), 0, "and the bit is set where a bit test looks for it")
	await physics(4)
	assert_signal_emit_count(p, "area_shape_entered", 0, "the layer-1 watcher never sees a layer-32 bullet")
	assert_eq(q_records.size(), 1, "the layer-32 watcher sees it exactly once")
	await physics(6)
	assert_eq(q_records.size(), 1, "a resting overlap is never reported twice")
	assert_eq(v.get_bullet_collision_count(0), 0, "the bullet's own mask matches neither watcher")


## Re-masking mid-flight switches which target counts: the bullet crosses a wall
## the old mask did not name (silently) and dies on the first wall the NEW mask
## names, with exactly one hit signal carrying that wall.
func test_flipping_the_mask_mid_flight_selects_the_new_masks_wall() -> void:
	var early := make_wall(Vector2(100, 0), Vector2(20, 400), 16) # layer 5
	var late := make_wall(Vector2(400, 0), Vector2(20, 400), 8) # layer 4
	late.name = "LateWall"
	await physics()
	var v := _flying(2) # mask layer 2: neither wall
	await _fly_until(v, func(): return v.get_bullet_transform(0).origin.x > 150.0)
	assert_eq(v.get_bullet_collision_count(0), 0, "the first wall is not on the bullet's mask")
	assert_true(is_instance_valid(early), "and the bullet passed it untouched")
	v.set_collision_mask_from_array([4]) # layer 4 = the late wall
	assert_eq(v.get_collision_mask(), 8, "the new mask")
	await _fly_until(v, func(): return not v.is_bullet_status_enabled(0))
	assert_eq(v.get_bullet_collision_count(0), 1, "only the wall on the NEW mask counted")
	assert_signal_emit_count(factory, "body_entered", 1, "exactly one hit signal")
	assert_eq(get_signal_parameters(factory, "body_entered", 0)[0], late, "naming the wall the new mask selected")
	assert_false(v.is_bullet_status_enabled(0), "one hit at max 1 kills it")


## A resting bullet inside an observing Area2D (whose mask names the bullet's
## layer): the monitorable flag decides whether the observer records it at all -
## `false` records nothing, `true` records exactly one overlap, never a second.
## The record is a SHAPE-level one and its area resolves to null: a volley's
## area is a bare physics RID with no Object behind it, so `area_entered` (which
## carries an Object) never fires for it. A resting area that turns monitorable
## AFTER it joined is never re-paired (Godot's broadphase), which is why the
## flag is read at spawn.
func test_a_resting_bullets_monitorable_flag_decides_who_sees_it() -> void:
	var observer := make_area(Vector2(0, 0), Vector2(400, 400), 0)
	observer.name = "Observer"
	observer.collision_mask = 2 # the volley's layer
	var seen: Array = []
	observer.area_shape_entered.connect(func(_rid: RID, area: Object, _si: int, _li: int) -> void: seen.append(area))
	watch_signals(observer)
	await physics()
	var off_bullet := _resting_at(Vector2.ZERO, false)
	await physics(4)
	assert_eq(seen.size(), 0, "a non-monitorable resting bullet is never seen")
	assert_signal_emit_count(observer, "area_entered", 0, "no area-level record either")
	assert_eq(off_bullet.get_bullet_collision_count(0), 0, "the bullet records nothing (its mask names no target here)")
	off_bullet.set_monitorable(true)
	await physics(4)
	assert_eq(seen.size(), 0, "a RESTING area that turns monitorable after joining is never re-paired")
	var on_bullet := _resting_at(Vector2(120, 0), true)
	await physics(4)
	assert_eq(seen.size(), 1, "a monitorable resting bullet is seen exactly once")
	assert_null(seen[0], "the observer resolves no Object: a volley's area is a bare physics RID")
	assert_signal_emit_count(observer, "area_entered", 0, "the Object-carrying signal never fires for it")
	await physics(8)
	assert_eq(seen.size(), 1, "a resting overlap is never re-reported")
	assert_eq(on_bullet.get_bullet_collision_count(0), 0, "and the bullet's own intake is unaffected by the flag")


## The same flag against a BODY: Godot pairs an area with a StaticBody2D only
## while the area is monitorable, so a non-monitorable volley records no body
## hit at all and a monitorable one records exactly one, never a second.
func test_monitorable_decides_body_records() -> void:
	make_wall(Vector2(0, 0), Vector2(80, 80), 8) # layer 4 = the volley's mask
	await physics()
	var off := _resting_at(Vector2.ZERO, false)
	await physics(4)
	assert_eq(off.get_bullet_collision_count(0), 0, "a non-monitorable volley records no body hit")
	assert_signal_emit_count(factory, "body_entered", 0, "and reports none")
	var on := _resting_at(Vector2.ZERO, true)
	await physics(4)
	assert_eq(on.get_bullet_collision_count(0), 1, "a monitorable volley records exactly one body hit")
	assert_signal_emit_count(factory, "body_entered", 1, "one hit signal")
	await physics(8)
	assert_eq(on.get_bullet_collision_count(0), 1, "a resting overlap is never re-counted")


## monitorable = being SEEN: a bullet whose area is not monitorable still counts
## its own hit on an Area2D once (the intake is the bullet's query, not the
## target's), exactly like a monitorable twin.
func test_a_non_monitorable_bullet_still_counts_its_own_area_hit() -> void:
	var area := make_area(Vector2(200, 0), Vector2(40, 400), 8)
	area.name = "Hurtbox"
	await physics()
	var off := _flying(4)
	await _fly_until(off, func(): return off.get_bullet_collision_count(0) > 0)
	assert_eq(off.get_bullet_collision_count(0), 1, "one hit for the non-monitorable bullet")
	assert_false(off.is_bullet_status_enabled(0), "which kills it at max 1")
	assert_signal_emit_count(factory, "area_entered", 1, "reported once")
	assert_eq(get_signal_parameters(factory, "area_entered", 0)[0], area, "naming the area")
	var on := _flying(4)
	on.set_monitorable(true)
	await _fly_until(on, func(): return on.get_bullet_collision_count(0) > 0)
	assert_eq(on.get_bullet_collision_count(0), 1, "the monitorable twin counts the same single hit")
	assert_signal_emit_count(factory, "area_entered", 2, "and reports its own")


## Mid-life max-collision counts: 0 survives every target, 1 dies on the first,
## and 2 (set AFTER the first hit) dies on the second; every count agrees.
func test_mid_life_collision_max_counts_0_1_and_2() -> void:
	var a := make_area(Vector2(100, 0), Vector2(40, 400), 8)
	var b := make_area(Vector2(300, 0), Vector2(40, 400), 8)
	b.name = "SecondArea"
	await physics()
	var survivor := _flying(4, 0)
	await _fly_until(survivor, func(): return survivor.get_bullet_transform(0).origin.x > 350.0)
	assert_eq(survivor.get_bullet_collision_count(0), 2, "max 0 survives both targets and counts both")
	assert_true(survivor.is_bullet_status_enabled(0), "and stays alive")
	var one := _flying(4, 1)
	await _fly_until(one, func(): return not one.is_bullet_status_enabled(0))
	assert_eq(one.get_bullet_collision_count(0), 1, "max 1 dies on the first target")
	assert_lte(one.get_bullet_transform(0).origin.x, 90.0, "at the first area, not the second")
	var two := _flying(4, 0)
	await _fly_until(two, func(): return two.get_bullet_collision_count(0) == 1)
	two.set_bullet_max_collision_count(2) # mid-life, after the first hit
	assert_eq(two.get_bullet_max_collision_count(), 2, "the new cap")
	assert_true(two.is_bullet_status_enabled(0), "still alive after hit 1")
	await _fly_until(two, func(): return not two.is_bullet_status_enabled(0))
	assert_eq(two.get_bullet_collision_count(0), 2, "max 2 dies on the second target")
	assert_gte(two.get_bullet_transform(0).origin.x, 270.0, "at the second area")
	assert_signal_emit_count(factory, "area_entered", 5, "five counted area hits in total")
	assert_true(is_instance_valid(a) and is_instance_valid(b), "the targets are untouched")


## The hitbox carries the data's collision_shape_offset, turned by the bullet's
## own rotation: the shape transform's origin is the pose origin plus the
## rotated offset and its basis is the pose basis, while the reported shape
## state describes the shape itself.
func test_the_shape_transform_carries_the_rotated_offset() -> void:
	var cases: Array = [
		[H.make_circle_shape(5.0), PhysicsServer2D.SHAPE_CIRCLE],
		[null, PhysicsServer2D.SHAPE_RECTANGLE],
		[null, PhysicsServer2D.SHAPE_CAPSULE],
	]
	var rect := RectangleShape2D.new()
	rect.size = Vector2(10, 20)
	var capsule := CapsuleShape2D.new()
	capsule.radius = 4.0
	capsule.height = 20.0
	cases[1][0] = rect
	cases[2][0] = capsule
	for case in cases:
		var d := H.make_volley_data(1, 0.0, 60.0)
		d.transforms = [Transform2D(0.7, Vector2(30.0, 40.0))]
		d.collision_shape = case[0]
		d.collision_shape_offset = Vector2(7, 0)
		var v := factory.spawn_volley(d)
		var pose: Transform2D = v.get_bullet_transform(0)
		var shape: Transform2D = v.debug_get_bullet_shape_transform(0)
		assert_almost_eq(shape.origin, pose.origin + Vector2(7, 0).rotated(pose.get_rotation()), Vector2(0.001, 0.001), "shape origin for type %d" % case[1])
		assert_almost_eq(shape.get_rotation(), pose.get_rotation(), 0.0001, "the shape turns with the bullet")
		var st: Dictionary = v.debug_get_shape_state()
		assert_eq(int(st["type"]), case[1], "reported shape type")
		assert_true(bool(st["valid"]), "reported shape valid")
		if case[1] == PhysicsServer2D.SHAPE_CIRCLE:
			assert_almost_eq(float(st["circle_radius"]), 5.0, 0.0001, "radius")
		elif case[1] == PhysicsServer2D.SHAPE_RECTANGLE:
			assert_eq(st["rect_size"], Vector2(10, 20), "size")
		else:
			assert_almost_eq(float(st["capsule_radius"]), 4.0, 0.0001, "capsule radius")
			assert_almost_eq(float(st["capsule_height"]), 20.0, 0.0001, "capsule height")
		v.clear_all_bullets()
		await idle(1)


## An unsupported collision shape falls back to the circle r16 with one exact
## error, the pool bucket says circle, and the bullet really HITS as a r16
## hitbox: resting 14 px short of a wall face (14 > r6, 14 < r16) it counts a
## hit where a real r6 twin counts none.
func test_an_unsupported_shape_falls_back_to_circle_r16_and_hits_as_one() -> void:
	make_wall(Vector2(200, 0), Vector2(20, 400), 8) # face at x = 190
	await physics()
	var convex := ConvexPolygonShape2D.new()
	convex.points = PackedVector2Array([Vector2(-5, -5), Vector2(5, -5), Vector2(0, 5)])
	var d := H.make_still_data(1)
	d.transforms = [Transform2D(0.0, Vector2(176, 0))] # 14 px from the face
	d.monitorable = true
	d.set_collision_mask_from_array([4])
	d.collision_shape = convex
	var v: BulletVolley2D = factory.spawn_volley(d)
	expect_error_sequence(["Unsupported collision shape type: ConvexPolygonShape2D - only RectangleShape2D/CircleShape2D/CapsuleShape2D supported. Falling back to circle r16."])
	var st: Dictionary = v.debug_get_shape_state()
	assert_eq(int(st["type"]), PhysicsServer2D.SHAPE_CIRCLE, "the fallback is a circle")
	assert_almost_eq(float(st["circle_radius"]), 16.0, 0.0001, "with the r16 fallback radius")
	var bucket: VolleyPoolKey2D = factory.debug_get_pool_bucket(v)
	assert_eq(bucket.shape_type, PhysicsServer2D.SHAPE_CIRCLE, "the pool bucket agrees")
	assert_eq(bucket.amount_bullets, 1, "and its amount")
	await physics(4)
	assert_eq(v.get_bullet_collision_count(0), 1, "the r16 hitbox reaches the wall")
	var control := H.make_still_data(1)
	control.transforms = [Transform2D(0.0, Vector2(176, 0))]
	control.monitorable = true
	control.set_collision_mask_from_array([4])
	control.collision_shape = H.make_circle_shape(6.0)
	var c: BulletVolley2D = factory.spawn_volley(control)
	await physics(4)
	assert_eq(c.get_bullet_collision_count(0), 0, "a real r6 hitbox does not reach it: the hit above is the r16 fallback")
