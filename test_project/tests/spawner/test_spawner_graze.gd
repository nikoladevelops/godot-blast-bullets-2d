extends BlastTest
## BulletSpawner2D Graze group: knobs gate in the inspector, setters reject
## and keep with exact texts, every fired volley (single shots, bursts,
## pattern lists, pooled reuse) is armed before any shot signal, nothing
## arms while graze is off, events bubble spawner -> factory exactly once
## each (factory volleys never touch a spawner), a spawner never grazes
## its own bullets, zones edited or removed after a shot keep the flying
## volley's arming, presets and adoption keep graze settings, setup warnings
## name empty or groupless zones, and an array mutated in place still arms
## its usable zones with one warning.


func _spawner(zones: Array, speed: float = 0.0) -> BulletSpawner2D:
	var d := H.make_volley_data(1, speed, 60.0)
	d.collision_shape = H.make_circle_shape(4.0)
	var sp := make_spawner(d, BulletSpawner2D.PATTERN_FROM_SELF, 1)
	sp.graze_zones = zones
	sp.graze_enabled = true
	return sp


func _shoot(sp: BulletSpawner2D) -> BulletVolley2D:
	var got: Array = []
	sp.volley_fired.connect(func(v: BulletVolley2D, _n: int) -> void: got.append(v), CONNECT_ONE_SHOT)
	assert_true(sp.shoot_once(), "fired")
	return got[0] if not got.is_empty() else null


func test_graze_api_is_bound() -> void:
	var sp := make_spawner()
	for m in ["get_graze_enabled", "set_graze_enabled", "get_graze_zones", "set_graze_zones", "get_graze_show_preview", "set_graze_show_preview", "get_graze_preview_during_runtime", "set_graze_preview_during_runtime", "get_graze_preview_line_width", "set_graze_preview_line_width", "resolve_graze_targets", "debug_get_graze_preview_circles", "debug_get_graze_preview_stats"]:
		assert_true(sp.has_method(m), "BulletSpawner2D.%s is bound" % m)
	for s in ["bullet_grazed", "bullet_graze_exited"]:
		assert_true(sp.has_signal(s), "BulletSpawner2D.%s is declared" % s)
	assert_false(sp.graze_enabled, "off by default")
	assert_eq(sp.graze_zones, [], "no zones by default")
	assert_true(sp.graze_show_preview, "editor preview on by default")
	assert_false(sp.graze_preview_during_runtime, "runtime preview off by default")
	assert_eq(sp.graze_preview_line_width, 1.5, "line width")


func test_graze_knobs_gate_in_the_inspector() -> void:
	var sp := make_spawner()
	var knobs := ["graze_zones", "graze_show_preview", "graze_preview_during_runtime", "graze_preview_line_width"]
	assert_true(is_editor_visible(sp, &"graze_enabled"), "the master switch always shows")
	for k in knobs:
		assert_false(is_editor_visible(sp, StringName(k)), k + " hides while graze is off")
	sp.graze_enabled = true
	for k in knobs:
		assert_true(is_editor_visible(sp, StringName(k)), k + " shows with graze on")
	sp.graze_show_preview = false
	assert_false(is_editor_visible(sp, &"graze_preview_line_width"), "the width hides without a preview toggle")
	sp.graze_preview_during_runtime = true
	assert_true(is_editor_visible(sp, &"graze_preview_line_width"), "a runtime preview shows it again")


func test_setters_reject_and_keep() -> void:
	var sp := make_spawner()
	var z := H.make_graze_zone()
	sp.graze_zones = [z]
	sp.graze_zones = [z, z, z, z, z]
	# Script-side typing already refuses these literals at parse time; an
	# untyped array reaches the setter's own checks.
	sp.set_graze_zones([z, BulletSpeedData2D.new()] as Array)
	sp.set("graze_zones", [7])
	sp.graze_preview_line_width = NAN
	sp.graze_preview_line_width = 0.0
	expect_error_sequence([
		"BulletSpawner2D: graze_zones holds at most 4 zones, keeping the old value.",
		"BulletSpawner2D: graze_zones entries must be BulletGrazeZone2D or null, keeping the old value.",
		"BulletSpawner2D: graze_zones entries must be BulletGrazeZone2D or null, keeping the old value.",
		"BulletSpawner2D: graze_preview_line_width must be finite and > 0, keeping the old value.",
		"BulletSpawner2D: graze_preview_line_width must be finite and > 0, keeping the old value.",
	])
	assert_eq(sp.graze_zones, [z], "zones kept")
	assert_eq(sp.graze_preview_line_width, 1.5, "width kept")
	sp.graze_zones = [null, z, null, null]
	assert_eq(sp.graze_zones, [null, z, null, null], "four entries with nulls are accepted as they are")


func test_every_shot_arms_its_volley_before_any_shot_signal() -> void:
	var z := H.make_graze_zone()
	var sp := _spawner([null, z])
	sp.homing_enabled = true
	sp.homing_node_group = &"graze_nobody"
	var seen: Array = []
	sp.pre_shoot.connect(func(v: BulletVolley2D, _n: int) -> void: seen.append(["pre_shoot", v.is_graze_armed()]))
	sp.homing_targets_resolved.connect(func(v: BulletVolley2D, _t: Array) -> void: seen.append(["homing_targets_resolved", v.is_graze_armed()]))
	sp.volley_fired.connect(func(v: BulletVolley2D, _n: int) -> void: seen.append(["volley_fired", v.is_graze_armed(), v.get_graze_zones()]))
	sp.shoot_once()
	expect_warning_sequence(["homing"]) # the empty homing group is reported once
	assert_eq(seen[0], ["pre_shoot", false], "pre_shoot sees the bare volley (it may still change it)")
	assert_eq(seen[1], ["homing_targets_resolved", true], "armed before the homing signals")
	assert_eq(seen[2], ["volley_fired", true, [null, z]], "armed with the spawner's zones, nulls in place")


func test_nothing_arms_while_graze_is_off_or_has_no_zone() -> void:
	var sp := _spawner([H.make_graze_zone()])
	sp.graze_enabled = false
	assert_false(_shoot(sp).is_graze_armed(), "graze off")
	sp.graze_enabled = true
	sp.graze_zones = []
	assert_false(_shoot(sp).is_graze_armed(), "no zones")
	sp.graze_zones = [null, null]
	assert_false(_shoot(sp).is_graze_armed(), "only empty slots")


func test_events_bubble_spawner_then_factory_exactly_once_each() -> void:
	var t := make_graze_target(Vector2(10, 0))
	var z := H.make_graze_zone([20.0])
	var sp := _spawner([z])
	var order: Array = []
	sp.bullet_grazed.connect(func(target: Node2D, v: BulletVolley2D, i: int, zone: BulletGrazeZone2D, r: int) -> void: order.append(["spawner", target, v, i, zone, r]))
	factory.bullet_grazed.connect(func(target: Node2D, v: BulletVolley2D, i: int, zone: BulletGrazeZone2D, r: int) -> void: order.append(["factory", target, v, i, zone, r]))
	var v := _shoot(sp)
	step_factory(3)
	assert_eq(order, [["spawner", t, v, 0, z, 0], ["factory", t, v, 0, z, 0]], "the spawner first, then the factory, same payload, once each")


func test_a_spawner_only_listener_handles_the_graze() -> void:
	make_graze_target(Vector2(10, 0))
	var sp := _spawner([H.make_graze_zone([20.0])])
	var log := H.record_graze(sp)
	_shoot(sp)
	step_factory(2)
	assert_eq(H.graze_kinds(log), ["enter:0:0"], "the spawner alone is enough (no unhandled warning)")


func test_factory_volleys_never_fire_spawner_signals() -> void:
	make_graze_target(Vector2(10, 0))
	var sp := _spawner([H.make_graze_zone([20.0])])
	var spawner_log := H.record_graze(sp)
	var factory_log := H.record_graze(factory)
	var v := graze_volley(H.transforms_at([Vector2.ZERO]), 0.0)
	v.graze_set_zones([H.make_graze_zone([20.0])])
	step_factory(2)
	assert_eq(spawner_log, [], "the spawner stays silent for a factory volley")
	assert_eq(factory_log.size(), 1, "the factory reports it")


func test_a_spawner_never_grazes_its_own_bullets() -> void:
	var sp := _spawner([H.make_graze_zone([20.0])])
	sp.add_to_group(&"graze_targets")
	var other := make_spawner(H.make_volley_data(1, 0.0, 60.0), BulletSpawner2D.PATTERN_FROM_SELF, 1)
	other.position = Vector2(10, 0)
	other.add_to_group(&"graze_targets")
	other.graze_zones = [H.make_graze_zone([20.0])]
	other.graze_enabled = true
	var log := H.record_graze(factory)
	_shoot(sp)
	_shoot(other)
	step_factory(2)
	assert_eq(log.size(), 2, "each spawner's bullet grazed the OTHER spawner only")
	assert_eq([log[0][1], log[1][1]], [other, sp], "never its own")
	assert_eq(sp.resolve_graze_targets(0), [other], "resolve skips the spawner itself")


func test_bursts_and_pattern_lists_arm_every_volley() -> void:
	var sp := _spawner([H.make_graze_zone()])
	var armed: Array = []
	sp.volley_fired.connect(func(v: BulletVolley2D, _n: int) -> void: armed.append(v.is_graze_armed()))
	sp.burst_enabled = true
	sp.burst_count = 3
	sp.burst_interval_sec = 0.01
	sp.begin_burst()
	for i in 30:
		await idle(1)
		if armed.size() >= 3:
			break
	assert_eq(armed, [true, true, true], "every burst shot")
	armed.clear()
	sp.spawn_pattern_list([{"helper_bullets_amount": 1}, {"helper_bullets_amount": 1}], true)
	await idle(2)
	assert_eq(armed, [true, true], "every pattern-list shot")


func test_pooled_reuse_arms_a_fresh_state() -> void:
	make_graze_target(Vector2(10, 0))
	var sp := _spawner([H.make_graze_zone([20.0])])
	var log := H.record_graze(factory)
	var first := _shoot(sp)
	step_factory()
	first.disable_bullet(0)
	assert_eq(first.debug_get_life_state(), "pooled", "back in the pool")
	var second := _shoot(sp)
	assert_eq(second, first, "the pool handed back the same instance")
	step_factory()
	assert_eq(H.graze_kinds(log), ["enter:0:0", "enter:0:0"], "the new life grazes again")


func test_edits_after_a_shot_keep_the_flying_volley_armed() -> void:
	var t := make_graze_target(Vector2(100, 0))
	var z := H.make_graze_zone([20.0])
	var sp := _spawner([z])
	var log := H.record_graze(factory)
	var v := _shoot(sp)
	sp.graze_zones = []
	sp.graze_enabled = false
	assert_true(v.is_graze_armed(), "the flying volley keeps its zones")
	z.ring_1_radius = 120.0
	step_factory()
	assert_eq(H.graze_kinds(log), ["enter:0:0"], "and follows edits made inside the zone resource")
	assert_eq(log[0][1], t, "named")


func test_presets_keep_graze_settings() -> void:
	var z := H.make_graze_zone()
	var sp := _spawner([z])
	sp.graze_show_preview = false
	sp.apply_pattern_preset(1)
	assert_true(sp.graze_enabled, "graze stays on")
	assert_eq(sp.graze_zones, [z], "zones kept")
	assert_false(sp.graze_show_preview, "preview toggle kept")


func test_adoption_keeps_the_arming_and_moves_the_signals() -> void:
	make_graze_target(Vector2(10, 0))
	var z := H.make_graze_zone([20.0])
	var a := _spawner([z])
	var b := make_spawner(H.make_volley_data(1, 0.0, 60.0), BulletSpawner2D.PATTERN_FROM_SELF, 1)
	var log_a := H.record_graze(a)
	var log_b := H.record_graze(b)
	var v := _shoot(a)
	v.set_is_auto_pooling_enabled(false)
	v.disable_bullet(0)
	v.enable_bullet(0)
	assert_true(b.adopt_live_volley(v), "adopted")
	assert_eq(v.get_graze_zones(), [z], "adoption leaves the zones alone")
	step_factory()
	assert_eq(log_a, [], "the old owner hears nothing")
	assert_eq(H.graze_kinds(log_b), ["enter:0:0"], "the adopter does")


func test_setup_warnings_name_empty_and_groupless_zones() -> void:
	var sp := make_spawner()
	assert_eq(_graze_warnings(sp), [], "graze off: nothing")
	sp.graze_enabled = true
	assert_eq(_graze_warnings(sp), ["Graze is enabled but graze_zones is empty: no bullet of this spawner can be grazed."], "no zones")
	var groupless := H.make_graze_zone()
	groupless.target_group = &""
	sp.graze_zones = [null, groupless]
	assert_eq(_graze_warnings(sp), ["graze_zones[0] is empty.", "graze_zones[1] has an empty target_group: it never finds a target."], "each bad slot named")
	sp.graze_zones = [H.make_graze_zone()]
	assert_eq(_graze_warnings(sp), [], "a usable zone")


func _graze_warnings(sp: BulletSpawner2D) -> Array:
	var out: Array = []
	for w in sp.get_setup_warnings():
		if "graze" in w.to_lower():
			out.append(w)
	return out


func test_resolve_graze_targets_follows_the_runtime_filter() -> void:
	var a := make_graze_target(Vector2(1, 0))
	var b := make_graze_target(Vector2(2, 0))
	var dying := make_graze_target(Vector2(3, 0))
	dying.queue_free()
	var sp := _spawner([H.make_graze_zone(), null])
	assert_eq(sp.resolve_graze_targets(0), [a, b], "live Node2D members in tree order")
	assert_eq(sp.resolve_graze_targets(1), [], "an empty slot resolves nothing")
	sp.resolve_graze_targets(2)
	expect_error_sequence(["BulletSpawner2D.resolve_graze_targets: zone_index 2 is out of range."])


func test_an_array_mutated_in_place_still_arms_its_usable_zones_and_warns_once() -> void:
	make_graze_target(Vector2(10, 0))
	var z := H.make_graze_zone([20.0])
	var sp := _spawner([z])
	var zones: Array = sp.graze_zones
	for i in 4:
		zones.append(H.make_graze_zone([5.0], &"graze_nobody"))
	var log := H.record_graze(factory)
	var v := _shoot(sp)
	_shoot(sp)
	expect_warning_sequence(["BulletSpawner2D: graze_zones was changed in place and holds more than 4 zones or entries that are not BulletGrazeZone2D; only the first 4 zones are used. Assign the whole array instead (graze_zones = [...])."])
	assert_eq(v.get_graze_zones().size(), 4, "the first four arm")
	step_factory()
	assert_eq(log.size(), 2, "both volleys graze with the first zone")
