extends Node
## Behavior snapshot for refactors (driven by tools/api_snapshot.py):
##   godot --headless --fixed-fps 60 --path test_project res://snapshot/api_snapshot.tscn \
##         -- --out=/abs/snapshot.json
## Dumps what the plugin does as short hashes, so a pure refactor can prove it
## changed NOTHING observable: the ClassDB surface of every plugin class, every
## static BulletPatterns2D helper over defaults + perturbed arguments, every
## spawner pattern source (transforms + preview) under every visible knob, setter
## outcomes for hostile values, and deterministic volley flight traces. Every
## entry also records the errors/warnings pushed while it ran (exact text).

const H := preload("res://tests/common/blast_test_helpers.gd")

const PLUGIN_CLASSES: Array[StringName] = [
	&"BulletRotationData2D", &"BulletEffectLayerData2D", &"BulletSpeedData2D", &"BulletCurvesData2D",
	&"BulletWobbleData2D", &"VolleyPoolKey2D", &"BulletAttachment2D", &"BulletPatterns2D",
	&"BulletFactory2D", &"BulletVolleyDebugger2D", &"BulletVolleyData2D", &"BulletVolley2D",
	&"BulletSpawner2D", &"PatternPreviewLayer2D", &"BulletGrazeZone2D", &"GrazePreviewLayer2D",
]
const AMOUNTS := [-1, 0, 1, 2, 7, 24, 10001]
const TRACE_STEP := 1.0 / 60.0


## Records every error/warning text (no file/line: those move with refactors).
class Capture extends Logger:
	var mutex := Mutex.new()
	var lines := PackedStringArray()

	func _log_error(_function: String, _file: String, _line: int, code: String, rationale: String,
			_editor_notify: bool, error_type: int, _script_backtraces: Array[ScriptBacktrace]) -> void:
		mutex.lock()
		lines.append("%d:%s%s" % [error_type, code, ("|" + rationale) if rationale != "" else ""])
		mutex.unlock()

	func take() -> PackedStringArray:
		mutex.lock()
		var out := lines
		lines = PackedStringArray()
		mutex.unlock()
		return out


var cap := Capture.new()
var out := {}
## Errors of a double evaluation's first pass, re-queued for the next _rec.
var push_warning_text := PackedStringArray()
var markers: Array = [Transform2D.IDENTITY, Transform2D(0.4, Vector2(1.25, 0.8), 0.0, Vector2(30, -12))]


func _ready() -> void:
	var out_path := ""
	var only := ""
	for arg in OS.get_cmdline_user_args():
		if arg.begins_with("--out="):
			out_path = arg.substr(6)
		elif arg.begins_with("--only="):
			only = arg.substr(7)
	if out_path == "":
		push_error("api_snapshot: pass -- --out=/abs/path.json")
		get_tree().quit(2)
		return
	OS.add_logger(cap)
	var sections := {
		"surface": _snap_surface,
		"patterns": _snap_patterns,
		"generate": _snap_generate,
		"layouts": _snap_layouts,
		"spawner": _snap_spawner,
		"setters": _snap_setters,
		"traces": _snap_traces,
		"volley_api": _snap_volley_api,
	}
	for name in sections:
		if only != "" and not (name in only.split(",")):
			continue
		out[name] = {}
		cap.take()
		await sections[name].call(name)
	OS.remove_logger(cap)
	var f := FileAccess.open(out_path, FileAccess.WRITE)
	if f == null:
		push_error("api_snapshot: cannot write " + out_path)
		get_tree().quit(3)
		return
	f.store_string(JSON.stringify(out, " ", true))
	f.close()
	await get_tree().process_frame
	get_tree().quit(0)


# ---- recording ---------------------------------------------------------------

func _hash(v: Variant) -> String:
	var ctx := HashingContext.new()
	ctx.start(HashingContext.HASH_SHA256)
	ctx.update(var_to_bytes(v))
	return ctx.finish().hex_encode().substr(0, 16)


func _sample(v: Variant) -> String:
	var s := ""
	if v is Array or (typeof(v) >= TYPE_PACKED_BYTE_ARRAY and typeof(v) <= TYPE_PACKED_VECTOR4_ARRAY):
		var parts := PackedStringArray()
		for i in mini(v.size(), 2):
			parts.append(var_to_str(v[i]))
		s = "n=%d %s" % [v.size(), ", ".join(parts)]
	else:
		s = var_to_str(v)
	return s.substr(0, 200)


func _rec(section: String, key: String, value: Variant, errors: PackedStringArray = PackedStringArray()) -> void:
	var errs := errors.duplicate()
	errs.append_array(push_warning_text)
	push_warning_text = PackedStringArray()
	errs.append_array(cap.take())
	var k := key
	var dup := 2
	while out[section].has(k):
		k = "%s#%d" % [key, dup]
		dup += 1
	out[section][k] = {"h": _hash(value), "s": _sample(value), "e": Array(errs)}


# ---- 1. ClassDB surface ------------------------------------------------------

func _prop_info(p: Dictionary) -> String:
	return "%s:%s:%s:%s:%s:%s" % [p.get("name"), type_string(p.get("type", 0)), p.get("class_name", ""),
			p.get("hint", 0), p.get("hint_string", ""), p.get("usage", 0)]


func _snap_surface(section: String) -> void:
	for cls in PLUGIN_CLASSES:
		var methods := {}
		for m in ClassDB.class_get_method_list(cls, true):
			var args := PackedStringArray()
			for a in m.get("args", []):
				args.append(_prop_info(a))
			var defs := PackedStringArray()
			for d in m.get("default_args", []):
				defs.append("%s=%s" % [type_string(typeof(d)), var_to_str(d)])
			methods[m["name"]] = {"args": args, "defaults": defs, "flags": m.get("flags", 0),
					"return": _prop_info(m.get("return", {}))}
		var names := methods.keys()
		names.sort()
		for n in names:
			_rec(section, "%s.method.%s" % [cls, n], methods[n])
		var props := PackedStringArray()
		for p in ClassDB.class_get_property_list(cls, true):
			props.append(_prop_info(p))
		_rec(section, "%s.properties" % cls, props)
		var sigs := PackedStringArray()
		for s in ClassDB.class_get_signal_list(cls, true):
			var args := PackedStringArray()
			for a in s.get("args", []):
				args.append(_prop_info(a))
			sigs.append("%s(%s)" % [s["name"], ", ".join(args)])
		sigs.sort()
		_rec(section, "%s.signals" % cls, sigs)
		var consts := PackedStringArray()
		for c in ClassDB.class_get_integer_constant_list(cls, true):
			consts.append("%s=%d enum=%s" % [c, ClassDB.class_get_integer_constant(cls, c),
					ClassDB.class_get_integer_constant_enum(cls, c, true)])
		_rec(section, "%s.constants" % cls, consts)
		_rec(section, "%s.parent" % cls, ClassDB.get_parent_class(cls))


# ---- 2. Static BulletPatterns2D helpers ---------------------------------------

func _ring_volley(n: int) -> Array:
	var arr: Array = []
	for i in n:
		var a := TAU * i / n
		arr.append(Transform2D(a, Vector2(cos(a), sin(a)) * 90.0))
	return arr


func _candidates(cls: StringName, arg: Dictionary) -> Array:
	var t: int = arg.get("type", TYPE_NIL)
	var cn: String = arg.get("class_name", "")
	var n: String = arg.get("name", "")
	if t == TYPE_INT and cn.contains("."):
		var parts := cn.split(".")
		var vals := []
		for c in ClassDB.class_get_enum_constants(parts[0], parts[1], true):
			vals.append(ClassDB.class_get_integer_constant(parts[0], c))
		return vals
	match t:
		TYPE_INT:
			if n in ["transforms_amount", "amount", "count"]:
				return [7, 1, 24]
			return [1, 2, 3, 0, 5, -1]
		TYPE_FLOAT:
			return [37.5, 0.6, 2.5, 120.0, -15.0, 0.0]
		TYPE_BOOL:
			return [true, false]
		TYPE_VECTOR2:
			return [Vector2(120, 40), Vector2(0, 1), Vector2(-60, 25)]
		TYPE_TRANSFORM2D:
			return markers
		TYPE_PACKED_VECTOR2_ARRAY:
			return [PackedVector2Array([Vector2(0, 0), Vector2(100, 0), Vector2(100, 80), Vector2(30, 120)]),
					PackedVector2Array([Vector2(0, 0), Vector2(200, 10)])]
		TYPE_PACKED_FLOAT32_ARRAY:
			return [PackedFloat32Array([1.0, 1.4, 2.0]), PackedFloat32Array()]
		TYPE_PACKED_INT32_ARRAY:
			return [PackedInt32Array([1, 3]), PackedInt32Array()]
		TYPE_DICTIONARY:
			return [{}, {"radius": 90.0, "count": 9}]
		TYPE_ARRAY:
			return [_ring_volley(7), []]
	return [null]


## Evaluates `f` twice under the same global seed; a result that re-rolls
## (unseeded RNG path) is recorded by size only. The first pass's errors are
## queued for the next _rec.
func _stable(f: Callable) -> Variant:
	seed(12345)
	var a: Variant = f.call()
	push_warning_text.append_array(cap.take())
	seed(12345)
	var b: Variant = f.call()
	cap.take()
	if _hash(a) != _hash(b):
		return "nondet n=%d" % (a.size() if (a is Array or a is Dictionary) else -1)
	return a


## Calls a static method twice under the same global seed: equal results are
## hashed, differing ones (unseeded RNG paths) only record their size.
func _call_static_twice(cls: StringName, m: StringName, args: Array) -> Array:
	seed(12345)
	var a: Variant = ClassDB.class_call_static.callv([cls, m] + args)
	var errs := cap.take()
	seed(12345)
	var b: Variant = ClassDB.class_call_static.callv([cls, m] + args)
	cap.take()
	if _hash(a) != _hash(b):
		var n: int = a.size() if (a is Array or a is Dictionary) else -1
		return ["nondet n=%d" % n, errs]
	return [a, errs]


func _snap_static_method(section: String, cls: StringName, info: Dictionary) -> void:
	var name: StringName = info["name"]
	var args: Array = info.get("args", [])
	var defs: Array = info.get("default_args", [])
	var nreq := args.size() - defs.size()
	var base := []
	var amount_i := -1
	var marker_i := -1
	for i in args.size():
		base.append(_candidates(cls, args[i])[0] if i < nreq else defs[i - nreq])
		if args[i]["name"] in ["transforms_amount", "amount", "count"]:
			amount_i = i
		if args[i]["type"] == TYPE_TRANSFORM2D and marker_i < 0:
			marker_i = i
	# Defaults across amounts and markers.
	for amount in (AMOUNTS if amount_i >= 0 else [null]):
		for mi in (markers.size() if marker_i >= 0 else 1):
			var call := base.duplicate()
			if amount_i >= 0:
				call[amount_i] = amount
			if marker_i >= 0:
				call[marker_i] = markers[mi]
			var r := _call_static_twice(cls, name, call)
			_rec(section, "%s#base a=%s m=%d" % [name, amount, mi], r[0], r[1])
	# One argument at a time (amount 7, rotated/scaled marker).
	var mid := base.duplicate()
	if amount_i >= 0:
		mid[amount_i] = 7
	if marker_i >= 0:
		mid[marker_i] = markers[1]
	for i in args.size():
		if i == amount_i or i == marker_i:
			continue
		for c in _candidates(cls, args[i]).slice(0, 4):
			if typeof(c) == typeof(mid[i]) and c == mid[i]:
				continue
			var call := mid.duplicate()
			call[i] = c
			var r := _call_static_twice(cls, name, call)
			_rec(section, "%s#%s=%s" % [name, args[i]["name"], var_to_str(c).substr(0, 60)], r[0], r[1])
	# Seeded combinations.
	var rng := RandomNumberGenerator.new()
	rng.seed = hash(String(name))
	for k in 6:
		var call := mid.duplicate()
		for i in args.size():
			if i == amount_i or i == marker_i or rng.randf() > 0.4:
				continue
			var cands := _candidates(cls, args[i])
			call[i] = cands[rng.randi_range(0, cands.size() - 1)]
		var r := _call_static_twice(cls, name, call)
		_rec(section, "%s#combo%d" % [name, k], r[0], r[1])


func _snap_patterns(section: String) -> void:
	var methods := ClassDB.class_get_method_list(&"BulletPatterns2D", true)
	methods.sort_custom(func(a, b): return String(a["name"]) < String(b["name"]))
	for info in methods:
		if String(info["name"]) in ["get_shapes", "generate"]:
			continue
		if (int(info.get("flags", 0)) & METHOD_FLAG_STATIC) == 0:
			continue
		_snap_static_method(section, &"BulletPatterns2D", info)
		await get_tree().process_frame
	_rec(section, "get_shapes", BulletPatterns2D.get_shapes())


# ---- 3. BulletPatterns2D.generate --------------------------------------------

func _snap_generate(section: String) -> void:
	var probe := BulletSpawner2D.new()
	var plist := probe.get_property_list()
	for shape in BulletPatterns2D.get_shapes():
		var id: int = shape["id"]
		for amount in AMOUNTS:
			for mi in markers.size():
				var m: Transform2D = markers[mi]
				_rec(section, "%d#a=%d m=%d" % [id, amount, mi],
						_stable(func() -> Variant: return BulletPatterns2D.generate(id, amount, m, {})))
		# Every knob of the shape (and the shared outline knobs) by short name.
		var prefixes: Array = [String(shape["knob_prefix"])] if String(shape["knob_prefix"]) != "" else []
		if shape["outline"]:
			prefixes.append("helper_outline_")
		for p in plist:
			var pname: String = p["name"]
			var hit := ""
			for pre in prefixes:
				if pname.begins_with(pre):
					hit = pre
			if hit == "" or (int(p.get("usage", 0)) & PROPERTY_USAGE_STORAGE) == 0:
				continue
			for v in _knob_values(p, probe.get(pname)):
				var params := {pname.trim_prefix("helper_"): v}
				_rec(section, "%d#%s=%s" % [id, pname.trim_prefix("helper_"), var_to_str(v).substr(0, 60)],
						_stable(func() -> Variant: return BulletPatterns2D.generate(id, 7, markers[1], params)))
	probe.free()
	seed(12345)
	_rec(section, "bad_shape", BulletPatterns2D.generate(99, 7, markers[0], {}))
	_rec(section, "bad_key", BulletPatterns2D.generate(0, 7, markers[0], {"radiuss": 1.0}))


# ---- 3b. Outline layouts: seeded knob combinations per loop shape ------------

## Layers (multi-ring, both layouts, every fill/side, twist, reverse, offset,
## caps, custom scales) and Fill Inside combinations for every outline shape:
## the one-knob sweeps never combine them.
func _snap_layouts(section: String) -> void:
	for shape in BulletPatterns2D.get_shapes():
		if not shape["outline"]:
			continue
		var id: int = shape["id"]
		var rng := RandomNumberGenerator.new()
		rng.seed = 7919 + id
		for k in 48:
			var params := {
				"outline_placement": 1,
				"outline_layer_count": [2, 3, 5][rng.randi_range(0, 2)],
				"outline_layer_layout": rng.randi_range(0, 1),
				"outline_layer_fill": rng.randi_range(0, 3),
				"outline_layer_side": rng.randi_range(0, 2),
				"outline_layer_scale": [0.2, 0.35, 0.6][rng.randi_range(0, 2)],
				"outline_layer_scale_curve": rng.randi_range(0, 1),
				"outline_layer_twist": [0, 1, -3][rng.randi_range(0, 2)],
				"outline_layer_max_dots": [0, 0, 3][rng.randi_range(0, 2)],
				"outline_layer_start_offset": [0, 0, 1][rng.randi_range(0, 2)],
				"outline_reverse": rng.randi_range(0, 1) == 1,
				"outline_slot_offset": [0, 2, -5][rng.randi_range(0, 2)],
				"outline_facing": rng.randi_range(0, 2),
			}
			if k % 6 == 5:
				params["outline_layer_scales"] = PackedFloat32Array([1.0, 1.5, 2.2, 0.6])
			if shape["corners"]:
				params["outline_distribution"] = rng.randi_range(0, 1)
				params["outline_corner_mode"] = rng.randi_range(0, 1)
				params["outline_corner_priority"] = rng.randi_range(0, 2)
				params["outline_corner_facing"] = rng.randi_range(0, 2)
				params["outline_edge_margin"] = [0.0, 6.0][rng.randi_range(0, 1)]
			var amount: int = [7, 12, 24, 31][rng.randi_range(0, 3)]
			var m: Transform2D = markers[k % 2]
			_rec(section, "%d#layers%d" % [id, k],
					_stable(func() -> Variant: return BulletPatterns2D.generate(id, amount, m, params)))
		for k in 8:
			var params := {
				"outline_placement": 2,
				"outline_fill_spacing": [12.0, 24.0, 40.0][rng.randi_range(0, 2)],
				"outline_fill_stagger": rng.randi_range(0, 1) == 1,
				"outline_fill_margin": [0.0, 4.0][rng.randi_range(0, 1)],
				"outline_facing": rng.randi_range(0, 2),
			}
			var amount: int = [9, 20][k % 2]
			var m: Transform2D = markers[k % 2]
			_rec(section, "%d#fill%d" % [id, k],
					_stable(func() -> Variant: return BulletPatterns2D.generate(id, amount, m, params)))


# ---- 4. Spawner: every source x every visible knob ---------------------------

func _knob_values(p: Dictionary, current: Variant) -> Array:
	var t: int = p["type"]
	if int(p.get("hint", 0)) == PROPERTY_HINT_ENUM and t == TYPE_INT:
		var vals := []
		var idx := 0
		for part in String(p.get("hint_string", "")).split(","):
			var colon := part.rfind(":")
			vals.append(int(part.substr(colon + 1)) if colon >= 0 else idx)
			idx += 1
		return vals
	match t:
		TYPE_INT:
			return [current + 1, current + 3, 0]
		TYPE_FLOAT:
			return [current * 1.5 + 7.0, current * 0.5 - 3.0]
		TYPE_BOOL:
			return [not current]
		TYPE_VECTOR2:
			return [current * 1.3 + Vector2(5, -3), Vector2(0, 1)]
		TYPE_PACKED_INT32_ARRAY:
			return [PackedInt32Array([0, 2])]
		TYPE_PACKED_FLOAT32_ARRAY:
			return [PackedFloat32Array([1.0, 1.5])]
	return []


func _spawner_state_once(sp: BulletSpawner2D) -> Array:
	seed(12345)
	var shots: Variant = sp.collect_spawn_transforms()
	return [shots, sp.debug_get_preview_dot_points(), sp.debug_get_preview_track_points(),
			sp.debug_get_preview_track_closed(), sp.debug_get_layer_rings()]


## Spawn transforms + preview geometry; sources with unseeded randomness
## (scatter, seed-0 jitter) re-roll per call, so they record sizes only.
func _spawner_state(sp: BulletSpawner2D) -> Array:
	var a := _spawner_state_once(sp)
	var errs := cap.take()
	var b := _spawner_state_once(sp)
	cap.take()
	for e in errs:
		push_warning_text.append(e)
	if _hash(a) != _hash(b):
		return ["nondet", a[0].size(), a[1].size(), a[2].size(), a[3], a[4].size()]
	return a


func _make_snapshot_spawner() -> BulletSpawner2D:
	var holder := Node2D.new()
	holder.position = Vector2(400, 300)
	holder.rotation = 0.35
	add_child(holder)
	var target := Node2D.new()
	target.position = Vector2(700, 420)
	holder.add_child(target)
	var path := Path2D.new()
	path.curve = Curve2D.new()
	for pt in [Vector2(0, 0), Vector2(120, -40), Vector2(260, 30), Vector2(340, 160)]:
		path.curve.add_point(pt)
	path.position = Vector2(-80, 60)
	holder.add_child(path)
	var sp := BulletSpawner2D.new()
	sp.set_shooting_enabled(false)
	sp.set_homing_enabled(false)
	sp.set_spawn_data(H.make_volley_data(4))
	sp.show_pattern_preview = true
	sp.show_preview_during_runtime = true
	sp.position = Vector2(25, -10)
	sp.rotation = 0.2
	for pos in [Vector2(40, 0), Vector2(-20, 35), Vector2(5, -60)]:
		var child := Node2D.new()
		child.position = pos
		sp.add_child(child)
	holder.add_child(sp)
	sp.helper_aimed_target = sp.get_path_to(target)
	sp.helper_path2d_path = sp.get_path_to(path)
	sp.helper_custom_transforms = [Transform2D(0.0, Vector2(10, 0)), Transform2D(1.0, Vector2(-30, 20)),
			Transform2D(2.5, Vector2(0, 45))]
	return sp


func _snap_spawner(section: String) -> void:
	var sp := _make_snapshot_spawner()
	await get_tree().process_frame
	await get_tree().process_frame
	cap.take()
	var sources: int = BulletPatterns2D.get_shapes().size()
	for src in sources:
		sp.pattern_source = src
		for amount in [1, 7, 24]:
			sp.helper_bullets_amount = amount
			_rec(section, "src%d#a=%d" % [src, amount], _spawner_state(sp))
		sp.helper_bullets_amount = 7
		for p in sp.get_property_list():
			var pname: String = p["name"]
			if (int(p.get("usage", 0)) & PROPERTY_USAGE_EDITOR) == 0:
				continue
			if not (pname.begins_with("helper_") or pname in ["pattern_scale", "transforms_scale",
					"spawn_position_offset", "spawn_position_offset_space"]):
				continue
			if pname == "helper_bullets_amount":
				continue
			var old: Variant = sp.get(pname)
			for v in _knob_values(p, old):
				sp.set(pname, v)
				_rec(section, "src%d#%s=%s" % [src, pname, var_to_str(v).substr(0, 60)],
						[sp.get(pname), _spawner_state(sp)])
				sp.set(pname, old)
				cap.take()
		await get_tree().process_frame
	# Presets on top of a fresh source each.
	var presets := ClassDB.class_get_enum_constants(&"BulletPatterns2D", &"PatternPreset", true)
	for c in presets:
		var id := ClassDB.class_get_integer_constant(&"BulletPatterns2D", c)
		if id < 0:
			continue
		sp.pattern_source = 1
		sp.apply_pattern_preset(id)
		var knobs := {}
		for p in sp.get_property_list():
			if String(p["name"]).begins_with("helper_") or String(p["name"]).begins_with("spin_"):
				knobs[p["name"]] = sp.get(p["name"])
		_rec(section, "preset#%s" % c, [knobs, _spawner_state(sp)])
	sp.get_parent().queue_free()
	await get_tree().process_frame


# ---- 5. Setter outcomes -------------------------------------------------------

func _probes(t: int) -> Array:
	match t:
		TYPE_FLOAT:
			return [NAN, INF, -INF, -1.0, 0.0, 0.5, 3.0, 1e9]
		TYPE_INT:
			return [-1, 0, 1, 3, 999999]
		TYPE_BOOL:
			return [true, false]
		TYPE_VECTOR2:
			return [Vector2(NAN, 0), Vector2(INF, 1), Vector2(0, 0), Vector2(3, -4)]
	return []


func _probe_setters(section: String, label: String, obj: Object) -> void:
	for p in obj.get_property_list():
		var usage := int(p.get("usage", 0))
		if (usage & PROPERTY_USAGE_EDITOR) == 0 or (usage & (PROPERTY_USAGE_GROUP | PROPERTY_USAGE_SUBGROUP | PROPERTY_USAGE_CATEGORY)) != 0:
			continue
		var pname: String = p["name"]
		if pname in ["shooting_enabled", "process_mode", "visible", "physics_interpolation_mode"]:
			continue
		var old: Variant = obj.get(pname)
		for v in _probes(int(p["type"])):
			obj.set(pname, v)
			_rec(section, "%s.%s=%s" % [label, pname, var_to_str(v)], var_to_str(obj.get(pname)))
			obj.set(pname, old)
			cap.take()


func _snap_setters(section: String) -> void:
	for cls in [&"BulletVolleyData2D", &"BulletSpeedData2D", &"BulletRotationData2D", &"BulletCurvesData2D",
			&"BulletWobbleData2D", &"BulletEffectLayerData2D", &"BulletGrazeZone2D"]:
		_probe_setters(section, cls, ClassDB.instantiate(cls))
	var sp := BulletSpawner2D.new()
	sp.set_shooting_enabled(false)
	_probe_setters(section, "BulletSpawner2D", sp)
	sp.free()
	var factory := BulletFactory2D.new()
	add_child(factory)
	await get_tree().process_frame
	await get_tree().process_frame
	cap.take()
	var v: BulletVolley2D = factory.spawn_volley(H.make_volley_data(4))
	_probe_setters(section, "BulletVolley2D", v)
	factory.reset()
	factory.queue_free()
	await get_tree().process_frame


# ---- 6. Deterministic volley flight traces -------------------------------------

func _volley_state(v: BulletVolley2D) -> Array:
	var rows := []
	for i in v.get_amount_bullets():
		rows.append([v.get_bullet_transform(i), v.get_bullet_velocity(i), v.get_bullet_direction(i),
				v.is_bullet_status_enabled(i)])
	return rows


func _trace(section: String, factory: BulletFactory2D, label: String, data: BulletVolleyData2D,
		setup: Callable = Callable(), steps: int = 90) -> void:
	seed(4242)
	var v: BulletVolley2D = factory.spawn_volley(data)
	if v == null:
		_rec(section, label + "#spawn", null)
		return
	if setup.is_valid():
		setup.call(v)
	_rec(section, label + "#t0", _volley_state(v))
	for step in steps:
		factory.debug_advance_time(TRACE_STEP)
		if step % 15 == 14:
			_rec(section, "%s#t%d" % [label, step + 1], _volley_state(v))
	v.clear_all_bullets()
	cap.take()


## A flight with graze zones armed: every graze event (kind, target name,
## bullet, zone index, ring) and each bullet's grazed rings per zone.
func _graze_trace(section: String, factory: BulletFactory2D, label: String, data: BulletVolleyData2D,
		zones: Array, group: StringName, setup: Callable = Callable(), steps: int = 120) -> void:
	seed(4242)
	var log: Array = []
	var on_enter := func(t: Node2D, _v: BulletVolley2D, i: int, z: BulletGrazeZone2D, r: int) -> void:
		log.append(["enter", str(t.name), i, zones.find(z), r])
	var on_exit := func(t: Node2D, _v: BulletVolley2D, i: int, z: BulletGrazeZone2D, r: int) -> void:
		log.append(["exit", str(t.name), i, zones.find(z), r])
	factory.bullet_grazed.connect(on_enter)
	factory.bullet_graze_exited.connect(on_exit)
	var v: BulletVolley2D = factory.spawn_volley(data)
	if setup.is_valid():
		setup.call(v)
	v.graze_set_zones(zones, group)
	for step in steps:
		factory.debug_advance_time(TRACE_STEP)
	_rec(section, label + "#events", log)
	var rings := []
	for i in v.get_amount_bullets():
		var row := []
		for z in zones.size():
			row.append(v.get_bullet_grazed_rings(i, z))
		rings.append(row)
	_rec(section, label + "#rings", rings)
	factory.bullet_grazed.disconnect(on_enter)
	factory.bullet_graze_exited.disconnect(on_exit)
	v.clear_all_bullets()
	cap.take()


func _graze_zone(radii: Array, regraze: int, bullet_size: bool) -> BulletGrazeZone2D:
	var z := BulletGrazeZone2D.new()
	z.ring_count = radii.size()
	for i in radii.size():
		z.set_ring_radius(i, radii[i])
	z.regraze = regraze
	z.count_bullet_size = bullet_size
	return z


func _curve(points: Array) -> Curve:
	var c := Curve.new()
	c.min_value = -2000.0
	c.max_value = 2000.0
	for pt in points:
		c.add_point(pt)
	return c


func _snap_traces(section: String) -> void:
	var factory := BulletFactory2D.new()
	add_child(factory)
	var target_a := Node2D.new()
	target_a.position = Vector2(400, 200)
	add_child(target_a)
	var target_b := Node2D.new()
	target_b.position = Vector2(-150, 260)
	add_child(target_b)
	var path := Path2D.new()
	path.curve = Curve2D.new()
	for pt in [Vector2(0, 0), Vector2(40, 30), Vector2(80, -30), Vector2(120, 0)]:
		path.curve.add_point(pt)
	add_child(path)
	await get_tree().process_frame
	await get_tree().process_frame
	cap.take()

	_trace(section, factory, "plain", H.make_volley_data(6, 220.0))
	var accel := H.make_volley_data(6, 50.0)
	var speeds: Array = []
	for i in 6:
		speeds.append(H.make_speed(50.0 + 10 * i, 300.0, 400.0))
	accel.all_bullet_speed_data = speeds
	_trace(section, factory, "accel", accel)
	var rot := H.make_volley_data(6, 150.0)
	rot.shared_bullet_rotation_data = H.make_rotation(3.0, 8.0, 2.0)
	_trace(section, factory, "rotation", rot)
	var adjust := H.make_volley_data(6, 150.0)
	adjust.shared_bullet_rotation_data = H.make_rotation(2.0, 8.0, 1.0)
	adjust.adjust_direction_based_on_rotation = true
	_trace(section, factory, "rotation_adjust", adjust)

	var curves := BulletCurvesData2D.new()
	curves.movement_speed_curve = _curve([Vector2(0, 100), Vector2(1, 400)])
	curves.rotation_speed_curve = _curve([Vector2(0, 0), Vector2(1, 5)])
	curves.x_direction_curve = _curve([Vector2(0, 0.0), Vector2(1, 1.0)])
	curves.y_direction_curve = _curve([Vector2(0, 0.5), Vector2(1, -0.5)])
	curves.rotate_towards_adjusted_direction = true
	curves.direction_curve_rotation_speed = 6.0
	var shared_curves := H.make_volley_data(6, 150.0)
	shared_curves.shared_bullet_curves_data = curves
	_trace(section, factory, "curves_shared", shared_curves)
	var per_curves := H.make_volley_data(6, 150.0)
	per_curves.all_bullet_curves_data = [curves, null, curves]
	_trace(section, factory, "curves_per_bullet", per_curves)

	for mode in [0, 1]:
		var wob := BulletWobbleData2D.new()
		wob.enabled = true
		wob.mode = mode
		wob.amplitude = 30.0
		wob.frequency_hz = 2.0
		wob.phase_step_per_bullet = 0.5
		wob.face_movement_direction = true
		wob.damping_per_sec = 0.3
		var wd := H.make_volley_data(6, 180.0)
		wd.shared_bullet_wobble_data = wob
		_trace(section, factory, "wobble_mode%d" % mode, wd)

	var grav := H.make_volley_data(6, 200.0)
	grav.gravity = Vector2(0, 300)
	_trace(section, factory, "gravity_shared", grav)
	var grav_per := H.make_volley_data(6, 200.0)
	grav_per.all_bullet_gravity = [Vector2(0, 400), Vector2(100, 0), Vector2(0, 0)]
	_trace(section, factory, "gravity_per_bullet", grav_per)
	var drag := H.make_volley_data(6, 300.0)
	drag.gravity = Vector2(0, 200)
	drag.linear_drag = 0.8
	_trace(section, factory, "gravity_drag", drag)

	var homing := H.make_volley_data(6, 180.0)
	homing.homing_smoothing = 4.0
	homing.homing_take_control_of_texture_rotation = true
	homing.bullet_homing_auto_pop_after_target_reached = true
	homing.shared_homing_deque_auto_pop_after_target_reached = true
	homing.homing_distance_before_reached = 30.0
	_trace(section, factory, "homing_per_bullet", homing.duplicate(), func(v: BulletVolley2D) -> void:
		v.all_bullets_push_back_homing_target(target_a)
		v.bullet_homing_push_back_global_position_target(0, Vector2(50, -200)))
	_trace(section, factory, "homing_shared", homing.duplicate(), func(v: BulletVolley2D) -> void:
		v.shared_homing_deque_push_back_homing_targets_array([target_b, Vector2(300, -100)]))
	_trace(section, factory, "homing_mixed", homing.duplicate(), func(v: BulletVolley2D) -> void:
		v.shared_homing_deque_push_back_homing_targets_array([target_b])
		v.bullet_homing_push_back_node2d_target(0, target_a)
		v.bullet_homing_push_back_global_position_target(1, Vector2(20, 40)), 150)

	var orbit := H.make_volley_data(6, 160.0)
	orbit.homing_smoothing = 6.0
	orbit.homing_take_control_of_texture_rotation = true
	for dir in [0, 1, 2]:
		_trace(section, factory, "orbit_dir%d" % dir, orbit.duplicate(), func(v: BulletVolley2D) -> void:
			v.all_bullets_push_back_homing_target(target_a)
			for i in v.get_amount_bullets():
				v.bullet_enable_orbiting(i, 60.0 + 5 * i, dir, i % 4, i % 3, 10.0, i % 3, i % 2 == 0), 150)

	var pattern := H.make_volley_data(6, 150.0)
	_trace(section, factory, "pattern_curve", pattern, func(v: BulletVolley2D) -> void:
		v.all_bullets_set_movement_pattern_from_curve(path.curve, true, true))
	_trace(section, factory, "inherited", H.make_volley_data(6, 150.0), func(v: BulletVolley2D) -> void:
		v.set_inherited_velocity_offset(Vector2(60, -20)))
	# Pool reuse: a fully featured life, expired, then a plain respawn.
	_trace(section, factory, "reuse_life1", homing.duplicate(), func(v: BulletVolley2D) -> void:
		v.all_bullets_push_back_homing_target(target_a), 30)
	_trace(section, factory, "reuse_life2", H.make_volley_data(6, 220.0))

	# Graze: a homing volley weaving through the rings of both targets: a
	# tight Once zone and a wide After Exit zone over the same targets, and
	# a fast line.
	target_a.name = "SnapGrazeA"
	target_b.name = "SnapGrazeB"
	target_a.add_to_group(&"snap_graze")
	target_b.add_to_group(&"snap_graze")
	_graze_trace(section, factory, "graze_homing", homing.duplicate(),
			[_graze_zone([80.0, 30.0, 10.0], BulletGrazeZone2D.REGRAZE_ONCE, true),
			_graze_zone([420.0], BulletGrazeZone2D.REGRAZE_AFTER_EXIT, false)], &"snap_graze",
			func(v: BulletVolley2D) -> void: v.all_bullets_push_back_homing_target(target_a))
	_graze_trace(section, factory, "graze_line", H.make_volley_data(6, 2400.0),
			[_graze_zone([205.0, 201.0], BulletGrazeZone2D.REGRAZE_ONCE, false)], &"snap_graze")
	# Many targets (the slab path past 8): a fast line through a field of 20.
	var field: Array = []
	for i in 20:
		var t := Node2D.new()
		t.name = "SnapField%d" % i
		t.position = Vector2(60 * i - 600, 10 * ((i * 7) % 5) - 20)
		t.add_to_group(&"snap_graze_field")
		add_child(t)
		field.append(t)
	_graze_trace(section, factory, "graze_field", H.make_volley_data(6, 2400.0),
			[_graze_zone([24.0, 12.0], BulletGrazeZone2D.REGRAZE_AFTER_EXIT, true)], &"snap_graze_field")
	for t in field:
		(t as Node).free()
	factory.reset()
	factory.queue_free()
	await get_tree().process_frame


# ---- 7. Every bound volley method x three argument sets ---------------------

## Everything a volley exposes per bullet (no instance ids: those vary).
func _volley_full_state(v: BulletVolley2D) -> Array:
	var rows := []
	for i in v.get_amount_bullets():
		rows.append([v.get_bullet_transform(i), v.debug_get_bullet_info(i), v.debug_get_orbiting_info(i),
				v.debug_get_bounce_info(i), v.debug_get_wobble_info(i), v.debug_get_curves_info(i)])
	rows.append(v.debug_get_clocks())
	return rows


## Objects become their class name (instance ids differ per run).
func _plain(x: Variant) -> Variant:
	if x is Object:
		return (x as Object).get_class() if is_instance_valid(x) else "<freed>"
	if x is Array:
		var a := []
		for e in x:
			a.append(_plain(e))
		return a
	if x is Dictionary:
		var d := {}
		for k in x:
			d[k] = _plain(x[k])
		return d
	return x


var _api_calls: Array = []
var _api_target: Node2D
var _api_path: Path2D
var _api_scene: PackedScene


func _api_object(cls: String, variant: int) -> Variant:
	if variant == 2:
		return null
	match cls:
		"BulletSpeedData2D":
			return H.make_speed(250.0 if variant == 0 else 40.0, 900.0, 50.0)
		"BulletRotationData2D":
			return H.make_rotation(2.0 if variant == 0 else -1.0, 8.0, 1.0)
		"BulletCurvesData2D":
			var c := BulletCurvesData2D.new()
			c.movement_speed_curve = _curve([Vector2(0, 100), Vector2(1, 300)])
			if variant == 0:
				c.x_direction_curve = _curve([Vector2(0, 0.0), Vector2(1, 1.0)])
			return c
		"BulletWobbleData2D":
			var w := BulletWobbleData2D.new()
			w.enabled = true
			w.mode = variant
			w.amplitude = 25.0
			w.frequency_hz = 2.0
			return w
		"Curve2D":
			return _api_path.curve
		"PackedScene":
			return _api_scene
		"Shape2D", "CircleShape2D":
			return H.make_circle_shape(5.0 + variant)
		"SpriteFrames":
			return H.make_sprite_frames()
		"Node2D", "Node", "Object", "CanvasItem":
			return _api_target
		"Resource":
			return H.make_speed(1.0, 2.0, 3.0)
	return null


func _api_arg(a: Dictionary, variant: int) -> Variant:
	var t := int(a.get("type", TYPE_NIL))
	var cn := String(a.get("class_name", ""))
	var n := String(a.get("name", ""))
	match t:
		TYPE_INT:
			if n == "bullet_index":
				return [1, 3, 99][variant]
			if n.ends_with("_start") or n == "start_index":
				return [0, 1, 3][variant]
			if n.ends_with("_end_inclusive") or n == "end_index_inclusive":
				return [-1, 2, 1][variant]
			if cn.contains("."):
				var parts := cn.split(".")
				var vals := []
				for c in ClassDB.class_get_enum_constants(parts[0], parts[1], true):
					vals.append(ClassDB.class_get_integer_constant(parts[0], c))
				return [vals[mini(1, vals.size() - 1)], vals[0], 99][variant]
			return [1, 3, -5][variant]
		TYPE_FLOAT:
			return [37.5, 0.25, NAN][variant]
		TYPE_BOOL:
			return [true, false, true][variant]
		TYPE_VECTOR2:
			return [Vector2(120, -40), Vector2(0, 1), Vector2(NAN, 0)][variant]
		TYPE_TRANSFORM2D:
			return [Transform2D(0.3, Vector2(50, 20)), Transform2D(), Transform2D(0.0, Vector2(INF, 0))][variant]
		TYPE_COLOR:
			return [Color(0.5, 0.2, 0.9), Color(1, 1, 1, 0.5), Color(NAN, 0, 0)][variant]
		TYPE_STRING, TYPE_STRING_NAME:
			return ["default", "", "missing"][variant]
		TYPE_NODE_PATH:
			return [get_path_to(_api_path), NodePath(), NodePath("nope")][variant]
		TYPE_DICTIONARY:
			return [{}, {"k": 1}, {}][variant]
		TYPE_CALLABLE:
			return [func(...args) -> void: _api_calls.append(["cb", args.size()]), Callable(), Callable()][variant]
		TYPE_PACKED_INT32_ARRAY:
			return [PackedInt32Array([0, 2]), PackedInt32Array(), PackedInt32Array([99, -1])][variant]
		TYPE_PACKED_FLOAT32_ARRAY:
			return [PackedFloat32Array([1.0, 1.5]), PackedFloat32Array(), PackedFloat32Array([NAN])][variant]
		TYPE_PACKED_VECTOR2_ARRAY:
			return [PackedVector2Array([Vector2(0, 0), Vector2(50, 10)]), PackedVector2Array(), PackedVector2Array([Vector2(NAN, 0)])][variant]
		TYPE_OBJECT:
			return _api_object(cn, variant)
		TYPE_NIL: # Variant arguments (node2d_or_global_position, custom data...)
			if n.contains("position") or n.contains("node2d") or n.contains("target"):
				return [_api_target, Vector2(200, 100), "bad"][variant]
			return [H.make_speed(1.0, 2.0, 3.0), 1.5, null][variant]
		TYPE_ARRAY:
			var hs := String(a.get("hint_string", ""))
			if hs != "" and ClassDB.class_exists(hs):
				return [[_api_object(hs, 0), _api_object(hs, 1)], [], [null]][variant]
			if n.contains("homing") or n.contains("node2d"):
				return [[_api_target, Vector2(200, 100)], [], [null, 5]][variant]
			if hs == "Transform2D" or n.contains("transform"):
				return [[Transform2D(0.0, Vector2(10, 0))], [], [null]][variant]
			if hs == "Vector2":
				return [[Vector2(0, 300), Vector2(40, 0)], [], [Vector2(NAN, 0)]][variant]
			return [[1.0, true], [], [null]][variant]
	return null


func _api_rich(v: BulletVolley2D) -> void:
	v.all_bullets_set_gravity(Vector2(0, 120))
	v.bullet_homing_push_back_node2d_target(0, _api_target)
	v.shared_homing_deque_push_back_global_position_target(Vector2(-200, 50))
	v.bullet_enable_orbiting(1, 40.0, BulletVolley2D.OrbitRight, BulletVolley2D.FaceTarget)
	v.all_bullets_set_wobble_data(_api_object("BulletWobbleData2D", 0))


func _snap_volley_api(section: String) -> void:
	var factory := BulletFactory2D.new()
	add_child(factory)
	_api_target = Node2D.new()
	_api_target.position = Vector2(300, 140)
	add_child(_api_target)
	_api_path = Path2D.new()
	_api_path.curve = Curve2D.new()
	for pt in [Vector2(0, 0), Vector2(40, 30), Vector2(80, -30), Vector2(120, 0)]:
		_api_path.curve.add_point(pt)
	add_child(_api_path)
	var probe: Node = load("res://tests/scenes/attachment_probe.gd").new()
	_api_scene = PackedScene.new()
	_api_scene.pack(probe)
	probe.free()
	await get_tree().process_frame
	await get_tree().process_frame
	cap.take()
	var methods := ClassDB.class_get_method_list(&"BulletVolley2D", true)
	methods.sort_custom(func(a, b): return String(a["name"]) < String(b["name"]))
	for m in methods:
		var name := String(m["name"])
		if name.begins_with("_"):
			continue
		for base in ["plain", "rich"]:
			for variant in 3:
				seed(4242)
				_api_calls.clear()
				var v: BulletVolley2D = factory.spawn_volley(H.make_volley_data(4, 150.0, 30.0))
				if base == "rich":
					_api_rich(v)
				factory.debug_advance_time(TRACE_STEP)
				cap.take()
				var args := []
				for a in m.get("args", []):
					args.append(_api_arg(a, variant))
				var ret: Variant = v.callv(name, args)
				var errs := cap.take()
				var after := []
				if is_instance_valid(v):
					after.append(_volley_full_state(v))
					factory.debug_advance_time(TRACE_STEP)
					if is_instance_valid(v):
						after.append(_volley_full_state(v))
				_rec(section, "%s#%s%d" % [name, base, variant], [_plain(ret), after, _api_calls.duplicate()], errs)
				if is_instance_valid(v) and not v.is_queued_for_deletion():
					v.clear_all_bullets()
				cap.take()
		await get_tree().process_frame
	factory.reset()
	factory.queue_free()
	await get_tree().process_frame
