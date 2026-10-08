extends BlastTest
## Spawner Sound group: bound API and defaults, knobs gate in the inspector,
## setters reject and keep with exact texts, listener sources resolve exactly
## their nodes, arming reaches every fired volley, orphans keep the settings.


func after_all() -> void:
	# Shots in this suite play real (looped) voices: same headless-audio
	# drain as the other sound suites (resets already stopped every voice).
	OS.delay_msec(500)
	await super()


func _shot_volley(sp: BulletSpawner2D) -> BulletVolley2D:
	watch_signals(sp)
	var before: int = get_signal_emit_count(sp, "volley_fired")
	assert_true(sp.shoot_once(), "shot fires")
	assert_signal_emit_count(sp, "volley_fired", before + 1, "exactly once more")
	return get_signal_parameters(sp, "volley_fired", before)[0] as BulletVolley2D


func test_bind_and_defaults() -> void:
	var sp := make_spawner()
	for m in ["set_sound_enabled", "get_sound_enabled", "set_sound_effects", "get_sound_effects", "set_sound_volume_db", "get_sound_volume_db", "set_sound_listener_source", "get_sound_listener_source", "set_sound_listener_node_group", "set_sound_listener_filter_group", "set_sound_listener_path", "set_sound_listener_node_name", "set_sound_listener_node_name_match_mode", "set_sound_listener_node_name_case_sensitive", "set_sound_listener_children_parent_path", "set_sound_listener_children_recursive", "set_sound_listener_global_positions", "set_sound_listener_update_interval", "refresh_sound_listeners", "resolve_sound_listeners", "debug_get_sound_detector_stats"]:
		assert_has_method(sp, m, "bound " + m)
	assert_false(sp.sound_enabled, "sound off by default")
	assert_true((sp.sound_effects as Array).is_empty(), "no entries by default")
	assert_eq(sp.sound_volume_db, 0.0, "volume offset defaults 0")
	assert_eq(sp.sound_listener_source, BulletSpawner2D.SOUND_LISTENER_GODOT_LISTENER, "Godot listener by default")
	assert_eq(sp.sound_listener_update_interval, 0.0, "interval defaults 0")
	assert_eq(BulletSpawner2D.SOUND_LISTENER_NODE_GROUP, 1, "group id serialized")
	assert_eq(BulletSpawner2D.SOUND_LISTENER_GLOBAL_POSITIONS, 6, "positions id serialized")


func test_setters_reject_and_keep() -> void:
	var sp := make_spawner()
	sp.set_sound_listener_source(-1)
	expect_error_sequence(["BulletSpawner2D: invalid sound_listener_source, keeping the old value."])
	assert_eq(sp.sound_listener_source, BulletSpawner2D.SOUND_LISTENER_GODOT_LISTENER, "source kept")
	sp.set_sound_listener_source(7)
	expect_error_sequence(["BulletSpawner2D: invalid sound_listener_source, keeping the old value."])
	sp.set_sound_listener_node_name_match_mode(4)
	expect_error_sequence(["BulletSpawner2D: invalid sound_listener_node_name_match_mode, keeping the old value."])
	assert_eq(sp.sound_listener_node_name_match_mode, 1, "match mode kept")
	sp.set_sound_listener_update_interval(-1.0)
	expect_error_sequence(["BulletSpawner2D: sound_listener_update_interval must be finite and >= 0 (0 scans every tick), keeping the old value."])
	assert_eq(sp.sound_listener_update_interval, 0.0, "interval kept")
	sp.set_sound_listener_global_positions(PackedVector2Array([Vector2.INF]))
	expect_error_sequence(["BulletSpawner2D: sound_listener_global_positions must hold finite positions only, keeping the old value."])
	assert_true(sp.sound_listener_global_positions.is_empty(), "positions kept")
	sp.set_sound_volume_db(NAN)
	expect_error_sequence(["BulletSpawner2D: sound_volume_db must be finite, keeping the old value."])
	assert_eq(sp.sound_volume_db, 0.0, "volume kept")
	sp.set_sound_effects(["nope"] as Array)
	expect_error_sequence(["BulletSpawner2D: sound_effects entries must be BulletSoundData2D or null, keeping the old value."])
	assert_true((sp.sound_effects as Array).is_empty(), "effects kept")
	sp.set_sound_volume_db(-4.0)
	assert_eq(sp.sound_volume_db, -4.0, "valid volume applies")


func test_knobs_gate_in_the_inspector() -> void:
	var sp := make_spawner()
	assert_true(is_editor_visible(sp, &"sound_enabled"), "master switch visible")
	assert_false(is_editor_visible(sp, &"sound_effects"), "entries hidden while off")
	sp.sound_enabled = true
	assert_true(is_editor_visible(sp, &"sound_effects"), "entries visible while on")
	assert_true(is_editor_visible(sp, &"sound_listener_source"), "source picker visible")
	assert_false(is_editor_visible(sp, &"sound_listener_node_group"), "group knob hidden for Godot listener")
	assert_false(is_editor_visible(sp, &"sound_listener_filter_group"), "filter hidden for Godot listener")
	assert_false(is_editor_visible(sp, &"sound_listener_update_interval"), "interval hidden for Godot listener")
	sp.sound_listener_source = BulletSpawner2D.SOUND_LISTENER_NODE_GROUP
	assert_true(is_editor_visible(sp, &"sound_listener_node_group"), "group knob for Node Group")
	assert_true(is_editor_visible(sp, &"sound_listener_filter_group"), "filter for node sources")
	assert_true(is_editor_visible(sp, &"sound_listener_update_interval"), "interval for node sources")
	assert_false(is_editor_visible(sp, &"sound_listener_path"), "path hidden for Node Group")
	sp.sound_listener_source = BulletSpawner2D.SOUND_LISTENER_MOUSE
	assert_false(is_editor_visible(sp, &"sound_listener_filter_group"), "filter hidden for Mouse")
	sp.sound_listener_source = BulletSpawner2D.SOUND_LISTENER_GLOBAL_POSITIONS
	assert_true(is_editor_visible(sp, &"sound_listener_global_positions"), "positions for Global Positions")
	sp.sound_enabled = false
	assert_false(is_editor_visible(sp, &"sound_listener_source"), "picker hidden while off")


func test_shots_arm_their_volleys() -> void:
	var sp := make_spawner(H.make_volley_data(4, 0.0, 30.0), BulletSpawner2D.PATTERN_FROM_HELPER_RING, 4)
	var v0: BulletVolley2D = _shot_volley(sp)
	assert_true((v0.sound_get_effects() as Array).is_empty(), "nothing arms while sound is off")
	sp.sound_enabled = true
	var s := H.make_sound(BulletSoundData2D.SOUND_ON_SHOT)
	sp.sound_effects = [null, s]
	var v1: BulletVolley2D = _shot_volley(sp)
	var armed: Array = v1.sound_get_effects()
	assert_eq(armed.size(), 2, "nulls kept in place")
	assert_null(armed[0], "null stays null")
	assert_eq(armed[1], s, "entry armed")
	assert_true(v1.is_sound_armed(), "mask set")


func test_listener_sources_resolve_exactly_their_nodes() -> void:
	var sp := make_spawner()
	sp.sound_enabled = true
	assert_true((sp.resolve_sound_listeners() as Array).is_empty(), "Godot listener resolves empty")
	sp.sound_listener_source = BulletSpawner2D.SOUND_LISTENER_NODE_GROUP
	sp.sound_listener_node_group = &"listeners"
	var listener := make_listener(Vector2(100, 0))
	assert_eq(sp.resolve_sound_listeners(), [listener], "group finds its member")
	assert_eq(sp.refresh_sound_listeners(), 1, "refresh counts it")
	sp.sound_listener_filter_group = &"flt"
	assert_true((sp.resolve_sound_listeners() as Array).is_empty(), "filter excludes non-members")
	listener.add_to_group(&"flt")
	assert_eq(sp.refresh_sound_listeners(), 1, "refresh picks up the member")
	assert_eq(sp.resolve_sound_listeners(), [listener], "filter admits members")
	sp.sound_listener_filter_group = &""
	sp.sound_listener_source = BulletSpawner2D.SOUND_LISTENER_NODE_PATH
	sp.sound_listener_path = sp.get_path_to(listener)
	assert_eq(sp.resolve_sound_listeners(), [listener], "path finds its node")
	sp.sound_listener_source = BulletSpawner2D.SOUND_LISTENER_NODE_NAME
	listener.name = "SndListener42"
	sp.sound_listener_node_name = "SndListener"
	assert_eq(sp.resolve_sound_listeners(), [listener], "name finds its node")
	sp.sound_listener_source = BulletSpawner2D.SOUND_LISTENER_NODE_CHILDREN
	var parent := Node2D.new()
	add(parent)
	var child := Node2D.new()
	child.position = Vector2(50, 0)
	parent.add_child(child)
	sp.sound_listener_children_parent_path = sp.get_path_to(parent)
	assert_eq(sp.resolve_sound_listeners(), [child], "children finds its nodes")
	sp.sound_listener_source = BulletSpawner2D.SOUND_LISTENER_GLOBAL_POSITIONS
	sp.sound_listener_global_positions = PackedVector2Array([Vector2(30, 40)])
	assert_eq(sp.resolve_sound_listeners(), [Vector2(30, 40)], "positions resolve as vectors")
	sp.sound_listener_source = BulletSpawner2D.SOUND_LISTENER_MOUSE
	mouse_to(Vector2(200, 100))
	var resolved: Array = sp.resolve_sound_listeners()
	assert_eq(resolved.size(), 1, "mouse resolves one point")
	assert_almost_eq(resolved[0], Vector2(200, 100), Vector2(1.0, 1.0), "mouse follows the cursor")


func test_refresh_refused_outside_the_tree() -> void:
	var sp := BulletSpawner2D.new()
	assert_eq(sp.refresh_sound_listeners(), 0, "nothing refreshed")
	expect_error_sequence(["BulletSpawner2D.refresh_sound_listeners: spawner is outside the scene tree, nothing refreshed."])
	sp.free()


func test_setup_warnings_name_empty_settings() -> void:
	var sp := make_spawner()
	for w in sp.get_setup_warnings():
		assert_false(str(w).begins_with("Sound"), "no sound warnings while off: " + str(w))
	sp.sound_enabled = true
	var warnings: Array = []
	for w in sp.get_setup_warnings():
		warnings.append(str(w))
	assert_has(warnings, "Sound is enabled but sound_effects is empty: no bullet of this spawner plays sounds.", "empty entries named")
	sp.sound_effects = [H.make_sound(BulletSoundData2D.SOUND_ON_SHOT)]
	sp.sound_listener_source = BulletSpawner2D.SOUND_LISTENER_NODE_GROUP
	warnings = []
	for w in sp.get_setup_warnings():
		warnings.append(str(w))
	assert_has(warnings, "sound_listener_source is Node Group but sound_listener_node_group is empty: sounds measure from Godot's listener.", "empty group names the fallback")


func test_in_place_mutation_warns_once_and_still_arms() -> void:
	var sp := make_spawner(H.make_volley_data(2, 0.0, 30.0), BulletSpawner2D.PATTERN_FROM_HELPER_RING, 2)
	sp.sound_enabled = true
	var s := H.make_sound(BulletSoundData2D.SOUND_ON_SHOT)
	sp.sound_effects = [s]
	var live: Array = sp.sound_effects
	live.append("nope")
	var v: BulletVolley2D = _shot_volley(sp)
	expect_warning_sequence(["BulletSpawner2D: sound_effects was changed in place and holds entries that are not BulletSoundData2D; only sounds are used. Assign the whole array instead (sound_effects = [...])."])
	var armed: Array = v.sound_get_effects()
	assert_eq(armed.size(), 2, "indices kept, misuse nulled")
	assert_eq(armed[0], s, "usable entry still arms")


func test_orphans_keep_their_settings() -> void:
	var sp := make_spawner(H.make_volley_data(2, 0.0, 30.0), BulletSpawner2D.PATTERN_FROM_HELPER_RING, 2)
	sp.sound_enabled = true
	sp.sound_effects = [H.make_sound(BulletSoundData2D.SOUND_ON_SHOT)]
	var v: BulletVolley2D = _shot_volley(sp)
	sp.queue_free()
	await idle(1)
	assert_eq((v.sound_get_effects() as Array).size(), 1, "orphan keeps its entries")
	assert_true(v.is_sound_armed(), "orphan keeps its mask")


func test_listener_edits_reach_flying_volleys() -> void:
	var sp := make_spawner(H.make_volley_data(2, 0.0, 30.0), BulletSpawner2D.PATTERN_FROM_HELPER_RING, 2)
	sp.sound_enabled = true
	sp.sound_effects = [H.make_sound(BulletSoundData2D.SOUND_ON_SHOT)]
	sp.sound_listener_source = BulletSpawner2D.SOUND_LISTENER_NODE_GROUP
	sp.sound_listener_node_group = &"listeners"
	var v: BulletVolley2D = _shot_volley(sp)
	assert_eq(sp.refresh_sound_listeners(), 0, "nobody yet")
	make_listener(Vector2(60, 0))
	assert_eq(sp.refresh_sound_listeners(), 1, "newcomer counts after refresh")
	assert_eq((v.sound_get_effects() as Array).size(), 1, "flying volley still armed")


func test_orphan_volleys_still_voice() -> void:
	godot_listener_at(Vector2.ZERO)
	var sp := make_spawner(H.make_volley_data(2, 200.0, 30.0), BulletSpawner2D.PATTERN_FROM_HELPER_RING, 2)
	sp.sound_enabled = true
	var hum := H.make_sound(BulletSoundData2D.SOUND_ON_FLIGHT)
	hum.min_interval_sec = 0.0
	hum.max_voices = 4
	sp.sound_effects = [hum]
	var v: BulletVolley2D = _shot_volley(sp)
	sp.queue_free()
	await idle(1)
	await physics(4)
	assert_true(is_instance_valid(v), "orphan volley alive")
	assert_gt(busy_voices().size(), 0, "orphan flight still voices through the factory")
