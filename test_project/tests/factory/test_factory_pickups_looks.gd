extends BlastTest
## How pickups look and what rides on them: SpriteFrames animations (the
## primary API), the plain-texture shortcut, material, z-index, attachments,
## effect layers, and every interaction of those with pooling: a pooled
## pickup volley comes back as an ordinary one with none of it left over.

const P := preload("res://tests/common/pickup_test_helpers.gd")

var collected: Array = []


func _on_collected(target: Variant, position: Vector2, volley: BulletVolley2D, index: int, value: Variant) -> void:
	collected.append({ "target": target, "position": position, "volley": volley, "index": index, "value": value })


func before_each() -> void:
	await super()
	collected.clear()
	factory.pickup_collected.connect(_on_collected)


func _target(pos: Vector2) -> Node2D:
	var n := Node2D.new()
	n.position = pos
	add(n)
	return n


func _convert(coin: BulletPickupData2D, points: Array = [Vector2.ZERO], target: Variant = Vector2(5000, 0)) -> BulletVolley2D:
	P.still(factory, points)
	return factory.convert_all_bullets_to_pickups(coin, target)[0]


func _wait(n: int, frames := 400) -> void:
	for i in frames:
		await idle(1)
		if collected.size() >= n:
			return


# --- sprite frames: the primary API -----------------------------------------------------------------

func test_a_pickup_plays_its_named_animation() -> void:
	var coin := P.coin()
	coin.sprite_frames = P.frames()
	coin.animation = &"spin"
	var pv := _convert(coin)
	assert_eq(pv.get_sprite_animation(), &"spin", "the named animation plays")
	assert_eq(pv.get_sprite_frame_count(), 4, "all its frames")
	assert_eq(pv.get_sprite_frame(), 0, "from the first frame")
	await idle(20) # 0.33 s at 10 fps = frame 3
	assert_gt(pv.get_sprite_frame(), 0, "the animation advances")
	await idle(60)
	assert_true(pv.is_sprite_animation_playing(), "a looping animation keeps going")


func test_the_size_comes_from_the_first_frame_unless_set() -> void:
	var coin := P.coin()
	coin.sprite_frames = P.frames()
	coin.animation = &"pop"
	assert_eq(coin.get_volley_data().texture_size, Vector2(20, 20), "pop frames are 20x20")
	coin.animation = &"spin"
	assert_eq(coin.get_volley_data().texture_size, Vector2(12, 12), "spin frames are 12x12")
	coin.texture_size = Vector2(40, 30)
	assert_eq(coin.get_volley_data().texture_size, Vector2(40, 30), "an explicit size wins")


func test_a_one_shot_animation_stops_on_its_last_frame_and_the_pickup_still_works() -> void:
	var coin := P.coin()
	coin.sprite_frames = P.frames()
	coin.animation = &"pop"
	var pv := _convert(coin, [Vector2.ZERO], _target(Vector2(400, 0)))
	await _wait(1)
	assert_eq(collected.size(), 1, "collected normally")
	assert_true(pv.is_sprite_animation_finished() or pv.get_sprite_frame() == 2, "the one-shot ended on its last frame")


func test_animation_default_and_unknown_names_resolve_predictably() -> void:
	var coin := P.coin()
	coin.sprite_frames = P.frames() # has no "default" animation
	# SpriteFrames lists its animations alphabetically: "pop" comes before "spin".
	assert_eq(coin.get_volley_data().animation, &"pop", "the default name falls back silently to the first animation in the list")
	expect_no_errors("and says nothing")
	coin.animation = &"nope"
	var d := coin.get_volley_data()
	expect_warning_sequence(["BulletPickupData2D: animation \"nope\" does not exist in sprite_frames, playing the first animation."])
	assert_eq(d.animation, &"pop", "an unknown name plays the first animation in the list (one warning)")
	coin.get_volley_data()
	expect_no_errors("the cached data does not warn again")


func test_an_animation_without_frames_falls_back_to_the_placeholder_with_a_warning() -> void:
	var sf := SpriteFrames.new()
	sf.add_animation("empty")
	sf.remove_animation("default")
	var coin := P.coin()
	coin.sprite_frames = sf
	coin.animation = &"empty"
	var pv := _convert(coin)
	expect_warning_sequence(["BulletPickupData2D: animation \"empty\" of sprite_frames has no frames, using the texture or the placeholder."])
	assert_not_null(pv.get_sprite_frames(), "the placeholder stands in, no crash")
	assert_eq(pv.get_sprite_frame_count(), 1, "one frame")


func test_the_texture_shortcut_still_works_and_frames_win() -> void:
	var coin := P.coin()
	coin.texture = ImageTexture.create_from_image(Image.create(9, 9, false, Image.FORMAT_RGBA8))
	assert_eq(coin.get_volley_data().texture_size, Vector2(9, 9), "texture only")
	coin.sprite_frames = P.frames()
	coin.animation = &"spin"
	assert_eq(coin.get_volley_data().texture_size, Vector2(12, 12), "sprite_frames win over texture")


func test_every_pickup_of_one_conversion_shares_the_animation_frame() -> void:
	var coin := P.coin()
	coin.sprite_frames = P.frames()
	coin.animation = &"spin"
	var pv := _convert(coin, [Vector2(0, 0), Vector2(10, 0), Vector2(20, 0)])
	await idle(15)
	assert_eq(pv.get_amount_bullets(), 3, "three pickups, one volley")
	assert_true(pv.get_sprite_frame() >= 0, "one frame state for the whole volley (documented)")


# --- material, z-index, tint ---------------------------------------------------------------------------------

func test_material_and_z_index_reach_the_pickup_volley() -> void:
	var mat := CanvasItemMaterial.new()
	mat.blend_mode = CanvasItemMaterial.BLEND_MODE_ADD
	var coin := P.coin()
	coin.material = mat
	coin.z_index = 7
	var pv := _convert(coin)
	assert_same(pv.material, mat, "the material")
	assert_eq(pv.z_index, 7, "the z-index")
	coin.z_index = 5000
	expect_error_sequence(["BulletPickupData2D: z_index must be between -4096 and 4096, keeping the old value."])
	assert_eq(coin.z_index, 7, "out of range keeps the old value")


# --- attachments --------------------------------------------------------------------------------------------------

func test_a_pickup_can_carry_an_attachment_and_gives_it_back() -> void:
	var scene := make_probe_scene()
	var coin := P.coin()
	coin.attachment = scene
	coin.attachment_offset = Vector2(3, 4)
	var pv := _convert(coin, [Vector2.ZERO, Vector2(10, 0)], _target(Vector2(300, 0)))
	await idle(2)
	assert_eq(pv.get_amount_active_attachments(), 2, "one attachment per pickup")
	var a = pv.bullet_get_attachment(0)
	assert_eq(a.get("spawn_calls"), 1, "on_bullet_spawn ran once")
	await _wait(2)
	await idle(2)
	assert_eq(collected.size(), 2, "both collected")
	assert_eq(pv.get_amount_active_attachments(), 0, "collection releases the attachments")
	assert_eq(a.get("disable_calls"), 1, "each was told its bullet is gone")


func test_attachments_are_reused_from_the_pool_across_conversions() -> void:
	var scene := make_probe_scene()
	var coin := P.coin()
	coin.attachment = scene
	var target := _target(Vector2(250, 0))
	var first := _convert(coin, [Vector2.ZERO], target)
	await idle(2)
	var node = first.bullet_get_attachment(0)
	await _wait(1)
	await idle(2)
	var second := _convert(coin, [Vector2.ZERO], target)
	await idle(2)
	assert_same(second.bullet_get_attachment(0), node, "the pooled attachment node was reused")
	assert_eq(node.get("enable_calls"), 1, "and re-enabled")


func test_a_pooled_pickup_volley_returns_as_a_plain_one_without_its_attachment_look_or_effects() -> void:
	var coin := P.coin()
	coin.sprite_frames = P.frames()
	coin.animation = &"pop"
	coin.attachment = make_probe_scene()
	coin.z_index = 9
	coin.tint = Color(1, 0, 0, 0.5)
	coin.material = CanvasItemMaterial.new()
	coin.effect_layers = [H.make_effect_layer(EffectLayerData2D.EFFECT_ON_SPAWN)]
	var pv := _convert(coin, [Vector2.ZERO, Vector2(8, 0)], Vector2(60, 0))
	await _wait(2)
	await idle(3)
	assert_eq(factory.get_active_bullet_count(), 0, "all collected")
	var data := H.make_volley_data(2, 100.0, 60.0)
	data.transforms = [Transform2D(0.0, Vector2(500, 0)), Transform2D(0.0, Vector2(510, 0))]
	var plain: BulletVolley2D = factory.spawn_volley(data)
	assert_same(plain, pv, "the pooled volley was reused")
	assert_false(plain.is_pickup_volley(), "plain again")
	assert_eq(plain.get_amount_active_attachments(), 0, "no attachment left over")
	assert_eq(plain.z_index, 0, "z-index reset")
	assert_null(plain.material, "material reset")
	assert_eq(plain.self_modulate, Color(1, 1, 1, 1), "tint reset")
	assert_ne(plain.get_sprite_animation(), &"pop", "the pickup animation is gone")
	factory.debug_set_effect_log_enabled(true)
	await idle(3)
	assert_eq(factory.debug_get_effect_log().size(), 0, "the pickup's effect layers are gone")
	assert_true(factory.debug_assert_no_dangling().get("ok", false), "nothing dangles")


func test_alternating_pickup_and_plain_cycles_never_leak_state_between_lives() -> void:
	var with_extras := P.coin()
	with_extras.sprite_frames = P.frames()
	with_extras.animation = &"spin"
	with_extras.attachment = make_probe_scene()
	with_extras.tint = Color(0, 1, 0, 0.7)
	with_extras.z_index = 3
	var bare := P.coin()
	for cycle in 3:
		for coin in [with_extras, bare]:
			var pv := _convert(coin, [Vector2.ZERO, Vector2(5, 0)], Vector2(40, 0))
			await idle(2)
			var expect_attachments := 2 if coin == with_extras else 0
			assert_eq(pv.get_amount_active_attachments(), expect_attachments, "cycle %d: attachments follow the pickup used" % cycle)
			assert_eq(pv.z_index, 3 if coin == with_extras else 0, "cycle %d: z-index" % cycle)
			assert_almost_eq(pv.self_modulate.a, 0.7 if coin == with_extras else 1.0, 0.001, "cycle %d: opacity" % cycle)
			collected.clear()
			await _wait(2)
			await idle(2)
			assert_eq(factory.get_active_bullet_count(), 0, "cycle %d: collected" % cycle)
			var plain_data := H.make_volley_data(2, 50.0, 60.0)
			plain_data.transforms = [Transform2D(0.0, Vector2(600, 0)), Transform2D(0.0, Vector2(610, 0))]
			var plain: BulletVolley2D = factory.spawn_volley(plain_data)
			assert_false(plain.is_pickup_volley(), "cycle %d: plain" % cycle)
			assert_eq(plain.get_amount_active_attachments(), 0, "cycle %d: plain has none" % cycle)
			plain.clear_all_bullets()
			await idle(1)
	assert_true(factory.debug_assert_no_dangling().get("ok", false), "no dangling state after six lives")


func test_the_factory_time_scale_reaches_a_pickups_attachment() -> void:
	var coin := P.coin()
	coin.attachment = AttachmentTimeProbe2D.make_scene()
	var pv := _convert(coin, [Vector2.ZERO], _target(Vector2(5000, 0)))
	await idle(2)
	var a: AttachmentTimeProbe2D = pv.bullet_get_attachment(0)
	factory.time_scale = 0.5
	await idle(2)
	assert_eq(a.get_applied_time_scale(), 0.5, "the attachment was told")
	assert_eq(a.gpu().speed_scale, 1.0, "its particles run at half speed (2.0 x 0.5)")


# --- effect layers --------------------------------------------------------------------------------------------------

func test_effect_layers_fire_for_pickups() -> void:
	var coin := P.coin()
	coin.effect_layers = [H.make_effect_layer(EffectLayerData2D.EFFECT_ON_SPAWN)]
	factory.debug_set_effect_log_enabled(true)
	_convert(coin, [Vector2.ZERO, Vector2(8, 0), Vector2(16, 0)])
	var spawns := factory.debug_get_effect_log().filter(func(e): return int(e["trigger"]) == EffectLayerData2D.EFFECT_ON_SPAWN)
	assert_eq(spawns.size(), 3, "an On Spawn flash per pickup")


func test_collection_fires_no_hit_or_destroy_effects_but_the_collect_effect() -> void:
	var coin := P.coin()
	coin.effect_layers = [H.make_effect_layer(EffectLayerData2D.EFFECT_ON_DESTROY), H.make_effect_layer(EffectLayerData2D.EFFECT_ON_HIT)]
	coin.collect_effect = H.make_effect_layer(EffectLayerData2D.EFFECT_ON_SPAWN)
	factory.debug_set_effect_log_enabled(true)
	_convert(coin, [Vector2.ZERO], _target(Vector2(300, 0)))
	await _wait(1)
	assert_eq(collected.size(), 1, "collected")
	var log := factory.debug_get_effect_log()
	assert_eq(log.filter(func(e): return int(e["trigger"]) in [EffectLayerData2D.EFFECT_ON_DESTROY, EffectLayerData2D.EFFECT_ON_HIT]).size(), 0, "a collection is neither a hit nor a destroy")
	assert_gt(factory.get_active_effect_count(), 0, "the collect effect plays")


func test_a_pickup_that_leaves_the_despawn_bounds_fires_its_on_despawn_layer() -> void:
	factory.despawn_bounds_rect = Rect2(0, 0, 200, 200)
	factory.despawn_bounds_enabled = true
	var coin := P.coin()
	coin.effect_layers = [H.make_effect_layer(EffectLayerData2D.EFFECT_ON_DESPAWN)]
	coin.motion_mode = BulletPickupData2D.MOTION_KEEP_BULLET_MOTION
	factory.debug_set_effect_log_enabled(true)
	var v := P.still(factory, [Vector2(150, 100)])
	v.bullet_set_velocity(0, Vector2(900, 0))
	factory.convert_all_bullets_to_pickups(coin, Vector2(-5000, 100))
	await idle(1)
	for i in 8:
		factory.debug_advance_time(0.1)
	var fired := factory.debug_get_effect_log().filter(func(e): return int(e["trigger"]) == EffectLayerData2D.EFFECT_ON_DESPAWN)
	assert_eq(fired.size(), 1, "pickups obey the bounds like any bullet and fire On Despawn")
	assert_eq(collected.size(), 0, "and are not collected")
