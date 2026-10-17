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

const DISABLE_TRIGGERS := ["disable_bullet", "killing_hit", "expiry", "clear_active", "clear_circle",
	"clear_rect", "despawn", "reset", "free_active", "pickups"]
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
