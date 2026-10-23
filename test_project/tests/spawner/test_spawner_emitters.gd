extends BlastTest
## Emitter bullets: with the spawner's Emitter Bullets group on, every flying
## bullet fires the whole pattern of the template spawner at
## emitter_spawner_path, timed by a BulletEmitterData2D (interval, start delay,
## stagger per bullet index, max shots, seeded chance), turned by its aim
## (flight heading, world, template rotation) plus spin per shot, optionally
## inheriting the bullet's velocity. Shots are recorded in the tick and fired
## by the factory after the sweep (per-sweep cap carries over in order, the
## pending cap drops with one warning). The template fires its own setup and
## signals; chains of emitters work, a chain back to the spawner is refused
## once. Edits, the switch and a new template reach bullets in flight; orphans
## keep firing (Clear drops their shots); freed templates are skipped; a
## handler may free anything mid-flush. Factory volleys use emitter_set.

const DT := 0.0625 # exact binary ticks: every schedule below lands on its tick

var shots: Array = [] # [tick, volley] per template shot
var tick := 0


func before_each() -> void:
	await super()
	shots.clear()
	tick = 0


func _step(n: int) -> void:
	for i in n:
		tick += 1
		step_factory(1, DT)


## A template spawner (shooting off) of `amount` still bullets on a ring.
func _template(amount := 1, speed := 0.0) -> BulletSpawner2D:
	var t := make_spawner(H.make_volley_data(amount, speed, 30.0), BulletSpawner2D.PATTERN_FROM_HELPER_RING, amount)
	t.name = "Template"
	t.volley_fired.connect(func(v: BulletVolley2D, _n: int) -> void: shots.append([tick, v]))
	return t


func _timing(interval := 0.25, delay := 0.0, stagger := 0.0, max_shots := 0) -> BulletEmitterData2D:
	var e := BulletEmitterData2D.new()
	e.interval_sec = interval
	e.start_delay_sec = delay
	e.stagger_sec = stagger
	e.max_shots = max_shots
	return e


func _emitting(tpl: BulletSpawner2D, timing: BulletEmitterData2D, amount := 1, speed := 0.0) -> BulletSpawner2D:
	var sp := make_spawner(H.make_volley_data(amount, speed, 30.0), BulletSpawner2D.PATTERN_FROM_HELPER_RING, amount)
	sp.emitter_enabled = true
	sp.emitter = timing
	sp.emitter_spawner_path = sp.get_path_to(tpl)
	return sp


func _fire(sp: BulletSpawner2D) -> BulletVolley2D:
	var got: Array = []
	sp.volley_fired.connect(func(v: BulletVolley2D, _n: int) -> void: got.append(v), CONNECT_ONE_SHOT)
	assert_true(sp.shoot_once(), "fires")
	return got[0] if got.size() > 0 else null


func _ticks() -> Array:
	var out: Array = []
	for s in shots:
		out.append(s[0])
	return out


func _angle_of(v: BulletVolley2D) -> float:
	return v.get_bullet_direction(0).angle()


# --- the resource ------------------------------------------------------------------------

func test_the_resource_defaults_and_checks() -> void:
	var e := BulletEmitterData2D.new()
	assert_eq([e.interval_sec, e.start_delay_sec, e.stagger_sec, e.max_shots, e.chance, e.seed, e.aim, e.spin_per_shot_degrees, e.inherit_bullet_velocity], [0.5, 0.0, 0.0, 0, 1.0, 0, BulletEmitterData2D.EMITTER_AIM_HEADING, 0.0, false], "defaults")
	assert_eq([BulletEmitterData2D.EMITTER_AIM_HEADING, BulletEmitterData2D.EMITTER_AIM_WORLD, BulletEmitterData2D.EMITTER_AIM_TEMPLATE], [0, 1, 2], "serialized aim ids")
	e.interval_sec = 0.0
	expect_error_sequence(["BulletEmitterData2D: interval_sec must be finite and > 0, keeping the old value."])
	e.start_delay_sec = -1.0
	expect_error_sequence(["BulletEmitterData2D: start_delay_sec must be finite and >= 0, keeping the old value."])
	e.stagger_sec = NAN
	expect_error_sequence(["BulletEmitterData2D: stagger_sec must be finite and >= 0, keeping the old value."])
	e.max_shots = -1
	expect_error_sequence(["BulletEmitterData2D: max_shots must be >= 0 (0 = no limit), keeping the old value."])
	e.chance = 1.5
	expect_error_sequence(["BulletEmitterData2D: chance must be between 0 and 1, keeping the old value."])
	e.set("aim", 9)
	expect_error_sequence(["BulletEmitterData2D: aim must be 0 (Heading), 1 (World) or 2 (Template), keeping the old value."])
	e.spin_per_shot_degrees = INF
	expect_error_sequence(["BulletEmitterData2D: spin_per_shot_degrees must be finite, keeping the old value."])
	assert_eq([e.interval_sec, e.start_delay_sec, e.stagger_sec, e.max_shots, e.chance, e.aim], [0.5, 0.0, 0.0, 0, 1.0, 0], "every refusal kept the old value")


func test_the_group_sits_after_splitting_and_gates_its_settings() -> void:
	var sp := make_spawner()
	var group := ""
	var found := {}
	for p in sp.get_property_list():
		var n := str(p["name"])
		if int(p["usage"]) & PROPERTY_USAGE_GROUP:
			group = n
		elif n in ["emitter_enabled", "emitter", "emitter_spawner_path"]:
			found[n] = [group, str(p["hint_string"])]
	assert_eq(found, {"emitter_enabled": ["Emitter Bullets", ""], "emitter": ["Emitter Bullets", "BulletEmitterData2D"], "emitter_spawner_path": ["Emitter Bullets", "PatternSpawner2D"]}, "group and hints")
	assert_false(sp.emitter_enabled, "off by default")
	assert_false(is_editor_visible(sp, &"emitter"), "hidden while off")
	assert_false(is_editor_visible(sp, &"emitter_spawner_path"), "hidden while off")
	sp.emitter_enabled = true
	assert_true(is_editor_visible(sp, &"emitter") and is_editor_visible(sp, &"emitter_spawner_path"), "shown while on")
	assert_not_null(sp.get_effective_emitter(), "an empty emitter uses a default timing")
	assert_eq(sp.get_effective_emitter().interval_sec, 0.5, "every 0.5 s")


# --- timing --------------------------------------------------------------------------------

func test_interval_delay_stagger_and_max_shots_schedule_every_bullet() -> void:
	var tpl := _template()
	var sp := _emitting(tpl, _timing(0.25, 0.125, 0.0625, 2), 2)
	var v := _fire(sp)
	assert_true(v.is_emitter_armed(), "the shot is armed before any signal")
	_step(16)
	# Bullet 0: 0.125 s (tick 2), 0.375 s (tick 6). Bullet 1 one stagger later:
	# tick 3, tick 7. Two shots each, then nothing.
	assert_eq(_ticks(), [2, 3, 6, 7], "each bullet on its own schedule, max_shots each")
	assert_eq([v.get_bullet_emitter_shots(0), v.get_bullet_emitter_shots(1)], [2, 2], "shot counts")


func test_the_first_shot_is_due_at_once_and_the_interval_repeats() -> void:
	var tpl := _template()
	_fire(_emitting(tpl, _timing(0.25)))
	_step(9)
	assert_eq(_ticks(), [1, 4, 8], "0 s (the first tick), then every 0.25 s")


func test_the_template_fires_its_own_shot_and_owns_it() -> void:
	var tpl := _template(3)
	var sp := _emitting(tpl, _timing(1.0))
	var v := _fire(sp)
	_step(1)
	assert_eq(shots.size(), 1, "one template shot")
	var shot: BulletVolley2D = shots[0][1]
	assert_eq(shot.get_amount_bullets(), 3, "the template's own pattern")
	assert_eq(shot.get_owner_spawner(), tpl, "owned by the template")
	assert_ne(shot, v, "a new volley")
	assert_almost_eq(shot.get_bullet_transform(0).origin.distance_to(v.get_bullet_transform(0).origin), tpl.helper_ring_radius, 0.01, "the ring sits around the emitting bullet")


# --- aim, spin and velocity -------------------------------------------------------------

func test_heading_world_and_template_aims() -> void:
	var tpl := _template()
	tpl.rotation = 0.3
	var angles := {}
	for aim in [BulletEmitterData2D.EMITTER_AIM_WORLD, BulletEmitterData2D.EMITTER_AIM_HEADING, BulletEmitterData2D.EMITTER_AIM_TEMPLATE]:
		var timing := _timing(1.0)
		timing.aim = aim
		var sp := _emitting(tpl, timing, 1, 100.0)
		sp.rotation = PI / 2.0 # the emitting bullet flies down (+Y)
		shots.clear()
		_fire(sp)
		_step(1)
		assert_eq(shots.size(), 1, "aim %d fired" % aim)
		angles[aim] = _angle_of(shots[0][1])
		sp.emitter_enabled = false
	var world: float = angles[BulletEmitterData2D.EMITTER_AIM_WORLD]
	assert_almost_eq(angle_difference(world, angles[BulletEmitterData2D.EMITTER_AIM_HEADING]), PI / 2.0, 0.001, "heading turns the pattern by the bullet's flight angle")
	assert_almost_eq(angle_difference(world, angles[BulletEmitterData2D.EMITTER_AIM_TEMPLATE]), 0.3, 0.001, "template turns it by the template's rotation")


func test_spin_per_shot_turns_each_shot_further() -> void:
	var tpl := _template()
	var timing := _timing(DT)
	timing.aim = BulletEmitterData2D.EMITTER_AIM_WORLD
	timing.spin_per_shot_degrees = 30.0
	_fire(_emitting(tpl, timing))
	_step(3)
	assert_eq(shots.size(), 3, "one shot per tick")
	var a0 := _angle_of(shots[0][1])
	assert_almost_eq(angle_difference(a0, _angle_of(shots[1][1])), deg_to_rad(30.0), 0.001, "shot 1: +30 deg")
	assert_almost_eq(angle_difference(a0, _angle_of(shots[2][1])), deg_to_rad(60.0), 0.001, "shot 2: +60 deg")


func test_inherit_bullet_velocity_carries_the_bullets_motion() -> void:
	var tpl := _template(1, 0.0)
	var timing := _timing(1.0)
	timing.inherit_bullet_velocity = true
	var v := _fire(_emitting(tpl, timing, 1, 120.0))
	_step(1)
	var shot: BulletVolley2D = shots[0][1]
	assert_almost_eq(shot.get_bullet_velocity(0).distance_to(v.get_bullet_velocity(0)), 0.0, 0.01, "a still template bullet flies with the emitter bullet")
	timing.inherit_bullet_velocity = false
	_step(16)
	assert_eq(shots[1][1].get_bullet_velocity(0), Vector2.ZERO, "off: the template's own motion only")


func test_chance_is_seeded_per_shot_and_bounded() -> void:
	var tpl := _template()
	var never := _timing(DT)
	never.chance = 0.0
	_fire(_emitting(tpl, never))
	_step(8)
	assert_eq(shots.size(), 0, "chance 0 never fires")
	var runs: Array = []
	for r in 2:
		var timing := _timing(DT)
		timing.chance = 0.5
		timing.seed = 99
		var sp := _emitting(tpl, timing)
		shots.clear()
		tick = 0
		_fire(sp)
		_step(64)
		runs.append(_ticks())
		sp.emitter_enabled = false
	assert_between(runs[0].size(), 16, 48, "about half of 64 shots")
	assert_eq(runs[1], runs[0], "the same seed and shot number: the same shots")


# --- chains, cycles ------------------------------------------------------------------------

func test_emitters_of_emitters_chain_through_their_templates() -> void:
	var leaf := _template()
	var middle := _emitting(leaf, _timing(DT), 1)
	middle.name = "Middle"
	var middle_shots: Array = []
	middle.volley_fired.connect(func(v: BulletVolley2D, _n: int) -> void: middle_shots.append(v))
	var root := _emitting(middle, _timing(1.0), 1)
	_fire(root)
	_step(1)
	assert_eq(middle_shots.size(), 1, "the root bullet fired the middle pattern")
	assert_true(middle_shots[0].is_emitter_armed(), "the middle shot is an emitter shot itself")
	_step(1)
	assert_gt(shots.size(), 0, "and its bullets fire the leaf pattern")


func test_a_chain_back_to_the_spawner_is_refused_once() -> void:
	var sp := make_spawner(H.make_volley_data(1, 0.0, 30.0), BulletSpawner2D.PATTERN_FROM_HELPER_RING, 1)
	sp.emitter_enabled = true
	sp.emitter_spawner_path = NodePath(".")
	var v := _fire(sp)
	expect_error_sequence(["BulletSpawner2D: emitter_spawner_path leads back to this spawner (an emitter cycle), its bullets do not emit."])
	assert_false(v.is_emitter_armed(), "not armed")
	_fire(sp)
	expect_no_errors() # latched until the configuration changes
	assert_true("emitter_spawner_path leads back to this spawner (an emitter cycle): its bullets do not emit." in sp.get_setup_warnings(), "named in the setup warnings")
	var other := make_spawner(H.make_volley_data(1, 0.0, 30.0), BulletSpawner2D.PATTERN_FROM_HELPER_RING, 1)
	other.emitter_enabled = true
	other.emitter_spawner_path = other.get_path_to(sp)
	sp.emitter_spawner_path = sp.get_path_to(other) # A -> B -> A
	_fire(other)
	expect_error_sequence(["BulletSpawner2D: emitter_spawner_path leads back to this spawner (an emitter cycle), its bullets do not emit."])
	_fire(sp)
	expect_error_sequence(["BulletSpawner2D: emitter_spawner_path leads back to this spawner (an emitter cycle), its bullets do not emit."])


# --- caps ---------------------------------------------------------------------------------------

func test_the_per_sweep_cap_carries_shots_over_in_order() -> void:
	var tpl := _template()
	factory.emitter_max_shots_per_sweep = 2
	_fire(_emitting(tpl, _timing(10.0), 5))
	_step(1)
	assert_eq(shots.size(), 2, "two shots in the first sweep")
	assert_eq(factory.debug_get_emitter_stats()["carried_over_last"], 3, "three wait")
	_step(2)
	assert_eq(_ticks(), [1, 1, 2, 2, 3], "then two per sweep, in order")
	factory.emitter_max_shots_per_sweep = -1
	expect_error_sequence(["BulletFactory2D: emitter_max_shots_per_sweep must be >= 0 (0 = no cap), keeping the old value."])


func test_the_pending_cap_drops_with_one_warning() -> void:
	var tpl := _template()
	factory.emitter_max_pending = 2
	_fire(_emitting(tpl, _timing(10.0), 5))
	_step(1)
	expect_warning_sequence(["BulletFactory2D: emitter_max_pending (2) reached, new emitter shots are dropped until the queue drains (raise it, or 0 = no cap)."])
	assert_eq(shots.size(), 2, "two recorded, three dropped")
	assert_eq(factory.debug_get_emitter_stats()["dropped_total"], 3, "counted")
	factory.emitter_max_pending = -5
	expect_error_sequence(["BulletFactory2D: emitter_max_pending must be >= 0 (0 = no cap), keeping the old value."])
	assert_eq(factory.emitter_max_pending, 2, "kept")


# --- edits in flight, switch, wake ---------------------------------------------------------

func test_edits_the_switch_and_a_new_template_reach_bullets_in_flight() -> void:
	var tpl := _template()
	var timing := _timing(0.25)
	var sp := _emitting(tpl, timing)
	_fire(sp)
	_step(1)
	timing.interval_sec = DT # applies from the next scheduled shot
	_step(5)
	assert_eq(_ticks(), [1, 4, 5, 6], "the shorter interval took over after the 0.25 s shot")
	sp.emitter_enabled = false
	_step(4)
	assert_eq(shots.size(), 4, "off: bullets in flight stop")
	sp.emitter_enabled = true
	_step(1)
	assert_eq(shots.size(), 5, "on again: they resume (one shot, no catch-up burst)")
	var other := _template()
	other.name = "Other"
	var other_shots: Array = []
	other.volley_fired.connect(func(v: BulletVolley2D, _n: int) -> void: other_shots.append(v))
	sp.emitter_spawner_path = sp.get_path_to(other)
	_step(1)
	assert_eq(other_shots.size(), 1, "a new template takes over in flight")


func test_raising_max_shots_reopens_a_bullet_that_ran_out() -> void:
	var tpl := _template()
	var timing := _timing(DT, 0.0, 0.0, 1)
	_fire(_emitting(tpl, timing))
	_step(3)
	assert_eq(shots.size(), 1, "one shot, then done")
	timing.max_shots = 3
	_step(3)
	assert_eq(shots.size(), 3, "the edit reached the bullet: two more")


func test_bullets_fired_while_off_never_emit_until_re_armed() -> void:
	var tpl := _template()
	var sp := _emitting(tpl, _timing(DT))
	sp.emitter_enabled = false
	var v := _fire(sp)
	assert_false(v.is_emitter_armed(), "fired while off: not armed")
	sp.emitter_enabled = true
	_step(2)
	assert_eq(shots.size(), 0, "turning it on does not arm older shots")
	assert_eq(sp.apply_to_live_volleys(BulletSpawner2D.LIVE_EMITTER), 1, "apply_to_live_volleys arms them")
	assert_eq(sp.apply_to_live_volleys(BulletSpawner2D.LIVE_EMITTER), 0, "armed alike: untouched")
	_step(1)
	assert_eq(shots.size(), 1, "and they fire")


func test_a_frozen_bullet_holds_and_a_wake_fires_once() -> void:
	var tpl := _template()
	var v := _fire(_emitting(tpl, _timing(0.25), 2))
	v.set_is_auto_pooling_enabled(false)
	_step(1)
	v.disable_bullet(0) # bullet 1 keeps the volley (and its clock) running
	_step(16) # a whole second: bullet 0 misses four intervals
	assert_eq([v.get_bullet_emitter_shots(0), v.get_bullet_emitter_shots(1)], [1, 5], "frozen: no shots; the live bullet kept firing")
	v.enable_bullet(0)
	_step(1)
	assert_eq(v.get_bullet_emitter_shots(0), 2, "woken: one shot")
	_step(3)
	assert_eq(v.get_bullet_emitter_shots(0), 2, "no catch-up burst for the missed intervals")
	_step(1)
	assert_eq(v.get_bullet_emitter_shots(0), 3, "the schedule goes on from the wake")


func test_a_parked_volley_holds_its_schedule() -> void:
	var tpl := _template()
	var v := _fire(_emitting(tpl, _timing(0.25)))
	v.set_is_auto_pooling_enabled(false)
	_step(1)
	v.disable_bullet(0) # its only bullet: the volley parks and stops ticking
	_step(16)
	v.enable_bullet(0)
	_step(2)
	assert_eq(v.get_bullet_emitter_shots(0), 1, "the parked clock held: the next shot is still 0.25 s after the first")
	_step(1)
	assert_eq(v.get_bullet_emitter_shots(0), 2, "and it comes on time")


# --- lifetimes of the pieces ---------------------------------------------------------------

func test_orphans_keep_firing_through_the_template() -> void:
	var tpl := _template()
	var keep := _emitting(tpl, _timing(DT))
	_fire(keep)
	_step(1)
	keep.free()
	_step(2)
	assert_eq(shots.size(), 3, "Keep Flying: the orphaned bullet still fires through the template")


func test_a_clear_orphan_policy_drops_the_queued_shots() -> void:
	var tpl := _template()
	factory.emitter_max_shots_per_sweep = 1
	var clear := _emitting(tpl, _timing(10.0), 3)
	clear.orphaned_volleys = BulletSpawner2D.ORPHANED_VOLLEYS_CLEAR
	_fire(clear)
	_step(1)
	assert_eq([shots.size(), factory.get_pending_emit_count()], [1, 2], "one fired, two waiting")
	clear.free()
	assert_eq(factory.get_pending_emit_count(), 0, "Clear: the freed spawner's queued shots go with its bullets")
	_step(3)
	assert_eq(shots.size(), 1, "nothing more fires")


func test_a_freed_template_is_skipped_silently() -> void:
	var tpl := _template()
	_fire(_emitting(tpl, _timing(DT)))
	_step(1)
	tpl.free()
	_step(3)
	assert_eq(shots.size(), 1, "no shots after the template went, no errors")


func test_handlers_may_free_the_template_the_spawner_or_the_factory_mid_flush() -> void:
	var tpl := _template()
	var sp := _emitting(tpl, _timing(DT), 4)
	_fire(sp)
	var doomed := [sp]
	tpl.volley_fired.connect(func(_v: BulletVolley2D, _n: int) -> void:
		if is_instance_valid(doomed[0]):
			doomed[0].free(), CONNECT_ONE_SHOT)
	_step(1)
	assert_eq(shots.size(), 4, "the spawner freed by the first shot's handler: the batch still fires")
	tpl.volley_fired.connect(func(_v: BulletVolley2D, _n: int) -> void:
		if is_instance_valid(tpl):
			tpl.queue_free(), CONNECT_ONE_SHOT)
	_step(1)
	await idle(1)
	assert_false(is_instance_valid(tpl), "the template freed itself from its own handler")
	_step(2)
	var tpl2 := _template()
	_fire(_emitting(tpl2, _timing(DT), 3))
	var f := factory
	tpl2.volley_fired.connect(func(_v: BulletVolley2D, _n: int) -> void:
		if is_instance_valid(f):
			BulletFactory2D.debug_free_like_release(f), CONNECT_ONE_SHOT)
	_step(1)
	assert_false(is_instance_valid(f), "the factory freed inside the flush (release path): no crash")


func test_reset_drops_queued_shots_and_pooling_drops_the_arming() -> void:
	var tpl := _template()
	factory.emitter_max_shots_per_sweep = 1
	var v := _fire(_emitting(tpl, _timing(10.0), 4))
	_step(1)
	assert_eq(factory.get_pending_emit_count(), 3, "three carried over")
	await idle(1) # one more factory tick: one carried shot fires
	var before := shots.size()
	factory.reset()
	assert_eq(factory.get_pending_emit_count(), 0, "reset dropped them")
	assert_false(is_instance_valid(v), "reset destroys the active volleys (and their arming)")
	_step(2)
	assert_eq(shots.size(), before, "no queued shot fires after the reset")


# --- factory volleys --------------------------------------------------------------------

func test_factory_volleys_use_emitter_set() -> void:
	var tpl := _template()
	var v: BulletVolley2D = quick_volley(2, 0.0, 30.0)
	for m in ["emitter_set", "emitter_clear", "emitter_get_data", "is_emitter_armed", "get_bullet_emitter_shots"]:
		assert_true(v.has_method(m), "BulletVolley2D.%s is bound" % m)
	assert_false(v.emitter_set(null, tpl), "null timing refused")
	expect_error_sequence(["emitter_set: emitter is null, nothing changed."])
	var plain := Node2D.new()
	add(plain)
	assert_false(v.emitter_set(_timing(), plain), "a plain node refused")
	expect_error_sequence(["emitter_set: template_spawner must be a PatternSpawner2D (a BulletSpawner2D or LaserSpawner2D), nothing changed."])
	assert_false(v.is_emitter_armed(), "nothing armed by the refusals")
	var timing := _timing(DT)
	assert_true(v.emitter_set(timing, tpl), "armed")
	assert_eq(v.emitter_get_data(), timing, "readout")
	_step(2)
	assert_eq(shots.size(), 4, "two bullets, two ticks")
	assert_eq(v.get_bullet_emitter_shots(1), 2, "per bullet")
	v.get_bullet_emitter_shots(5)
	expect_error_sequence(["Invalid bullet index in get_bullet_emitter_shots: 5 (amount_bullets: 2)"])
	v.emitter_clear()
	_step(2)
	assert_eq(shots.size(), 4, "cleared: no more shots")


func test_a_pooled_volley_drops_its_emitter_and_refuses_one() -> void:
	var tpl := _template()
	var d := H.make_volley_data(1, 0.0, DT)
	var v: BulletVolley2D = factory.spawn_volley(d)
	v.emitter_set(_timing(10.0), tpl)
	_step(2)
	await idle(1)
	assert_true(v.is_pooled(), "expired into the pool")
	assert_false(v.is_emitter_armed(), "the pool dropped the emitter")
	assert_false(v.emitter_set(_timing(), tpl), "a stale handle refuses")
	expect_error_sequence(["emitter_set: this volley is in the pool (its last bullet died), so this handle is stale. Spawn a new volley instead."])
	var again: BulletVolley2D = factory.spawn_volley(H.make_volley_data(1, 0.0, 30.0))
	assert_eq(again, v, "reused")
	var before := shots.size()
	_step(3)
	assert_eq(shots.size(), before, "the next life never emits")


# --- warnings and explain ----------------------------------------------------------------

func test_setup_warnings_and_explain_name_the_emitter() -> void:
	var sp := make_spawner()
	sp.emitter_enabled = true
	assert_true("emitter_enabled is on but emitter_spawner_path is empty: the bullets have no pattern to fire. Point it at a BulletSpawner2D (or LaserSpawner2D) with its shooting off." in sp.get_setup_warnings(), "empty path")
	var plain := Node2D.new()
	add(plain)
	sp.emitter_spawner_path = sp.get_path_to(plain)
	assert_true("emitter_spawner_path does not point at a BulletSpawner2D or LaserSpawner2D: the bullets have no pattern to fire." in sp.get_setup_warnings(), "not a spawner")
	var tpl := _template()
	tpl.set_shooting_enabled(true)
	sp.emitter_spawner_path = sp.get_path_to(tpl)
	assert_true("The emitter template Template also shoots on its own: turn its shooting off if it should only fire from the bullets." in sp.get_setup_warnings(), "a template that shoots too")
	tpl.set_shooting_enabled(false)
	var lines: PackedStringArray = sp.debug_explain_features()
	assert_true("Emitter Bullets: every bullet fires Template's pattern every 0.5 s, turned to its flight heading." in lines, "explain: %s" % str(lines))


## The cycle error re-arms whenever a shot finds no cycle: fixed through the
## template's own path and made again later, the cycle reports again instead
## of silently stopping the emission.
func test_a_cycle_made_again_through_the_template_reports_again() -> void:
	const CYCLE := "BulletSpawner2D: emitter_spawner_path leads back to this spawner (an emitter cycle), its bullets do not emit."
	var a := make_spawner(H.make_volley_data(1, 0.0, 30.0), BulletSpawner2D.PATTERN_FROM_HELPER_RING, 1)
	var b := make_spawner(H.make_volley_data(1, 0.0, 30.0), BulletSpawner2D.PATTERN_FROM_HELPER_RING, 1)
	a.emitter_enabled = true
	a.emitter_spawner_path = a.get_path_to(b)
	b.emitter_enabled = true
	b.emitter_spawner_path = b.get_path_to(a) # A -> B -> A
	_fire(a)
	expect_error_sequence([CYCLE])
	b.emitter_spawner_path = NodePath() # fixed through the template alone
	var v := _fire(a)
	expect_no_errors("no cycle now")
	assert_true(v.is_emitter_armed(), "it emits again")
	b.emitter_spawner_path = b.get_path_to(a) # the cycle is back
	_fire(a)
	expect_error_sequence([CYCLE])


## A new emitter resource reaches bullets in flight: bullets that used up
## the old one's max_shots fire again under a resource without a cap.
func test_a_swapped_emitter_resource_reaches_bullets_in_flight() -> void:
	var tpl := _template()
	var capped := _timing(0.25, 0.0, 0.0, 1)
	var free := _timing(0.25, 0.0, 0.0, 0) # made up front, like a preloaded resource
	var sp := _emitting(tpl, capped)
	_fire(sp)
	_step(8)
	assert_eq(shots.size(), 1, "max_shots 1: one shot")
	sp.emitter = free
	_step(8)
	assert_gt(shots.size(), 1, "the new resource (no cap) fires again")
	var before := shots.size()
	sp.emitter = capped
	_step(8)
	sp.emitter = null # the factory's default resource (no cap)
	_step(8)
	assert_gt(shots.size(), before, "back to no cap through the default resource")


## A cycle made while bullets fly (the path now leads back to the spawner):
## the bullets in flight stop firing at once, never the spawner's own pattern
## (the next shot reports the cycle, once).
func test_a_cycle_made_in_flight_stops_the_flying_bullets() -> void:
	var tpl := _template()
	var sp := _emitting(tpl, _timing(0.25))
	var own: Array = []
	sp.volley_fired.connect(func(v: BulletVolley2D, _n: int) -> void: own.append(v))
	_fire(sp)
	_step(2)
	var fired := shots.size()
	assert_gt(fired, 0, "the bullets fire the template")
	own.clear()
	sp.emitter_spawner_path = NodePath(".")
	_step(8)
	expect_no_errors()
	assert_eq(own.size(), 0, "they never fire the spawner's own pattern")
	assert_eq(shots.size(), fired, "nor the old template")
	_fire(sp)
	expect_error_sequence(["BulletSpawner2D: emitter_spawner_path leads back to this spawner (an emitter cycle), its bullets do not emit."])
