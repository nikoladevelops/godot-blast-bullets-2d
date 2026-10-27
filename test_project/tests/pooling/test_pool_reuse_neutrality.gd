extends BlastTest
## Pool reuse neutrality: a volley that comes back out of the pool (after any
## life, or pre-populated) is indistinguishable from a cold volley of the data
## it is spawned with. Merged from four suites that pinned this one contract:
##
## A volley that lived with EVERY feature switched on, expired into the pool
## and was reused for plain bullets must be indistinguishable from a volley
## that was never used: homing queues, orbit, bounce, wobble, gravity,
## curves, movement patterns, trails, timers, attachments, custom data, tint,
## spin, graze and flocking are all reset, and the reused bullets sit and fly exactly where a
## cold volley's bullets do. Guards the single reset path of the merged
## volley class (reset_transient_volley_state + reset_motion_feature_state).
##
## Pool neutrality: a volley that lived with non-default runtime settings,
## went back to the pool and was popped again must read exactly like a cold
## volley built from the same plain data. Runtime-only knobs (ones that spawn
## data never seeds) are the classic leak: they survive the reset unless the
## new life resets them explicitly.
##
## A pooled volley's reuse is a new life: pool hit, zeroed collision counts,
## a fresh dedup window (the same wall counts again), old custom timers never
## fire, no attachments carried over, reseeded ballistics win over the old
## per-bullet edits.
##
## Spawn-time attachment callbacks see the same volley state on a cold spawn
## and on a pooled reuse: the bullets already report enabled (a disable_bullet
## from the callback must take effect on both paths).

const N := 4
const LOADED_LIFETIME := 0.25 # seconds: short enough to expire into the pool quickly

var _timer_fired := 0


func _on_timer() -> void:
	_timer_fired += 1


func _plain() -> BulletVolleyData2D:
	return H.make_volley_data(N, 120.0, 5.0)


func _loaded() -> BulletVolleyData2D:
	var d := H.make_volley_data(N, 120.0, LOADED_LIFETIME)
	var curves := BulletCurvesData2D.new()
	curves.movement_speed_curve = H.make_flat_curve(300.0)
	d.shared_bullet_curves_data = curves
	d.shared_bullet_rotation_data = H.make_rotation(4.0, 10.0)
	var wobble := BulletWobbleData2D.new()
	wobble.enabled = true
	wobble.amplitude = 30.0
	d.shared_bullet_wobble_data = wobble
	d.gravity = Vector2(0, 90)
	d.bounce_mask = 8
	d.bounce_strength = 0.8
	d.effect_layers = [H.make_effect_layer(BulletEffectLayerData2D.EFFECT_TRAIL_FOLLOW, 2)]
	d.shared_bullets_custom_data = Resource.new()
	d.self_modulate = Color(1, 0.2, 0.2, 1)
	d.shared_bullet_attachment = make_probe_scene()
	return d


func _pattern_curve() -> Curve2D:
	var c := Curve2D.new()
	c.add_point(Vector2.ZERO)
	c.add_point(Vector2(200, 50))
	return c


## Fields a fresh life must reproduce. Instance-specific values (generation,
## ids) are left out on purpose.
func _volley_state(v: BulletVolley2D) -> Dictionary:
	var info: Dictionary = v.debug_get_volley_info()
	var fx: Dictionary = v.debug_get_effect_layers_info()
	return {
		"amount": info.get("amount_bullets"),
		"active": info.get("active_bullets"),
		"owner": info.get("owner_spawner_id"),
		"tint": info.get("self_modulate"),
		"shared_homing": v.shared_homing_deque_check_homing_targets_amount(),
		"timers": v.debug_get_timer_count(),
		"trail_bakes": fx.get("trail_bake_count"),
		"shared_custom_data": v.get_shared_bullets_custom_data(),
		"shared_pattern": v.has_shared_movement_pattern(),
		"graze": _graze_state(v),
		"flock": [v.flock_get(), v.flock_get_scope(), v.debug_get_bullet_flock_neighbors(0), v.debug_get_bullet_flock_steering(0)],
		"sound": [v.sound_get_effects(), v.sound_get_listener_group()],
		"split": [v.split_get_data(), v.split_get_depth()],
		"pickup": v.is_pickup_volley(),
		"collision": [v.get_collision_layer(), v.get_collision_mask(), v.get_bullet_max_collision_count()],
		"material": [v.material, v.texture, v.z_index, v.light_mask, v.visibility_layer],
		"groups": v.get_groups().filter(func(g): return not str(g).begins_with("_")),
		"meta": v.get_meta_list(),
	}


## The keys whose values differ: {key: [a, b]} (empty = equal).
func _diff(a: Dictionary, b: Dictionary) -> Dictionary:
	var out := {}
	for k in a:
		if not b.has(k) or str(a[k]) != str(b[k]):
			out[k] = [a[k], b.get(k)]
	return out


func _graze_state(v: BulletVolley2D) -> Dictionary:
	var g: Dictionary = v.debug_get_graze_info()
	g.erase("generation") # bumped per arming/release: instance history, not state
	return g


func _bullet_state(v: BulletVolley2D, i: int) -> Dictionary:
	return {
		"bullet": v.debug_get_bullet_info(i),
		"curves": v.debug_get_curves_info(i),
		"pattern": v.debug_get_pattern_info(i),
		"wobble": v.debug_get_wobble_info(i),
		"gravity": v.debug_get_gravity_info(i),
		"bounce": v.debug_get_bounce_info(i),
		"orbit": v.debug_get_orbiting_info(i),
		"attachment": v.debug_get_attachment_info(i),
		"custom_data": v.bullet_get_custom_data(i),
		"rotation": v.get_bullet_rotation_data(i).rotation_speed,
	}


func test_reused_volley_matches_a_cold_volley() -> void:
	var v: BulletVolley2D = factory.spawn_volley(_loaded())
	assert_not_null(v, "loaded volley spawned")
	await idle(1)
	# Runtime-only features on top of the data.
	v.shared_homing_deque_push_back_global_position_target(Vector2(400, 400))
	v.bullet_homing_push_back_global_position_target(1, Vector2(-300, 0))
	v.bullet_enable_orbiting(0, 48.0, BulletVolley2D.OrbitLeft)
	v.set_shared_movement_pattern_curve(_pattern_curve())
	v.attach_time_based_function(1.0, _on_timer, true)
	v.bullet_set_custom_data(2, Resource.new())
	make_graze_target(Vector2(0, 0))
	v.graze_set_zones([H.make_graze_zone([400.0])], &"graze_targets")
	factory.bullet_grazed.connect(func(_t: Node2D, _v: BulletVolley2D, _i: int, _z: GrazeZone2D, _r: int) -> void: pass)
	v.flock_join(H.make_flock(1.5, 1.0, 1.0))
	v.sound_set_effects([H.make_sound(BulletSoundData2D.SOUND_ON_HIT)], &"listeners")
	var stage := BulletSplitStage2D.new()
	stage.split_on_lifetime_over = false # the expiry pools it plain, no children
	var chain := BulletSplitData2D.new()
	chain.stages = [stage]
	v.split_set_data(chain, _plain())
	v.add_to_group(&"loaded_life")
	v.set_meta(&"loaded_life", 1)
	expect_no_errors("every feature switches on cleanly")
	for i in 120: # early break; the lifetime is 15 ticks
		await physics()
		if not bool(v.debug_get_volley_info().get("is_active", true)):
			break
	await idle(2) # the lifetime hold flushes on the idle frame
	assert_true(bool(v.debug_get_volley_info().get("is_pooled", false)), "expired into the pool")
	assert_gt(int(factory.debug_get_graze_stats()["events_total"]), 0, "the loaded life really grazed")
	assert_eq(int(factory.debug_get_flock_stats()["armed_volleys"]), 0, "the pooled life left its flock")

	var hits_before := int(factory.debug_get_pool_hit_stats()["hits"])
	var plain := _plain() # one data for both: its texture is compared too
	var reused: BulletVolley2D = factory.spawn_volley(plain)
	assert_eq(reused, v, "the pooled instance is reused")
	assert_eq(int(factory.debug_get_pool_hit_stats()["hits"]), hits_before + 1, "counted as a pool hit")
	var cold: BulletVolley2D = factory.spawn_volley(plain)
	assert_ne(cold, reused, "the second spawn allocates a fresh volley")

	assert_eq(_diff(_volley_state(reused), _volley_state(cold)), {}, "volley-wide state matches a cold volley")
	for i in N:
		assert_eq(_bullet_state(reused, i), _bullet_state(cold, i), "bullet %d state matches a cold volley" % i)
		assert_eq(reused.get_bullet_transform(i), cold.get_bullet_transform(i), "bullet %d starts at the cold pose" % i)
		assert_eq(reused.get_bullet_velocity(i), cold.get_bullet_velocity(i), "bullet %d starts with the cold velocity" % i)

	await physics(30)
	for i in N:
		var a: Vector2 = reused.get_bullet_transform(i).origin
		var b: Vector2 = cold.get_bullet_transform(i).origin
		assert_almost_eq(a.distance_to(b), 0.0, 0.001, "bullet %d flies like the cold volley" % i)
	assert_eq(_timer_fired, 0, "the previous life's timer never fires")



## Spawn data that leaves a mark everywhere a cold spawn of other data must
## not see: a pre-populated volley is seeded once from it and must come out
## of the pool exactly like a cold volley of the data it is spawned with.
func _populate_data() -> BulletVolleyData2D:
	var d := _loaded()
	d.shared_bullet_attachment = null # populate never attaches
	d.material = CanvasItemMaterial.new()
	d.z_index = 7
	d.light_mask = 3
	return d


func test_a_populated_volley_matches_a_cold_volley() -> void:
	var key := VolleyPoolKey2D.make(N, PhysicsServer2D.SHAPE_CIRCLE)
	var data := _populate_data()
	data.collision_shape = H.make_circle_shape(6.0)
	factory.populate_bullets_pool(key, data, 1)
	var plain := _plain()
	plain.collision_shape = H.make_circle_shape(6.0)
	var reused: BulletVolley2D = factory.spawn_volley(plain)
	assert_eq(int(factory.debug_get_pool_hit_stats()["hits"]), 1, "the populated volley was reused")
	var cold: BulletVolley2D = factory.spawn_volley(plain)
	assert_eq(_diff(_volley_state(reused), _volley_state(cold)), {}, "volley-wide state matches a cold volley")
	for i in N:
		assert_eq(_bullet_state(reused, i), _bullet_state(cold, i), "bullet %d state matches a cold volley" % i)


## A pre-populated volley waits in the pool as a released one: it keeps none
## of the populate data's resources alive (custom data, curves, material...).
func test_a_populated_volley_keeps_no_resource_of_its_data() -> void:
	var key := VolleyPoolKey2D.make(N, PhysicsServer2D.SHAPE_CIRCLE)
	var data := _populate_data()
	data.collision_shape = H.make_circle_shape(6.0)
	var refs := [weakref(data.shared_bullets_custom_data), weakref(data.shared_bullet_curves_data), weakref(data.shared_bullet_wobble_data), weakref(data.material), weakref(data.sprite_frames)]
	factory.populate_bullets_pool(key, data, 2)
	data = null
	var alive: Array = []
	for r in refs:
		alive.append(r.get_ref() != null)
	assert_eq(alive, [false, false, false, false, false], "the pool holds none of them")


## A volley that went back to the pool keeps none of its life's resources
## either: the material and its shader parameter values go with the life.
func test_a_pooled_volley_keeps_no_material() -> void:
	var data := _plain()
	var shader := Shader.new()
	shader.code = "shader_type canvas_item; instance uniform vec4 tint : source_color = vec4(1.0);"
	var mat := ShaderMaterial.new()
	mat.shader = shader
	data.material = mat
	data.instance_shader_parameters = {"tint": Color(1, 0, 0, 1)}
	var v: BulletVolley2D = factory.spawn_volley(data)
	var ref: WeakRef = weakref(mat)
	await idle(1)
	factory.clear_active_bullets()
	await idle(1)
	assert_true(v.is_pooled(), "pooled")
	assert_true(v.has_method("debug_check_invariants"), "bound")
	assert_eq(v.debug_check_invariants(), "", "a pooled volley passes every invariant")
	assert_null(v.material, "no material while pooled")
	data.material = null
	mat = null
	assert_null(ref.get_ref(), "the pool keeps the material alive no more")
	var reused: BulletVolley2D = factory.spawn_volley(_plain())
	assert_eq(reused, v, "reused")
	assert_null(reused.get_instance_shader_parameter(&"tint"), "the old life's parameter is gone")


# ---- Runtime-only knobs and node decorations (property sweep) ----

const SWEEP_N := 3


func _sweep_plain() -> BulletVolleyData2D:
	return H.make_volley_data(SWEEP_N, 120.0, 5.0)


## Drains every bullet so the volley goes back to the pool, then spawns the
## same key again and returns the reused instance (asserted to be a pool hit).
func _drain_and_reuse(v: BulletVolley2D) -> BulletVolley2D:
	for i in v.get_amount_bullets():
		v.disable_bullet(i)
	await idle(1)
	var reused: BulletVolley2D = factory.spawn_volley(_sweep_plain())
	assert_same(reused, v, "same key reuses the pooled instance")
	return reused


func test_collision_dedup_mode_is_reset_for_the_next_life() -> void:
	var v: BulletVolley2D = factory.spawn_volley(_sweep_plain())
	v.set_collision_dedup_by_object(false)
	var reused := await _drain_and_reuse(v)
	assert_true(reused.get_collision_dedup_by_object(), "dedup mode is per life: the next owner starts object-level (default)")


## Properties the sweep leaves alone: engine bookkeeping the plugin owns
## (interpolation mode, multimesh, texture), lifecycle flags with their own
## suite (auto pooling: a pooling-off volley parks instead of pooling), and
## Node identity knobs that never affect bullets.
const SWEEP_SKIP := [
	"script", "multimesh", "texture", "visible", "physics_interpolation_mode",
	"process_thread_group", "auto_translate_mode", "unique_name_in_owner",
	"editor_description", "is_auto_pooling_enabled", "is_attachments_auto_pooling_enabled",
	"effect_layers", "bullets_current_collision_count",
]


## A value every setter accepts and that differs from `current`.
func _mutated(p: Dictionary, current: Variant) -> Variant:
	match int(p.type):
		TYPE_BOOL:
			return not bool(current)
		TYPE_INT:
			return int(current) + 1
		TYPE_FLOAT:
			return 0.37 if not is_equal_approx(float(current), 0.37) else 0.21
		TYPE_VECTOR2:
			return Vector2(3, 4)
		TYPE_COLOR:
			return Color(0.5, 0.4, 0.3, 0.9)
		TYPE_OBJECT:
			# Resource hints may list several classes ("A,B"): take the first.
			var cls: String = String(p.hint_string).get_slice(",", 0)
			if cls != "" and ClassDB.class_exists(cls) and ClassDB.can_instantiate(cls):
				return ClassDB.instantiate(cls)
	return null


func test_every_volley_property_returns_to_its_cold_value_after_reuse() -> void:
	var v: BulletVolley2D = factory.spawn_volley(_sweep_plain())
	var cold := {}
	var mutated: Array = []
	for p in v.get_property_list():
		var usage := int(p.usage)
		if (usage & (PROPERTY_USAGE_STORAGE | PROPERTY_USAGE_EDITOR)) == 0 or String(p.name) in SWEEP_SKIP:
			continue
		var current: Variant = v.get(p.name)
		var value: Variant = _mutated(p, current)
		if value == null:
			continue
		cold[p.name] = current
		v.set(p.name, value)
		mutated.append(p.name)
	expect_no_errors("every mutation was accepted")
	assert_gt(mutated.size(), 40, "the sweep really mutated the volley (%d properties)" % mutated.size())
	var reused := await _drain_and_reuse(v)
	var leaks: Array = []
	for name in mutated:
		var now: Variant = reused.get(name)
		var was: Variant = cold[name]
		var same: bool = (now == was) if typeof(was) != TYPE_FLOAT else is_equal_approx(float(now), float(was))
		if not same:
			leaks.append("%s: %s (cold %s)" % [name, str(now), str(was)])
	assert_eq(" | ".join(leaks), "", "no runtime setting of the previous life leaks into the reused volley")


func test_node_decorations_are_cleared_on_reuse() -> void:
	var v: BulletVolley2D = factory.spawn_volley(_sweep_plain())
	v.add_to_group("enemy_bullets")
	v.set_meta("owner_tag", 42)
	var reused := await _drain_and_reuse(v)
	assert_false(reused.is_in_group("enemy_bullets"), "user groups are dropped")
	assert_false(reused.has_meta("owner_tag"), "metadata is dropped")
	var internal_groups := 0
	for g in reused.get_groups():
		if String(g).begins_with("_"):
			internal_groups += 1
	assert_gt(internal_groups, 0, "engine-internal groups are kept")


# ---- A pooled reuse is a new life (real wall hits, timers, edits) ----

var _old_timer_fired := false


func _on_old_timer() -> void:
	_old_timer_fired = true


func _wall_data() -> BulletVolleyData2D:
	var d := H.make_volley_data(1, 900.0, 60.0)
	d.monitorable = true
	d.collision_shape = H.make_circle_shape(6.0)
	d.bullet_max_collision_count = 0
	return d


func test_pooled_reuse_is_a_new_life() -> void:
	make_wall(Vector2(200, 0), Vector2(20, 400), 8, 2)
	await physics()
	var a: BulletVolley2D = factory.spawn_volley(_wall_data())
	for i in 40:
		await physics()
		if a.get_bullet_collision_count(0) >= 1:
			break
	assert_eq(a.get_bullet_collision_count(0), 1, "first life registered the hit")
	await idle(1)
	a.attach_time_based_function(0.5, _on_old_timer, false, true)
	assert_eq(a.debug_get_timer_count(), 1, "old-life timer attached")
	var spd := BulletSpeedData2D.new()
	spd.speed = 111.0
	spd.max_speed = 222.0
	a.set_bullet_speed_data(0, spd)
	for i in a.get_amount_bullets():
		a.disable_bullet(i)
	await idle(1)
	assert_eq(factory.debug_get_bullets_pool_amount(), 1, "emptied volley parked pooled")

	factory.debug_reset_pool_stats()
	var b: BulletVolley2D = factory.spawn_volley(_wall_data())
	assert_eq(int(factory.debug_get_pool_hit_stats().get("hits", 0)), 1, "second life is a pool hit")
	assert_eq(b.get_bullet_collision_count(0), 0, "collision count reset")
	assert_eq(b.debug_get_timer_count(), 0, "new life holds no timers")
	assert_eq(b.get_amount_active_attachments(), 0, "no attachments leak across")
	assert_almost_eq(b.get_bullet_speed_data(0).speed, 900.0, 0.01, "reseeded speed wins over the old edit")

	var rehit := false
	for i in 40:
		await physics()
		if b.get_bullet_collision_count(0) >= 1:
			rehit = true
			break
	assert_true(rehit, "dedup window is fresh: the same wall registers again")
	_old_timer_fired = false
	for i in 90:
		await physics()
		if _old_timer_fired:
			break
	assert_false(_old_timer_fired, "old 0.5s timer stayed dead after pooling")


# ---- Spawn-time callbacks see the same state on both paths ----

func _status_probe_scene() -> PackedScene:
	var probe: Node = AttachmentStatusProbe2D.new()
	var ps := PackedScene.new()
	assert_eq(ps.pack(probe), OK, "probe scene packs")
	probe.free()
	return ps


func before_each() -> void:
	await super()
	AttachmentStatusProbe2D.volley = null
	AttachmentStatusProbe2D.spawn_seen.clear()
	AttachmentStatusProbe2D.enable_seen.clear()


func test_enable_callback_on_pooled_reuse_sees_enabled_bullets() -> void:
	var data := H.make_volley_data(2, 0.0, 30.0)
	data.shared_bullet_attachment = _status_probe_scene()
	var cold: BulletVolley2D = factory.spawn_volley(data)
	await idle(1)
	factory.clear_active_bullets()
	await idle(1)
	# The pooled volley IS this instance: point the probe at it before the
	# respawn, because the callbacks run inside spawn_volley.
	AttachmentStatusProbe2D.volley = cold
	var reused: BulletVolley2D = factory.spawn_volley(data)
	assert_same(reused, cold, "the pooled volley was reused")
	assert_eq(AttachmentStatusProbe2D.enable_seen.size(), 2, "one on_bullet_enable per pooled attachment")
	assert_eq(str(AttachmentStatusProbe2D.enable_seen), "[true, true]", "bullet 0 already reads enabled inside the callback")


func _sprite_state(v: BulletVolley2D) -> Array:
	return [v.get_sprite_animation(), v.get_sprite_frame(), v.get_sprite_frame_count(), v.is_sprite_animation_finished(), v.is_sprite_animation_playing(), v.get_sprite_frames()]


## A life without frames starts with a clean animation state, also when the
## pooled volley's last life played (and finished, stopped, sought) another.
func test_a_frameless_life_starts_with_a_clean_animation_state() -> void:
	var d := H.make_volley_data(2, 0.0, 5.0)
	var sf := SpriteFrames.new()
	sf.add_animation(&"walk")
	sf.set_animation_loop(&"walk", false)
	sf.set_animation_speed(&"walk", 30.0)
	for i in 3:
		sf.add_frame(&"walk", ImageTexture.create_from_image(Image.create_empty(4, 4, false, Image.FORMAT_RGBA8)))
	d.sprite_frames = sf
	d.animation = &"walk"
	var v: BulletVolley2D = factory.spawn_volley(d)
	await physics(20)
	assert_true(v.is_sprite_animation_finished(), "the walk finished")
	v.stop_sprite_animation()
	v.clear_all_bullets()
	await idle(2)
	var plain := H.make_volley_data(2, 0.0, 5.0)
	plain.sprite_frames = null # drawn by texture_size
	var reused: BulletVolley2D = factory.spawn_volley(plain)
	assert_eq(reused, v, "the pooled instance is reused")
	var cold: BulletVolley2D = factory.spawn_volley(plain)
	assert_eq(_sprite_state(reused), _sprite_state(cold), "the animation state of a cold frameless volley")


## A pooled handle refuses new frames and effect layers: nothing a script
## hands it is kept alive while it waits in the pool.
func test_a_pooled_handle_keeps_no_frames_or_layers() -> void:
	var v: BulletVolley2D = factory.spawn_volley(_plain())
	v.clear_all_bullets()
	await idle(2)
	assert_true(v.is_pooled(), "pooled")
	var sf := SpriteFrames.new()
	sf.add_frame(&"default", ImageTexture.create_from_image(Image.create_empty(4, 4, false, Image.FORMAT_RGBA8)))
	var layer := H.make_effect_layer(BulletEffectLayerData2D.EFFECT_TRAIL_FOLLOW, 2)
	var refs := [weakref(sf), weakref(layer)]
	assert_false(v.play_sprite_animation(sf), "refused")
	expect_error_sequence(["play_sprite_animation: this volley is in the pool (its last bullet died), so this handle is stale. Spawn a new volley instead."])
	var layers: Array[BulletEffectLayerData2D] = [layer]
	v.set_effect_layers(layers)
	expect_error_sequence(["set_effect_layers: this volley is in the pool (its last bullet died), so this handle is stale. Spawn a new volley instead."])
	sf = null
	layer = null
	layers.clear()
	assert_eq([refs[0].get_ref(), refs[1].get_ref()], [null, null], "nothing kept alive by the pooled volley")
