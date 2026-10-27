extends BlastTest
## Locks the cleaned-up spawner API: legacy and misleading members stay
## removed (re-adding one must be a deliberate decision), and inputs that
## used to be accepted silently are now rejected or warned exactly once.

const REMOVED_METHODS := [
	"get_pooled_volley_count", "is_orbit_armed", "debug_get_cache_state",
	"start_spinning", "stop_spinning", "is_spinning",
	"begin_telegraph", "fire_burst_volley", "next_shoot_interval_sec",
	"get_helper_flower_bullets_per_petal", "set_helper_flower_bullets_per_petal",
	"get_homing_debug_log_volleys", "get_adjust_direction_based_on_rotation",
	"get_shared_homing_auto_pop_after_target_reached",
]
const REMOVED_PROPERTIES := [
	"helper_flower_bullets_per_petal", "homing_debug_log_volleys",
	"adjust_direction_based_on_rotation", "shared_homing_auto_pop_after_target_reached",
]


func test_removed_members_stay_removed() -> void:
	var sp := make_spawner()
	for m in REMOVED_METHODS:
		assert_false(sp.has_method(m), "removed method stays gone: " + m)
	var names := {}
	for p in sp.get_property_list():
		names[str(p.name)] = true
	for prop in REMOVED_PROPERTIES:
		assert_false(names.has(prop), "removed property stays gone: " + prop)
	assert_true(sp.has_method("begin_burst"), "begin_burst stays the public manual trigger")


func test_cache_info_reports_the_spawn_data_template() -> void:
	var sp := make_spawner()
	assert_true(sp.shoot_once(), "shot fires")
	assert_false(sp.debug_get_pattern_cache_info()["template_valid"], "a spawner without a split chain never copies its data")
	sp.split_data = BulletSplitData2D.create_preset(BulletSplitData2D.PRESET_BURST_RING)
	sp.split_enabled = true
	assert_false(sp.debug_get_pattern_cache_info()["template_valid"], "no template before the first splitting shot")
	assert_true(sp.shoot_once(), "shot fires")
	var info: Dictionary = sp.debug_get_pattern_cache_info()
	assert_true(info["template_valid"], "the first splitting shot primes the template")
	assert_true(info["spawn_id_match"], "template matches the live resource")


## The split chain keeps the shot's data for its children: an edit made after
## the shot reaches the next shot, never the children of bullets in flight.
## A spawner without a split chain hands its own resource to the factory (a
## spawn reads it once and never writes it).
func test_spawn_data_edits_reach_only_later_shots() -> void:
	var sp := make_spawner(H.make_volley_data(2, 0.0, 30.0), BulletSpawner2D.PATTERN_FROM_HELPER_RING, 2)
	sp.split_data = BulletSplitData2D.create_preset(BulletSplitData2D.PRESET_BURST_RING)
	sp.split_enabled = true
	watch_signals(sp)
	assert_true(sp.shoot_once(), "splitting shot fires")
	var first: BulletVolley2D = get_signal_parameters(sp, "volley_fired", 0)[0]
	var data: BulletVolleyData2D = sp.spawn_data
	var transforms_before: Array = data.transforms.duplicate()
	data.max_life_time = 3.0
	assert_true(sp.shoot_once(), "the next shot fires")
	assert_eq(data.transforms, transforms_before, "a shot never writes the user's resource")
	watch_signals(factory)
	first.all_bullets_split()
	await idle(3)
	var children: BulletVolley2D = null
	for i in get_signal_emit_count(sp, "bullet_split"):
		children = get_signal_parameters(sp, "bullet_split", i)[2]
	assert_not_null(children, "the first volley split")
	assert_almost_eq(children.get_bullet_life_time_left(0), 30.0, 0.5, "its children kept the shot's 30 s lifetime")


func test_legacy_transforms_source_key_is_an_unknown_key() -> void:
	var sp := make_spawner()
	sp.spawn_pattern_list([{"transforms_source": BulletSpawner2D.PATTERN_FROM_HELPER_FAN}], true)
	expect_error_sequence(["unknown entry key 'transforms_source' (renamed to 'pattern_source')"])
	assert_eq(sp.pattern_source, BulletSpawner2D.PATTERN_FROM_HELPER_RING, "the legacy key no longer switches the source")


func test_one_auto_pop_switch_drives_both_queue_kinds() -> void:
	var sp := make_spawner(H.make_volley_data(2, 50.0, 30.0))
	var target := Node2D.new()
	target.position = Vector2(300, 0)
	add(target)
	sp.set_homing_enabled(true)
	sp.set_homing_target_source(BulletSpawner2D.HOMING_SOURCE_NODE_PATH)
	sp.set_homing_target_path(sp.get_path_to(target))
	sp.set_homing_auto_pop_after_target_reached(true)
	for mode in [BulletSpawner2D.HOMING_SHARED, BulletSpawner2D.HOMING_PER_BULLET]:
		sp.set_homing_mode(mode)
		assert_true(is_editor_visible(sp, &"homing_auto_pop_after_target_reached"), "visible in mode %d" % mode)
	sp.set_homing_mode(BulletSpawner2D.HOMING_SHARED)
	assert_true(sp.shoot_once(), "shared homing shot")
	var shared: BulletVolley2D = sp.get_live_volleys()[0]
	assert_true(shared.get_shared_homing_deque_auto_pop_after_target_reached(), "shared queue pops")
	sp.set_homing_mode(BulletSpawner2D.HOMING_PER_BULLET)
	assert_true(sp.shoot_once(), "per-bullet homing shot")
	var per: BulletVolley2D = sp.get_live_volleys()[1]
	assert_true(per.get_bullet_homing_auto_pop_after_target_reached(), "per-bullet queues pop")


func test_spawn_data_keeps_its_own_adjust_direction_flag() -> void:
	# The spawner used to overwrite this per homing volley (default false).
	var data := H.make_volley_data(2, 50.0, 30.0)
	data.adjust_direction_based_on_rotation = true
	var sp := make_spawner(data)
	sp.set_homing_enabled(true)
	assert_true(sp.shoot_once(), "homing shot")
	var volley: BulletVolley2D = sp.get_live_volleys()[0]
	assert_true(volley.get_adjust_direction_based_on_rotation(), "the data's flag reaches the volley untouched")
