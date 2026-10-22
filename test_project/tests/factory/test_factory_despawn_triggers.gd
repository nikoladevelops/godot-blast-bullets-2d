extends BlastTest
## Despawn bounds beyond culling: a spawner outside the bounds does not
## shoot (volley_skipped "outside_despawn_bounds", opt-out per spawner), and
## a culled bullet fires the On Despawn effect and sound triggers and the
## factory's bullets_despawned signal.

var despawned: Array = []
var skipped: Array = []


func _on_despawned(positions: PackedVector2Array, volley: BulletVolley2D, indexes: Array) -> void:
	despawned.append({ "positions": positions, "volley": volley, "indexes": indexes })


func before_each() -> void:
	await super()
	despawned.clear()
	skipped.clear()
	factory.despawn_bounds_rect = Rect2(0, 0, 400, 300)
	factory.despawn_bounds_enabled = true


func _spawner(pos: Vector2) -> BulletSpawner2D:
	var sp := make_spawner(H.make_volley_data(2, 0.0, 30.0), BulletSpawner2D.PATTERN_FROM_HELPER_RING, 2)
	sp.position = pos
	sp.helper_ring_radius = 5.0
	sp.volley_skipped.connect(func(reason): skipped.append(reason))
	return sp


func _flyers(points: Array, velocity: Vector2, data: BulletVolleyData2D = null) -> BulletVolley2D:
	var d := data if data != null else H.make_volley_data(points.size(), 0.0, 30.0)
	var arr: Array = []
	for p in points:
		arr.append(Transform2D(0.0, p))
	d.transforms = arr
	var v := factory.spawn_volley(d)
	for i in points.size():
		v.bullet_set_velocity(i, velocity)
	return v


# --- spawners outside the bounds --------------------------------------------------------------

func test_a_spawner_outside_the_bounds_does_not_shoot() -> void:
	var sp := _spawner(Vector2(900, 100))
	assert_false(sp.shoot_once(), "refused")
	assert_eq(skipped, [&"outside_despawn_bounds"], "with a reason")
	assert_eq(factory.get_active_bullet_count(), 0, "nothing was spawned")
	expect_no_errors("a skip is not an error")


func test_inside_the_bounds_it_shoots_and_the_edges_count_as_inside() -> void:
	var sp := _spawner(Vector2(200, 100))
	assert_true(sp.shoot_once(), "inside")
	sp.position = Vector2(400, 300)
	assert_true(sp.shoot_once(), "on the edge is inside")
	assert_eq(skipped, [], "no skip")
	sp.position = Vector2(400.5, 300)
	assert_false(sp.shoot_once(), "half a pixel out")


func test_the_option_and_the_switch_allow_shooting_from_outside() -> void:
	var sp := _spawner(Vector2(900, 100))
	sp.shoot_outside_despawn_bounds = true
	assert_true(sp.shoot_once(), "opted in")
	sp.shoot_outside_despawn_bounds = false
	assert_false(sp.shoot_once(), "opted out again")
	factory.despawn_bounds_enabled = false
	assert_true(sp.shoot_once(), "no bounds, no rule")
	assert_false(sp.shoot_outside_despawn_bounds, "off by default")


func test_the_muzzle_offset_decides() -> void:
	var sp := _spawner(Vector2(900, 100))
	sp.spawn_position_offset = Vector2(-700, 0) # the muzzle sits at (200, 100)
	assert_true(sp.shoot_once(), "the muzzle is inside")
	sp.position = Vector2(200, 100)
	sp.spawn_position_offset = Vector2(900, 0)
	assert_false(sp.shoot_once(), "inside the bounds itself, but the muzzle is outside")


## Bullets are born at the effective generator (transforms_generator), so
## the generator's position decides, not the spawner node's.
func test_an_external_generator_decides() -> void:
	var gen: Node2D = add(Node2D.new())
	gen.position = Vector2(900, 100)
	var sp := _spawner(Vector2(200, 100))
	sp.set_transforms_generator(gen)
	assert_false(sp.shoot_once(), "the spawner sits inside, but its generator (where bullets spawn) is outside")
	assert_eq(skipped, [&"outside_despawn_bounds"], "with a reason")
	gen.position = Vector2(200, 100)
	sp.position = Vector2(900, 100)
	assert_true(sp.shoot_once(), "the spawner sits outside, but its generator is inside")


func test_an_auto_firing_spawner_waits_outside_and_starts_when_it_enters() -> void:
	var sp := _spawner(Vector2(900, 100))
	sp.shoot_interval_sec = 0.1
	sp.set_shooting_enabled(true)
	await idle(30)
	assert_eq(factory.get_active_bullet_count(), 0, "outside: no bullets")
	assert_gt(skipped.size(), 3, "every pull reports the skip")
	sp.position = Vector2(100, 100)
	await idle(30)
	assert_gt(factory.get_active_bullet_count(), 0, "inside: it fires")
	expect_no_errors("waiting outside never logs an error")


func test_bursts_and_telegraphs_respect_the_rule_too() -> void:
	var sp := _spawner(Vector2(900, 100))
	sp.burst_enabled = true
	sp.burst_count = 3
	sp.burst_interval_sec = 0.05
	sp.begin_burst()
	await idle(30)
	assert_eq(factory.get_active_bullet_count(), 0, "no burst volley left the muzzle")
	assert_gt(skipped.size(), 0, "they were skipped")


# --- On Despawn effect, sound and signal ------------------------------------------------------------

func test_the_on_despawn_effect_fires_for_culled_bullets_only() -> void:
	var data := H.make_volley_data(3, 0.0, 30.0)
	data.effect_layers = [H.make_effect_layer(BulletEffectLayerData2D.EFFECT_ON_DESPAWN)]
	factory.debug_set_effect_log_enabled(true)
	var v := _flyers([Vector2(380, 100), Vector2(390, 100), Vector2(100, 100)], Vector2(0, 0), data)
	v.bullet_set_velocity(0, Vector2(600, 0))
	v.bullet_set_velocity(1, Vector2(600, 0))
	await idle(1)
	for i in 6:
		factory.debug_advance_time(0.1)
	var log := factory.debug_get_effect_log().filter(func(e): return int(e["trigger"]) == BulletEffectLayerData2D.EFFECT_ON_DESPAWN)
	assert_eq(log.size(), 2, "one effect per culled bullet")
	for e in log:
		assert_gt(e["position"].x, 400.0, "fired at the pose it left from")
	assert_true(v.is_bullet_status_enabled(2), "the bullet inside is untouched")


func test_other_removals_never_fire_on_despawn() -> void:
	var data := H.make_volley_data(2, 0.0, 0.2)
	data.effect_layers = [H.make_effect_layer(BulletEffectLayerData2D.EFFECT_ON_DESPAWN)]
	factory.debug_set_effect_log_enabled(true)
	var v := _flyers([Vector2(100, 100), Vector2(110, 100)], Vector2(0, 0), data)
	v.clear_bullet(0)
	await idle(30) # the other one expires
	assert_eq(factory.debug_get_effect_log().size(), 0, "clear and expiry are not despawns")


func test_the_on_despawn_sound_trigger() -> void:
	godot_listener_at(Vector2(500, 100))
	factory.debug_set_sound_log_enabled(true)
	var sound := H.make_sound(BulletSoundData2D.SOUND_ON_DESPAWN)
	var v := _flyers([Vector2(390, 100)], Vector2(600, 0))
	assert_true(v.sound_set_effects([sound]), "armed")
	await idle(1)
	for i in 4:
		factory.debug_advance_time(0.1)
	await idle(1)
	var played := 0
	for e in factory.debug_get_sound_log():
		if int(e["sound"]) == sound.get_instance_id():
			played += 1
	assert_eq(played, 1, "the despawn sound played once")


func test_the_bullets_despawned_signal() -> void:
	factory.bullets_despawned.connect(_on_despawned)
	var v := _flyers([Vector2(380, 100), Vector2(100, 100), Vector2(390, 120)], Vector2(0, 0))
	v.bullet_set_velocity(0, Vector2(600, 0))
	v.bullet_set_velocity(2, Vector2(600, 0))
	await idle(1)
	for i in 6:
		factory.debug_advance_time(0.1)
	assert_eq(despawned.size(), 1, "once per volley that lost bullets")
	assert_eq(Array(despawned[0]["indexes"]), [0, 2], "which bullets")
	assert_eq(despawned[0]["positions"].size(), 2, "and where they were")
	for p in despawned[0]["positions"]:
		assert_true(p.x > 400.0 or p.y > 300.0, "positions are the poses they left from")
	assert_same(despawned[0]["volley"], v, "the volley")


func test_a_despawn_handler_may_spawn() -> void:
	var made := []
	factory.bullets_despawned.connect(func(positions, _v, _i):
		for p in positions:
			var d := H.make_volley_data(1, 0.0, 30.0)
			d.transforms = [Transform2D(0.0, Vector2(50, 50))]
			made.append(factory.spawn_volley(d)))
	var v := _flyers([Vector2(380, 100)], Vector2(600, 0))
	await idle(1)
	for i in 6:
		factory.debug_advance_time(0.1)
	assert_eq(made.size(), 1, "the handler spawned from inside the sweep")
	assert_not_null(made[0], "successfully")
	assert_not_null(v)


func test_trigger_ids_are_stable_and_validated() -> void:
	assert_eq(BulletEffectLayerData2D.EFFECT_ON_DESPAWN, 7, "effect trigger id is serialized: appended after On Clear")
	assert_eq(BulletSoundData2D.SOUND_ON_DESPAWN, 11, "sound trigger id is serialized: appended after On Flight")
	var layer := BulletEffectLayerData2D.new()
	layer.trigger = 7
	assert_eq(layer.trigger, 7, "accepted")
	layer.trigger = 8
	assert_eq(layer.trigger, 8, "On Split (8) is accepted")
	layer.trigger = 9
	expect_errors_containing("BulletEffectLayerData2D: trigger must be", 1, "9 is out of range")
	var sound := BulletSoundData2D.new()
	sound.trigger = 11
	assert_eq(sound.trigger, 11, "accepted")
	sound.trigger = 12
	assert_eq(sound.trigger, 12, "On Split (12) is accepted")
	sound.trigger = 13
	expect_errors_containing("BulletSoundData2D: trigger must be", 1, "13 is out of range")


## A burst whose first-shot warning is refused (the muzzle outside) never
## loses that warning: back inside, the first shot is telegraphed before it
## fires, and the refused attempt waits a burst interval instead of retrying
## on the very next frame.
func test_a_burst_warning_refused_outside_is_given_once_inside() -> void:
	var sp := _spawner(Vector2(900, 100))
	sp.burst_enabled = true
	sp.burst_count = 3
	sp.burst_interval_sec = 0.1
	sp.telegraph_enabled = true
	sp.telegraph_sec = 0.1
	var log: Array = []
	sp.volley_telegraphed.connect(func(_aim) -> void: log.append("telegraph"))
	sp.volley_fired.connect(func(_v, _n) -> void: log.append("shot"))
	sp.begin_burst()
	for i in 10:
		await idle(1)
		if not skipped.is_empty():
			break
	assert_eq(skipped, [&"outside_despawn_bounds"], "the warning is refused outside")
	sp.position = Vector2(100, 100)
	for i in 60:
		await idle(1)
		if log.size() >= 2:
			break
	assert_eq(log.slice(0, 2), ["telegraph", "shot"], "the first burst shot is warned before it fires")
