extends BlastTest
## BulletPickupData2D as a resource: defaults, `changed` only on accepted
## edits, the generated spawn data follows every edit (cached until then),
## texture / animation / placeholder looks, fades that cannot outlast the
## lifetime, infinite lifetimes, and a save/load round trip. The generic
## reject-and-keep sweep lives in test_data_setter_contract.


func _count_changed(p: BulletPickupData2D) -> Array:
	var counter := [0]
	p.changed.connect(func(): counter[0] += 1)
	return counter


func test_defaults() -> void:
	var p := BulletPickupData2D.new()
	assert_eq(p.scale_multiplier, 1.0, "scale 1")
	assert_eq(p.tint, Color(1, 1, 1, 1), "opaque white")
	assert_eq(p.lifetime_sec, 12.0, "12 s")
	assert_eq(p.collect_radius, 24.0, "24 px")
	assert_eq(p.magnet_delay_sec, 0.35, "short burst before the magnet")
	assert_eq([p.collision_layer, p.collision_mask], [0, 0], "no physics by default")
	assert_null(p.value, "no payload")
	assert_false(p.inherit_custom_data, "custom data not copied")
	assert_false(p.fire_clear_effects, "silent conversion")


func test_accepted_edits_emit_changed_and_rejected_ones_do_not() -> void:
	var p := BulletPickupData2D.new()
	var n := _count_changed(p)
	p.collect_radius = 40.0
	p.tint = Color(1, 0, 0, 0.5)
	p.value = "gem"
	p.collision_layer = 8
	assert_eq(n[0], 4, "four accepted edits, four changes")
	p.collect_radius = 40.0
	assert_eq(n[0], 4, "setting the same radius again is not a change")
	p.collect_radius = -1.0
	expect_error_sequence(["BulletPickupData2D: collect_radius must be finite and > 0, keeping the old value."])
	p.drag = NAN
	expect_error_sequence(["BulletPickupData2D: drag must be finite and >= 0, keeping the old value."])
	p.tint = Color(NAN, 0, 0, 1)
	expect_error_sequence(["BulletPickupData2D: tint must be finite, keeping the old value."])
	p.texture_size = Vector2(-1, 5)
	expect_error_sequence(["BulletPickupData2D: texture_size must be finite with both components >= 0, keeping the old value."])
	p.collision_mask = -2
	expect_error_sequence(["BulletPickupData2D: collision_mask must be >= 0, keeping the old value."])
	assert_eq(n[0], 4, "no change for any rejected edit")


func test_scatter_and_other_pairs_never_reject_against_each_other() -> void:
	var p := BulletPickupData2D.new()
	p.scatter_speed_min = 300.0 # above the current max of 0: accepted
	assert_eq(p.scatter_speed_min, 300.0, "no pair rejection")
	p.scatter_speed_max = 100.0
	assert_eq([p.scatter_speed_min, p.scatter_speed_max], [300.0, 100.0], "stored as given; normalized at use")


func test_spawn_data_follows_edits() -> void:
	var p := BulletPickupData2D.new()
	p.tint = Color(1, 0, 0, 0.25)
	p.lifetime_sec = 3.0
	p.collect_radius = 33.0
	var a := p.get_volley_data()
	assert_same(p.get_volley_data(), a, "cached while nothing changes")
	assert_eq(a.self_modulate, Color(1, 0, 0, 0.25), "tint -> self_modulate")
	assert_eq(a.max_life_time, 3.0, "lifetime")
	assert_false(a.is_life_time_infinite, "finite")
	assert_eq(a.homing_distance_before_reached, 33.0, "collect radius -> reach distance")
	p.tint = Color(0, 1, 0, 1)
	var b := p.get_volley_data()
	assert_not_same(b, a, "an edit drops the cache")
	assert_eq(b.self_modulate, Color(0, 1, 0, 1), "and the new data has the new tint")


func test_lifetime_zero_means_infinite_and_fades_never_exceed_the_lifetime() -> void:
	var p := BulletPickupData2D.new()
	p.lifetime_sec = 0.0
	p.fade_out_sec = 2.0
	var inf := p.get_volley_data()
	assert_true(inf.is_life_time_infinite, "lifetime 0 = infinite")
	assert_eq(inf.fade_out_sec, 0.0, "fade-out needs an expiry: dropped")
	p.lifetime_sec = 1.0
	p.fade_in_sec = 5.0
	p.fade_out_sec = 5.0
	var d := p.get_volley_data()
	assert_eq([d.fade_in_sec, d.fade_out_sec], [1.0, 1.0], "fades are clamped to the lifetime, no errors")
	expect_no_errors("building the data never complains")


func test_looks_texture_animation_and_placeholder() -> void:
	var plain := BulletPickupData2D.new()
	var placeholder := plain.get_volley_data()
	assert_not_null(placeholder.sprite_frames, "a placeholder disc stands in")
	assert_eq(placeholder.texture_size, Vector2(16, 16), "at its own size")
	var p := BulletPickupData2D.new()
	var image := Image.create(32, 24, false, Image.FORMAT_RGBA8)
	p.texture = ImageTexture.create_from_image(image)
	assert_eq(p.get_volley_data().texture_size, Vector2(32, 24), "a texture sets the size")
	p.texture_size = Vector2(10, 10)
	assert_eq(p.get_volley_data().texture_size, Vector2(10, 10), "an explicit size wins")
	var frames := SpriteFrames.new()
	frames.add_frame("default", ImageTexture.create_from_image(Image.create(8, 8, false, Image.FORMAT_RGBA8)))
	p.texture_size = Vector2.ZERO
	p.sprite_frames = frames
	var animated := p.get_volley_data()
	assert_same(animated.sprite_frames, frames, "an animation beats the plain texture")
	assert_eq(animated.texture_size, Vector2(8, 8), "size from its first frame")


func test_physics_only_when_a_layer_or_mask_is_set() -> void:
	var p := BulletPickupData2D.new()
	assert_false(p.get_volley_data().emit_collision_signals, "pure distance collection: no collision signals")
	p.collision_mask = 4
	p.collision_radius = 12.0
	var d := p.get_volley_data()
	assert_eq(d.collision_mask, 4, "mask")
	assert_not_null(d.collision_shape, "a circle shape")
	assert_eq((d.collision_shape as CircleShape2D).radius, 12.0, "of the collision radius")
	assert_true(d.emit_collision_signals, "and the factory's area_entered works")


func test_resource_round_trip() -> void:
	var p := BulletPickupData2D.new()
	p.tint = Color(0.2, 0.4, 0.6, 0.8)
	p.value = { "coins": 3 }
	p.lifetime_sec = 7.5
	p.magnet_max_speed = 321.0
	p.collision_layer = 16
	p.inherit_custom_data = true
	var path := "user://blast_pickup_roundtrip.tres"
	assert_eq(ResourceSaver.save(p, path), OK, "saves")
	var loaded: BulletPickupData2D = ResourceLoader.load(path, "", ResourceLoader.CACHE_MODE_IGNORE)
	for prop in ["tint", "value", "lifetime_sec", "magnet_max_speed", "collision_layer", "inherit_custom_data"]:
		assert_eq(loaded.get(prop), p.get(prop), prop + " survives")
	DirAccess.remove_absolute(ProjectSettings.globalize_path(path))
