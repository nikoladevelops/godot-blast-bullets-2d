extends BlastTest
## Who hears a bullet event of a SPAWNER-owned volley: the owning spawner while
## a handler is connected to that signal ON IT, otherwise BulletFactory2D (the
## documented fall-through, `core/signal_route2d.hpp`) - never both, so a game
## can count every hit of every NPC spawner once on one factory handler and a
## spawner with its own handler keeps its own events.
##
## The signal list is walked from ClassDB: every signal BOTH classes declare
## with a `BulletVolley2D` payload is the set a spawner-owned event may travel
## through (hits, bounces, expiry, graze, split), and this file drives the
## routing of every signal it can trigger (hits, bounces, expiry) from a real
## shot. The payload of each is checked against what ClassDB declares, with the
## volley LIVE and its books intact inside the handler. Finally, an event
## nobody handles is explained by exactly ONE warning per emitter (never a
## push_error), and events that happened while nothing was connected are never
## replayed to a handler connected afterwards.
##
## Graze and split routing live in their own suites
## (`spawner/test_spawner_graze.gd`, `spawner/test_spawner_graze_orphans.gd`,
## `spawner/test_spawner_split.gd`) because they need their own setup.

## The routed signals this file triggers (payloads walked from ClassDB).
const ROUTED := [&"area_entered", &"body_entered", &"bounce_area_entered", &"bounce_body_entered", &"life_time_over"]

## Every signal declared on BOTH classes with a BulletVolley2D payload, in
## ClassDB order (alphabetical): the complete set a spawner-owned volley's
## events route through. A NEW such signal fails this pin, forcing its routing
## to be covered before it ships.
const SHARED_VOLLEY_PAYLOADS := [
	"area_entered", "body_entered", "bounce_area_entered", "bounce_body_entered",
	"bullet_graze_exited", "bullet_grazed", "bullet_split", "life_time_over",
]

## One row per triggerable signal: what makes the event happen. A bounce that
## consumes its hit (bounce_hit_consumed) emits BOTH its bounce signal and the
## hit signal of the same contact, so those rows name both and the matrix
## checks that both travel to the same emitter.
const CASES := [
	{"sig": "area_entered", "kind": "area", "mask": [5], "layer": 16, "max_count": 1},
	{"sig": "body_entered", "kind": "body", "mask": [3], "layer": 4, "max_count": 1},
	{"sig": "bounce_area_entered", "kind": "bounce_area", "mask": [4], "layer": 8, "max_count": 1, "bounce_mask": [4], "hit_signal": "area_entered"},
	{"sig": "bounce_body_entered", "kind": "bounce_body", "mask": [4], "layer": 8, "max_count": 1, "bounce_mask": [4], "hit_signal": "body_entered"},
	{"sig": "life_time_over", "kind": "expiry", "mask": [3], "layer": 4, "max_count": 0, "lifetime": 0.1},
]

## One row per handler call [which, signal, params, in-handler observations].
var log: Array = []


func before_each() -> void:
	await super()
	log.clear()


## The declared arguments of `sig` on `cls` (ClassDB, inherited included).
func _declared(cls: String, sig: StringName) -> Array:
	for s in ClassDB.class_get_signal_list(cls, true):
		if str(s["name"]) == str(sig):
			return s["args"]
	return []


## The index of the `volley` argument of a routed signal (-1 when it has none).
func _volley_arg_index(sig: StringName) -> int:
	var args := _declared("BulletSpawner2D", sig)
	for i in args.size():
		if str(args[i]["name"]) == "volley":
			return i
	return -1


## A handler for `which` (a name for the report) that records the payload AND
## reads the payload inside the emit (the volley must be alive in there).
func _recorder(which: String, sig: StringName) -> Callable:
	return func(p0 = null, p1 = null, p2 = null, p3 = null, p4 = null) -> void:
		var all_args: Array = [p0, p1, p2, p3, p4]
		var args: Array = _declared("BulletSpawner2D", sig)
		var row := {
			"which": which,
			"signal": str(sig),
			"params": all_args.slice(0, args.size()),
			"in_physics": Engine.is_in_physics_frame(),
		}
		var vi := _volley_arg_index(sig)
		if vi >= 0 and all_args[vi] is BulletVolley2D:
			var v: BulletVolley2D = all_args[vi]
			row["volley"] = v
			row["volley_live"] = is_instance_valid(v) and not v.is_queued_for_deletion() and v.is_volley_active()
			row["invariants"] = v.debug_check_invariants()
			row["amount_bullets"] = v.get_amount_bullets()
			if vi + 1 < args.size() and typeof(all_args[vi + 1]) == TYPE_INT:
				# The stated bullet must be alive in the handler (the plugin
				# kills after the handler, and only if the count is still max).
				row["bullet_enabled"] = v.is_bullet_status_enabled(int(all_args[vi + 1]))
		log.append(row)


func heard(which: String, sig: StringName) -> Array:
	return log.filter(func(r): return r["which"] == which and r["signal"] == str(sig))


func _heard_total(sig: StringName) -> int:
	return log.filter(func(r): return r["signal"] == str(sig)).size()


func _drop_all_handlers(obj: Object, sig: StringName) -> void:
	for c in obj.get_signal_connection_list(sig):
		obj.disconnect(sig, c["callable"])


func _hit_data(row: Dictionary) -> BulletVolleyData2D:
	var d := H.make_volley_data(1, 600.0, row.get("lifetime", 5.0))
	d.transforms = [Transform2D(0.0, Vector2.ZERO)]
	d.monitorable = true
	d.set_collision_mask_from_array(row["mask"])
	if row.has("bounce_mask"):
		d.set_bounce_mask_from_array(row["bounce_mask"])
	d.bullet_max_collision_count = row["max_count"]
	d.collision_shape = H.make_circle_shape(6.0)
	if row.has("hit_signal"):
		# A consumed bounce: one contact, one bounce signal AND the hit signal
		# (the killing hit ends the bullet, so it never comes back).
		d.bounce_hit_consumed = true
	if row["kind"] == "expiry":
		d.is_life_time_over_signal_enabled = true
	return d


## The target that makes the event happen; returns it so the payload can be
## checked against the node that was really hit (identity, not just a type).
func _make_target(row: Dictionary) -> Object:
	match row["kind"]:
		"area", "bounce_area":
			return make_area(Vector2(120, 0), Vector2(20, 400), row["layer"])
		"body", "bounce_body":
			return make_wall(Vector2(120, 0), Vector2(20, 400), row["layer"])
	return null


## Fires one shot and waits for its routed event.
func _fire(sp: BulletSpawner2D, sig: StringName) -> void:
	var before := _heard_total(sig)
	assert_true(sp.shoot_once(), "the shot fired")
	for i in 180:
		await physics()
		if _heard_total(sig) > before:
			return
	assert_gt(_heard_total(sig), before, "the %s event arrived" % sig)


func _check_payload(row: Dictionary, cls: String, sig: StringName, target: Object = null) -> void:
	var args := _declared(cls, sig)
	assert_eq(row["params"].size(), args.size(), "the payload has the %d declared arguments" % args.size())
	var wrong: Array = []
	for i in args.size():
		var want_type := int(args[i]["type"])
		var got = row["params"][i]
		if want_type == TYPE_OBJECT:
			var cls_name := str(args[i].get("class_name", ""))
			var ok: bool = got == null or (got is Object and (cls_name == "" or (got as Object).is_class(cls_name)))
			if not ok:
				wrong.append("arg %d (%s): %s" % [i, args[i]["name"], got])
		elif typeof(got) != want_type:
			wrong.append("arg %d (%s): type %d, declared %d" % [i, args[i]["name"], typeof(got), want_type])
	assert_eq(wrong, [], "every payload argument matches its ClassDB declaration")
	if target != null:
		assert_eq(row["params"][0], target, "the payload names the target that was hit")
	assert_true(row["in_physics"], "the emit lands inside the physics frame (a live tick)")
	var vi := _volley_arg_index(sig)
	assert_true(row["volley_live"], "the payload volley is LIVE inside the handler")
	assert_eq(row["invariants"], "", "and its books are intact inside the handler")
	assert_eq(row["amount_bullets"], 1, "the volley carries the shot's one bullet")
	if row.has("bullet_enabled"):
		assert_true(row["bullet_enabled"], "the named bullet is still alive in the handler")


## The complete routed set, walked from ClassDB: every signal both classes
## declare with a BulletVolley2D payload is pinned here, and the signals this
## suite triggers are all of them that a shot can produce.
func test_every_signal_both_classes_declare_with_a_volley_payload_is_pinned() -> void:
	var shared: Array = []
	for s in ClassDB.class_get_signal_list("BulletFactory2D", true):
		var name := str(s["name"])
		if not ClassDB.class_has_signal("BulletSpawner2D", name):
			continue
		var carries_volley := false
		for a in s["args"]:
			if str(a.get("class_name", "")) == "BulletVolley2D":
				carries_volley = true
		if carries_volley:
			shared.append(name)
	shared.sort()
	assert_eq(shared, SHARED_VOLLEY_PAYLOADS, "the shared volley-payload signals (ClassDB walk)")
	for sig in ROUTED:
		assert_true(SHARED_VOLLEY_PAYLOADS.has(str(sig)), "%s is one of them" % sig)
		assert_eq(_declared("BulletSpawner2D", sig), _declared("BulletFactory2D", sig), "%s carries the same payload on both classes" % sig)


## The routing matrix: with a spawner handler the event goes to the spawner and
## the factory hears nothing; once the spawner lets go, the very same event
## goes to the factory and the spawner hears nothing - each event handled
## exactly once, never both.
func test_a_spawner_handler_takes_the_event_and_the_factory_only_after_it_lets_go(row: Dictionary = use_parameters(CASES)) -> void:
	var sig: StringName = StringName(row["sig"])
	# A consumed bounce carries its hit signal along: both must travel the same
	# way (one contact, one of each), so both are connected and checked.
	var sigs: Array[StringName] = [sig]
	if row.has("hit_signal"):
		sigs.append(StringName(row["hit_signal"]))
	var target := _make_target(row)
	await physics() # the target joins the physics space
	var sp := make_spawner(_hit_data(row), BulletSpawner2D.PATTERN_FROM_SELF, 1)
	for s in sigs:
		sp.connect(s, _recorder("spawner", s))
		assert_false(factory.has_connections(s), "the factory starts with no %s handler" % s)
	await _fire(sp, sig)
	for s in sigs:
		assert_eq(heard("spawner", s).size(), 1, "phase 1: the spawner heard %s exactly once" % s)
		assert_eq(heard("factory", s).size(), 0, "phase 1: the factory stayed silent")
		_check_payload(heard("spawner", s)[0], "BulletSpawner2D", s, target)
	# The spawner lets go: every one of the contact's signals falls through to
	# the factory.
	for s in sigs:
		_drop_all_handlers(sp, s)
		assert_false(sp.has_connections(s), "the spawner has no %s handler any more" % s)
		factory.connect(s, _recorder("factory", s))
	await _fire(sp, sig)
	for s in sigs:
		assert_eq(heard("spawner", s).size(), 1, "phase 2: the disconnected spawner heard no more %s" % s)
		assert_eq(heard("factory", s).size(), 1, "phase 2: the factory took over %s" % s)
		_check_payload(heard("factory", s)[0], "BulletFactory2D", s, target)
		assert_eq(_heard_total(s), 2, "two %s events, each handled exactly once (never both)" % s)


## An event nobody handles at all: a factory-owned volley's hit warns ONCE on
## the factory (the emitter), names the missing handler and is a WARNING, never
## a push_error - and the hit that happened while nobody was connected is never
## replayed to a handler connected later.
func test_an_unhandled_factory_hit_warns_once_and_is_never_replayed() -> void:
	make_wall(Vector2(120, 0))
	await physics()
	var row := {"mask": [3], "max_count": 0, "kind": "body", "layer": 4}
	assert_false(factory.has_connections(&"body_entered"), "nobody is connected")
	factory.spawn_volley(_hit_data(row))
	factory.spawn_volley(_hit_data(row))
	for i in 60:
		await physics()
		if factory.get_frame_stats()["collision_records_total"] >= 2:
			break
	assert_gte(int(factory.get_frame_stats()["collision_records_total"]), 2, "two hits were counted")
	expect_warning("nothing is connected to its area_entered or body_entered signal", "a factory-owned hit nobody handles", "BulletFactory2D '")
	expect_no_errors("the explanation is a warning, never a push_error")
	# Nothing buffered the two events: the handler connected now hears only what
	# happens after it.
	var heard_events: Array = []
	factory.body_entered.connect(func(_b, _v, _i): heard_events.append(1))
	assert_eq(heard_events, [], "the earlier hits were not replayed")
	factory.spawn_volley(_hit_data(row))
	for i in 60:
		await physics()
		if not heard_events.is_empty():
			break
	assert_eq(heard_events.size(), 1, "the next hit reaches the new handler exactly once")


## Same contract for a SPAWNER-owned volley: the spawner is the emitter (its
## own warning wording names the factory fall-through) and the factory stays
## silent because it is not the emitter of an event it cannot hear either.
func test_an_unhandled_spawner_hit_warns_once_on_the_spawner() -> void:
	make_wall(Vector2(120, 0))
	await physics()
	var sp := make_spawner(_hit_data({"mask": [3], "max_count": 0, "kind": "body", "layer": 4}), BulletSpawner2D.PATTERN_FROM_SELF, 1)
	assert_false(sp.has_connections(&"body_entered"), "nobody is connected on the spawner")
	assert_false(factory.has_connections(&"body_entered"), "nor on the factory")
	assert_true(sp.shoot_once(), "shot")
	for i in 60:
		await physics()
		if factory.get_frame_stats()["collision_records_total"] >= 1:
			break
	expect_warning("nothing is connected to its area_entered or body_entered signal (on it or on BulletFactory2D)", "a spawner-owned hit nobody handles", "BulletSpawner2D '")
	expect_no_errors("the explanation is a warning, never a push_error")
	var heard_events: Array = []
	sp.body_entered.connect(func(_b, _v, _i): heard_events.append(1))
	assert_eq(heard_events, [], "the earlier hit was not replayed")
	assert_true(sp.shoot_once(), "shot again")
	for i in 60:
		await physics()
		if not heard_events.is_empty():
			break
	assert_eq(heard_events.size(), 1, "the next hit reaches the spawner's own handler")


## Two spawners, both with a hit handler and neither driving the factory's:
## only the shooter's volley is heard by its own spawner.
func test_two_watching_spawners_hear_only_their_own_volleys() -> void:
	make_wall(Vector2(120, 0))
	await physics()
	var row := {"mask": [3], "max_count": 1, "kind": "body", "layer": 4}
	var a := make_spawner(_hit_data(row), BulletSpawner2D.PATTERN_FROM_SELF, 1)
	var b := make_spawner(_hit_data(row), BulletSpawner2D.PATTERN_FROM_SELF, 1)
	a.connect(&"body_entered", _recorder("a", &"body_entered"))
	b.connect(&"body_entered", _recorder("b", &"body_entered"))
	assert_true(a.shoot_once(), "A shot")
	for i in 60:
		await physics()
		if _heard_total(&"body_entered") > 0:
			break
	assert_eq(heard("a", &"body_entered").size(), 1, "A heard its own volley's hit")
	assert_eq(heard("b", &"body_entered").size(), 0, "B's volley never hit anything")
	assert_false(factory.has_connections(&"body_entered"), "the factory has no handler")
	# A factory-spawned volley handed to B belongs to B.
	var v := factory.spawn_volley(_hit_data(row))
	assert_true(b.adopt_live_volley(v), "B adopted the script volley")
	assert_true(is_instance_valid(v) and v.is_volley_active(), "the adopted volley lives")
	assert_eq(int(v.debug_get_volley_info()["owner_spawner_id"]), int(b.get_instance_id()), "the volley is B's before it hits")
	for i in 60:
		await physics()
		if heard("b", &"body_entered").size() > 0:
			break
	assert_eq(heard("b", &"body_entered").size(), 1, "B heard the adopted volley's hit")
	assert_eq(heard("a", &"body_entered").size(), 1, "A still heard only its own")
