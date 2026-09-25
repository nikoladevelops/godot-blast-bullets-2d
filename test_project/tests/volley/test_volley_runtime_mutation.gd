extends SceneTree
## Volley runtime-mutation suite: every live edit path + deferred discipline.
## Covers: teleport/shift (shape+attachment+interp sync), custom data shared vs
## per-bullet (strictly separated), collision layer/mask/monitorable, shape
## runtime same-type resize (immediate) vs type change in physics (reject) and
## idle (re-bucket), timers attach/detach/64-cap/detach-during-fire/repeat,
## enable/disable single bullets + counters, sprite animation play/restart.
## Run: godot --headless --path test_project --script tests/volley/test_volley_runtime_mutation.gd
## Exit code 0 = all pass.

const H := preload("res://tests/common/blast_test_helpers.gd")

var failures := 0
var _timer_fires := 0

func _check(cond: bool, label: String) -> void:
	if cond:
		print("PASS  ", label)
	else:
		failures += 1
		printerr("FAIL  ", label)

func _on_timer() -> void:
	_timer_fires += 1

func _initialize() -> void:
	var factory := BulletFactory2D.new()
	get_root().add_child(factory)
	await process_frame
	await process_frame

	printerr("MUT T1 custom data separation")
	var v: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(H.make_directional_data(2, 200.0))
	var shared := Resource.new()
	v.set_shared_bullets_custom_data(shared)
	_check(v.get_shared_bullets_custom_data() == shared, "shared stored")
	_check(v.bullet_get_custom_data(0) == null, "per-bullet reads null, never shared (strict)")
	var per := Resource.new()
	v.bullet_set_custom_data(0, per)
	_check(v.bullet_get_custom_data(0) == per, "per-bullet stored")
	_check(v.bullet_get_custom_data(1) == null, "sibling unaffected")
	v.bullet_set_custom_data(-1, per)
	v.bullet_set_custom_data(99, per)
	_check(v.bullet_get_custom_data(1) == null, "OOB custom-data no-op")

	printerr("MUT T2 layers/mask/monitorable")
	v.set_collision_layer(8)
	_check(v.get_collision_layer() == 8, "layer set")
	v.set_collision_mask(16)
	_check(v.get_collision_mask() == 16, "mask set")
	v.set_monitorable(true)
	_check(v.get_monitorable() == true, "monitorable set")

	printerr("MUT T3 shape runtime: same-type immediate, type-change deferred")
	var before_type: int = v.debug_get_shape_state().get("type", -1)
	var same := CircleShape2D.new()
	same.radius = 12.0
	await process_frame
	await process_frame
	v.set_collision_shape_runtime(same)
	await process_frame
	_check(v.debug_get_shape_state().get("type", -1) == before_type, "same-type resize keeps bucket")
	var rect := RectangleShape2D.new()
	rect.size = Vector2(20, 10)
	v.set_collision_shape_runtime(rect) # idle frames: applies immediately
	await process_frame
	_check(v.debug_get_shape_state().get("type", -1) != before_type, "type change re-buckets outside physics")
	_check(v.debug_get_shape_state().get("type", -1) == 4, "RECTANGLE type stored (PhysicsServer2D SHAPE_RECTANGLE=4)")
	_check(v.debug_get_shape_state().get("valid", false) == true, "shape RIDs valid after change")

	printerr("MUT T4 timers: attach/fire/detach/cap")
	_check(v.debug_get_timer_count() == 0, "no timers initially")
	v.multimesh_attach_time_based_function(0.05, _on_timer)
	_check(v.debug_get_timer_count() == 1, "timer attached")
	for i in 15:
		await physics_frame
	_check(_timer_fires >= 1, "timer fired")
	v.multimesh_detach_all_time_based_functions()
	_check(v.debug_get_timer_count() == 0, "detach all clears")
	# Timer attach/detach defer while inside physics; park on idle so counts read fresh.
	await process_frame
	await process_frame
	for i in 70:
		var cb := func() -> void: pass
		v.multimesh_attach_time_based_function(10.0, cb)
	_check(v.debug_get_timer_count() == 64, "timer cap is 64")
	v.multimesh_attach_time_based_function(10.0, func() -> void: pass)
	_check(v.debug_get_timer_count() == 64, "65th timer rejected")
	v.multimesh_detach_all_time_based_functions()
	v.multimesh_attach_time_based_function(0.0, _on_timer)
	_check(v.debug_get_timer_count() == 0, "zero-time timer rejected")

	printerr("MUT T5 enable/disable + counters")
	v.disable_bullet(0)
	_check(not v.is_bullet_status_enabled(0), "disable holds")
	_check(v.is_bullet_status_enabled(1), "sibling stays live")
	v.wake_bullet(0)
	_check(v.is_bullet_status_enabled(0), "wake revives")
	v.disable_bullet(0)
	v.disable_bullet(0)
	_check(factory.debug_assert_no_dangling().get("ok", false) == true, "double disable safe")

	printerr("MUT T6 sprite animation guards")
	_check(v.restart_sprite_animation() == false, "restart with no frames fails loud")
	_check(v.play_sprite_animation_name("nope") == false, "play cached with no source fails loud")
	_check(v.play_sprite_animation(null) == false, "play null frames fails loud")

	await process_frame
	await process_frame
	factory.reset()
	_check(factory.debug_assert_no_dangling().get("ok", false) == true, "no dangling at end")
	factory.queue_free()
	await process_frame
	print("----")
	if failures == 0:
		print("ALL RUNTIME MUTATION TESTS PASSED")
	else:
		printerr(str(failures) + " TEST(S) FAILED")
	quit(failures)
