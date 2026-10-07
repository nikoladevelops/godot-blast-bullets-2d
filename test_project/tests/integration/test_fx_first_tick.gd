extends BlastTest
## One-shot newborn timing: an effect fired mid-sweep (hit, bounce, expiry)
## must show frame 0 at the end of its birth sweep and live its full total.
## The birth sweep's delta covers time BEFORE the birth; aging the newborn by
## it in the same sweep shortens frame 0 (skipped when shorter than a tick)
## and kills the effect one sweep early.


## Two-frame layer: frame 0 lasts 0.5/60 s (shorter than one simulated tick),
## frame 1 lasts 5.0/60 s. Total 5.5/60 s: 6 aging sweeps with 0.5-tick margins
## on both expiry boundaries, so float epsilon cannot flip the counts.
func _short_first_frame_layer(trigger: int) -> BulletEffectLayerData2D:
	var sf := SpriteFrames.new()
	sf.set_animation_speed("default", 60.0)
	sf.set_animation_loop("default", false)
	var img := Image.create_empty(8, 8, false, Image.FORMAT_RGBA8)
	img.fill(Color.WHITE)
	sf.add_frame("default", ImageTexture.create_from_image(img), 0.5)
	sf.add_frame("default", ImageTexture.create_from_image(img), 5.0)
	var layer := BulletEffectLayerData2D.new()
	layer.trigger = trigger
	layer.sprite_frames = sf
	return layer


func _expiry_shard() -> int:
	var st: Dictionary = factory.debug_get_effect_state()
	var bakes: Array = st.get("bakes", [])
	if bakes.is_empty():
		return -9
	var live: Array = (bakes[0] as Dictionary).get("live_slots", [])
	if live.is_empty():
		return -1
	return int((live[0] as Dictionary).get("shard", -9))


func test_mid_sweep_fire_shows_frame_zero_on_its_birth_sweep() -> void:
	var data := H.make_volley_data(1, 0.0, 0.05) # expires on the 3rd tick, mid-sweep
	data.effect_layers = [_short_first_frame_layer(BulletEffectLayerData2D.EFFECT_ON_LIFETIME_OVER)]
	var v: BulletVolley2D = factory.spawn_volley(data)
	assert_not_null(v, "volley spawned")
	for i in 12: # early-break wait: expiry lands on the 3rd tick
		await physics(1)
		if factory.debug_get_active_bullets_amount() == 0:
			break
	assert_eq(factory.debug_get_active_bullets_amount(), 0, "volley expired")
	assert_eq(_expiry_shard(), 0, "birth sweep shows frame zero, not frame one")


func test_mid_sweep_fire_lives_its_full_total() -> void:
	var data := H.make_volley_data(1, 0.0, 0.05) # expires on the 3rd tick, mid-sweep
	data.effect_layers = [_short_first_frame_layer(BulletEffectLayerData2D.EFFECT_ON_LIFETIME_OVER)]
	var v: BulletVolley2D = factory.spawn_volley(data)
	assert_not_null(v, "volley spawned")
	var live_sweeps := 0
	var saw_death := false
	for i in 14: # early-break wait: 5.5-tick total can never need more
		await physics(1)
		if factory.debug_get_active_bullets_amount() != 0:
			continue
		if _expiry_shard() == -1:
			saw_death = live_sweeps > 0
			if saw_death:
				break
		else:
			live_sweeps += 1
	assert_true(saw_death, "effect died within the window")
	assert_eq(live_sweeps, 6, "a 5.5-tick effect stays live for 6 sweeps, not 5")
