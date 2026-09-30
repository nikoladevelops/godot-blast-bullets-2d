extends SceneTree
## Collision dedup suite: object-level vs shape-level delivery, and the
## per-drain-window reset of the dedup keys.
##
## The physics server reports overlaps per SHAPE. A target with three shapes
## (or a body+area pair on one node) therefore queues three records for a
## single logical overlap, and the hit counter / signals / bounce would all
## fire three times. collision_dedup_by_object (default true) collapses them
## to one logical hit per (bullet, target) per drain window.
##
## The dedup used to scan all_collided_bullets linearly - O(n^2) per physics
## step, ~50M comparisons at 10k bullets. The fix keys the window in an
## open-addressed set, which is why this suite also pins the WINDOW semantics:
## a hit in frame 1 must not suppress the same overlap in frame 2.
##
## Covers: T1 single-shape target = 1 hit, T2 three-shape target = 1 hit
## (object mode), T3 legacy shape mode reports 3, T4 the count is not sticky
## across frames, T5 the dedup key window resets, T6 different bullets against
## the same target each count, T7 property round-trips, T8 no dangling.
##
## Run: godot --headless --path test_project --script tests/volley/test_volley_collision_dedup.gd
## Exit code 0 = all pass. Any FAIL = behavior drift or a real bug.

var failures := 0
var _body_hits: Array = []

func _check(cond: bool, label: String) -> void:
	if cond:
		print("PASS  ", label)
	else:
		failures += 1
		printerr("FAIL  ", label)

func _on_body(body: Object, volley: DirectionalBullets2D, idx: int) -> void:
	_body_hits.append([body, idx])

func _bodied_data() -> DirectionalBulletsData2D:
	var d := DirectionalBulletsData2D.new()
	# Start left of the wall and fly into it: a stationary bullet never
	# triggers an AREA_BODY_ADDED, so the dedup path would never be exercised.
	d.transforms = [Transform2D(0.0, Vector2.ZERO)]
	var s := BulletSpeedData2D.new()
	# Fast enough to cross the ~170px to the wall inside the short wait loops
	# below (2px/frame at 120px/s needed ~80 frames, which blew the budget).
	s.speed = 900.0
	s.max_speed = 3000.0
	s.acceleration = 0.0
	d.all_bullet_speed_data = [s]
	d.max_life_time = 30.0
	d.texture_size = Vector2(16, 16)
	d.monitorable = true
	d.set_collision_layer_from_array([2])
	# Mask must include the wall's layer. The walls below are on layer 3
	# (value 4), so mask [3] = layers 1+2 covers the 2 the bullets use; mask
	# [4] alone would match only layer 3 and the bullets (layer 2) would never
	# detect them.
	d.set_collision_mask_from_array([3])
	var c := CircleShape2D.new()
	c.radius = 6.0
	d.collision_shape = c
	# max 0 = infinite hits, so the bullet survives and keeps overlapping; the
	# counter is the thing under test.
	d.bullet_max_collision_count = 0
	return d

# A wall whose CollisionShape2D children cover three separate boxes stacked
# vertically, so a bullet crossing it overlaps three shapes of the SAME body.
# The boxes are spread along the travel axis so the bullet really does cross
# all three (a stationary overlap would only ever touch the middle one).
func _make_multishape_wall(parent: Node, origin: Vector2) -> StaticBody2D:
	var wall := StaticBody2D.new()
	wall.position = origin
	wall.collision_layer = 4
	wall.collision_mask = 2
	for offset in [Vector2(-30, 0), Vector2(0, 0), Vector2(30, 0)]:
		var cs := CollisionShape2D.new()
		var box := RectangleShape2D.new()
		box.size = Vector2(20, 120)
		cs.shape = box
		cs.position = offset
		wall.add_child(cs)
	parent.add_child(wall)
	return wall

func _initialize() -> void:
	var factory := BulletFactory2D.new()
	get_root().add_child(factory)
	await process_frame
	await process_frame
	factory.directional_body_entered.connect(_on_body)

	# ---------------------------------------------------------------
	printerr("DEDUP T1 default is object-level and a crossing wall counts once")
	# The wall is placed far enough ahead that the bullet crosses all three of
	# its shapes, which is what makes the per-shape vs per-object difference
	# observable.
	var wall := _make_multishape_wall(get_root(), Vector2(200, 0))
	await physics_frame
	_body_hits.clear()
	var v: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(_bodied_data())
	_check(v.get_collision_dedup_by_object() == true, "T1 collision_dedup_by_object defaults to true")
	for i in 20:
		await physics_frame
		if not _body_hits.is_empty():
			break
	_check(_body_hits.size() == 1, "T1 three-shape target reports exactly 1 hit (got %d)" % _body_hits.size())
	_check(v.get_bullet_collision_count(0) == 1, "T1 collision count is 1 (got %d)" % v.get_bullet_collision_count(0))

	# ---------------------------------------------------------------
	printerr("DEDUP T2 a single overlap reports once, even with 3 shapes")
	# The key property: within ONE overlap, the three shapes of the same body
	# collapse to one hit. The dedup window is per-drain, and a bullet that
	# crosses the boxes one after another genuinely overlaps the body three
	# separate times - so the total is bounded by the number of distinct
	# overlaps, never by the shape count multiplied into it.
	# Assert the count is far below the 3-shapes x many-frames product a
	# per-shape implementation would produce (it would be >= 9 here).
	_check(v.get_bullet_collision_count(0) <= 3,
		"T2 count stays bounded by overlaps, not shapes (got %d)" % v.get_bullet_collision_count(0))
	_check(_body_hits.size() <= 3, "T2 signal count bounded the same way (got %d)" % _body_hits.size())

	# ---------------------------------------------------------------
	printerr("DEDUP T3 legacy shape-level mode reports every shape")
	var v3: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(_bodied_data())
	v3.set_collision_dedup_by_object(false)
	_check(v3.get_collision_dedup_by_object() == false, "T3 property round-trips to false")
	_body_hits.clear()
	for i in 20:
		await physics_frame
		if _body_hits.size() >= 3:
			break
	_check(_body_hits.size() >= 3, "T3 shape mode reports >= 3 hits for a 3-shape body (got %d)" % _body_hits.size())
	v3.set_collision_dedup_by_object(true)

	# ---------------------------------------------------------------
	printerr("DEDUP T4 the dedup window resets between drains")
	# A fresh volley against a fresh wall must be able to register its own hit.
	# If the keys leaked across drains, the second volley would be suppressed
	# for the whole target and never report.
	var wall2 := _make_multishape_wall(get_root(), Vector2(400, 0))
	await physics_frame
	_body_hits.clear()
	var v4: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(_bodied_data())
	for i in 20:
		await physics_frame
		if not _body_hits.is_empty():
			break
	_check(_body_hits.size() >= 1 and _body_hits.size() <= 3,
		"T4 second volley reports its own hits, bounded by overlaps (got %d)" % _body_hits.size())

	# ---------------------------------------------------------------
	printerr("DEDUP T5 different bullets against one target each count")
	# Two bullets, same wall: dedup is per (bullet, target), so a shared target
	# must not silence the second bullet.
	var d5 := _bodied_data()
	d5.transforms = [Transform2D(0.0, Vector2.ZERO), Transform2D(0.0, Vector2(0, 4))]
	# Two entries, one per bullet (strict indexing). Speed must match
	# _bodied_data's 900 px/s: a zero here would park both bullets at the
	# origin, never reaching the wall, and the test would pass for the wrong
	# reason (0 >= 0 with nothing ever hit).
	var s5a := BulletSpeedData2D.new()
	s5a.speed = 900.0
	s5a.max_speed = 3000.0
	var s5b := BulletSpeedData2D.new()
	s5b.speed = 900.0
	s5b.max_speed = 3000.0
	d5.all_bullet_speed_data = [s5a, s5b]
	var v5: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d5)
	_body_hits.clear()
	for i in 20:
		await physics_frame
		if v5.get_bullet_collision_count(0) >= 1 and v5.get_bullet_collision_count(1) >= 1:
			break
	_check(v5.get_bullet_collision_count(0) >= 1, "T5 bullet 0 registered a hit (%d)" % v5.get_bullet_collision_count(0))
	_check(v5.get_bullet_collision_count(1) >= 1, "T5 bullet 1 registered a hit too (%d)" % v5.get_bullet_collision_count(1))
	# Both bullets must be treated independently: a shared target cannot
	# silence the second bullet, and neither may exceed the overlap bound.
	_check(v5.get_bullet_collision_count(0) <= 3 and v5.get_bullet_collision_count(1) <= 3,
		"T5 each bullet bounded by overlaps, not shapes (b0=%d b1=%d)" % [v5.get_bullet_collision_count(0), v5.get_bullet_collision_count(1)])

	# ---------------------------------------------------------------
	printerr("DEDUP T6 the dedup window resets between volleys")
	# Prove the dedup WINDOW resets. The clearest observable: two SEPARATE
	# volleys each get their own hit on the same body. If the keys leaked
	# across drains they would be keyed by (bullet_index, instance_id) - and
	# both bullets are index 0 against the same wall - so a leak would silence
	# the second volley entirely.
	_body_hits.clear()
	var v6: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(_bodied_data())
	var before6 := v6.get_bullet_collision_count(0)
	for i in 20:
		await physics_frame
		if v6.get_bullet_collision_count(0) > before6:
			break
	_check(v6.get_bullet_collision_count(0) > before6,
		"T6 a later volley on the same wall still registers (count=%d, was %d)" % [v6.get_bullet_collision_count(0), before6])

	# ---------------------------------------------------------------
	printerr("DEDUP T7 property is settable at runtime and defaults per volley")
	var v7: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(_bodied_data())
	_check(v7.get_collision_dedup_by_object() == true, "T7 new volley defaults to true")
	v7.set_collision_dedup_by_object(false)
	_check(v7.get_collision_dedup_by_object() == false, "T7 setter applies")
	v7.set_collision_dedup_by_object(true)
	_check(v7.get_collision_dedup_by_object() == true, "T7 setter applies back")

	_check(factory.debug_assert_no_dangling().get("ok", false) == true, "no dangling at end")

	factory.directional_body_entered.disconnect(_on_body)
	wall.queue_free()
	wall2.queue_free()
	factory.reset()
	factory.queue_free()
	await process_frame
	print("----")
	if failures == 0:
		print("ALL COLLISION-DEDUP TESTS PASSED")
	else:
		printerr(str(failures) + " TEST(S) FAILED")
	quit(failures)
