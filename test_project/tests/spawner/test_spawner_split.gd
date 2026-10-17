extends BlastTest
## The spawner's Splitting group: split_enabled + split_data sit between
## Flocking and Graze, split_data hides while off, setup warnings name an
## empty chain or stage; fired volleys are armed before volley_fired;
## children copy the spawner's spawn data, stay owned by the spawner (their
## bullet_split / hits on the spawner, never the factory, counted as its
## volleys) and keep what the stage keeps (custom data, hit count, graze,
## sound, flock, homing re-aimed by the spawner); turning splitting off
## stops it for bullets in flight (on again resumes it) and bullets fired
## while off never split; orphan policies apply to children; the explain
## readout has a Splitting line; a saved scene keeps every setting.

const DT := 1.0 / 60.0
const SCENE_PATH := "user://split_spawner_round_trip.tscn"

var spawner_splits: Array = []
var factory_splits: Array = []


func before_each() -> void:
	await super()
	spawner_splits.clear()
	factory_splits.clear()
	factory.bullet_split.connect(func(pos, cause, children, depth): factory_splits.append([pos, cause, children, depth]))


func _chain(count := 2, lifetime_split := true) -> BulletSplitData2D:
	var s := BulletSplitStage2D.new()
	s.count = count
	s.split_on_lifetime_over = lifetime_split
	var d := BulletSplitData2D.new()
	d.stages = [s]
	return d


## A spawner of `amount` still bullets that expire after one tick, splitting by `chain`.
func _split_spawner(chain: BulletSplitData2D, amount := 2, lifetime := DT * 0.5) -> BulletSpawner2D:
	var sp := make_spawner(H.make_volley_data(amount, 0.0, lifetime), BulletSpawner2D.PATTERN_FROM_HELPER_RING, amount)
	sp.split_enabled = true
	sp.split_data = chain
	sp.bullet_split.connect(func(pos, cause, children, depth): spawner_splits.append([pos, cause, children, depth]))
	return sp


func _fire(sp: BulletSpawner2D) -> BulletVolley2D:
	var got: Array = []
	sp.volley_fired.connect(func(v: BulletVolley2D, _n: int) -> void: got.append(v), CONNECT_ONE_SHOT)
	assert_true(sp.shoot_once(), "fires")
	return got[0] if got.size() > 0 else null


func _has(lines: PackedStringArray, fragment: String) -> bool:
	for line in lines:
		if fragment in line:
			return true
	return false


func test_the_splitting_group_sits_after_flocking_and_gates_its_chain() -> void:
	var sp := make_spawner()
	assert_false(sp.split_enabled, "off by default")
	assert_null(sp.split_data, "no chain by default")
	var group := ""
	var found := {}
	for p in sp.get_property_list():
		var name := str(p["name"])
		if int(p["usage"]) & PROPERTY_USAGE_GROUP:
			group = name
		elif name in ["split_enabled", "split_data"]:
			found[name] = [group, str(p["hint_string"])]
	assert_eq(found.get("split_enabled", [""])[0], "Splitting", "split_enabled in Splitting")
	assert_eq(found.get("split_data", ["", ""]), ["Splitting", "BulletSplitData2D"], "split_data takes a BulletSplitData2D")
	assert_false(is_editor_visible(sp, &"split_data"), "hidden while off")
	sp.split_enabled = true
	assert_true(is_editor_visible(sp, &"split_data"), "shown while on")


func test_setup_warnings_name_an_empty_chain_or_stage() -> void:
	var sp := make_spawner()
	assert_false(_has(sp.get_setup_warnings(), "split"), "off: nothing to say")
	sp.split_enabled = true
	assert_true(_has(sp.get_setup_warnings(), "split_enabled is on but split_data is empty: nothing splits. Assign a BulletSplitData2D."), "no chain")
	sp.split_data = BulletSplitData2D.new()
	assert_true(_has(sp.get_setup_warnings(), "split_data has no stages: nothing splits. Add a BulletSplitStage2D per generation."), "no stages")
	sp.split_data.stages = [BulletSplitStage2D.new(), null]
	assert_true(_has(sp.get_setup_warnings(), "split_data.stages[1] is empty: generation 1 and every later one never split."), "an empty entry")
	sp.split_data.stages = [BulletSplitStage2D.new()]
	assert_false(_has(sp.get_setup_warnings(), "split"), "a full chain: no warning")


func test_fired_volleys_are_armed_before_volley_fired() -> void:
	var chain := _chain()
	var sp := _split_spawner(chain)
	var seen: Array = []
	sp.volley_fired.connect(func(v: BulletVolley2D, _n: int) -> void: seen.append([v.split_get_data(), v.split_get_depth()]))
	sp.shoot_once()
	assert_eq(seen, [[chain, 0]], "the handler sees the armed chain, generation 0")


func test_children_belong_to_the_spawner_and_signal_only_there() -> void:
	var sp := _split_spawner(_chain(3))
	var v := _fire(sp)
	step_factory(1)
	assert_eq(spawner_splits.size(), 2, "both bullets split, reported on the spawner")
	assert_eq(factory_splits.size(), 0, "never on the factory")
	var children: BulletVolley2D = spawner_splits[0][2]
	assert_eq(int(children.debug_get_volley_info()["owner_spawner_id"]), sp.get_instance_id(), "owned by the spawner")
	assert_eq(children.get_amount_bullets(), 3, "count")
	assert_ne(children, v, "a new volley")
	assert_eq(sp.get_active_live_bullet_count(), 6, "the six children count as the spawner's live bullets")


func test_off_stops_splits_in_flight_and_old_shots_never_arm() -> void:
	var sp := _split_spawner(_chain(), 2, 0.1)
	_fire(sp)
	sp.split_enabled = false
	step_factory(10)
	assert_eq(spawner_splits.size(), 0, "off: bullets in flight expire without splitting")
	sp.split_enabled = true
	_fire(sp)
	step_factory(10)
	assert_eq(spawner_splits.size(), 2, "on: the next shot splits")
	spawner_splits.clear()
	sp.split_enabled = false
	var plain := _fire(sp)
	sp.split_enabled = true
	assert_null(plain.split_get_data(), "fired while off: never armed")
	step_factory(10)
	assert_eq(spawner_splits.size(), 0, "and never splits")


func test_children_copy_the_spawn_data_and_keep_what_the_stage_keeps() -> void:
	var chain := _chain(2)
	var stage: BulletSplitStage2D = chain.stages[0]
	stage.keep_collision_count = true
	var data := H.make_volley_data(1, 0.0, DT * 0.5)
	data.bullet_max_collision_count = 5
	var sp := make_spawner(data, BulletSpawner2D.PATTERN_FROM_HELPER_RING, 1)
	sp.split_enabled = true
	sp.split_data = chain
	sp.bullet_split.connect(func(pos, cause, children, depth): spawner_splits.append([pos, cause, children, depth]))
	sp.graze_enabled = true
	sp.graze_zones = [H.make_graze_zone([30.0])]
	sp.graze_node_group = &"graze_targets"
	sp.sound_enabled = true
	sp.sound_effects = [H.make_sound(BulletSoundData2D.SOUND_ON_HIT)]
	var flock := H.make_flock(1.0)
	sp.flock_enabled = true
	sp.flock = flock
	var v := _fire(sp)
	assert_eq(v.sound_get_effects().size(), 1, "the parent carries the sound")
	var tag := Resource.new()
	v.bullet_set_custom_data(0, tag)
	v.set_bullet_collision_count(0, 2)
	step_factory(1)
	assert_eq(spawner_splits.size(), 1, "split")
	var c: BulletVolley2D = spawner_splits[0][2]
	assert_eq(c.get_bullet_max_collision_count(), 5, "the spawn data's settings")
	assert_eq([c.bullet_get_custom_data(0), c.bullet_get_custom_data(1)], [tag, tag], "custom data kept")
	assert_eq([c.get_bullet_collision_count(0), c.get_bullet_collision_count(1)], [2, 2], "hit count kept (keep_collision_count)")
	assert_true(bool(c.debug_get_graze_info()["armed"]), "graze kept")
	assert_eq(c.sound_get_effects().size(), 1, "sound kept")
	assert_eq(c.flock_get(), flock, "flock kept")
	# Nothing kept.
	spawner_splits.clear()
	for prop in ["keep_custom_data", "keep_collision_count", "keep_graze", "keep_sound", "keep_flock"]:
		stage.set(prop, false)
		assert_eq(stage.get(prop), false, prop + " off")
	var w := _fire(sp)
	w.bullet_set_custom_data(0, tag)
	w.set_bullet_collision_count(0, 2)
	step_factory(1)
	var d: BulletVolley2D = spawner_splits[0][2]
	assert_ne(d.bullet_get_custom_data(0), tag, "custom data dropped")
	assert_eq(d.get_bullet_collision_count(0), 0, "fresh hit count")
	assert_false(bool(d.debug_get_graze_info()["armed"]), "no graze")
	assert_eq(d.sound_get_effects().size(), 0, "no sound")
	assert_null(d.flock_get(), "no flock")


func test_keep_homing_lets_the_spawner_aim_the_children() -> void:
	var target := Node2D.new()
	target.position = Vector2(300, 0)
	add(target)
	target.add_to_group(&"split_targets")
	var sp := _split_spawner(_chain(2), 1)
	sp.set_homing_enabled(true)
	sp.homing_node_group = &"split_targets"
	_fire(sp)
	step_factory(1)
	var c: BulletVolley2D = spawner_splits[0][2]
	assert_eq(c.shared_homing_deque_get_current_homing_target(), target, "children chase the spawner's target")
	(sp.split_data.stages[0] as BulletSplitStage2D).keep_homing = false
	spawner_splits.clear()
	_fire(sp)
	step_factory(1)
	assert_null((spawner_splits[0][2] as BulletVolley2D).shared_homing_deque_get_current_homing_target(), "keep_homing off: no target")


func test_orphan_policies_reach_the_children() -> void:
	# Hand To Factory: children of an orphan are factory volleys, reported on the factory.
	var sp := _split_spawner(_chain(2), 1, 0.05)
	sp.orphaned_volleys = BulletSpawner2D.ORPHANED_VOLLEYS_HAND_TO_FACTORY
	_fire(sp)
	sp.free()
	step_factory(5)
	assert_eq(factory_splits.size(), 1, "the orphan split, reported on the factory")
	assert_eq(int((factory_splits[0][2] as BulletVolley2D).debug_get_volley_info()["owner_spawner_id"]), 0, "factory-owned children")
	# Keep Flying: the orphan splits silently (its spawner is gone), children keep flying.
	factory_splits.clear()
	var keep := _split_spawner(_chain(2), 1, 0.05)
	keep.orphaned_volleys = BulletSpawner2D.ORPHANED_VOLLEYS_KEEP_FLYING
	_fire(keep)
	keep.free()
	step_factory(5)
	assert_eq(factory_splits.size(), 0, "silent, like an orphan's hits")
	assert_eq(factory.get_active_bullet_count(), 2, "but the two children fly")


func test_explain_has_a_splitting_line() -> void:
	var sp := make_spawner()
	assert_true(_has(sp.debug_explain_features(), "Splitting: off."), "off")
	sp.split_enabled = true
	assert_true(_has(sp.debug_explain_features(), "Splitting: on, no stages: nothing splits."), "empty")
	sp.split_data = _chain(4)
	assert_true(_has(sp.debug_explain_features(), "Splitting: on, 1 generation(s) split. A fired bullet bursts into 4 when it is killed by a hit, expires."), "the first stage in words")


func test_a_saved_scene_keeps_the_splitting_settings() -> void:
	var sp := make_spawner()
	sp.split_enabled = true
	var chain := _chain(5)
	chain.repeat_last_stage = true
	chain.max_depth = 3
	sp.split_data = chain
	var holder := Node2D.new()
	add(holder)
	sp.reparent(holder)
	sp.owner = holder
	var scene := PackedScene.new()
	assert_eq(scene.pack(holder), OK, "packed")
	assert_eq(ResourceSaver.save(scene, SCENE_PATH), OK, "saved")
	var loaded := ResourceLoader.load(SCENE_PATH, "", ResourceLoader.CACHE_MODE_IGNORE) as PackedScene
	DirAccess.remove_absolute(ProjectSettings.globalize_path(SCENE_PATH))
	var copy := loaded.instantiate()
	var back := copy.get_child(0) as BulletSpawner2D
	assert_not_null(back, "the spawner came back")
	assert_true(back.split_enabled, "switch")
	assert_eq([back.split_data.stages.size(), back.split_data.repeat_last_stage, back.split_data.max_depth], [1, true, 3], "chain")
	assert_eq((back.split_data.stages[0] as BulletSplitStage2D).count, 5, "stage")
	copy.free()


func test_setup_warning_names_an_exploding_chain() -> void:
	var sp := make_spawner()
	sp.split_enabled = true
	var chain := _chain(2)
	chain.repeat_last_stage = true
	chain.max_depth = 64
	sp.split_data = chain
	assert_true(_has(sp.get_setup_warnings(), "split_data can turn one fired bullet into more than 1000000000000 bullets over its generations"), "2^64 named, never capped")
	chain.max_depth = 4
	assert_false(_has(sp.get_setup_warnings(), "can turn one fired bullet"), "2 + 4 + 8 + 16 = 30: fine")
	(chain.stages[0] as BulletSplitStage2D).count = 400
	chain.max_depth = 2
	assert_true(_has(sp.get_setup_warnings(), "split_data can turn one fired bullet into 160400 bullets over its generations"), "400 + 400 x 400")
