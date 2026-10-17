extends BlastTest
## BulletSplitStage2D and BulletSplitData2D as resources: documented defaults,
## inspector order and gating, `changed` only on accepted edits, exact reject
## texts, the stage lookup per generation (chain end, repeat_last_stage up to
## max_depth, empty entries), the child angle and speed tables, presets, and a
## .tres round trip of a whole chain. The generic NaN/range sweep lives in
## test_data_setter_contract.

const TRES_PATH := "user://split_round_trip.tres"
const STAGE_PROPS := ["split_on_hit", "split_on_lifetime_over", "split_after_sec", "split_on_despawn", "split_on_clear",
	"split_on_bounce", "split_on_non_lethal_hit", "split_on_homing_reached", "split_on_graze",
	"count", "chance", "seed", "direction_mode", "spread_degrees", "angle_offset_degrees", "relative_to_parent_heading",
	"angle_jitter_degrees", "speed_mode", "speed_multiplier_min", "speed_multiplier_max", "speed_distribution", "fixed_speed",
	"spawn_offset_px", "scale_multiplier", "child_data", "keep_custom_data", "keep_collision_count", "keep_homing",
	"keep_graze", "keep_sound", "keep_flock", "inherit_parent_velocity", "fire_death_effects", "parent_survives",
	"max_splits_per_bullet"]


func _count_changed(r: Resource) -> Array:
	var counter := [0]
	r.changed.connect(func(): counter[0] += 1)
	return counter


func _editor_props(cls: String) -> Array:
	var out: Array = []
	for p in ClassDB.class_get_property_list(cls, true):
		if int(p["usage"]) & PROPERTY_USAGE_STORAGE:
			out.append(str(p["name"]))
	return out


func _visible(obj: Object, prop: String) -> bool:
	for p in obj.get_property_list():
		if str(p["name"]) == prop:
			return (int(p["usage"]) & PROPERTY_USAGE_EDITOR) != 0
	return false


func test_stage_defaults() -> void:
	var s := BulletSplitStage2D.new()
	assert_eq([s.split_on_hit, s.split_on_lifetime_over, s.split_after_sec, s.split_on_despawn, s.split_on_clear],
		[true, true, 0.0, false, false], "hit and expiry split; no timer; despawn and clear never split by default")
	assert_eq([s.count, s.chance, s.seed], [3, 1.0, 0], "three children, always, global RNG")
	assert_eq([s.direction_mode, s.spread_degrees, s.angle_offset_degrees, s.relative_to_parent_heading, s.angle_jitter_degrees],
		[BulletSplitStage2D.DIRECTION_FAN, 60.0, 0.0, true, 0.0], "a 60 degree fan around the parent's heading")
	assert_eq([s.speed_mode, s.speed_multiplier_min, s.speed_multiplier_max, s.speed_distribution, s.fixed_speed],
		[BulletSplitStage2D.SPEED_MULTIPLY_PARENT, 1.0, 1.0, BulletSplitStage2D.SPEED_RAMP, 100.0], "the parent's speed")
	assert_eq([s.spawn_offset_px, s.scale_multiplier], [0.0, 1.0], "same spot, same size")
	assert_null(s.child_data, "the parent's data")
	assert_eq([s.keep_custom_data, s.keep_collision_count, s.keep_homing, s.keep_graze, s.keep_sound, s.keep_flock, s.inherit_parent_velocity],
		[true, false, true, true, true, true, false], "keep everything but the hit count and inherited momentum")
	assert_true(s.fire_death_effects, "the parent's own death effects still fire")
	assert_eq([s.split_on_bounce, s.split_on_non_lethal_hit, s.split_on_homing_reached, s.split_on_graze], [false, false, false, false], "the non-lethal triggers are opt-in")
	assert_eq([s.parent_survives, s.max_splits_per_bullet], [false, 1], "children replace the parent; a surviving parent splits once")


func test_stage_inspector_order_is_pinned() -> void:
	assert_eq(_editor_props("BulletSplitStage2D"), STAGE_PROPS, "stage properties in inspector (and .tres) order")
	assert_eq(_editor_props("BulletSplitData2D"), ["stages", "repeat_last_stage", "max_depth"], "chain properties")


func test_stage_gating_follows_the_modes() -> void:
	var s := BulletSplitStage2D.new()
	assert_true(_visible(s, "spread_degrees"), "fan uses the spread")
	assert_false(_visible(s, "fixed_speed"), "fixed speed hides outside Fixed")
	s.direction_mode = BulletSplitStage2D.DIRECTION_RING
	assert_false(_visible(s, "spread_degrees"), "a ring has no spread")
	s.speed_mode = BulletSplitStage2D.SPEED_FIXED
	assert_true(_visible(s, "fixed_speed"), "fixed speed shows in Fixed")
	assert_false(_visible(s, "max_splits_per_bullet"), "the limit hides while the parent never survives")
	s.parent_survives = true
	assert_true(_visible(s, "max_splits_per_bullet"), "shown with parent_survives")
	var d := BulletSplitData2D.new()
	assert_false(_visible(d, "max_depth"), "max_depth hides without repeat")
	d.repeat_last_stage = true
	assert_true(_visible(d, "max_depth"), "max_depth shows with repeat")


func test_accepted_edits_emit_changed_and_rejected_ones_do_not() -> void:
	var s := BulletSplitStage2D.new()
	var n := _count_changed(s)
	s.count = 8
	s.chance = 0.5
	s.direction_mode = BulletSplitStage2D.DIRECTION_RING
	s.keep_homing = false
	s.child_data = BulletVolleyData2D.new()
	assert_eq(n[0], 5, "five accepted edits, five changes")
	s.count = 8
	s.keep_homing = false
	assert_eq(n[0], 5, "same values are not changes")
	s.count = 0
	expect_error_sequence(["BulletSplitStage2D: count must be between 1 and 20000, keeping the old value."])
	s.chance = 1.5
	expect_error_sequence(["BulletSplitStage2D: chance must be finite and between 0 and 1, keeping the old value."])
	s.spread_degrees = 361.0
	expect_error_sequence(["BulletSplitStage2D: spread_degrees must be finite and between 0 and 360, keeping the old value."])
	s.split_after_sec = -0.5
	expect_error_sequence(["BulletSplitStage2D: split_after_sec must be finite and >= 0, keeping the old value."])
	s.angle_offset_degrees = INF
	expect_error_sequence(["BulletSplitStage2D: angle_offset_degrees must be finite, keeping the old value."])
	s.scale_multiplier = 0.0
	expect_error_sequence(["BulletSplitStage2D: scale_multiplier must be finite and > 0, keeping the old value."])
	s.direction_mode = 3
	expect_error_sequence(["BulletSplitStage2D: direction_mode must be 0 (Fan), 1 (Ring) or 2 (Random In Spread), keeping the old value."])
	s.speed_mode = -1
	expect_error_sequence(["BulletSplitStage2D: speed_mode must be 0 (Multiply Parent), 1 (Fixed) or 2 (From Spawn Data), keeping the old value."])
	s.speed_distribution = 2
	expect_error_sequence(["BulletSplitStage2D: speed_distribution must be 0 (Ramp) or 1 (Random), keeping the old value."])
	s.max_splits_per_bullet = -1
	expect_error_sequence(["BulletSplitStage2D: max_splits_per_bullet must be >= 0 (0 = no limit), keeping the old value."])
	assert_eq([s.count, s.chance, s.direction_mode], [8, 0.5, BulletSplitStage2D.DIRECTION_RING], "old values kept")
	assert_eq(n[0], 5, "no change for any rejected edit")
	var d := BulletSplitData2D.new()
	d.max_depth = 0
	expect_error_sequence(["BulletSplitData2D: max_depth must be between 1 and 64, keeping the old value."])
	d.max_depth = 65
	expect_error_sequence(["BulletSplitData2D: max_depth must be between 1 and 64, keeping the old value."])
	assert_eq(d.max_depth, 8, "default kept")


func test_count_follows_the_pattern_limit() -> void:
	var s := BulletSplitStage2D.new()
	BulletPatterns2D.set_max_bullets_per_pattern(50)
	s.count = 50
	assert_eq(s.count, 50, "the limit itself is accepted")
	s.count = 51
	expect_error_sequence(["BulletSplitStage2D: count must be between 1 and 50, keeping the old value."])
	assert_eq(s.count, 50, "kept")


func test_min_max_pairs_never_reject_each_other() -> void:
	var s := BulletSplitStage2D.new()
	s.speed_multiplier_min = 3.0 # above max 1: accepted, normalized at use
	assert_eq([s.speed_multiplier_min, s.speed_multiplier_max], [3.0, 1.0], "stored as given")


func test_stage_lookup_per_generation() -> void:
	var a := BulletSplitStage2D.new()
	var b := BulletSplitStage2D.new()
	var d := BulletSplitData2D.new()
	assert_null(d.get_stage_for_depth(0), "an empty chain never splits")
	assert_eq(d.get_generation_count(), 0, "no generation splits")
	d.stages = [a, b]
	assert_eq([d.get_stage_for_depth(0), d.get_stage_for_depth(1)], [a, b], "stage d splits generation d")
	assert_null(d.get_stage_for_depth(2), "past the chain")
	assert_null(d.get_stage_for_depth(-1), "negative depth")
	assert_eq(d.get_generation_count(), 2, "two generations")
	d.repeat_last_stage = true
	d.max_depth = 4
	assert_eq([d.get_stage_for_depth(2), d.get_stage_for_depth(3)], [b, b], "the last stage repeats")
	assert_null(d.get_stage_for_depth(4), "up to max_depth generations")
	assert_eq(d.get_generation_count(), 4, "max_depth generations")
	d.stages = [a, null]
	assert_null(d.get_stage_for_depth(1), "an empty entry does not split")
	assert_null(d.get_stage_for_depth(3), "nor does repeating it")


func test_child_angles_and_speed_multipliers() -> void:
	var s := BulletSplitStage2D.new()
	s.spread_degrees = 90.0
	assert_almost_eq(rad_to_deg(s.child_angle_radians(0, 3, 0.0, 0.5)), -45.0, 0.001, "fan starts at -spread/2")
	assert_almost_eq(rad_to_deg(s.child_angle_radians(2, 3, 0.0, 0.5)), 45.0, 0.001, "fan ends at +spread/2")
	assert_almost_eq(rad_to_deg(s.child_angle_radians(0, 1, 0.0, 0.5)), 0.0, 0.001, "a single child flies at the offset")
	s.direction_mode = BulletSplitStage2D.DIRECTION_RING
	s.angle_offset_degrees = 10.0
	assert_almost_eq(rad_to_deg(s.child_angle_radians(1, 4, 0.0, 0.5)), 100.0, 0.001, "ring: 360/count apart from the offset")
	s.direction_mode = BulletSplitStage2D.DIRECTION_RANDOM_IN_SPREAD
	assert_almost_eq(rad_to_deg(s.child_angle_radians(0, 4, 1.0, 0.5)), 55.0, 0.001, "random roll 1 = +spread/2 (+ offset)")
	s.angle_jitter_degrees = 5.0
	assert_almost_eq(rad_to_deg(s.child_angle_radians(0, 4, 0.5, 1.0)), 15.0, 0.001, "jitter roll 1 = +jitter")
	s.speed_multiplier_min = 2.0
	s.speed_multiplier_max = 0.5 # normalized: 0.5 .. 2
	assert_eq([s.child_speed_multiplier(0, 4, 0.0), s.child_speed_multiplier(3, 4, 0.0)], [0.5, 2.0], "ramp from min to max")
	assert_eq(s.child_speed_multiplier(0, 1, 0.0), 0.5, "a single child takes the min")
	s.speed_distribution = BulletSplitStage2D.SPEED_RANDOM
	assert_eq(s.child_speed_multiplier(0, 4, 0.5), 1.25, "random roll 0.5 = the middle")


func test_presets() -> void:
	var ring := BulletSplitData2D.create_preset(BulletSplitData2D.PRESET_BURST_RING)
	assert_eq(ring.stages.size(), 1, "one generation")
	assert_eq([ring.stages[0].count, ring.stages[0].direction_mode], [8, BulletSplitStage2D.DIRECTION_RING], "ring of 8")
	var cascade := BulletSplitData2D.create_preset(BulletSplitData2D.PRESET_CASCADE)
	assert_eq(cascade.stages.size(), 2, "two generations")
	var d := BulletSplitData2D.new()
	var n := _count_changed(d)
	d.apply_preset(BulletSplitData2D.PRESET_FAN_FORWARD)
	assert_eq(n[0], 1, "one change")
	assert_eq(d.stages[0].count, 3, "fan of 3")
	d.apply_preset(7)
	expect_error_sequence(["BulletSplitData2D: preset must be 0 (Burst Ring), 1 (Fan Forward) or 2 (Cascade), keeping the old values."])
	assert_null(BulletSplitData2D.create_preset(-1))
	expect_error_sequence(["BulletSplitData2D.create_preset: preset must be 0 (Burst Ring), 1 (Fan Forward) or 2 (Cascade), nothing created."])


func test_enum_ids_are_locked() -> void:
	assert_eq([BulletSplitStage2D.DIRECTION_FAN, BulletSplitStage2D.DIRECTION_RING, BulletSplitStage2D.DIRECTION_RANDOM_IN_SPREAD], [0, 1, 2], "direction ids")
	assert_eq([BulletSplitStage2D.SPEED_MULTIPLY_PARENT, BulletSplitStage2D.SPEED_FIXED, BulletSplitStage2D.SPEED_FROM_SPAWN_DATA], [0, 1, 2], "speed mode ids")
	assert_eq([BulletSplitStage2D.SPEED_RAMP, BulletSplitStage2D.SPEED_RANDOM], [0, 1], "distribution ids")
	assert_eq(BulletEffectLayerData2D.EFFECT_ON_SPLIT, 8, "effect trigger appended after On Despawn")
	assert_eq(BulletSoundData2D.SOUND_ON_SPLIT, 12, "sound trigger appended after On Despawn")


func test_chain_survives_a_tres_round_trip() -> void:
	var s := BulletSplitStage2D.new()
	s.count = 5
	s.chance = 0.25
	s.seed = 42
	s.direction_mode = BulletSplitStage2D.DIRECTION_RANDOM_IN_SPREAD
	s.speed_mode = BulletSplitStage2D.SPEED_FIXED
	s.fixed_speed = 333.0
	s.split_after_sec = 0.75
	s.keep_graze = false
	s.fire_death_effects = false
	var d := BulletSplitData2D.new()
	d.stages = [s, BulletSplitStage2D.new()]
	d.repeat_last_stage = true
	d.max_depth = 5
	assert_eq(ResourceSaver.save(d, TRES_PATH), OK, "saved")
	var back := ResourceLoader.load(TRES_PATH, "", ResourceLoader.CACHE_MODE_IGNORE) as BulletSplitData2D
	DirAccess.remove_absolute(ProjectSettings.globalize_path(TRES_PATH))
	assert_not_null(back, "loads back")
	if back == null:
		return
	assert_eq([back.stages.size(), back.repeat_last_stage, back.max_depth], [2, true, 5], "chain settings survive")
	var first: BulletSplitStage2D = back.stages[0]
	for prop in STAGE_PROPS:
		if prop != "child_data":
			assert_eq(first.get(prop), s.get(prop), prop + " survives")
