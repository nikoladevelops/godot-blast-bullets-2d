extends BlastTest
## Working-case contract for the BulletVolley2D methods no other suite calls:
## aim helpers (direction and texture toward a node or point, whole-volley
## transforms), every homing push / replace / mouse variant on per-bullet and
## shared deques, the all_bullets_* orbit setters and queries, attachment
## batch get / detach / re-enable, sprite animation control with its finished
## signal, ranged state reset, movement pattern from a path, the pooling flag
## reset, and the getters that mirror the spawn data. Ranges apply to exactly
## the requested bullets.

const EPS := 0.001


## Three still bullets at x = 0, 16, 32 (y = 0) under an untransformed
## factory, so bullet-local and global positions agree.
func _still(n := 3) -> BulletVolley2D:
	return factory.spawn_volley(H.make_still_data(n))


func _node_at(pos: Vector2) -> Node2D:
	var n := Node2D.new()
	n.position = pos
	add(n)
	return n


func test_aim_helpers_turn_direction_and_texture_toward_a_point_or_node() -> void:
	var v := _still()
	var up := Vector2(16, -100) # straight above bullet 1
	var below := _node_at(Vector2(16, 200)) # straight below bullet 1
	v.all_bullets_set_direction_towards_position(up)
	assert_almost_eq(v.get_bullet_direction(1), Vector2(0, -1), Vector2(EPS, EPS), "bullet 1 aims at the point")
	assert_almost_eq(v.get_bullet_direction(0), (up - Vector2(0, 0)).normalized(), Vector2(EPS, EPS), "bullet 0 aims at it from its own spot")
	v.all_bullets_set_direction_towards_node2d(below, 1, 1)
	assert_almost_eq(v.get_bullet_direction(1), Vector2(0, 1), Vector2(EPS, EPS), "ranged: bullet 1 aims at the node")
	assert_almost_eq(v.get_bullet_direction(2), (up - Vector2(32, 0)).normalized(), Vector2(EPS, EPS), "bullet 2 outside the range is untouched")
	v.set_bullet_direction_towards_node2d(0, below)
	assert_almost_eq(v.get_bullet_direction(0), Vector2(16, 200).normalized(), Vector2(EPS, EPS), "single bullet toward a node")
	v.all_bullets_set_texture_rotation_towards_position(up)
	assert_almost_eq(v.get_bullet_texture_rotation_radians(1), -PI / 2, EPS, "texture faces the point")
	v.all_bullets_set_texture_rotation_towards_node2d(below)
	assert_almost_eq(v.get_bullet_texture_rotation_radians(1), PI / 2, EPS, "texture faces the node")
	v.set_bullet_texture_rotation_towards_node2d(2, below)
	assert_almost_eq(v.get_bullet_texture_rotation_radians(2), Vector2(-16, 200).angle(), EPS, "single bullet texture toward a node")
	v.all_bullets_set_texture_rotation_degrees(45.0, 1, 2)
	var degs: Array = Array(v.all_bullets_get_texture_rotation_degrees(0, 2))
	assert_almost_eq(float(degs[1]), 45.0, EPS, "ranged degrees: bullet 1")
	assert_almost_eq(float(degs[2]), 45.0, EPS, "ranged degrees: bullet 2")
	assert_almost_eq(float(degs[0]), rad_to_deg(Vector2(16, 200).angle()), EPS, "bullet 0 kept its texture")
	v.all_bullets_set_transforms(Transform2D(0.5, Vector2(100, 50)), true, 0, 1)
	assert_almost_eq(v.get_bullet_global_transform(0).origin, Vector2(100, 50), Vector2(EPS, EPS), "moved to the new transform")
	assert_almost_eq(v.get_bullet_direction(1), Vector2.from_angle(0.5), Vector2(EPS, EPS), "direction follows the transform when asked")
	assert_almost_eq(v.get_bullet_global_transform(2).origin, Vector2(32, 0), Vector2(EPS, EPS), "bullet 2 outside the range stays")


func test_every_homing_push_and_replace_variant() -> void:
	var v := _still(2)
	var a := _node_at(Vector2(500, 0))
	var b := _node_at(Vector2(-500, 0))
	v.all_bullets_push_back_homing_target(a)
	v.all_bullets_push_front_homing_target(b)
	assert_same(v.bullet_get_current_homing_target(0), b, "push front wins the front")
	v.all_bullets_push_back_homing_targets_array([Vector2(1, 1), Vector2(2, 2)])
	assert_same(v.bullet_get_current_homing_target(1), b, "push back keeps the front")
	assert_eq(Array(v.all_bullets_get_homing_targets_amount()), [4, 4], "four targets each")
	v.all_bullets_push_front_homing_targets_array([Vector2(5, 5)], 1, 1)
	assert_eq(v.bullet_get_current_homing_target(1), Vector2(5, 5), "ranged array push front")
	assert_same(v.bullet_get_current_homing_target(0), b, "bullet 0 untouched")
	assert_eq(Array(v.all_bullets_get_homing_targets_amount()), [4, 5], "counts per bullet")
	v.all_bullets_replace_homing_targets_with_new_target_array([a])
	assert_eq(Array(v.all_bullets_get_homing_targets_amount()), [1, 1], "replace leaves only the new targets")
	assert_same(v.bullet_get_current_homing_target(0), a, "replaced front")
	v.bullet_homing_push_front_homing_target(1, Vector2(9, 9))
	v.bullet_homing_push_front_node2d_target(0, b)
	assert_eq(v.bullet_get_current_homing_target(1), Vector2(9, 9), "single push front (position)")
	assert_same(v.bullet_get_current_homing_target(0), b, "single push front (node)")
	v.all_bullets_replace_homing_targets_with_mouse()
	assert_eq(Array(v.all_bullets_get_homing_targets_amount()), [1, 1], "replace with the mouse")
	v.all_bullets_push_back_mouse_position_target()
	v.all_bullets_push_front_mouse_position_target(0, 0)
	v.bullet_homing_push_front_mouse_position_target(1)
	assert_eq(Array(v.all_bullets_get_homing_targets_amount()), [3, 3], "mouse pushes add one target each")
	v.shared_homing_deque_push_back_node2d_target(a)
	v.shared_homing_deque_push_front_node2d_target(b)
	assert_same(v.shared_homing_deque_get_current_homing_target(), b, "shared push front")
	v.shared_homing_deque_push_back_mouse_position_target()
	assert_same(v.shared_homing_deque_get_current_homing_target(), b, "shared mouse push back keeps the front")
	v.shared_homing_deque_push_front_mouse_position_target()
	assert_false(v.shared_homing_deque_get_current_homing_target() is Node2D, "shared mouse push front takes the front")
	v.set_bullet_homing_auto_pop_after_target_reached(false)
	v.set_shared_homing_deque_auto_pop_after_target_reached(false)
	assert_false(v.get_bullet_homing_auto_pop_after_target_reached(), "per-bullet auto pop off")
	assert_false(v.get_shared_homing_deque_auto_pop_after_target_reached(), "shared auto pop off")


func test_all_bullets_orbit_setters_apply_to_their_range_only() -> void:
	var v := _still()
	v.all_bullets_enable_orbiting(40.0, BulletVolley2D.OrbitRight, BulletVolley2D.FaceTarget, 0, 1)
	assert_eq(Array(v.all_bullets_is_orbiting_enabled()), [true, true, false], "orbiting on for the range")
	assert_eq(Array(v.all_bullets_is_orbiting_locked()), [false, false, false], "nothing locked without a target")
	v.all_bullets_set_orbiting_direction(BulletVolley2D.OrbitLeft, 2, 2)
	expect_errors_containing("has orbiting disabled", 1, "a bullet must orbit before it can be configured")
	assert_eq(Array(v.all_bullets_is_orbiting_enabled()), [true, true, false], "the refused setter enabled nothing")
	v.bullet_enable_orbiting(2, 40.0)
	v.all_bullets_set_orbiting_direction(BulletVolley2D.OrbitLeft, 1, 2)
	v.all_bullets_set_orbiting_texture_rotation(BulletVolley2D.FaceOrbitingDirection, 1, 2)
	v.all_bullets_set_orbiting_follow_mode(BulletVolley2D.FollowDeadzone, 1, 2)
	v.all_bullets_set_orbiting_follow_deadzone(12.0, 1, 2)
	v.all_bullets_set_orbiting_lock_policy(BulletVolley2D.RelockOnTargetChange, 1, 2)
	v.all_bullets_set_orbiting_rigid_follow(false, 1, 2)
	for i in [1, 2]:
		assert_eq(v.bullet_get_orbiting_direction(i), BulletVolley2D.OrbitLeft, "direction %d" % i)
		assert_eq(v.bullet_get_orbiting_texture_rotation(i), BulletVolley2D.FaceOrbitingDirection, "texture rotation %d" % i)
		assert_eq(v.bullet_get_orbiting_follow_mode(i), BulletVolley2D.FollowDeadzone, "follow mode %d" % i)
		assert_almost_eq(v.bullet_get_orbiting_follow_deadzone(i), 12.0, EPS, "deadzone %d" % i)
		assert_eq(v.bullet_get_orbiting_lock_policy(i), BulletVolley2D.RelockOnTargetChange, "lock policy %d" % i)
		assert_false(v.bullet_get_orbiting_rigid_follow(i), "rigid follow %d" % i)
	assert_eq(v.bullet_get_orbiting_direction(0), BulletVolley2D.OrbitRight, "bullet 0 outside the range kept its direction")
	assert_eq(v.bullet_get_orbiting_follow_mode(0), BulletVolley2D.FollowTarget, "and its follow mode")
	assert_true(v.bullet_get_orbiting_rigid_follow(0), "and its rigid follow")


func test_all_bullets_orbit_center_repins_locked_rings_in_range_only() -> void:
	var v: BulletVolley2D = factory.spawn_volley(H.make_volley_data(2, 250.0))
	v.set_homing_smoothing(8.0)
	v.all_bullets_push_back_homing_target(Vector2(400, 0))
	v.all_bullets_enable_orbiting(60.0)
	v.all_bullets_set_orbiting_center(Vector2(1, 1))
	expect_errors_containing("is not locked onto its ring yet", 2, "one warning per unlocked bullet")
	for i in 120:
		await physics()
		if Array(v.all_bullets_is_orbiting_locked()) == [true, true]:
			break
	assert_eq(Array(v.all_bullets_is_orbiting_locked()), [true, true], "both rings locked")
	var c0: Vector2 = v.bullet_get_orbiting_center(0)
	v.all_bullets_set_orbiting_center(Vector2(410, 10), 1, 1)
	assert_eq(v.bullet_get_orbiting_center(1), Vector2(410, 10), "bullet 1 re-pinned")
	assert_eq(v.bullet_get_orbiting_center(0), c0, "bullet 0 outside the range kept its center")


func test_attachment_batch_get_detach_and_reenable() -> void:
	var ps := make_probe_scene()
	var v := _still()
	v.all_bullets_set_attachment(ps, Vector2(0, -8))
	var atts: Array = v.all_bullets_get_attachments()
	assert_eq(atts.size(), 3, "one per bullet")
	assert_true(atts.all(func(a): return a is BulletAttachment2D), "all attached")
	assert_ne(atts[0], atts[1], "distinct instances")
	var detached: BulletAttachment2D = v.bullet_set_attachment_to_null(1)
	assert_same(detached, atts[1], "detach hands the node back")
	assert_null(v.bullet_get_attachment(1), "the slot is empty")
	assert_true(is_instance_valid(detached), "detached, not freed")
	var rest: Array = v.all_bullets_set_attachment_to_null(0, 0)
	assert_eq(rest.size(), 1, "ranged detach returns one")
	assert_same(rest[0], atts[0], "bullet 0's attachment")
	assert_eq(Array(v.all_bullets_get_attachments()).map(func(a): return a != null), [false, false, true], "only bullet 2 still carries one")
	v.disable_bullet(2, false) # keep (suspend) the attachment
	v.bullet_enable_attachment(2)
	assert_same(v.bullet_get_attachment(2), atts[2], "re-enabled in place")
	v.bullet_set_attachment(0, ps, Vector2(NAN, 0))
	expect_error_sequence(["bullet_set_attachment: bullet_attachment_offset must be finite, nothing attached."])
	assert_null(v.bullet_get_attachment(0), "a non-finite offset attaches nothing")
	detached.queue_free()
	(rest[0] as Node).queue_free()


func test_sprite_animation_queries_stop_resume_and_one_finished_signal() -> void:
	var d := H.make_still_data(2)
	var frames := H.make_effect_frames(4, 10.0) # non-looping, 0.1 s per frame
	d.sprite_frames = frames
	var v: BulletVolley2D = factory.spawn_volley(d)
	watch_signals(v)
	assert_same(v.get_sprite_frames(), frames, "the spawn data's frames")
	assert_eq(v.get_sprite_animation(), &"default", "default animation")
	assert_eq(v.get_sprite_frame_count(), 4, "four frames")
	assert_eq(v.get_sprite_frame(), 0, "starts on frame 0")
	assert_true(v.is_sprite_animation_playing(), "playing")
	await idle(7) # 0.117 s
	assert_eq(v.get_sprite_frame(), 1, "advanced one frame")
	v.stop_sprite_animation()
	assert_false(v.is_sprite_animation_playing(), "stopped")
	await idle(30)
	assert_eq(v.get_sprite_frame(), 1, "a stopped animation holds its frame")
	v.resume_sprite_animation()
	assert_true(v.is_sprite_animation_playing(), "resumed")
	for i in 40:
		await idle(1)
		if v.is_sprite_animation_finished():
			break
	assert_true(v.is_sprite_animation_finished(), "a non-looping animation finishes")
	assert_eq(v.get_sprite_frame(), 3, "on its last frame")
	assert_signal_emit_count(v, "sprite_animation_finished", 1, "finished fires exactly once")
	await idle(20)
	assert_signal_emit_count(v, "sprite_animation_finished", 1, "and never again")


func test_ranged_reset_state_clears_only_the_requested_ledgers() -> void:
	var v := _still()
	v.set_bullets_current_collision_count_no_return([2, 1, 3])
	assert_eq(Array(v.get_bullets_current_collision_count()), [2, 1, 3], "hit counts written")
	v.all_bullets_reset_state(1, 2)
	assert_eq(Array(v.get_bullets_current_collision_count()), [2, 0, 0], "only bullets 1 and 2 reset")
	v.all_bullets_reset_state()
	assert_eq(Array(v.get_bullets_current_collision_count()), [0, 0, 0], "whole volley by default")


func test_movement_pattern_from_path_applies_to_its_range() -> void:
	var v := _still()
	var path := Path2D.new()
	var curve := Curve2D.new()
	for p in [Vector2(0, 0), Vector2(50, 20), Vector2(100, 0)]:
		curve.add_point(p)
	path.curve = curve
	add(path)
	v.all_bullets_set_movement_pattern_from_path(path, false, true, 1, 2)
	assert_null(v.get_bullet_movement_pattern_curve(0), "bullet 0 outside the range has none")
	assert_not_null(v.get_bullet_movement_pattern_curve(1), "bullet 1 rides the path")
	assert_not_null(v.get_bullet_movement_pattern_curve(2), "bullet 2 rides the path")


func test_pooling_flags_reset_to_their_defaults() -> void:
	var v := _still()
	v.set_is_auto_pooling_enabled(false)
	v.set_is_attachments_auto_pooling_enabled(false)
	v.reset_pooling_flags_to_default()
	assert_true(v.get_is_auto_pooling_enabled(), "volley auto pooling back on")
	assert_true(v.get_is_attachments_auto_pooling_enabled(), "attachment auto pooling back on")


func test_getters_mirror_the_spawn_data() -> void:
	var d := H.make_still_data(2)
	d.collision_shape = H.make_circle_shape(5.0)
	d.fade_in_sec = 0.3
	d.fade_out_sec = 0.4
	var ramp := Gradient.new()
	d.modulate_ramp = ramp
	var curves := BulletCurvesData2D.new()
	d.shared_bullet_curves_data = curves
	var wobble := BulletWobbleData2D.new()
	d.shared_bullet_wobble_data = wobble
	var layer := H.make_effect_layer(BulletEffectLayerData2D.EFFECT_ON_HIT)
	d.effect_layers = [layer]
	d.bounce_mode = 1
	d.bounce_hit_consumed = true
	d.bounce_max_count = 3
	d.bounce_randomness_deg = 7.0
	d.bounce_rotate_texture = false
	d.bounce_rotation_smooth = 0.5
	d.bounce_cooldown_sec = 0.2
	var v: BulletVolley2D = factory.spawn_volley(d)
	assert_same(v.get_collision_shape(), d.collision_shape, "collision shape")
	assert_almost_eq(v.get_fade_in_sec(), 0.3, EPS, "fade in")
	assert_almost_eq(v.get_fade_out_sec(), 0.4, EPS, "fade out")
	assert_same(v.get_modulate_ramp(), ramp, "modulate ramp")
	assert_same(v.get_shared_bullet_curves_data(), curves, "shared curves")
	assert_same(v.get_shared_bullet_wobble_data(), wobble, "shared wobble")
	assert_eq(Array(v.get_effect_layers()), [layer], "effect layers")
	assert_eq([v.get_bounce_mode(), v.get_bounce_hit_consumed(), v.get_bounce_max_count(), v.get_bounce_rotate_texture()], [1, true, 3, false], "bounce knobs")
	assert_almost_eq(v.get_bounce_randomness_deg(), 7.0, EPS, "bounce randomness")
	assert_almost_eq(v.get_bounce_rotation_smooth(), 0.5, EPS, "bounce rotation smooth")
	assert_almost_eq(v.get_bounce_cooldown_sec(), 0.2, EPS, "bounce cooldown")
	assert_null(v.get_shared_movement_pattern_curve(), "no shared movement pattern")
