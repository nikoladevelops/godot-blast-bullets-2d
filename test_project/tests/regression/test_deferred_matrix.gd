extends BlastTest
## Every structural BulletFactory2D operation has a `*_deferred` twin that runs
## on the next idle frame (a plain call_deferred() from a physics callback still
## flushes inside it, `integration/test_engine_facts.gd`). This suite walks
## EVERY bound twin from ClassDB and pins, one row per twin:
##   - the deferred call from inside a physics callback queues (nothing applied
##     synchronously) and applies EXACTLY ONCE on the next idle frame, with
##     concrete state (volley counts, pool buckets, attachment/laser pools,
##     emission counters) checked again two frames later,
##   - the plain twin from the same callback is refused with its pinned text
##     and changes nothing (it queues nothing either). Where the plain call is
##     not a STRUCTURAL refusal but the busy latch (clear_active_bullets,
##     convert_all_bullets_to_pickups) the real text is pinned by a dedicated
##     test at the bottom - those two name "busy", not the deferred hint.
##
## The walk is generic: a NEW bound `*_deferred` method fails
## `test_the_walk_covers_every_bound_deferred_method` until it gets a row here.
##
## `free_volley_deferred` has NO plain twin (`free_volley` is not bound), so it
## has no refusal row; the deferred-shape call of a volley is a volley method
## (`set_collision_shape_runtime_deferred`) and is pinned by
## `volley/test_volley_freeze_contract.gd` / `factory/test_factory_handler_rejects.gd`.

## Every bound BulletFactory2D method ending in _deferred (ClassDB order,
## sorted), i.e. what this file must cover.
const TWINS := [
	"clear_active_bullets_deferred",
	"convert_all_bullets_to_pickups_deferred",
	"free_active_bullets_deferred",
	"free_attachments_pool_deferred",
	"free_attachments_pool_for_scene_deferred",
	"free_bullets_pool_deferred",
	"free_disabled_bullets_deferred",
	"free_lasers_pool_deferred",
	"free_volley_deferred",
	"populate_attachments_pool_deferred",
	"populate_bullets_pool_deferred",
	"populate_lasers_pool_deferred",
	"populate_pickup_pool_deferred",
	"reset_deferred",
]

var ctx: Dictionary = {}
var counts: Dictionary = {}


func before_each() -> void:
	await super()
	ctx.clear()
	counts = {"reset_finished": 0, "bullets_cleared": 0, "bullets_converted": 0}
	for sig in counts:
		factory.connect(sig, func(_a = null, _b = null, _c = null, _d = null): counts[sig] += 1)


# ---- readouts the rows pin -----------------------------------------------------


func _spawn(bullets: int) -> BulletVolley2D:
	var d := H.make_volley_data(bullets, 0.0, 30.0)
	d.collision_shape = H.make_circle_shape(6.0)
	return factory.spawn_volley(d)


func _pooled_data(bullets: int) -> BulletVolleyData2D:
	var d := H.make_volley_data(bullets, 0.0, 30.0)
	d.collision_shape = H.make_circle_shape(6.0)
	return d


## Pre-warms one bucket (amount `bullets`, circle): returns its key.
func _prewarm(bullets: int, instances: int) -> VolleyPoolKey2D:
	var d := _pooled_data(bullets)
	var key: VolleyPoolKey2D = BulletFactory2D.debug_expected_pool_key(d)
	factory.populate_bullets_pool(key, d, instances)
	return key


func _active() -> int:
	return factory.debug_get_active_volleys_amount()


func _pooled() -> int:
	return factory.debug_get_bullets_pool_amount()


func _buckets() -> int:
	return factory.debug_get_bullets_pool_info().size()


func _attachment_pool() -> int:
	return factory.debug_get_attachments_pool_amount()


func _laser_pool() -> int:
	return factory.debug_get_laser_pool_amount()


## Active volleys that are pickup volleys (the conversion's visible result).
func _pickup_volleys() -> int:
	var n := 0
	for v in factory.get_active_volleys(true):
		if (v as BulletVolley2D).get_pickup_data() != null:
			n += 1
	return n


func _emitted(sig: String) -> int:
	return int(counts.get(sig, 0))


func _alive(obj: Variant) -> bool:
	return is_instance_valid(obj)


# ---- the rows -------------------------------------------------------------------


func _arm_two_bullet_volleys(_c: Dictionary) -> void:
	_spawn(2)
	_spawn(3)


func _arm_two_volleys_and_a_pool(_c: Dictionary) -> void:
	_spawn(2)
	_spawn(3)
	_prewarm(4, 1)


func _arm_two_pools(_c: Dictionary) -> void:
	_prewarm(2, 1)
	_prewarm(3, 1)


func _arm_attachment_pool(c: Dictionary) -> void:
	c["scene"] = make_probe_scene()
	factory.populate_attachments_pool(c["scene"], 2)


func _arm_two_scene_pools(c: Dictionary) -> void:
	c["scene"] = make_probe_scene()
	c["other"] = make_probe_scene()
	factory.populate_attachments_pool(c["scene"], 2)
	factory.populate_attachments_pool(c["other"], 3)


func _arm_volley_and_a_sibling(c: Dictionary) -> void:
	c["v"] = _spawn(2)
	_spawn(3)


func _arm_pool_and_volleys(_c: Dictionary) -> void:
	_prewarm(2, 2)
	_spawn(3)


## One row per twin: `arm` prepares the state, `args` builds the arguments at
## call time (so they see the armed objects), `state` is the concrete readout
## this row pins, `after` the readout expected once the deferred call applied.
## `plain_error` is the text the plain twin reports in this context ("" = it is
## not refused here; see the dedicated tests at the bottom).
func _rows() -> Array:
	var pool_data := _pooled_data(2)
	var pool_key: VolleyPoolKey2D = BulletFactory2D.debug_expected_pool_key(pool_data)
	var pickup := BulletPickupData2D.new()
	pickup.lifetime_sec = 20.0
	return [
		{
			"deferred": &"reset_deferred", "plain": &"reset", "plain_error": "Use reset_deferred() instead",
			"arm": _arm_pool_and_volleys,
			"args": func(_c: Dictionary) -> Array: return [],
			"state": func(_c: Dictionary) -> Array: return [_active(), _pooled(), _emitted("reset_finished")],
			"after": func(_c: Dictionary) -> Array: return [0, 0, 1],
		},
		{
			"deferred": &"free_active_bullets_deferred", "plain": &"free_active_bullets", "plain_error": "Use free_active_bullets_deferred() instead",
			"arm": _arm_two_volleys_and_a_pool,
			"args": func(_c: Dictionary) -> Array: return [],
			"state": func(_c: Dictionary) -> Array: return [_active(), _pooled()],
			"after": func(_c: Dictionary) -> Array: return [0, 1],
		},
		{
			"deferred": &"clear_active_bullets_deferred", "plain": "", "plain_error": "",
			"arm": _arm_two_volleys_and_a_pool,
			"args": func(_c: Dictionary) -> Array: return [],
			"state": func(_c: Dictionary) -> Array: return [_active(), _pooled(), _emitted("bullets_cleared")],
			"after": func(c: Dictionary) -> Array: return [0, int(c["before"][1]) + 2, 2],
		},
		{
			"deferred": &"free_disabled_bullets_deferred", "plain": &"free_disabled_bullets", "plain_error": "Use free_disabled_bullets_deferred() instead",
			"arm": func(c: Dictionary) -> void: _prewarm(2, 2),
			"args": func(_c: Dictionary) -> Array: return [],
			"state": func(_c: Dictionary) -> Array: return [_pooled(), _buckets()],
			"after": func(_c: Dictionary) -> Array: return [0, 0],
		},
		{
			"deferred": &"free_bullets_pool_deferred", "plain": &"free_bullets_pool", "plain_error": "Use free_bullets_pool_deferred() instead",
			"arm": _arm_two_pools,
			"args": func(_c: Dictionary) -> Array: return [],
			"state": func(_c: Dictionary) -> Array: return [_pooled(), _buckets()],
			"after": func(_c: Dictionary) -> Array: return [0, 0],
		},
		{
			"deferred": &"populate_bullets_pool_deferred", "plain": &"populate_bullets_pool", "plain_error": "Use populate_bullets_pool_deferred() instead",
			"arm": func(_c: Dictionary) -> void: pass,
			"args": func(_c: Dictionary) -> Array: return [pool_key, pool_data, 3],
			"state": func(_c: Dictionary) -> Array: return [_pooled(), _buckets()],
			"after": func(_c: Dictionary) -> Array: return [3, 1],
		},
		{
			"deferred": &"free_attachments_pool_deferred", "plain": &"free_attachments_pool", "plain_error": "Use free_attachments_pool_deferred() instead",
			"arm": _arm_attachment_pool,
			"args": func(_c: Dictionary) -> Array: return [],
			"state": func(_c: Dictionary) -> Array: return [_attachment_pool()],
			"after": func(_c: Dictionary) -> Array: return [0],
		},
		{
			"deferred": &"populate_attachments_pool_deferred", "plain": &"populate_attachments_pool", "plain_error": "Use populate_attachments_pool_deferred() instead",
			"arm": func(c: Dictionary) -> void: c["scene"] = make_probe_scene(),
			"args": func(c: Dictionary) -> Array: return [c["scene"], 2],
			"state": func(_c: Dictionary) -> Array: return [_attachment_pool()],
			"after": func(_c: Dictionary) -> Array: return [2],
		},
		{
			"deferred": &"free_attachments_pool_for_scene_deferred", "plain": &"free_attachments_pool_for_scene", "plain_error": "Use free_attachments_pool_for_scene_deferred() instead",
			"arm": _arm_two_scene_pools,
			"args": func(c: Dictionary) -> Array: return [c["scene"]],
			"state": func(_c: Dictionary) -> Array: return [_attachment_pool()],
			"after": func(_c: Dictionary) -> Array: return [3],
		},
		{
			"deferred": &"free_volley_deferred", "plain": "", "plain_error": "",
			"arm": _arm_volley_and_a_sibling,
			"args": func(c: Dictionary) -> Array: return [c["v"]],
			"state": func(c: Dictionary) -> Array: return [_active(), _alive(c["v"])],
			# queue_free() itself is safe anywhere and runs at once (the
			# volley leaves the factory's listings now); the deletion lands
			# in the frame's tail.
			"sync": func(_c: Dictionary) -> Array: return [1, true],
			"after": func(_c: Dictionary) -> Array: return [1, false],
		},
		{
			"deferred": &"populate_lasers_pool_deferred", "plain": &"populate_lasers_pool", "plain_error": "Use populate_lasers_pool_deferred() instead",
			"arm": func(_c: Dictionary) -> void: pass,
			"args": func(_c: Dictionary) -> Array: return [2, 3],
			"state": func(_c: Dictionary) -> Array: return [_laser_pool()],
			"after": func(_c: Dictionary) -> Array: return [3],
		},
		{
			"deferred": &"free_lasers_pool_deferred", "plain": &"free_lasers_pool", "plain_error": "Use free_lasers_pool_deferred() instead",
			"arm": func(_c: Dictionary) -> void: factory.populate_lasers_pool(2, 2),
			"args": func(_c: Dictionary) -> Array: return [2],
			"state": func(_c: Dictionary) -> Array: return [_laser_pool()],
			"after": func(_c: Dictionary) -> Array: return [0],
		},
		{
			"deferred": &"convert_all_bullets_to_pickups_deferred", "plain": "", "plain_error": "",
			"arm": func(_c: Dictionary) -> void: _spawn(2),
			"args": func(_c: Dictionary) -> Array: return [pickup, Vector2(400, 0), 0],
			"state": func(_c: Dictionary) -> Array: return [_active(), _pickup_volleys(), _emitted("bullets_converted")],
			"after": func(_c: Dictionary) -> Array: return [1, 1, 1],
		},
		{
			"deferred": &"populate_pickup_pool_deferred", "plain": &"populate_pickup_pool", "plain_error": "Use populate_bullets_pool_deferred() instead",
			"arm": func(_c: Dictionary) -> void: pass,
			"args": func(_c: Dictionary) -> Array: return [pickup, 2, 2],
			"state": func(_c: Dictionary) -> Array: return [_pooled(), _buckets()],
			"after": func(_c: Dictionary) -> Array: return [2, 1],
		},
	]


# ---- the walk -------------------------------------------------------------------


## The walk is generic: every bound `*_deferred` method has exactly one row, and
## every row names methods that exist.
func test_the_walk_covers_every_bound_deferred_method() -> void:
	var found: Array = []
	for m in ClassDB.class_get_method_list("BulletFactory2D", true):
		var name := str(m["name"])
		if name.ends_with("_deferred"):
			found.append(name)
	found.sort()
	assert_eq(found, TWINS, "every bound *_deferred method (ClassDB walk)")
	var rows := _rows()
	assert_eq(rows.size(), TWINS.size(), "one row per bound twin")
	var named: Array = []
	for row in rows:
		named.append(str(row["deferred"]))
		assert_true(ClassDB.class_has_method("BulletFactory2D", row["deferred"]), "%s is bound" % row["deferred"])
		if str(row["plain"]) != "":
			assert_true(ClassDB.class_has_method("BulletFactory2D", row["plain"]), "%s's plain twin %s is bound" % [row["deferred"], row["plain"]])
	named.sort()
	assert_eq(named, TWINS, "the rows cover exactly those twins")


## From inside a physics callback each twin queues (nothing applied
## synchronously) and applies exactly ONCE on the next idle frame: the concrete
## state asserted per row is identical after two more frames.
func test_every_deferred_twin_applies_exactly_once_from_a_physics_callback(row: Dictionary = use_parameters(_rows())) -> void:
	ctx.clear()
	row["arm"].call(ctx)
	var before: Array = row["state"].call(ctx)
	ctx["before"] = before
	await physics() # inside the physics frame: structural calls are refused here
	var args: Array = row["args"].call(ctx)
	factory.callv(row["deferred"], args)
	if row.has("sync"):
		assert_eq(row["state"].call(ctx), row["sync"].call(ctx), "%s: the queued call's synchronous part (queue_free() is safe anywhere)" % row["deferred"])
	else:
		assert_eq(row["state"].call(ctx), before, "%s: nothing applied synchronously" % row["deferred"])
	await idle(2)
	var want: Array = row["after"].call(ctx)
	assert_eq(row["state"].call(ctx), want, "%s: applied exactly once on the next idle frame" % row["deferred"])
	await idle(2)
	assert_eq(row["state"].call(ctx), want, "%s: and never a second time" % row["deferred"])
	expect_no_errors()


## The plain twin refuses from the same callback with its pinned text and
## changes NOTHING (no synchronous effect, nothing queued); the deferred twin
## then still works from that context.
func test_every_plain_twin_is_refused_from_a_physics_callback_with_its_pinned_text(row: Dictionary = use_parameters(_rows().filter(func(r): return str(r["plain_error"]) != ""))) -> void:
	ctx.clear()
	row["arm"].call(ctx)
	var before: Array = row["state"].call(ctx)
	ctx["before"] = before
	await physics()
	var args: Array = row["args"].call(ctx)
	factory.callv(row["plain"], args)
	expect_error(str(row["plain_error"]), "%s from inside a physics frame" % row["plain"])
	assert_eq(row["state"].call(ctx), before, "%s: the refused call changed nothing" % row["plain"])
	await idle(2)
	assert_eq(row["state"].call(ctx), before, "%s: and queued nothing either" % row["plain"])
	await physics()
	factory.callv(row["deferred"], row["args"].call(ctx))
	await idle(2)
	assert_eq(row["state"].call(ctx), row["after"].call(ctx), "%s: the deferred twin works from the same context" % row["deferred"])
	await idle(1)
	expect_no_errors()


# ---- the two busy-latch twins (their plain call is not a structural refusal) ----


## `_hit_data`: one bullet that hits the wall at x = 60 and never dies.
func _hit_data() -> BulletVolleyData2D:
	var d := H.make_volley_data(1, 300.0, 30.0)
	d.transforms = [Transform2D(0.0, Vector2.ZERO)]
	d.monitorable = true
	d.set_collision_mask_from_array([3])
	d.collision_shape = H.make_circle_shape(6.0)
	d.bullet_max_collision_count = 0
	return d


func _first_hit(action: Callable) -> bool:
	# The test bullets keep flying after the handler ran: a game handles those
	# events (the handler below is the real one for the row).
	handle_events(factory)
	make_wall(Vector2(60, 0))
	await physics()
	var ran: Array = []
	var handler := func(_b, _v, _i):
		if ran.is_empty():
			ran.append(true)
			action.call()
	factory.body_entered.connect(handler)
	factory.spawn_volley(_hit_data())
	for i in 90:
		await physics()
		if not ran.is_empty():
			break
	factory.body_entered.disconnect(handler)
	return not ran.is_empty()


## `clear_active_bullets` is NOT a structural refusal: from a hit handler it
## clears synchronously (that is the documented "the player got hit: clear the
## screen" path), and the deferred twin applies the same clear exactly once on
## the next idle frame from a physics callback where nothing is cleared yet.
func test_clear_active_bullets_from_a_hit_handler_clears_at_once() -> void:
	_spawn(2)
	_spawn(3)
	var cleared: Array = []
	var ran := await _first_hit(func(): cleared.append(factory.clear_active_bullets()))
	assert_true(ran, "the hit handler ran inside the sweep")
	assert_eq(cleared, [6], "the handler's clear took every live bullet at once")
	assert_eq(_active(), 0, "no volley is active any more")
	assert_eq(_emitted("bullets_cleared"), 3, "one announcement per cleared volley")
	expect_no_errors()
	# The deferred twin queues the same clear from a physics frame.
	_spawn(2)
	await physics()
	factory.clear_active_bullets_deferred()
	assert_eq(_active(), 1, "nothing cleared synchronously")
	await idle(2)
	assert_eq(_active(), 0, "the deferred clear ran on the idle frame")
	assert_eq(_emitted("bullets_cleared"), 4, "one more announcement")
	await idle(2)
	assert_eq(_emitted("bullets_cleared"), 4, "and exactly once")
	expect_no_errors()


## Same for the conversion: a hit handler may convert at once (it is not
## structural), and the deferred twin converts exactly once.
func test_convert_all_bullets_from_a_hit_handler_converts_at_once() -> void:
	_spawn(2)
	var pickup := BulletPickupData2D.new()
	pickup.lifetime_sec = 20.0
	var out: Array = []
	var ran := await _first_hit(func(): out.append(factory.convert_all_bullets_to_pickups(pickup, Vector2(400, 0))))
	assert_true(ran, "the hit handler ran inside the sweep")
	assert_eq(out.size(), 1, "the handler's conversion returned its pickup volley")
	assert_eq(_emitted("bullets_converted"), 1, "one conversion")
	assert_eq(_pickup_volleys(), 1, "one pickup volley")
	expect_no_errors()
	_spawn(2)
	await physics()
	factory.convert_all_bullets_to_pickups_deferred(pickup, Vector2(400, 0))
	assert_eq(_emitted("bullets_converted"), 1, "nothing converted synchronously")
	await idle(2)
	assert_eq(_emitted("bullets_converted"), 2, "the deferred conversion ran on the idle frame")
	await idle(2)
	assert_eq(_emitted("bullets_converted"), 2, "and exactly once")
	expect_no_errors()


## The two twins that are under the busy latch (a factory operation is running)
## report their own pinned texts there - not the deferred hint. Reached through
## an attachment's on_bullet_disable, which runs inside the clear.
class BusyProbe extends BulletAttachment2D:
	static var calls: Array = []
	static var results: Array = []

	func on_bullet_disable() -> void:
		if calls.is_empty():
			return
		var pending := calls
		calls = []
		for c in pending:
			results.append(c.call())


func _busy_scene() -> PackedScene:
	var probe := BusyProbe.new()
	var ps := PackedScene.new()
	ps.pack(probe)
	probe.free()
	return ps


func test_the_two_busy_latch_twins_report_the_busy_text_inside_a_running_operation() -> void:
	BusyProbe.results = []
	var d := H.make_volley_data(2, 0.0, 30.0)
	d.collision_shape = H.make_circle_shape(6.0)
	d.shared_bullet_attachment = _busy_scene()
	var pickup := BulletPickupData2D.new()
	pickup.lifetime_sec = 20.0
	factory.spawn_volley(d)
	await idle(1)
	assert_eq(_active(), 1, "one volley with the probe attachment")
	BusyProbe.calls = [
		func(): return factory.clear_active_bullets(),
		func(): return factory.convert_all_bullets_to_pickups(pickup, Vector2(400, 0)),
	]
	factory.clear_active_bullets()
	await idle(1)
	assert_eq(BusyProbe.results.size(), 2, "the busy callbacks ran inside the clear")
	assert_eq(BusyProbe.results[0], 0, "the nested clear cleared nothing")
	assert_eq(BusyProbe.results[1], [], "the nested conversion converted nothing")
	expect_error("Error when trying to clear active bullets. BulletFactory2D is currently busy. Ignoring the request")
	expect_error("is busy (a sweep is running). Convert after it, e.g. from the next frame (convert_all_bullets_to_pickups_deferred queues it), nothing converted.")
	BusyProbe.calls = []
	expect_no_errors()


## A `set_collision_shape_runtime_deferred` queued from inside a hit handler
## applies exactly ONCE on the next idle frame, and a later life of that data
## always starts with ITS OWN shape - never with the queued one.
##
## KNOWN BUG (reported, deliberately NOT pinned here): the case also asks that a
## queued call "never touched the pool entry" when the handler pools the volley
## in the same breath. It DOES. `set_collision_shape_runtime_deferred` queues a
## lambda that resolves the volley by instance id only
## (src/bullet_volley/bullet_volley2d_collision.cpp, the queue_structural_call
## lambda in the method) and `set_collision_shape_runtime` itself has no
## pooled-handle refusal (same file, its guard list ends at the factory-alive /
## structural-unsafe checks): the flush therefore mutates the POOLED instance -
## its shape becomes the queued one and the pool RE-BUCKETS the entry (a warm
## 1-bullet-circle entry turns into a rectangle entry), so the next same-key
## spawn is a MISS instead of the hit it must be. Reproduced by the shape-change
## -then-pool probe and by the failing spec form of this test: "the pooled
## entry's shape is untouched" got type 4 (RECTANGLE) instead of 3 (CIRCLE);
## pool hit stats show hits 0 / misses +1 where a hit was guaranteed.
func test_a_deferred_shape_change_applies_once_and_never_reaches_a_later_life() -> void:
	var data := _hit_data()
	var seen: Array = []
	var ran := await _first_hit(func():
		var v: BulletVolley2D = factory.get_active_volleys()[0]
		seen.append(v)
		v.set_collision_shape_runtime_deferred(RectangleShape2D.new()))
	assert_true(ran, "the handler ran")
	var v: BulletVolley2D = seen[0]
	await idle(2)
	assert_eq(int(v.debug_get_shape_state()["type"]), PhysicsServer2D.SHAPE_RECTANGLE, "the queued rectangle applied on the idle frame")
	await idle(2)
	assert_eq(int(v.debug_get_shape_state()["type"]), PhysicsServer2D.SHAPE_RECTANGLE, "and only once")
	# The next life of the same data must wear its own shape, not the rectangle.
	for i in v.get_amount_bullets():
		v.disable_bullet(i)
	await idle(1)
	assert_true(v.is_pooled(), "the volley pooled itself")
	var next: BulletVolley2D = factory.spawn_volley(data)
	assert_eq(int(next.debug_get_shape_state()["type"]), PhysicsServer2D.SHAPE_CIRCLE, "the next life wears its own data's shape")
	assert_almost_eq(float(next.debug_get_shape_state()["circle_radius"]), 6.0, 0.0001, "with its own radius")
	expect_no_errors()
