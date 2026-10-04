extends BlastTest
## Live edit paths: custom data shared vs per-bullet (strict), collision
## layer/mask/monitorable, runtime shape resize (same type) vs type change
## (re-bucket), timers attach/fire/detach/64-cap/zero-time, single-bullet
## enable/disable, sprite animation guards.

var v: BulletVolley2D
var _timer_fires := 0


func _on_timer() -> void:
	_timer_fires += 1


func before_each() -> void:
	await super()
	_timer_fires = 0
	v = quick_volley(2, 200.0)


func test_custom_data_separation() -> void:
	var shared := Resource.new()
	v.set_shared_bullets_custom_data(shared)
	assert_eq(v.get_shared_bullets_custom_data(), shared, "shared stored")
	assert_null(v.bullet_get_custom_data(0), "per-bullet reads null, never shared")
	var per := Resource.new()
	v.bullet_set_custom_data(0, per)
	assert_eq(v.bullet_get_custom_data(0), per, "per-bullet stored")
	assert_null(v.bullet_get_custom_data(1), "sibling unaffected")
	v.bullet_set_custom_data(-1, per)
	v.bullet_set_custom_data(99, per)
	expect_errors_containing("Invalid bullet index", 2)
	assert_null(v.bullet_get_custom_data(1), "OOB writes are no-ops")


func test_layers_mask_monitorable() -> void:
	v.set_collision_layer(8)
	assert_eq(v.get_collision_layer(), 8)
	v.set_collision_mask(16)
	assert_eq(v.get_collision_mask(), 16)
	v.set_monitorable(true)
	assert_true(v.get_monitorable())


func test_runtime_shape_resize_and_type_change() -> void:
	var before_type: int = v.debug_get_shape_state().get("type", -1)
	v.set_collision_shape_runtime(H.make_circle_shape(12.0))
	assert_eq(v.debug_get_shape_state().get("type", -1), before_type, "same-type resize keeps the type")
	assert_almost_eq(float(v.debug_get_shape_state().get("circle_radius", 0.0)), 12.0, 0.01, "radius applied")
	var rect := RectangleShape2D.new()
	rect.size = Vector2(20, 10)
	v.set_collision_shape_runtime(rect)
	assert_eq(v.debug_get_shape_state().get("type", -1), PhysicsServer2D.SHAPE_RECTANGLE, "type change applied at idle")
	assert_true(v.debug_get_shape_state().get("valid", false), "shape RIDs valid after the change")


func test_runtime_type_change_refused_inside_physics() -> void:
	await physics()
	var rect := RectangleShape2D.new()
	rect.size = Vector2(20, 10)
	v.set_collision_shape_runtime(rect)
	expect_error_sequence(["set_collision_shape_runtime cannot run inside a physics frame or while bullets are being processed (e.g. inside area_entered/body_entered handlers). Use set_collision_shape_runtime_deferred() instead: it runs on the next idle frame (a plain call_deferred() still runs inside the physics frame)."])
	assert_eq(v.debug_get_shape_state().get("type", -1), PhysicsServer2D.SHAPE_CIRCLE, "type unchanged")


func test_timers_attach_fire_detach_cap() -> void:
	assert_eq(v.debug_get_timer_count(), 0, "no timers initially")
	v.attach_time_based_function(0.05, _on_timer)
	assert_eq(v.debug_get_timer_count(), 1, "timer attached")
	await physics(15)
	assert_gte(_timer_fires, 1, "timer fired")
	await idle(1)
	v.detach_all_time_based_functions()
	assert_eq(v.debug_get_timer_count(), 0, "detach all clears")
	for i in 70:
		v.attach_time_based_function(10.0, func() -> void: pass)
	assert_eq(v.debug_get_timer_count(), 64, "timer cap is 64")
	expect_errors_containing("timer limit", 6)
	v.detach_all_time_based_functions()
	v.attach_time_based_function(0.0, _on_timer)
	expect_error("time value that is above 0")
	assert_eq(v.debug_get_timer_count(), 0, "zero-time timer rejected")


func test_single_bullet_enable_disable() -> void:
	v.disable_bullet(0)
	assert_false(v.is_bullet_status_enabled(0), "disable holds")
	assert_true(v.is_bullet_status_enabled(1), "sibling stays live")
	v.wake_bullet(0)
	assert_true(v.is_bullet_status_enabled(0), "wake revives")
	v.disable_bullet(0)
	v.disable_bullet(0)
	assert_true(factory.debug_assert_no_dangling().get("ok", false), "double disable safe")


func test_sprite_animation_guards() -> void:
	assert_true(v.restart_sprite_animation(), "restart works with baked frames")
	assert_true(v.play_sprite_animation_name("nope"), "unknown animation falls back to the first one")
	expect_error("missing animation 'nope'")
	assert_false(v.play_sprite_animation(null), "null frames fail loud")
	expect_error("is null")
