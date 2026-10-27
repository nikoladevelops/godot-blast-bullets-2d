extends BlastTest
## A finished laser waits in the pool: its handle is stale. EVERY write
## (every bound LaserVolley2D method that is not a read, from ClassDB, so new
## methods are covered) refuses it with one stale-handle error and changes
## nothing; the next life that reuses it starts exactly like a fresh laser.
## Reads return neutral values. A laser made with LaserVolley2D.new() and
## never fired refuses writes the same way, with its own message.

const DT := 0.0625
const EXEMPT := {"queue_free": "Node API", "free": "Node API"}

var _node: Node2D


func before_each() -> void:
	await super()
	_node = Node2D.new()
	_node.position = Vector2(40, 60)
	add(_node)


func _data() -> LaserData2D:
	return H.make_laser(0.0, 0.0, 0.0, 0.0, 200.0)


func _fire(data: LaserData2D) -> LaserVolley2D:
	var poses: Array[Transform2D] = [Transform2D(0.3, Vector2(10, 20)), Transform2D(-0.4, Vector2(10, 20))]
	return factory.spawn_laser(data, poses)


## Arguments for a write: valid values (a refusal must not depend on them).
func _args(m: Dictionary) -> Array:
	var out: Array = []
	for a in m.get("args", []):
		var n := String(a["name"])
		match int(a["type"]):
			TYPE_INT:
				out.append(0)
			TYPE_FLOAT:
				out.append(1.5)
			TYPE_BOOL:
				out.append(true)
			TYPE_VECTOR2:
				out.append(Vector2(10, 20))
			TYPE_STRING_NAME, TYPE_STRING:
				out.append(&"laser_handle_group")
			TYPE_ARRAY:
				if n == "zones":
					out.append([H.make_graze_zone([30.0])])
				elif n == "effects":
					out.append([H.make_laser_sound(LaserSoundData2D.LASER_SOUND_ON_HIT)])
				else:
					out.append([Vector2(100, 100)])
			TYPE_OBJECT:
				out.append(_node)
			_:
				out.append(Vector2(30, 40)) # Variant
	return out


func _writes() -> Array:
	var out: Array = []
	for m in ClassDB.class_get_method_list(&"LaserVolley2D", true):
		var name := String(m["name"])
		if name.begins_with("_") or name.begins_with("get_") or name.begins_with("is_") or name.begins_with("debug_") or EXEMPT.has(name):
			continue
		out.append(m)
	return out


## Everything a new life could inherit (the life id aside).
func _state(l: LaserVolley2D) -> String:
	var rows: Array = [l.get_beam_count(), l.get_phase(), l.get_anchor(), l.get_active_time_left(), l.is_graze_armed(), l.is_sound_armed(), l.get_graze_zones()]
	for i in l.get_beam_count():
		rows.append([l.get_beam_points(i), l.get_beam_angle(i), l.get_pierced_targets(i)])
	return str(rows)


func _stale(fn: String) -> String:
	return "%s: this laser finished (it waits in the pool), so this handle is stale. Fire a new laser instead." % fn


func test_the_sweep_finds_every_write() -> void:
	var names: Array = []
	for m in _writes():
		names.append(String(m["name"]))
	for expected in ["set_beam_angle", "set_beam_target", "set_targets", "set_anchor", "end_laser", "set_active_time_left", "graze_set_zones", "graze_clear", "sound_set_effects"]:
		assert_has(names, expected, "covers " + expected)


func test_every_write_on_a_pooled_laser_is_refused_once_and_never_reaches_the_next_life() -> void:
	var data := _data()
	var cold := _fire(data)
	step_factory(1, DT)
	var want := _state(cold)
	cold.end_laser(false)
	step_factory(1, DT)
	assert_true(cold.is_pooled(), "pooled")
	var problems: Array = []
	for m in _writes():
		var name := String(m["name"])
		cold.callv(name, _args(m))
		var texts: Array = []
		for err in get_errors():
			if not err.handled:
				texts.append(err.contains_text(_stale(name)))
				err.handled = true
		if texts != [true]:
			problems.append("%s: %d errors, expected the one stale-handle error" % [name, texts.size()])
		var again := _fire(data)
		if again != cold:
			problems.append("%s: the pool handed out another laser" % name)
			continue
		step_factory(1, DT)
		if _state(again) != want:
			problems.append("%s: the next life inherited it" % name)
		again.end_laser(false)
		step_factory(1, DT)
	assert_eq("\n".join(problems), "", "every write on a pooled laser is refused")


func test_reads_on_a_pooled_laser_are_neutral() -> void:
	var l := _fire(_data())
	l.graze_set_zones([H.make_graze_zone([30.0])], &"laser_handle_group")
	step_factory(1, DT)
	l.end_laser(false)
	step_factory(1, DT)
	assert_eq([l.get_beam_count(), l.is_laser_active(), l.get_phase(), l.get_anchor() == null, l.get_active_time_left(), l.get_laser_data() == null, l.get_owner_spawner() == null, l.is_graze_armed(), l.is_sound_armed(), l.get_graze_zones(), l.get_current_width()],
		[0, false, LaserVolley2D.PHASE_FINISHED, true, 0.0, true, true, false, false, [], 0.0], "nothing left of its life")
	assert_eq([l.debug_check_invariants(), l.debug_get_draw_state(1.0), (l.debug_get_body_mesh(1.0)["points"] as PackedVector2Array).size()], ["", [], 0], "clean, draws nothing")
	l.get_beam_start(0)
	expect_error_sequence(["Invalid beam index in get_beam_start: 0 (beam count: 0)"])


func test_a_laser_that_was_never_fired_refuses_writes() -> void:
	var l := LaserVolley2D.new()
	add(l)
	var problems: Array = []
	for m in _writes():
		var name := String(m["name"])
		l.callv(name, _args(m))
		var texts: Array = []
		for err in get_errors():
			if not err.handled:
				texts.append(err.contains_text("%s: this laser was never fired (BulletFactory2D.spawn_laser or a LaserSpawner2D fires lasers), nothing changed." % name))
				err.handled = true
		if texts != [true]:
			problems.append("%s: %s" % [name, texts])
	assert_eq("\n".join(problems), "", "one refusal per write")
	assert_eq([l.get_beam_count(), l.is_laser_active(), l.debug_check_invariants()], [0, false, ""], "still empty and consistent")
	await idle(1) # its draw runs with nothing to draw


func test_the_pool_drops_what_a_script_added_to_the_laser() -> void:
	var data := _data()
	var l := _fire(data)
	l.set_meta(&"damage", 5)
	l.add_to_group(&"enemy_lasers")
	l.modulate = Color(1, 0, 0)
	l.position = Vector2(30, 40)
	l.process_mode = Node.PROCESS_MODE_DISABLED
	l.top_level = true
	l.texture_filter = CanvasItem.TEXTURE_FILTER_NEAREST
	step_factory(1, DT)
	l.end_laser(false)
	step_factory(1, DT)
	var again := _fire(data)
	assert_eq(again, l, "the same laser from the pool")
	assert_eq([again.get_meta_list().size(), again.is_in_group(&"enemy_lasers"), again.modulate, again.position, again.process_mode, again.top_level, again.texture_filter],
		[0, false, Color.WHITE, Vector2.ZERO, Node.PROCESS_MODE_INHERIT, false, CanvasItem.TEXTURE_FILTER_PARENT_NODE], "a neutral node, like a fresh laser")
