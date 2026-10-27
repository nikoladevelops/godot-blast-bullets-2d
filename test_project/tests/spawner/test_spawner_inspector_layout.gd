extends BlastTest
## The inspector of every spawner is ONE coherent layout: Setup first (where
## the factory and the generator are), what a shot fires, where and when it
## fires, what the bullets / beams do, then Graze, Sound and the editor
## groups. Each concrete spawner registers the shared groups itself
## (PatternSpawner2D binds their accessors, registers no property), so no
## base-class section trails its own. Every property sits in exactly one
## group, the shared ones exist on both spawners with the same type and hint,
## and a laser spawner leaves out what does nothing for a beam (collision
## rings, scale randomization).

const BULLET_GROUPS := ["Setup", "Bullets", "Patterns", "Shooting", "Time", "Spin", "Randomization",
	"Bullet Colors", "Homing", "Orbiting", "Flocking", "Splitting", "Emitter Bullets",
	"Graze", "Sound", "Preview", "Movement", "Performance"]
const LASER_GROUPS := ["Setup", "Lasers", "Patterns", "Shooting", "Time", "Spin", "Randomization",
	"Laser Targeting", "Graze", "Sound", "Preview", "Movement", "Performance"]
const LASER_LEAVES_OUT := ["preview_draw_collision_rings", "preview_collision_ring_color", "preview_collision_ring_width",
	"bullet_scale_mode", "bullet_scale_min", "bullet_scale_max", "bullet_scale_min_xy", "bullet_scale_max_xy"]


## [groups in order, {property: [group, type, hint, hint_string]}] the
## class registers itself (its whole inspector section).
func _layout(cls: String) -> Array:
	var groups: Array = []
	var props := {}
	var group := ""
	for p in ClassDB.class_get_property_list(cls, true):
		var usage := int(p["usage"])
		if usage & PROPERTY_USAGE_GROUP:
			group = str(p["name"])
			groups.append(group)
		elif not (usage & PROPERTY_USAGE_SUBGROUP):
			props[str(p["name"])] = [group, int(p["type"]), int(p["hint"]), str(p["hint_string"])]
	return [groups, props]


func test_each_spawner_lays_out_its_groups_in_one_order() -> void:
	assert_eq(_layout("BulletSpawner2D")[0], BULLET_GROUPS, "bullet spawner groups")
	assert_eq(_layout("LaserSpawner2D")[0], LASER_GROUPS, "laser spawner groups")


func test_the_shared_base_registers_no_property() -> void:
	assert_eq(ClassDB.class_get_property_list("PatternSpawner2D", true).size(), 0, "PatternSpawner2D only binds methods, signals and enums")
	assert_true(ClassDB.class_has_method("PatternSpawner2D", "shoot_once", true), "the shared script API stays on the base")


func test_every_property_sits_in_one_group_and_the_shared_ones_match() -> void:
	var bullets: Dictionary = _layout("BulletSpawner2D")[1]
	var lasers: Dictionary = _layout("LaserSpawner2D")[1]
	var loose: Array = []
	for cls_props in [bullets, lasers]:
		for name in cls_props:
			if cls_props[name][0] == "":
				loose.append(name)
	assert_eq(loose, [], "no property outside a group")
	var shared_groups := ["Setup", "Patterns", "Shooting", "Time", "Spin", "Randomization", "Graze", "Sound", "Preview", "Movement", "Performance"]
	var mismatched: Array = []
	var missing: Array = []
	for name in bullets:
		if not shared_groups.has(bullets[name][0]):
			continue
		if not lasers.has(name):
			if not LASER_LEAVES_OUT.has(name):
				missing.append(name)
		elif lasers[name] != bullets[name] and name != "sound_effects":
			mismatched.append("%s: %s vs %s" % [name, bullets[name], lasers[name]])
	assert_eq(missing, [], "every shared property exists on both (except what a laser leaves out)")
	assert_eq(mismatched, [], "and with the same group, type and hint")
	# Each spawner arms its own sound class (same group, type and hint kind).
	assert_eq(bullets["sound_effects"], ["Sound", TYPE_ARRAY, PROPERTY_HINT_ARRAY_TYPE, "BulletSoundData2D"], "bullet sounds")
	assert_eq(lasers["sound_effects"], ["Sound", TYPE_ARRAY, PROPERTY_HINT_ARRAY_TYPE, "LaserSoundData2D"], "laser sounds")
	for name in LASER_LEAVES_OUT:
		assert_true(bullets.has(name), "%s stays on the bullet spawner" % name)
		assert_false(lasers.has(name), "%s is left out of the laser spawner" % name)
	for name in lasers:
		if lasers[name][0] in ["Lasers", "Laser Targeting"]:
			assert_false(bullets.has(name), "%s is laser-only" % name)
	for name in bullets:
		if bullets[name][0] in ["Bullets", "Bullet Colors", "Homing", "Orbiting", "Flocking", "Splitting", "Emitter Bullets"]:
			assert_false(lasers.has(name), "%s is bullet-only" % name)


func test_a_left_out_knob_still_scripts_and_never_saves() -> void:
	var sp := LaserSpawner2D.new()
	add(sp)
	sp.set_bullet_scale_mode(PatternSpawner2D.BULLET_SCALE_UNIFORM)
	assert_eq(sp.get_bullet_scale_mode(), PatternSpawner2D.BULLET_SCALE_UNIFORM, "the shared accessor still works from a script")
	var saved := false
	for p in sp.get_property_list():
		if str(p["name"]) == "bullet_scale_mode":
			saved = true
	assert_false(saved, "but it is not a laser spawner property (never in the inspector or a scene)")


func test_a_scene_saved_with_the_old_layout_loads_every_value() -> void:
	var sp := make_spawner(H.make_volley_data(2))
	sp.helper_ring_radius = 77.0
	sp.shoot_interval_sec = 0.75
	sp.graze_update_interval = 0.5
	sp.preview_z_index = 11
	sp.movement_speed = 321.0
	sp.homing_max_targets = 3
	var packed := PackedScene.new()
	sp.owner = null
	assert_eq(packed.pack(sp), OK, "packs")
	var loaded: BulletSpawner2D = packed.instantiate()
	add(loaded)
	assert_eq([loaded.helper_ring_radius, loaded.shoot_interval_sec, loaded.graze_update_interval, loaded.preview_z_index, loaded.movement_speed, loaded.homing_max_targets],
		[77.0, 0.75, 0.5, 11, 321.0, 3], "every group round-trips by name")
