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
	var lasers := LaserSpawner2D.new()
	lasers.set_shooting_enabled(false)
	lasers.laser_data = H.make_laser()
	add(lasers)
	lasers.set_bullet_factory(factory)
	probes.append(["LaserSpawner2D", lasers])
	probes.append(["LaserVolley2D", factory.spawn_laser(H.make_laser(), [Transform2D()])])
	for cls in ["BulletVolleyData2D", "BulletSpeedData2D", "BulletRotationData2D", "BulletCurvesData2D", "BulletWobbleData2D", "BulletEffectLayerData2D", "GrazeZone2D", "BulletFlock2D", "BulletPickupData2D", "BulletSoundData2D", "LaserSoundData2D", "LaserSpriteLook2D", "LaserEffectData2D", "VolleyPoolKey2D", "LaserData2D", "BulletEmitterData2D", "BulletSplitData2D", "BulletSplitStage2D"]:
		probes.append([cls, ClassDB.instantiate(cls)])
	var att: BulletAttachment2D = BulletAttachment2D.new()
	add(att)
	probes.append(["BulletAttachment2D", att])


## Every one-argument setter of the class itself with its getter twin.
func _pairs(cls: String) -> Array:
	var methods := {}
	var c := StringName(cls)
	while c != &"" and ClassDB.class_get_api_type(c) == ClassDB.API_EXTENSION: # the class and its plugin bases
		for m in ClassDB.class_get_method_list(c, true):
			methods[str(m["name"])] = m
		c = ClassDB.get_parent_class(c)
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


func test_every_enum_the_api_mentions_is_bound_with_its_constants() -> void:
	# A property, argument or return typed with an enum names it
	# "<Class>.<Enum>". Scripts can only use it (typed assignment, named
	# constants, autocompletion) when that class binds that enum with its
	# constants: a namespace-level C++ enum leaks as "BlastBullets2D.<Enum>",
	# which typed GDScript then refuses to assign from the class constants.
	var problems: Array = []
	var seen := 0
	for cls in ClassDB.get_class_list():
		if ClassDB.class_get_api_type(cls) != ClassDB.API_EXTENSION:
			continue
		var typed: Array = [] # [where, class_name]
		for p in ClassDB.class_get_property_list(cls, true):
			if int(p["usage"]) & PROPERTY_USAGE_CLASS_IS_ENUM:
				typed.append(["%s.%s" % [cls, p["name"]], str(p["class_name"])])
		for m in ClassDB.class_get_method_list(cls, true):
			var ret: Dictionary = m["return"]
			if int(ret["usage"]) & PROPERTY_USAGE_CLASS_IS_ENUM:
				typed.append(["%s.%s() return" % [cls, m["name"]], str(ret["class_name"])])
			for a in m["args"]:
				if int(a["usage"]) & PROPERTY_USAGE_CLASS_IS_ENUM:
					typed.append(["%s.%s(%s)" % [cls, m["name"], a["name"]], str(a["class_name"])])
		for entry in typed:
			seen += 1
			var parts: PackedStringArray = (entry[1] as String).split(".")
			if parts.size() != 2 or not ClassDB.class_exists(parts[0]):
				problems.append("%s: enum %s names no class" % entry)
			elif not ClassDB.class_has_enum(parts[0], parts[1], true) or ClassDB.class_get_enum_constants(parts[0], parts[1], true).is_empty():
				problems.append("%s: %s binds no such enum (or no constants)" % entry)
	assert_gt(seen, 50, "the sweep reached the enum-typed API (%d uses)" % seen)
	assert_eq("\n".join(problems), "", "every enum the API mentions is bound with its constants")


func test_enums_work_in_typed_scripts() -> void:
	# The enum fix above as users write it: typed variables, named constants.
	var curves := BulletCurvesData2D.new()
	curves.x_direction_curve_mode = BulletCurvesData2D.Override
	var mode: BulletCurvesData2D.DirectionCurveMode = curves.get_x_direction_curve_mode()
	assert_eq(mode, BulletCurvesData2D.Override, "a typed DirectionCurveMode round-trips")
	var v := quick_volley(2)
	assert_eq(v.bullet_homing_check_current_target_type(0), BulletVolley2D.NotHoming, "no target yet")
	v.bullet_homing_push_back_global_position_target(0, Vector2(100, 0))
	var kind: BulletVolley2D.HomingType = v.bullet_homing_check_current_target_type(0)
	assert_eq(kind, BulletVolley2D.GlobalPositionTarget, "a typed HomingType compares with the named constants")
	var scope: BulletFlock2D.Scope = BulletFlock2D.SCOPE_PER_VOLLEY
	assert_true(v.flock_join(BulletFlock2D.new(), scope), "typed Scope arguments")


func test_every_enum_property_is_typed_with_its_enum() -> void:
	# An int property that picks from a list (PROPERTY_HINT_ENUM) reads back
	# as its enum: typed GDScript, docs (enum=) and autocompletion then know
	# the named constants. Found by API type, so new properties are covered.
	var untyped: Array = []
	var seen := 0
	for cls in ClassDB.get_class_list():
		if ClassDB.class_get_api_type(cls) != ClassDB.API_EXTENSION:
			continue
		var returns := {}
		# Inherited too: a concrete spawner registers properties whose
		# accessors PatternSpawner2D binds.
		for m in ClassDB.class_get_method_list(cls, false):
			returns[str(m["name"])] = m["return"]
		for p in ClassDB.class_get_property_list(cls, true):
			if int(p["type"]) != TYPE_INT or int(p["hint"]) != PROPERTY_HINT_ENUM:
				continue
			seen += 1
			var getter := str(ClassDB.class_get_property_getter(cls, p["name"]))
			var ret: Dictionary = returns.get(getter, {})
			if ret.is_empty() or not (int(ret["usage"]) & PROPERTY_USAGE_CLASS_IS_ENUM):
				untyped.append("%s.%s" % [cls, p["name"]])
	assert_gte(seen, 30, "enum properties found")
	assert_eq("\n".join(untyped), "", "every enum property reads back as its enum")


## Every inspector text of every plugin class (property hint strings and
## names) reads as written: a UTF-8 character handed to godot-cpp's
## String(const char *) is decoded as Latin-1 and shows as mojibake ("Â²",
## "â€”"). Non-ASCII text must be built with String::utf8.
func test_inspector_texts_have_no_mojibake() -> void:
	var bad: Array = []
	for p in probes:
		for prop in (p[1] as Object).get_property_list():
			for field in ["name", "hint_string", "class_name"]:
				var text := str(prop[field])
				for marker in ["Â", "Ã", "â€"]:
					if marker in text:
						bad.append("%s.%s %s: %s" % [p[0], prop["name"], field, text])
	assert_eq(bad, [], "no Latin-1 decoded UTF-8 in any property text")
	var accel := ""
	for prop in BulletPickupData2D.new().get_property_list():
		if str(prop["name"]) == "magnet_acceleration":
			accel = str(prop["hint_string"])
	assert_eq(accel, "0,10000,1,or_greater,suffix:px/s²", "the acceleration suffix reads px/s²")


## No bound method of a plugin class takes a typed (non-Resource) object:
## godot-cpp converts such an argument through the object's raw pointer
## before the plugin can check it, so a freed node a script still holds was
## read after free (a crash). Object arguments are Variants read by id.
func test_no_bound_method_takes_a_typed_node_or_object() -> void:
	var typed: Array = []
	for cls in ["BulletFactory2D", "BulletVolley2D", "BulletSpawner2D", "LaserSpawner2D", "PatternSpawner2D", "LaserVolley2D", "BulletAttachment2D", "BulletPatterns2D"]:
		for m in ClassDB.class_get_method_list(cls, true):
			for a in m["args"]:
				if int(a["type"]) == TYPE_OBJECT:
					var cn := str(a["class_name"])
					if cn == "" or not ClassDB.is_parent_class(cn, "RefCounted"):
						typed.append("%s.%s(%s: %s)" % [cls, m["name"], a["name"], cn])
	assert_eq(typed, [], "object arguments are Variants")


## Every former node argument refuses a freed object with one error naming
## it, and changes nothing.
func test_a_freed_object_is_refused_by_every_object_argument() -> void:
	var gone := Node2D.new()
	add_child(gone)
	gone.free()
	var volley: BulletVolley2D = probes[0][1]
	var spawner: BulletSpawner2D = probes[2][1]
	var laser: LaserVolley2D = probes[4][1]
	var t: Array[Transform2D] = [Transform2D()]
	var calls := [
		[factory, "reactivate_volley", [gone], "volley"],
		[factory, "free_volley_deferred", [gone], "volley"],
		[factory, "debug_get_pool_bucket", [gone], "volley"],
		[factory, "spawn_laser", [H.make_laser(), t, gone], "anchor"],
		[volley, "set_bullet_direction_towards_node2d", [0, gone], "target_node"],
		[volley, "all_bullets_set_direction_towards_node2d", [gone], "target_node"],
		[volley, "set_bullet_texture_rotation_towards_node2d", [0, gone], "target_node"],
		[volley, "all_bullets_set_texture_rotation_towards_node2d", [gone], "target_node"],
		[volley, "bullet_homing_push_front_node2d_target", [0, gone], "new_homing_target"],
		[volley, "bullet_homing_push_back_node2d_target", [0, gone], "new_homing_target"],
		[volley, "shared_homing_deque_push_front_node2d_target", [gone], "new_homing_target"],
		[volley, "shared_homing_deque_push_back_node2d_target", [gone], "new_homing_target"],
		[volley, "set_bullet_movement_pattern_from_path", [0, gone], "path_holding_pattern"],
		[volley, "all_bullets_set_movement_pattern_from_path", [gone], "path_holding_pattern"],
		[volley, "emitter_set", [BulletEmitterData2D.new(), gone], "template_spawner"],
		[spawner, "adopt_live_volley", [gone], "volley"],
		[spawner, "set_bullet_factory", [gone], "factory"],
		[spawner, "set_transforms_generator", [gone], "generator"],
		[spawner, "set_helper_aimed_target", [gone], "target"],
		[spawner, "set_helper_path2d_node", [gone], "node"],
		[spawner, "set_movement_path_node", [gone], "node"],
		[laser, "set_anchor", [gone], "anchor"],
	]
	var problems: Array = []
	for c in calls:
		(c[0] as Object).callv(c[1], c[2])
		var hits := 0
		var others: Array = []
		for err in get_errors():
			if err.handled:
				continue
			err.handled = true
			if err.contains_text("%s: %s must be a " % [c[1], c[3]]) and err.contains_text("(a freed object is neither), nothing changed."):
				hits += 1
			else:
				others.append(err.code)
		if hits != 1 or not others.is_empty():
			problems.append("%s: %d refusals, other errors %s" % [c[1], hits, others])
	BulletFactory2D.debug_free_like_release(gone)
	var release_hits := 0
	for err in get_errors():
		if not err.handled:
			err.handled = true
			release_hits += 1 if err.contains_text("debug_free_like_release: object must be a Object or null") else 0
	if release_hits != 1:
		problems.append("debug_free_like_release: %d refusals" % release_hits)
	assert_eq(problems, [], "one refusal each, nothing else")
	assert_eq(spawner.get_bullet_factory(), factory, "the spawner kept its factory")
	assert_true(laser.is_laser_active(), "the laser fires on")
