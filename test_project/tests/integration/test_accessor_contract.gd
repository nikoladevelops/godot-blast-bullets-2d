extends BlastTest
## Accessor contract, discovered from ClassDB so every future accessor is
## covered without touching this file. For every plugin class, every bound
## one-argument set_X with a get_X / is_X twin either ACCEPTS a changed value
## (the getter then returns exactly it) or REJECTS it with an error and keeps
## the old value. NaN and +/-INF are rejected by every float and vector
## setter (old value kept). Catches swapped binds, setters that silently ignore or clamp,
## getters reading the wrong field, and rejects that half-apply.

# Setters whose effect is structural, not a stored value (each listed with
# the reason it cannot round-trip through its getter in isolation).
const NOT_A_VALUE := {
	# Pauses/resumes the whole factory (pinned by the pause suites).
	"BulletFactory2D.set_is_factory_processing_bullets": true,
}

var probes: Array = [] # [label, object]


func before_each() -> void:
	await super()
	probes.clear()
	var volley_data := H.make_volley_data(3, 50.0, 30.0)
	volley_data.sprite_frames = H.make_effect_frames(4, 10.0)
	var v: BulletVolley2D = factory.spawn_volley(volley_data)
	probes.append(["BulletVolley2D", v])
	probes.append(["BulletFactory2D", factory])
	probes.append(["BulletSpawner2D", make_spawner(H.make_volley_data(2))])
	for cls in ["BulletVolleyData2D", "BulletSpeedData2D", "BulletRotationData2D", "BulletCurvesData2D", "BulletWobbleData2D", "BulletEffectLayerData2D", "BulletGrazeZone2D", "VolleyPoolKey2D"]:
		probes.append([cls, ClassDB.instantiate(cls)])
	var att: BulletAttachment2D = BulletAttachment2D.new()
	add(att)
	probes.append(["BulletAttachment2D", att])


## Every one-argument setter of the class itself with its getter twin.
func _pairs(cls: String) -> Array:
	var methods := {}
	for m in ClassDB.class_get_method_list(cls, true):
		methods[str(m["name"])] = m
	var out: Array = []
	for name in methods:
		if not name.begins_with("set_"):
			continue
		var m: Dictionary = methods[name]
		if (m["args"] as Array).size() != 1:
			continue
		var stem: String = name.substr(4)
		var getter := ""
		for g in ["get_" + stem, "is_" + stem, "get_is_" + stem]:
			if methods.has(g) and (methods[g]["args"] as Array).size() - (methods[g]["default_args"] as Array).size() == 0:
				getter = g
				break
		if getter != "":
			out.append([name, getter, int(m["args"][0]["type"])])
	return out


func _changed(v: Variant, arg_type: int) -> Variant:
	match arg_type:
		TYPE_BOOL:
			return not bool(v)
		TYPE_INT:
			return int(v) + 1
		TYPE_FLOAT:
			return float(v) * 1.25 + 0.5 if is_finite(float(v)) else 1.5
		TYPE_VECTOR2:
			return (v as Vector2) * 1.25 + Vector2(0.5, 0.25) if (v as Vector2).is_finite() else Vector2(1.5, 0.75)
		TYPE_COLOR:
			return Color(0.25, 0.5, 0.75, 0.5) if v != Color(0.25, 0.5, 0.75, 0.5) else Color(0.5, 0.25, 0.75, 1.0)
		TYPE_STRING:
			return str(v) + "x"
		TYPE_STRING_NAME:
			return StringName(str(v) + "x")
	return null


func _same(a: Variant, b: Variant) -> bool:
	if typeof(a) == TYPE_FLOAT and typeof(b) == TYPE_FLOAT:
		return is_equal_approx(a, b) or (is_nan(a) and is_nan(b))
	if typeof(a) == TYPE_VECTOR2 and typeof(b) == TYPE_VECTOR2:
		return (a as Vector2).is_equal_approx(b)
	if typeof(a) == TYPE_COLOR and typeof(b) == TYPE_COLOR:
		return (a as Color).is_equal_approx(b)
	return a == b


## Claims everything pushed since the last call; returns how many were real
## errors (a push_warning is a notice, e.g. "the spawner stays where it is"
## when movement turns on without a path, never a rejection).
func _claim_errors() -> int:
	var n := 0
	for err in get_errors():
		if not err.handled:
			err.handled = true
			if not err.is_push_warning():
				n += 1
	return n


func _try(obj: Object, setter: String, getter: String, value: Variant) -> Dictionary:
	var old: Variant = obj.call(getter)
	_claim_errors()
	obj.call(setter, value)
	var errors := _claim_errors()
	var now: Variant = obj.call(getter)
	obj.call(setter, old) # restore (a rejected restore is reported by the sweep's own value)
	_claim_errors()
	return {"old": old, "now": now, "errors": errors}


func test_every_setter_accepts_exactly_or_rejects_and_keeps() -> void:
	var problems: Array = []
	var checked := 0
	for probe in probes:
		var cls: String = probe[0]
		var obj: Object = probe[1]
		for pair in _pairs(cls):
			var setter: String = pair[0]
			var getter: String = pair[1]
			if NOT_A_VALUE.has(cls + "." + setter):
				continue
			var nv: Variant = _changed(obj.call(getter), pair[2])
			if nv == null:
				continue
			var r := _try(obj, setter, getter, nv)
			checked += 1
			if r["errors"] > 0:
				if not _same(r["now"], r["old"]):
					problems.append("%s.%s(%s) rejected but changed %s -> %s" % [cls, setter, nv, r["old"], r["now"]])
			elif not _same(r["now"], nv):
				problems.append("%s.%s(%s) accepted silently but %s reads %s" % [cls, setter, nv, getter, r["now"]])
	assert_gt(checked, 300, "the sweep reached the plugin's accessors (%d pairs)" % checked)
	assert_eq(problems, [], "every setter either round-trips or rejects loudly and keeps the old value")


func test_non_finite_values_are_rejected_by_every_float_and_vector_setter() -> void:
	var problems: Array = []
	var checked := 0
	for probe in probes:
		var cls: String = probe[0]
		var obj: Object = probe[1]
		for pair in _pairs(cls):
			var setter: String = pair[0]
			var getter: String = pair[1]
			if (pair[2] != TYPE_FLOAT and pair[2] != TYPE_VECTOR2) or NOT_A_VALUE.has(cls + "." + setter):
				continue
			for x in [NAN, INF, -INF]:
				var bad: Variant = x if pair[2] == TYPE_FLOAT else Vector2(x, 1.0)
				var r := _try(obj, setter, getter, bad)
				checked += 1
				if r["errors"] == 0:
					problems.append("%s.%s(%s) accepted silently (reads %s)" % [cls, setter, bad, r["now"]])
				elif not _same(r["now"], r["old"]):
					problems.append("%s.%s(%s) rejected but changed %s -> %s" % [cls, setter, bad, r["old"], r["now"]])
	assert_gt(checked, 300, "the sweep reached the float/vector setters (%d calls)" % checked)
	assert_eq(problems, [], "NaN and +/-INF never reach a stored value")


func test_every_bound_method_names_every_argument() -> void:
	# An argument bound without a name shows as "_unnamed_argN" in the docs
	# and the editor's autocompletion. Every plugin class, found by API type.
	var unnamed: Array = []
	var classes := 0
	for cls in ClassDB.get_class_list():
		if ClassDB.class_get_api_type(cls) != ClassDB.API_EXTENSION:
			continue
		classes += 1
		for m in ClassDB.class_get_method_list(cls, true):
			for a in m["args"]:
				var arg_name := str(a["name"])
				if arg_name.is_empty() or arg_name.begins_with("_unnamed_arg"):
					unnamed.append("%s.%s(%s)" % [cls, m["name"], arg_name])
	assert_gte(classes, 16, "every plugin class checked")
	assert_eq(unnamed, [], "every argument has a name")
