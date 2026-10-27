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


func test_surviving_parents_are_named_in_warnings_and_explain() -> void:
	var sp := make_spawner()
	sp.split_enabled = true
	var chain := _chain(3, false)
	var stage: BulletSplitStage2D = chain.stages[0]
	stage.split_on_hit = false
	stage.split_on_bounce = true
	stage.parent_survives = true
	sp.split_data = chain
	assert_true(_has(sp.debug_explain_features(), "A fired bullet bursts into 3 when it bounces. The bullet keeps flying after a split that does not kill it (at most 1 split(s) per bullet)."), "explain")
	assert_false(_has(sp.get_setup_warnings(), "has no max_splits_per_bullet"), "a limit: no warning")
	stage.max_splits_per_bullet = 0
	assert_true(_has(sp.get_setup_warnings(), "split_data.stages[0] keeps the parent and has no max_splits_per_bullet: a bullet splits again on every bounce, surviving hit, homing reach or graze."), "unlimited survivor named")
	stage.max_splits_per_bullet = 40000
	assert_true(_has(sp.get_setup_warnings(), "split_data can turn one fired bullet into 120000 bullets"), "the size estimate counts every split a survivor may make (3 x 40000)")


const ORPHAN_POLICIES := [
	BulletSpawner2D.ORPHANED_VOLLEYS_KEEP_FLYING,
	BulletSpawner2D.ORPHANED_VOLLEYS_HAND_TO_FACTORY,
	BulletSpawner2D.ORPHANED_VOLLEYS_CLEAR,
	BulletSpawner2D.ORPHANED_VOLLEYS_REMOVE,
]


## Two parents split into two long-lived children each (On Clear splits too:
## the orphan cleanup must never use it).
func _orphan_chain() -> BulletSplitData2D:
	var chain := _chain(2)
	chain.stages[0].split_on_clear = true
	chain.stages[0].child_data = H.make_volley_data(1, 0.0, 30.0)
	return chain


## Runs one orphan case; returns [factory splits, live bullets, On Split effects].
func _orphan_case(policy: int, timing: String) -> Array:
	factory_splits.clear()
	factory.debug_clear_effect_log()
	factory.debug_set_effect_log_enabled(true)
	var sp := _split_spawner(_orphan_chain(), 2, DT * 0.5 if timing != "before_capture" else 30.0)
	sp.spawn_data.effect_layers = [H.make_effect_layer(EffectLayerData2D.EFFECT_ON_SPLIT)]
	sp.orphaned_volleys = policy
	if timing == "carry_over":
		factory.split_max_children_per_sweep = 2 # one parent's children per sweep
	var trap_split: BulletVolley2D = null
	if timing == "during_flush":
		# A factory volley ticked (and split) BEFORE the spawner's: its
		# bullet_split handler frees the spawner while the spawner's splits
		# still wait in the running flush.
		var trap_data := H.make_volley_data(1, 0.0, DT * 0.5)
		trap_split = factory.spawn_volley(trap_data)
		var trap_chain := _chain(1)
		trap_chain.stages[0].child_data = H.make_volley_data(1, 0.0, 30.0)
		trap_split.split_set_data(trap_chain, trap_data)
	var v := _fire(sp)
	match timing:
		"during_flush":
			var doomed := [sp]
			factory.bullet_split.connect(func(_p, _c, _ch, _d) -> void:
				if is_instance_valid(doomed[0]):
					doomed[0].free(), CONNECT_ONE_SHOT)
			step_factory(3)
		"before_capture":
			sp.free() # the parents are orphans before they ever split
			v.set_life_time_left(0.001) # the survivors (Keep / Hand) split now
			step_factory(3)
		"between":
			# A factory volley ticked after the spawner's: its expiry handler
			# frees the spawner after the parents' splits were recorded and
			# before the flush spawns them.
			var trap_data := H.make_volley_data(1, 0.0, DT * 0.5)
			trap_data.is_life_time_over_signal_enabled = true
			var trap := factory.spawn_volley(trap_data)
			var doomed := [sp]
			factory.life_time_over.connect(func(volley, _i) -> void:
				if volley == trap and is_instance_valid(doomed[0]):
					doomed[0].free(), CONNECT_ONE_SHOT)
			step_factory(3)
		"carry_over":
			step_factory(1) # the first parent's children spawn, the second waits
			sp.free()
			step_factory(3)
	factory.split_max_children_per_sweep = 0
	var effects := 0
	for e in factory.debug_get_effect_log():
		if int(e.get("trigger", -1)) == EffectLayerData2D.EFFECT_ON_SPLIT:
			effects += 1
	return [factory_splits.size(), factory.get_active_bullet_count(), effects]


## orphaned_volleys decides what a freed spawner's queued splits become, at
## every moment it can be freed: before its bullets split, between a split
## being recorded and spawned, and with splits carried over to a later sweep.
## Keep Flying: silent children fly. Hand To Factory: factory-owned children,
## reported on the factory. Clear / Remove: nothing of the spawner survives,
## and the cleanup itself never splits.
func test_orphan_policies_reach_every_queued_split() -> void:
	# [factory splits, live bullets, On Split effects at the parents]
	var expected := {
		"before_capture": [[0, 4, 2], [2, 4, 2], [0, 0, 0], [0, 0, 0]],
		"between": [[0, 4, 2], [2, 4, 2], [0, 0, 2], [0, 0, 2]],
		"carry_over": [[0, 4, 2], [1, 4, 2], [0, 0, 2], [0, 0, 2]],
		# + the factory volley's own split (one report, one child)
		"during_flush": [[1, 5, 2], [3, 5, 2], [1, 1, 2], [1, 1, 2]],
	}
	var problems: Array = []
	for timing in expected:
		for p in ORPHAN_POLICIES.size():
			var got := _orphan_case(ORPHAN_POLICIES[p], timing)
			var want: Array = expected[timing][p]
			if got != want:
				problems.append("%s / policy %d: got %s, want %s" % [timing, p, got, want])
			factory.free_active_bullets()
			await idle(1)
	assert_eq("\n".join(problems), "", "every policy at every moment")



## Graze, sound, flock and split are armed BEFORE pre_shoot: its handler sees
## an armed volley, and what the handler sets instead sticks (the spawner's
## arming never overwrites it afterwards).
func test_pre_shoot_sees_the_arming_and_its_own_arming_sticks() -> void:
	var sp := _split_spawner(_chain(2), 1, 5.0)
	sp.flock_enabled = true
	sp.flock = H.make_flock(1.0)
	sp.graze_enabled = true
	sp.graze_zones = [H.make_graze_zone([20.0])]
	sp.graze_node_group = &"pre_shoot_grazers"
	sp.sound_enabled = true
	sp.sound_effects = [H.make_sound(BulletSoundData2D.SOUND_ON_HIT)]
	var mine_chain := _chain(3)
	var mine_flock := H.make_flock(2.0)
	var mine_sound := H.make_sound(BulletSoundData2D.SOUND_ON_CLEAR)
	var seen: Array = []
	sp.pre_shoot.connect(func(v: BulletVolley2D, _n: int) -> void:
		seen.append([v.split_get_data() == sp.split_data, v.flock_get() == sp.flock, int(v.debug_get_graze_info()["zone_slots"]), v.sound_get_effects().size()])
		v.split_set_data(mine_chain, H.make_volley_data(1, 0.0, 5.0))
		v.flock_join(mine_flock)
		v.graze_set_zones([H.make_graze_zone([10.0]), H.make_graze_zone([30.0])], &"mine")
		v.sound_set_effects([mine_sound]))
	var v := _fire(sp)
	expect_no_errors()
	assert_eq(seen, [[true, true, 1, 1]], "pre_shoot sees every feature armed")
	assert_eq(v.split_get_data(), mine_chain, "the handler's chain stuck")
	assert_eq(v.flock_get(), mine_flock, "the handler's flock stuck")
	assert_eq(int(v.debug_get_graze_info()["zone_slots"]), 2, "the handler's zones stuck")
	assert_eq(v.sound_get_effects(), [mine_sound], "the handler's sounds stuck")



## Seeded splits are salted with the shot number and the parent's earlier
## splits: two shots of one spawner split differently, a surviving parent's
## second split differs from its first, and the same seed with the same
## events (a fresh world) repeats exactly.
func test_seeded_splits_differ_per_shot_and_per_repeat_split() -> void:
	var runs: Array = []
	for run in 2:
		if run > 0:
			factory.free()
			factory = BulletFactory2D.new()
			add_child_autofree(factory)
			await idle(1)
		var chain := _chain(4, false)
		var stage: BulletSplitStage2D = chain.stages[0]
		stage.direction_mode = BulletSplitStage2D.DIRECTION_RANDOM_IN_SPREAD
		stage.seed = 5
		stage.parent_survives = true
		stage.max_splits_per_bullet = 0
		stage.child_data = H.make_volley_data(1, 0.0, 30.0)
		var sp := _split_spawner(chain, 1, 30.0)
		var children: Array = []
		sp.bullet_split.connect(func(_p, _c, ch, _d) -> void: children.append(ch))
		var first := _fire(sp)
		var second := _fire(sp)
		first.split_bullet(0)
		second.split_bullet(0)
		step_factory(1)
		first.split_bullet(0) # the surviving parent's second split
		step_factory(1)
		var angles: Array = []
		for ch in children:
			var a: Array = []
			for i in ch.get_amount_bullets():
				a.append(snappedf(ch.get_bullet_direction(i).angle(), 0.0001))
			angles.append(a)
		runs.append(angles)
		sp.free()
	assert_eq(runs[0].size(), 3, "three splits")
	assert_ne(runs[0][0], runs[0][1], "the second shot splits another way")
	assert_ne(runs[0][0], runs[0][2], "a repeat split of one parent splits another way")
	assert_eq(runs[0], runs[1], "same seed, same events: the same splits")


## A stage edit is a chain edit: the chain relays its stages' `changed` (only
## the stages it holds now), and the spawner listens to its chain (only the
## one it holds now), like graze zones.
func test_stage_edits_reach_the_chain_and_the_spawner() -> void:
	var chain := _chain(2)
	var stage: BulletSplitStage2D = chain.stages[0]
	var seen := [0]
	chain.changed.connect(func() -> void: seen[0] += 1)
	stage.count = 5
	assert_eq(seen[0], 1, "the chain re-emits its stage's changed")
	var old_stage := stage
	chain.stages = [BulletSplitStage2D.new()]
	seen[0] = 0
	old_stage.count = 6
	assert_eq(seen[0], 0, "a replaced stage is no longer relayed")
	var sp := make_spawner()
	sp.split_data = chain
	assert_eq(chain.changed.get_connections().filter(func(c): return c["callable"].get_object() == sp).size(), 1, "the spawner listens to its chain")
	sp.split_data = _chain(2)
	assert_eq(chain.changed.get_connections().filter(func(c): return c["callable"].get_object() == sp).size(), 0, "and lets a replaced one go")


func test_orphaned_children_home_like_their_parent() -> void:
	# The spawner set its volleys' homing steering (texture control,
	# smoothing, reach); a split after the spawner is gone hands the
	# children the parent's target AND that steering (they used to get the
	# spawn data's defaults: homing that cannot steer, plus a warning).
	var target := Node2D.new()
	target.position = Vector2(0, 300)
	add(target)
	target.add_to_group(&"orphan_split_targets")
	var chain := _chain(2, false)
	(chain.stages[0] as BulletSplitStage2D).split_after_sec = 0.2
	var sp := _split_spawner(chain, 1, 5.0)
	sp.helper_ring_radius = 0.0
	(sp.spawn_data as BulletVolleyData2D).shared_bullet_speed_data = H.make_speed(120.0)
	(sp.spawn_data as BulletVolleyData2D).all_bullet_speed_data = []
	sp.orphaned_volleys = BulletSpawner2D.ORPHANED_VOLLEYS_HAND_TO_FACTORY
	sp.homing_node_group = &"orphan_split_targets"
	sp.homing_take_control_of_texture_rotation = true
	sp.homing_smoothing = 7.0
	sp.homing_distance_before_reached = 9.0
	sp.homing_retarget_interval_sec = 1000.0
	sp.set_homing_enabled(true)
	var parent := _fire(sp)
	assert_true(parent.homing_take_control_of_texture_rotation, "the spawner steers its volley")
	step_factory(2)
	sp.free() # orphaned before the split
	for k in 15:
		factory.debug_advance_time(DT)
	assert_eq(factory_splits.size(), 1, "the orphan split (handed to the factory)")
	var c: BulletVolley2D = factory_splits[0][2]
	assert_eq(c.shared_homing_deque_get_current_homing_target(), target, "the children chase the parent's target")
	assert_true(c.homing_take_control_of_texture_rotation, "with the parent's texture control")
	assert_almost_eq(c.get_homing_smoothing(), 7.0, 1e-5, "its smoothing")
	assert_almost_eq(c.get_homing_distance_before_reached(), 9.0, 1e-5, "its reach")
	var before: Vector2 = c.get_bullet_direction(0)
	for k in 10:
		factory.debug_advance_time(DT)
	assert_gt(absf(angle_difference(before.angle(), c.get_bullet_direction(0).angle())), 0.05, "and they really turn toward it")
	expect_no_errors("no inert-homing warning")
