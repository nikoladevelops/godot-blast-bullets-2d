extends BlastTest
## Attachment callbacks are user code that runs INSIDE plugin operations:
## on_bullet_disable inside every way a bullet ends (disable, killing hit,
## expiry, clears, despawn, reset, free, pickup conversion), on_bullet_enable
## inside a wake and a pooled spawn, on_bullet_spawn inside a fresh spawn.
## Each case frees, with free() from there: the volley, ANOTHER volley the
## operation has not reached yet, the whole factory, or the factory on every
## callback (so again from inside its own teardown).
## The contract: the plugin never touches freed memory afterwards (a write
## into freed memory passes every assert: tools/run_sanitizer_tests.py runs
## this suite under AddressSanitizer), never hangs, keeps running, and a
## surviving factory keeps balanced books and keeps spawning.

const DISABLE_TRIGGERS := ["disable_bullet", "clear_bullet", "killing_hit", "expiry", "clear_active", "clear_circle",
	"clear_rect", "despawn", "reset", "free_active", "pickups", "deferred_queue"]
const VICTIMS := ["volley", "other_volley", "factory", "factory_again"]

var probe_scene: PackedScene
var orphans_at_case_start := 0


func before_each() -> void:
	await super()
	AttachmentHookProbe2D.clear_hook()
	if probe_scene == null:
		var probe: Node = AttachmentHookProbe2D.new()
		probe_scene = PackedScene.new()
		assert_eq(probe_scene.pack(probe), OK, "probe scene packs")
		probe.free()


func after_each() -> void:
	AttachmentHookProbe2D.clear_hook()
	await super()


## Three bullets carrying the probe (shared attachment: applied at spawn and
## at every wake). A moving volley flies into a wall at x = 60.
func _data(moving: bool, lifetime := 30.0) -> BulletVolleyData2D:
	var d := H.make_volley_data(3, 600.0 if moving else 0.0, lifetime)
	d.transforms = [Transform2D(), Transform2D(0.0, Vector2(0, 30)), Transform2D(0.0, Vector2(0, 60))]
	d.monitorable = true
	d.set_collision_mask_from_array([3])
	d.collision_shape = H.make_circle_shape(6.0)
	d.bullet_max_collision_count = 1
	d.shared_bullet_attachment = probe_scene
	return d


func _arm(when: Array, victim: String, v: BulletVolley2D, other: BulletVolley2D = null) -> void:
	AttachmentHookProbe2D.hook_on = when
	if victim == "volley" or victim == "other_volley":
		var target := v if victim == "volley" else other
		AttachmentHookProbe2D.hook = func() -> void:
			if is_instance_valid(target):
				target.free()
		return
	check_factory_after = false
	AttachmentHookProbe2D.hook_repeat = victim == "factory_again"
	var f := factory
	AttachmentHookProbe2D.hook = func() -> void:
		if is_instance_valid(f):
			f.free()


func _await_hook(frames: int) -> void:
	for k in frames:
		await physics(1)
		if AttachmentHookProbe2D.fired > 0:
			break


func _trigger(trigger: String, v: BulletVolley2D) -> void:
	match trigger:
		"disable_bullet":
			v.disable_bullet(0)
		"clear_bullet":
			v.clear_bullet(0)
		"killing_hit", "expiry":
			await _await_hook(30)
		"clear_active":
			factory.clear_active_bullets()
		"clear_circle":
			factory.clear_bullets_in_circle(Vector2.ZERO, 500.0)
		"clear_rect":
			factory.clear_bullets_in_rect(Rect2(-500, -500, 1000, 1000))
		"despawn":
			factory.despawn_bounds_rect = Rect2(5000, 5000, 10, 10)
			factory.despawn_bounds_enabled = true
			await _await_hook(10)
		"reset":
			factory.reset()
		"free_active":
			factory.free_active_bullets()
		"pickups":
			factory.convert_all_bullets_to_pickups(BulletPickupData2D.new(), Vector2(300, 0))
		"deferred_queue":
			# Several queued structural calls: the first one's callbacks run
			# while the rest still wait in the same flush.
			factory.reset_deferred()
			factory.free_active_bullets_deferred()
			factory.clear_active_bullets_deferred()
			await idle(2)


## Claims the case's errors and returns their texts: no case may print one.
func _claim_errors() -> Array:
	var texts: Array = []
	for err in get_errors():
		if not err.handled:
			err.handled = true
			texts.append(str(err.code))
	return texts


## Runs the world on, then checks: the hook ran, nothing was left orphaned (a
## refused second free must still finish the first one), and a surviving
## factory keeps balanced books and keeps spawning. Returns the problems.
func _settle_and_check(where: String) -> Array:
	for k in 3:
		await physics(1)
	await idle(2)
	var problems: Array = []
	var orphans := int(Performance.get_monitor(Performance.OBJECT_ORPHAN_NODE_COUNT)) - orphans_at_case_start
	if orphans != 0:
		problems.append("%s: %d orphan nodes" % [where, orphans])
	if AttachmentHookProbe2D.fired < 1 or (AttachmentHookProbe2D.fired > 1 and not AttachmentHookProbe2D.hook_repeat):
		problems.append("%s: the hook ran %d times" % [where, AttachmentHookProbe2D.fired])
	if is_instance_valid(factory):
		var books: Dictionary = factory.debug_assert_no_dangling()
		if not books["ok"]:
			problems.append("%s: %s" % [where, books["error"]])
		if factory.spawn_volley(H.make_volley_data(2, 100.0, 1.0)) == null:
			problems.append("%s: the factory stopped spawning" % where)
	return problems


func _run_disable_case(trigger: String, victim: String) -> Array:
	if trigger == "killing_hit":
		make_wall(Vector2(60, 0))
		factory.body_entered.connect(func(_b, _v, _i) -> void: pass)
		await physics()
	if trigger == "expiry":
		factory.life_time_over.connect(func(_v, _i) -> void: pass)
	var d := _data(trigger in ["killing_hit", "despawn"], 0.05 if trigger == "expiry" else 30.0)
	if trigger == "clear_bullet":
		# The On Clear flash fires after the disable's callback: it must
		# never run on a volley that callback freed.
		d.effect_layers = [H.make_effect_layer(BulletEffectLayerData2D.EFFECT_ON_CLEAR)]
	var v: BulletVolley2D = factory.spawn_volley(d)
	var other: BulletVolley2D = factory.spawn_volley(d)
	await idle(1)
	_arm(["disable"], victim, v, other)
	await _trigger(trigger, v)
	return await _settle_and_check("%s / %s" % [trigger, victim])


func test_freeing_inside_on_bullet_disable_never_touches_freed_memory() -> void:
	var problems: Array = []
	for trigger in DISABLE_TRIGGERS:
		for victim in VICTIMS:
			await _new_case()
			problems.append_array(await _run_disable_case(trigger, victim))
			problems.append_array(_claim_errors().map(func(e): return "%s / %s error: %s" % [trigger, victim, e]))
	assert_eq("\n".join(problems), "", "every disable path survives a free from its callback")


func test_freeing_inside_on_bullet_enable_of_a_wake_never_touches_freed_memory() -> void:
	var problems: Array = []
	for victim in VICTIMS:
		await _new_case()
		var v: BulletVolley2D = factory.spawn_volley(_data(false))
		await idle(1)
		v.set_is_auto_pooling_enabled(false)
		v.disable_bullet(0, false) # keep the attachment: the wake tells it on_bullet_enable
		await idle(1)
		var other: BulletVolley2D = factory.spawn_volley(_data(false))
		_arm(["enable"], victim, v, other)
		v.enable_bullet(0)
		problems.append_array(await _settle_and_check("wake / %s" % victim))
		problems.append_array(_claim_errors().map(func(e): return "wake / %s error: %s" % [victim, e]))
	assert_eq("\n".join(problems), "", "a wake survives a free from its callback")


func test_freeing_inside_a_spawn_never_returns_a_freed_volley() -> void:
	# A fresh spawn runs on_bullet_spawn, a pooled one (volley and attachments
	# from the pools) on_bullet_enable: both run while spawn_volley is still
	# setting the volley up.
	var problems: Array = []
	for pooled in [false, true]:
		for victim in ["volley", "factory", "factory_again"]:
			await _new_case()
			var where := "%s spawn / %s" % ["pooled" if pooled else "fresh", victim]
			var d := _data(false)
			var old: BulletVolley2D = null
			if pooled:
				old = factory.spawn_volley(d)
				await idle(1)
				factory.clear_active_bullets()
				await idle(1)
			_arm(["spawn", "enable"], victim, old)
			if not pooled and victim == "volley":
				# The volley does not exist before the spawn: the hook frees
				# the live volley that was not there before.
				var known: Array = Array(factory.debug_get_live_volley_ids(0))
				var f := factory
				AttachmentHookProbe2D.hook = func() -> void:
					for id in f.debug_get_live_volley_ids(0):
						if not known.has(id):
							instance_from_id(id).free()
							return
			var got = factory.spawn_volley(d)
			if got != null and not is_instance_valid(got):
				problems.append("%s: spawn_volley returned a freed volley" % where)
			problems.append_array(await _settle_and_check(where))
			problems.append_array(_claim_errors().map(func(e): return "%s error: %s" % [where, e]))
	assert_eq("\n".join(problems), "", "a spawn survives a free from its callbacks")


## A fresh spawn's on_bullet_spawn that spawns ANOTHER fresh volley (or frees
## an older one) changes the factory's volley list before the first spawn
## files its own volley: both volleys must still tick (move, expire) and the
## books must stay balanced.
func test_spawning_or_freeing_inside_a_fresh_spawn_keeps_every_volley_ticking() -> void:
	var problems: Array = []
	for action in ["spawn_other", "free_other"]:
		await _new_case()
		var f := factory
		var older: BulletVolley2D = f.spawn_volley(H.make_volley_data(2, 100.0, 30.0))
		await idle(1)
		var inner: Array = []
		AttachmentHookProbe2D.hook_on = ["spawn"]
		AttachmentHookProbe2D.hook = func() -> void:
			if action == "spawn_other":
				inner.append(f.spawn_volley(H.make_volley_data(5, 100.0, 30.0))) # another key: a pool miss
			elif is_instance_valid(older):
				older.free()
		var outer: BulletVolley2D = f.spawn_volley(_data(true))
		if outer == null:
			problems.append("%s: the outer spawn failed" % action)
			continue
		var start: Vector2 = outer.get_bullet_transform(0).origin
		await idle(1)
		var books: Dictionary = f.debug_assert_no_dangling()
		if not books["ok"]:
			problems.append("%s: %s" % [action, books["error"]])
		for k in 5:
			await physics(1)
		if outer.get_bullet_transform(0).origin == start:
			problems.append("%s: the outer volley never moved (not ticking)" % action)
		problems.append_array(await _settle_and_check(action))
		problems.append_array(_claim_errors().map(func(e): return "%s error: %s" % [action, e]))
	assert_eq("\n".join(problems), "", "both volleys tick after user code changed the volley list mid-spawn")


## A spawn whose own callback clears every bullet (on_bullet_spawn of a fresh
## volley, on_bullet_enable of a pooled one, the pickup volley of a
## conversion) drains the volley before the spawn returns: the spawn never
## hands out the pooled handle, never re-tracks it, and the pool keeps a
## plain volley (a later ordinary volley reusing it is no pickup).
func test_a_volley_drained_by_its_own_spawn_callbacks_is_never_handed_out() -> void:
	var problems: Array = []
	for path in ["fresh", "pooled", "pickups"]:
		await _new_case()
		var f := factory
		var d := _data(false)
		if path == "pooled":
			f.spawn_volley(d)
			await idle(1)
			f.clear_active_bullets()
			await idle(1)
		var pickup := BulletPickupData2D.new()
		if path == "pickups":
			# A first conversion (no hook) pools a pickup volley, so the
			# hooked conversion below reuses it (on_bullet_enable).
			pickup.attachment = probe_scene
			f.spawn_volley(H.make_volley_data(3, 0.0, 30.0))
			await idle(1)
			f.convert_all_bullets_to_pickups(pickup, Vector2(300, 0))
			await idle(1)
			f.clear_active_bullets()
			await idle(1)
			f.spawn_volley(H.make_volley_data(3, 0.0, 30.0))
			await idle(1)
		AttachmentHookProbe2D.hook_on = ["spawn", "enable"]
		AttachmentHookProbe2D.hook = func() -> void:
			f.clear_active_bullets()
		var got = null
		if path == "pickups":
			f.convert_all_bullets_to_pickups(pickup, Vector2(300, 0))
		else:
			got = f.spawn_volley(d)
		if got != null and (not is_instance_valid(got) or got.is_pooled()):
			problems.append("%s: spawn_volley returned a drained volley" % path)
		await idle(1)
		var books: Dictionary = f.debug_assert_no_dangling()
		if not books["ok"]:
			problems.append("%s: %s" % [path, books["error"]])
		AttachmentHookProbe2D.clear_hook()
		# Reuse every pooled instance of that size as a plain volley.
		for k in 4:
			var plain: BulletVolley2D = f.spawn_volley(H.make_volley_data(3, 0.0, 30.0))
			if plain != null and plain.is_pickup_volley():
				problems.append("%s: a plain volley came out of the pool as a pickup" % path)
		problems.append_array(_claim_errors().map(func(e): return "%s error: %s" % [path, e]))
	assert_eq("\n".join(problems), "", "a volley drained during its spawn stays pooled and plain")


func _split_chain() -> BulletSplitData2D:
	var stage := BulletSplitStage2D.new()
	stage.count = 3
	var chain := BulletSplitData2D.new()
	chain.stages = [stage]
	return chain


func test_freeing_inside_a_split_never_touches_freed_memory() -> void:
	# Parents carry the probe: on_bullet_disable runs right after each
	# parent's split was recorded (the batch disable of an expiry).
	# Children carry the probe: on_bullet_spawn / on_bullet_enable run inside
	# the factory's split flush, with more splits of the batch still waiting.
	var problems: Array = []
	for side in ["parents", "children"]:
		for victim in VICTIMS:
			if side == "children" and victim == "other_volley":
				continue
			await _new_case()
			var where := "split %s / %s" % [side, victim]
			var plain := _data(false, 0.05)
			plain.shared_bullet_attachment = null
			var probed := _data(false, 0.05)
			var parent_data := probed if side == "parents" else plain
			var child_data := plain if side == "parents" else _data(false)
			var v: BulletVolley2D = factory.spawn_volley(parent_data)
			v.split_set_data(_split_chain(), child_data)
			var other: BulletVolley2D = factory.spawn_volley(parent_data)
			other.split_set_data(_split_chain(), child_data)
			await idle(1)
			if side == "parents":
				_arm(["disable"], victim, v, other)
			else:
				_arm(["spawn", "enable"], victim, v)
				if victim == "volley":
					# The children volley being set up right now.
					var known: Array = Array(factory.debug_get_live_volley_ids(0))
					var f := factory
					AttachmentHookProbe2D.hook = func() -> void:
						for id in f.debug_get_live_volley_ids(0):
							if not known.has(id):
								instance_from_id(id).free()
								return
			await _await_hook(30)
			problems.append_array(await _settle_and_check(where))
			problems.append_array(_claim_errors().map(func(e): return "%s error: %s" % [where, e]))
	assert_eq("\n".join(problems), "", "a split survives a free from any callback inside it")


func test_freeing_inside_a_non_lethal_split_never_touches_freed_memory() -> void:
	# The parent leaves through disable_bullet inside the bounce drain, the
	# hit drain, the homing dispatch and the graze dispatch: its
	# on_bullet_disable frees the volley, another volley or the factory.
	var problems: Array = []
	for trigger in ["bounce", "non_lethal_hit", "homing_reached", "graze"]:
		for victim in VICTIMS:
			await _new_case()
			var where := "split on %s / %s" % [trigger, victim]
			var d := _data(trigger in ["bounce", "non_lethal_hit"])
			var stage := BulletSplitStage2D.new()
			stage.count = 2
			# Children and the other volley may touch the walls too: handled.
			factory.body_entered.connect(func(_b, _v, _i) -> void: pass)
			factory.area_entered.connect(func(_a, _v, _i) -> void: pass)
			match trigger:
				"bounce":
					make_wall(Vector2(60, 0), Vector2(20, 400), 8, 2)
					d.set_collision_mask_from_array([4])
					d.set_bounce_mask_from_array([4])
					stage.split_on_bounce = true
				"non_lethal_hit":
					make_wall(Vector2(60, 0))
					d.bullet_max_collision_count = 5
					stage.split_on_non_lethal_hit = true
				"homing_reached":
					d = _data(true)
					d.set_collision_mask_from_array([])
					d.homing_distance_before_reached = 10.0
					d.homing_take_control_of_texture_rotation = true
					stage.split_on_homing_reached = true
					stage.keep_homing = false # the plain children data cannot steer
				"graze":
					d = _data(true)
					d.set_collision_mask_from_array([])
					make_graze_target(Vector2(60, 15), &"matrix_grazers")
					factory.bullet_grazed.connect(func(_t, _v, _i, _z, _r) -> void: pass)
					stage.split_on_graze = true
			await physics()
			var chain := BulletSplitData2D.new()
			chain.stages = [stage]
			var plain := _data(false)
			plain.shared_bullet_attachment = null
			var v: BulletVolley2D = factory.spawn_volley(d)
			v.split_set_data(chain, plain)
			var other: BulletVolley2D = factory.spawn_volley(_data(false))
			if trigger == "homing_reached":
				v.shared_homing_deque_push_back_global_position_target(Vector2(40, 0))
			if trigger == "graze":
				v.graze_set_zones([H.make_graze_zone([30.0])], &"matrix_grazers")
			await idle(1)
			_arm(["disable"], victim, v, other)
			await _await_hook(40)
			problems.append_array(await _settle_and_check(where))
			problems.append_array(_claim_errors().map(func(e): return "%s error: %s" % [where, e]))
	assert_eq("\n".join(problems), "", "every non-lethal split survives a free from the parent's callback")


## Every case starts clean: no armed hook, a fresh factory (the last case may
## have freed its factory or left world settings such as despawn bounds).
func _new_case() -> void:
	AttachmentHookProbe2D.clear_hook()
	if is_instance_valid(factory):
		factory.queue_free()
		await idle(1)
	factory = BulletFactory2D.new()
	add_child_autofree(factory)
	check_factory_after = true
	await idle()
	orphans_at_case_start = int(Performance.get_monitor(Performance.OBJECT_ORPHAN_NODE_COUNT))


## An attachment callback that runs while the factory is busy (the pool
## release of a drained volley holds the busy latch) and enables a parked
## volley: the enable is refused up front with the busy wording and changes
## nothing (it used to succeed but leave the volley out of the tick set, so
## it never moved).
func test_enable_volley_inside_a_busy_factory_is_refused_and_changes_nothing() -> void:
	var plain := _data(true)
	plain.shared_bullet_attachment = null
	var parked: BulletVolley2D = factory.spawn_volley(plain)
	parked.set_is_auto_pooling_enabled(false)
	for i in 3:
		parked.disable_bullet(i)
	await idle(1)
	assert_true(parked.is_parked(), "parked before the case")
	var results: Array = []
	var books_in_hook: Array = []
	AttachmentHookProbe2D.hook_on = ["disable"]
	AttachmentHookProbe2D.hook = func() -> void:
		results.append(parked.enable_volley(plain))
		books_in_hook.append(parked.is_parked())
	var hooked: BulletVolley2D = factory.spawn_volley(_data(false))
	await idle(1)
	factory.clear_active_bullets()
	assert_eq(results, [false], "the enable inside the busy factory is refused")
	expect_error_sequence(["enable_volley: BulletFactory2D is currently busy. Ignoring the request."])
	assert_eq(books_in_hook, [true], "the refused enable changed nothing")
	await idle(1)
	assert_true(parked.is_parked(), "still parked after the clear")
	assert_true(parked.enable_volley(plain), "an idle-frame enable works")
	var start: Vector2 = parked.get_bullet_transform(0).origin
	for k in 3:
		await physics(1)
	assert_ne(parked.get_bullet_transform(0).origin, start, "the enabled volley ticks")
	assert_true(is_instance_valid(hooked), "hooked volley alive")



## clear_bullet from inside an attachment callback is refused by the same
## re-entrancy latch as disable_bullet: it reports false and records nothing
## (no split, no On Clear sound or flash) for a bullet that does not die.
func test_clear_bullet_refused_by_the_latch_records_no_split() -> void:
	var d := _data(false)
	d.effect_layers = [H.make_effect_layer(BulletEffectLayerData2D.EFFECT_ON_CLEAR)]
	var stage := BulletSplitStage2D.new()
	stage.count = 2
	stage.split_on_clear = true
	var chain := BulletSplitData2D.new()
	chain.stages = [stage]
	var v: BulletVolley2D = factory.spawn_volley(d)
	var plain := _data(false)
	plain.shared_bullet_attachment = null
	v.split_set_data(chain, plain)
	await idle(1)
	var splits: Array = []
	factory.bullet_split.connect(func(_p, _c, _ch, _d) -> void: splits.append(1))
	factory.debug_set_effect_log_enabled(true)
	var results: Array = []
	AttachmentHookProbe2D.hook_on = ["disable"]
	AttachmentHookProbe2D.hook = func() -> void:
		results.append(v.clear_bullet(1))
	v.disable_bullet(0)
	expect_error_sequence(["disable_bullet: re-entrant call from inside on_bullet_enable/on_bullet_disable is not allowed. Use call_deferred to change bullet state from that callback."])
	assert_eq(results, [false], "the refused clear reports false")
	assert_true(v.is_bullet_status_enabled(1), "bullet 1 still flies")
	factory.debug_advance_time(1.0 / 60.0)
	assert_eq(splits.size(), 0, "no split for a bullet that did not die")
	assert_eq(factory.debug_get_effect_log().size(), 0, "no On Clear flash either")


## A spawner shot runs the attachment callbacks of its new volley (fresh:
## on_bullet_spawn, pooled: on_bullet_enable) inside shoot_once, outside any
## of the spawner's own emits: a callback may free() the spawner itself. The
## shot then stops without touching the freed spawner (manual shot, auto-fire
## and a burst step), and the factory keeps running.
func test_freeing_the_spawner_inside_its_shot_never_touches_freed_memory() -> void:
	var problems: Array = []
	for pooled in [false, true]:
		for path in ["shoot_once", "auto_fire", "burst"]:
			await _new_case()
			var where := "%s %s" % ["pooled" if pooled else "fresh", path]
			var d := _data(false)
			if pooled:
				factory.spawn_volley(d)
				await idle(1)
				factory.clear_active_bullets()
				await idle(1)
			var sp := make_spawner(d, BulletSpawner2D.PATTERN_FROM_HELPER_RING, 3)
			if path == "burst":
				sp.burst_enabled = true
				sp.burst_count = 3
				sp.burst_interval_sec = 0.01
			AttachmentHookProbe2D.hook_on = ["spawn", "enable"]
			AttachmentHookProbe2D.hook = func() -> void:
				if is_instance_valid(sp):
					sp.free()
			match path:
				"shoot_once":
					sp.shoot_once()
				"auto_fire", "burst":
					sp.shoot_interval_sec = 0.01
					sp.set_shooting_enabled(true)
					for k in 10:
						await idle(1)
						if AttachmentHookProbe2D.fired > 0:
							break
			problems.append_array(await _settle_and_check(where))
			if is_instance_valid(sp):
				problems.append("%s: the spawner outlived its free()" % where)
			problems.append_array(_claim_errors().map(func(e): return "%s error: %s" % [where, e]))
	assert_eq("\n".join(problems), "", "a shot survives its spawner being freed by a callback")


## debug_advance_time steps the whole bullet world: a factory that is not
## in the tree yet, or is being freed (user code inside its teardown calls
## it), refuses with one error and advances nothing.
func test_debug_advance_time_refuses_outside_a_live_factory() -> void:
	var loose := BulletFactory2D.new()
	assert_false(loose.debug_advance_time(0.016), "not ready: nothing advanced")
	expect_error_sequence(["BulletFactory2D::debug_advance_time: the factory is not in the scene tree or is being freed, nothing advanced."])
	loose.free()
	await _new_case()
	var f := factory
	f.spawn_volley(_data(false))
	await idle(1)
	var results: Array = []
	# A child of the factory dies inside the factory's teardown: its
	# tree_exiting handler is user code running there.
	var child := Node.new()
	f.add_child(child)
	child.tree_exiting.connect(func() -> void: results.append(f.debug_advance_time(0.016)))
	check_factory_after = false
	f.free()
	assert_eq(results, [false], "teardown: nothing advanced")
	expect_error_sequence(["BulletFactory2D::debug_advance_time: the factory is not in the scene tree or is being freed, nothing advanced."])


## A mass free (reset(), free_active_bullets()) deletes one volley after the
## other, and each deletion runs the attachment callbacks of its bullets. A
## callback that reads the factory and spawner counters in between must never
## see a volley that was already deleted (they walk every tracked volley).
func test_counters_read_inside_a_mass_free_never_touch_deleted_volleys() -> void:
	var problems: Array = []
	for how in ["reset", "free_active"]:
		await _new_case()
		var f := factory
		var sp := make_spawner(_data(false), BulletSpawner2D.PATTERN_FROM_HELPER_RING, 3)
		for k in 4:
			f.spawn_volley(_data(false))
			sp.shoot_once()
		await idle(1)
		var reads: Array = []
		AttachmentHookProbe2D.hook_on = ["disable"]
		AttachmentHookProbe2D.hook_repeat = true
		AttachmentHookProbe2D.hook = func() -> void:
			if is_instance_valid(f) and is_instance_valid(sp):
				reads.append([f.count_bullets_in_circle(Vector2.ZERO, 1e6), f.count_bullets_in_rect(Rect2(-1e6, -1e6, 2e6, 2e6)), f.get_active_bullet_count(), f.debug_get_active_bullets_amount(), f.debug_get_live_volley_ids(0).size(), sp.get_live_volley_count(), sp.get_active_live_bullet_count()])
		if how == "reset":
			f.reset()
		else:
			f.free_active_bullets()
		if reads.is_empty():
			problems.append("%s: the callbacks never ran" % how)
		AttachmentHookProbe2D.clear_hook()
		problems.append_array(_claim_errors().map(func(e): return "%s error: %s" % [how, e]))
	assert_eq("\n".join(problems), "", "counters read during a mass free stay on live volleys")


## A graze can split its bullet (split_on_graze); the split disables the
## parent, whose on_bullet_disable is user code. Freeing the owner spawner
## there must not leave the graze dispatch holding the dead spawner (it is
## named in the "nothing connected to bullet_grazed" warning right after).
func test_freeing_the_spawner_inside_a_graze_split_never_touches_freed_memory() -> void:
	var grazed := Node2D.new()
	grazed.position = Vector2(20, 0)
	grazed.add_to_group(&"cfm_graze")
	add(grazed)
	var stage := BulletSplitStage2D.new()
	stage.count = 2
	stage.split_on_graze = true
	stage.split_on_lifetime_over = false
	var chain := BulletSplitData2D.new()
	chain.stages = [stage]
	var d := _data(true)
	d.transforms = [Transform2D(), Transform2D(0.0, Vector2(0, 300))]
	d.bullet_max_collision_count = 0
	var sp := make_spawner(d, BulletSpawner2D.PATTERN_FROM_HELPER_CUSTOM, 2)
	sp.helper_custom_transforms = [Transform2D(), Transform2D(0.0, Vector2(0, 300))]
	sp.orphaned_volleys = BulletSpawner2D.ORPHANED_VOLLEYS_KEEP_FLYING
	sp.graze_enabled = true
	sp.graze_zones = [H.make_graze_zone([30.0])]
	sp.graze_node_group = &"cfm_graze"
	sp.split_enabled = true
	sp.split_data = chain
	AttachmentHookProbe2D.hook_on = ["disable"]
	AttachmentHookProbe2D.hook = func() -> void:
		if is_instance_valid(sp):
			sp.free()
	assert_true(sp.shoot_once(), "shot")
	for k in 20:
		await physics(1)
		if AttachmentHookProbe2D.fired > 0:
			break
	assert_gt(AttachmentHookProbe2D.fired, 0, "the split's disable callback ran")
	AttachmentHookProbe2D.clear_hook()
	await physics(2)
	for err in get_errors(): # the warning about the unhandled graze may name the factory
		if err.is_push_warning():
			err.handled = true
	assert_eq(_claim_errors(), [], "no error")


## An attachment scene whose script runs a hook from _init (the scene is
## instantiated) and from on_time_scale_changed. Hooks live in Engine meta,
## armed per test and consumed once.
const HOSTILE_ATTACHMENT := """
extends BulletAttachment2D
func _run(key: StringName) -> void:
	var cb: Callable = Engine.get_meta(key, Callable())
	if cb.is_valid():
		Engine.remove_meta(key)
		cb.call()
func _init() -> void:
	_run(&"bb_hostile_init")
func on_time_scale_changed(_scale: float) -> void:
	_run(&"bb_hostile_scale")
"""


func _hostile_scene() -> PackedScene:
	var script := GDScript.new()
	script.source_code = HOSTILE_ATTACHMENT
	assert_eq(script.reload(), OK, "the hostile attachment compiles")
	var node: Node = BulletAttachment2D.new()
	node.set_script(script)
	var scene := PackedScene.new()
	assert_eq(scene.pack(node), OK, "packs")
	node.free()
	return scene


func _clear_hostile_meta() -> void:
	for key in [&"bb_hostile_init", &"bb_hostile_scale"]:
		if Engine.has_meta(key):
			Engine.remove_meta(key)


func _free_victim(victim: String, v: BulletVolley2D) -> Callable:
	if victim == "factory":
		check_factory_after = false
	var f := factory
	return func() -> void:
		var target: Object = v if victim == "volley" else f
		if is_instance_valid(target):
			target.free()


## The scene's _init runs inside the attach (a fresh instantiate): at a
## spawn (shared attachment; the factory is the only handle there) and from
## bullet_set_attachment.
func test_freeing_inside_an_attachment_init_never_touches_freed_memory(p: Array = use_parameters([["spawn", "factory"], ["set", "volley"], ["set", "factory"]])) -> void:
	var scene := _hostile_scene()
	var v: BulletVolley2D = null
	if p[0] == "set":
		v = factory.spawn_volley(H.make_still_data(3))
	Engine.set_meta(&"bb_hostile_init", _free_victim(p[1], v))
	if p[0] == "spawn":
		var d := H.make_still_data(3)
		d.shared_bullet_attachment = scene
		assert_null(factory.spawn_volley(d), "a spawn whose factory died returns nothing")
	else:
		v.bullet_set_attachment(1, scene)
	_clear_hostile_meta()
	await idle(2)
	if p[1] == "factory":
		assert_false(is_instance_valid(factory), "the factory is gone")
	else:
		assert_false(is_instance_valid(v), "the volley is gone")
	expect_no_errors()


## A slowed volley hands its time scale to a new attachment
## (on_time_scale_changed, user code) inside bullet_set_attachment.
func test_freeing_inside_a_time_scale_handoff_never_touches_freed_memory(victim: String = use_parameters(["volley", "factory"])) -> void:
	var scene := _hostile_scene()
	var v: BulletVolley2D = factory.spawn_volley(H.make_still_data(3))
	factory.time_scale = 0.5
	await idle(2) # the factory pushes the volley's scale
	Engine.set_meta(&"bb_hostile_scale", _free_victim(victim, v))
	v.bullet_set_attachment(0, scene)
	_clear_hostile_meta()
	await idle(2)
	assert_false(is_instance_valid(v), "the volley is gone")
	expect_no_errors()


## all_bullets_set_attachment attaches bullet by bullet: a callback that
## frees the volley ends the loop.
func test_freeing_inside_all_bullets_set_attachment_stops_the_loop() -> void:
	var v: BulletVolley2D = factory.spawn_volley(H.make_still_data(3))
	AttachmentHookProbe2D.hook_on = ["spawn"]
	AttachmentHookProbe2D.hook = func() -> void:
		if is_instance_valid(v):
			v.free()
	v.all_bullets_set_attachment(probe_scene, Vector2.ZERO)
	assert_eq(AttachmentHookProbe2D.fired, 1, "the first attach freed the volley")
	assert_false(is_instance_valid(v), "the volley is gone")
	await idle(2)
	expect_no_errors()


## bullet_free_attachment hides the attachment first: its visibility_changed
## handlers run there and may free the volley. (Freeing the attachment or the
## factory holding it there deletes Godot's own emitter mid-emit: an engine
## crash the plugin cannot guard, AGENTS.md §14.)
func test_freeing_inside_visibility_changed_of_a_freed_attachment() -> void:
	var victim := "volley"
	var d := H.make_still_data(2)
	d.shared_bullet_attachment = probe_scene
	var v: BulletVolley2D = factory.spawn_volley(d)
	var att: Node2D = v.bullet_get_attachment(0)
	assert_not_null(att, "bullet 0 carries an attachment")
	if victim == "factory":
		check_factory_after = false
	var f := factory
	att.visibility_changed.connect(func() -> void:
		var target: Object = f if victim == "factory" else v
		if is_instance_valid(target):
			BulletFactory2D.debug_free_like_release(target))
	v.bullet_free_attachment(0)
	await idle(2)
	assert_false(is_instance_valid(att), "the attachment is gone")
	expect_no_errors()
