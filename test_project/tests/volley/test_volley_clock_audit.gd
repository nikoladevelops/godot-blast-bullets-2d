extends BlastTest
## Clock audit. Every clock a volley advances (the curve clock, lifetime,
## fade, sprite animation, homing interval, custom timers; the gravity,
## homing and wobble windows all read the curve clock):
## - restarts on a new life (pool reuse);
## - stands still while the volley is PARKED and while the factory is paused,
##   then resumes exactly where it stopped;
## - advances exactly once per tick, even when a handler pools another volley
##   mid-sweep (swap-remove) or spawns one (which starts next tick);
## - stays finite and consistent under a 0.5 s hitch and a zero delta.
## Timers attached with execute_only_if_volley_is_active (the default) HOLD
## while the volley is parked, so a one-shot is never consumed unseen; the
## others keep running, so a parked volley can be woken by its own timer.

const TICK := 1.0 / 60.0
const VOLLEY_CLOCKS := ["curves_elapsed_time", "life_time_left", "anim_frame_index", "anim_frame_time_left", "fade_alpha", "homing_update_timer"]


## Still bullets with every clock armed: 3 s life, 4-frame non-looping
## animation at 10 fps (0.1 s per frame), 1 s fade-in, 0.25 s homing interval.
## Timing: from an idle point, await idle(k) runs exactly k factory ticks.
func _data(n := 2, life := 3.0, lifetime_signal := false) -> BulletVolleyData2D:
	var d := H.make_volley_data(n, 0.0, life)
	d.is_life_time_over_signal_enabled = lifetime_signal
	d.sprite_frames = H.make_effect_frames(4, 10.0)
	d.fade_in_sec = 1.0
	d.homing_update_interval = 0.25
	return d


func _armed(life := 3.0, lifetime_signal := false) -> BulletVolley2D:
	var v: BulletVolley2D = factory.spawn_volley(_data(2, life, lifetime_signal))
	v.shared_homing_deque_push_back_global_position_target(Vector2(5000, 0)) # drives the homing interval
	return v


func _volley_clocks(v: BulletVolley2D) -> Array:
	var c: Dictionary = v.debug_get_clocks()
	var out: Array = []
	for k in VOLLEY_CLOCKS:
		out.append(c[k])
	for t in c["timers"]:
		out.append(t["time_left"])
	return out


func _curve(v: BulletVolley2D) -> float:
	return v.debug_get_clocks()["curves_elapsed_time"]


func _all_finite(c: Dictionary) -> bool:
	for k in VOLLEY_CLOCKS:
		if not is_finite(float(c[k])):
			return false
	for t in c["timers"]:
		if not is_finite(float(t["time_left"])):
			return false
	return true


func test_every_clock_restarts_on_a_new_life() -> void:
	var v := _armed()
	v.attach_time_based_function(0.5, func(): pass, true)
	var fresh := _volley_clocks(v)
	await idle(20)
	var aged: Dictionary = v.debug_get_clocks()
	assert_almost_eq(float(aged["curves_elapsed_time"]), 20 * TICK, 1e-6, "the curve clock advanced 20 ticks")
	assert_almost_eq(float(aged["life_time_left"]), 3.0 - 20 * TICK, 1e-6, "the life clock in lockstep")
	assert_eq(int(aged["anim_frame_index"]), 3, "animation moved on (0.33 s at 0.1 s per frame)")
	assert_almost_eq(float(aged["fade_alpha"]), 20 * TICK, 1e-6, "fade-in in lockstep (1 s ramp)")
	assert_ne(float(aged["homing_update_timer"]), 0.0, "the homing interval ticks")
	assert_almost_eq(float(aged["timers"][0]["time_left"]), 0.5 - 20 * TICK, 1e-6, "the timer in lockstep")
	v.disable_bullet(0)
	v.disable_bullet(1)
	assert_true(v.is_pooled(), "pooled")
	assert_eq(v.debug_get_clocks()["timers"], [], "a pooled volley holds no timers")
	var reused := _armed()
	assert_same(reused, v, "pool reuse")
	reused.attach_time_based_function(0.5, func(): pass, true)
	assert_eq(_volley_clocks(reused), fresh, "every clock restarts on the new life")


func test_a_parked_volley_holds_every_clock_and_resumes_on_wake() -> void:
	var v := _armed()
	v.attach_time_based_function(1.0, func(): pass, true)
	v.set_is_auto_pooling_enabled(false)
	await idle(6)
	v.disable_bullet(0)
	v.disable_bullet(1)
	assert_eq(v.debug_get_life_state(), "parked", "parked, not pooled")
	var held := _volley_clocks(v)
	await idle(45)
	assert_eq(_volley_clocks(v), held, "nothing advances while parked")
	v.enable_bullet(0)
	await idle(12)
	assert_almost_eq(_curve(v), 18 * TICK, 1e-6, "the clock resumed: 6 live ticks + 12 after the wake, none while parked")
	assert_almost_eq(float(v.debug_get_clocks()["timers"][0]["time_left"]), 1.0 - 18 * TICK, 1e-6, "the timer resumed in lockstep")


func test_an_active_only_one_shot_survives_parking_and_fires_after_the_wake() -> void:
	var v := _armed()
	var fired := {"active_only": 0, "always": 0}
	v.attach_time_based_function(0.2, func(): fired["active_only"] += 1) # execute_only_if_volley_is_active = true
	v.attach_time_based_function(0.2, func(): fired["always"] += 1, false, false)
	v.set_is_auto_pooling_enabled(false)
	await idle(6) # 0.1 s
	v.disable_bullet(0)
	v.disable_bullet(1)
	await idle(30) # 0.5 s parked: both would be due
	assert_eq(fired, {"active_only": 0, "always": 1}, "only the always-run timer fires while parked")
	assert_eq(v.debug_get_timer_count(), 1, "the active-only one-shot is held, not consumed")
	v.enable_bullet(1)
	await idle(5)
	assert_eq(fired["active_only"], 0, "held time resumes: 0.1 s were left, not due yet")
	await idle(2) # the 6th live tick lands on 0.1 s; float rounding may push it one tick
	assert_eq(fired, {"active_only": 1, "always": 1}, "the held one-shot fires once, 0.1 s after the wake")
	assert_eq(v.debug_get_timer_count(), 0, "and is gone afterwards")


func test_a_parked_volley_can_wake_itself_with_an_always_run_timer() -> void:
	var v := _armed()
	v.set_is_auto_pooling_enabled(false)
	v.attach_time_based_function(0.25, func(): v.enable_bullet(0), false, false)
	v.disable_bullet(0)
	v.disable_bullet(1)
	assert_eq(v.debug_get_life_state(), "parked", "parked")
	await idle(14)
	assert_false(v.is_bullet_status_enabled(0), "not before 0.25 s")
	await idle(2) # the 15th tick lands on 0.25 s; float rounding may push it one tick
	assert_true(v.is_bullet_status_enabled(0), "the timer woke the volley at 0.25 s")
	assert_eq(v.debug_get_life_state(), "active", "active again")


func test_a_paused_factory_freezes_every_clock() -> void:
	var v := _armed()
	v.attach_time_based_function(0.5, func(): pass, true)
	await idle(3)
	factory.set_is_factory_processing_bullets(false)
	var held := _volley_clocks(v)
	await idle(40)
	assert_eq(_volley_clocks(v), held, "nothing advances while the factory is paused")
	assert_false(factory.debug_advance_time(0.5), "a manual step is refused while paused too")
	assert_eq(_volley_clocks(v), held, "and changed nothing")
	factory.set_is_factory_processing_bullets(true)
	await idle(6)
	assert_almost_eq(_curve(v), 9 * TICK, 1e-6, "resumes exactly: 3 ticks before the pause + 6 after")


## A expires on the first tick; its handler runs `action` on B (not ticked
## yet) and spawns D. Returns [b, c, d] after exactly one tick.
func _mid_sweep(action: Callable) -> Array:
	var a: BulletVolley2D = factory.spawn_volley(_data(1, 0.01, true))
	var b: BulletVolley2D = factory.spawn_volley(_data(3))
	var c: BulletVolley2D = factory.spawn_volley(_data(4))
	var out := [b, c, null]
	factory.life_time_over.connect(func(v: BulletVolley2D, _idx: Array):
		if v == a:
			action.call(b)
			out[2] = factory.spawn_volley(_data(5)))
	await idle(1)
	return out


func test_pooling_a_volley_mid_sweep_keeps_every_other_volley_on_one_tick() -> void:
	var r := await _mid_sweep(func(b: BulletVolley2D):
		for i in 3:
			b.disable_bullet(i))
	assert_true((r[0] as BulletVolley2D).is_pooled(), "B went to the pool inside A's handler")
	assert_almost_eq(_curve(r[1]), TICK, 1e-9, "C ticked exactly once")
	assert_almost_eq(_curve(r[2]), 0.0, 1e-9, "a volley spawned mid-sweep does not tick that frame")
	await idle(1)
	assert_almost_eq(_curve(r[1]), 2 * TICK, 1e-9, "C: one tick per frame")
	assert_almost_eq(_curve(r[2]), TICK, 1e-9, "the new volley starts on the next tick")


func test_freeing_a_volley_mid_sweep_never_skips_the_volley_swapped_into_its_slot() -> void:
	# free() swap-removes the volley list: the LAST volley (C) moves into B's
	# slot, which the sweep has not reached, while C's own slot is gone.
	var r := await _mid_sweep(func(b: BulletVolley2D): b.free())
	assert_false(is_instance_valid(r[0]), "B freed inside A's handler")
	assert_almost_eq(_curve(r[1]), TICK, 1e-9, "C still ticked exactly once on the swap-remove frame")
	await idle(1)
	assert_almost_eq(_curve(r[1]), 2 * TICK, 1e-9, "C: one tick per frame afterwards")
	assert_almost_eq(_curve(r[2]), TICK, 1e-9, "the volley spawned in the handler started on the next tick")


func test_freeing_an_already_ticked_volley_mid_sweep_never_skips_the_last_one() -> void:
	# B's handler frees A (already ticked): the last volley moves into A's
	# slot, BEHIND the sweep, and must still tick this frame.
	var a: BulletVolley2D = factory.spawn_volley(_data(2))
	var b: BulletVolley2D = factory.spawn_volley(_data(1, 0.01, true))
	var c: BulletVolley2D = factory.spawn_volley(_data(4))
	factory.life_time_over.connect(func(v: BulletVolley2D, _idx: Array):
		if v == b and is_instance_valid(a):
			a.free())
	await idle(1)
	assert_false(is_instance_valid(a), "A freed inside B's handler")
	assert_almost_eq(_curve(c), TICK, 1e-9, "C ticked exactly once although it moved behind the sweep")


func test_a_warm_respawn_mid_sweep_waits_for_the_next_tick_like_a_cold_one() -> void:
	# B pools inside A's handler, then the same handler respawns B's key: the
	# pool hands B back for a new life while its snapshot entry is still
	# ahead in this sweep. It must start next tick, exactly like a cold spawn.
	var a: BulletVolley2D = factory.spawn_volley(_data(1, 0.01, true))
	var b: BulletVolley2D = factory.spawn_volley(_data(3))
	var reborn := [null]
	factory.life_time_over.connect(func(v: BulletVolley2D, _idx: Array):
		if v == a:
			for i in 3:
				b.disable_bullet(i)
			reborn[0] = factory.spawn_volley(_data(3)))
	await idle(1)
	assert_same(reborn[0], b, "the pool handed B back mid-sweep")
	assert_almost_eq(_curve(b), 0.0, 1e-9, "the new life did not tick on its spawn frame")
	await idle(1)
	assert_almost_eq(_curve(b), TICK, 1e-9, "one tick per frame from the next frame on")


func test_a_half_second_hitch_and_a_zero_delta_stay_finite_and_consistent() -> void:
	var v := _armed(0.4, true)
	var fired := [0]
	v.attach_time_based_function(0.1, func(): fired[0] += 1, true)
	var expired := [0]
	factory.life_time_over.connect(func(_v, idx: Array): expired[0] += idx.size())
	# One live tick first: a fresh volley's first homing decision is due at
	# t = 0, so even a zero step would (legitimately) re-arm that interval.
	await idle(1)
	var before := _volley_clocks(v)
	assert_true(factory.debug_advance_time(0.0), "a zero step runs")
	assert_eq(_volley_clocks(v), before, "and advances nothing")
	assert_true(factory.debug_advance_time(0.25), "a quarter-second hitch")
	var c: Dictionary = v.debug_get_clocks()
	assert_true(_all_finite(c), "every clock finite after the hitch")
	assert_almost_eq(float(c["curves_elapsed_time"]), 0.25 + TICK, 1e-9, "curve clock jumped by the hitch")
	assert_almost_eq(float(c["life_time_left"]), 0.15 - TICK, 1e-9, "life clock jumped by the hitch")
	assert_eq(int(c["anim_frame_index"]), 2, "animation advanced two frames in one step (0.27 s at 0.1 s per frame)")
	assert_almost_eq(float(c["fade_alpha"]), 0.25 + TICK, 1e-6, "fade follows the curve clock")
	assert_eq(fired[0], 1, "a repeating timer fires once per tick, never a burst (anti-spiral)")
	assert_true(float(c["timers"][0]["time_left"]) > 0.0, "and resyncs to a positive time")
	assert_true(factory.debug_advance_time(0.5), "a half-second hitch past the end of life")
	assert_eq(expired[0], 2, "both bullets expired exactly once")
	assert_true(v.is_pooled(), "the expired volley pooled")
	assert_eq(fired[0], 1, "a pooled volley's timer never fires again")
	for bad in [-0.1, INF, NAN]:
		assert_false(factory.debug_advance_time(bad), "rejected: %s" % bad)
		expect_error_sequence(["BulletFactory2D::debug_advance_time: delta must be finite and >= 0, nothing advanced."])
