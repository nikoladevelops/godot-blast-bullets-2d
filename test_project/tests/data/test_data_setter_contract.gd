extends BlastTest
## Reject-and-keep contract for every spawn-data resource, discovered from
## ClassDB so new properties are covered automatically (the spawner has its
## own suite, spawner/test_spawner_setter_contract.gd). For each class's own
## stored properties:
##   - FLOAT: NaN, +INF and -INF;
##   - VECTOR2 / COLOR: a NaN component;
##   - INT with an enum hint: ids just outside the hint;
##   - a range hint (min,max without or_greater / or_less): just outside it;
## must each push exactly ONE error, worded "<Class>: <property> ...,
## keeping the old value.", and leave the value unchanged.

const CLASSES := ["BulletVolleyData2D", "BulletSpeedData2D", "BulletRotationData2D", "BulletCurvesData2D", "BulletWobbleData2D", "BulletEffectLayerData2D", "BulletGrazeZone2D"]
const KEEP_TEXT := "keeping the old value."

## Properties whose range hint is an editor slider only (any value is valid).
const RANGE_IS_A_SLIDER := {
}


func _own_props(cls: String) -> Array:
	var out: Array = []
	for p in ClassDB.class_get_property_list(cls, true):
		var usage := int(p.get("usage", 0))
		if (usage & PROPERTY_USAGE_STORAGE) == 0:
			continue
		if (usage & (PROPERTY_USAGE_GROUP | PROPERTY_USAGE_SUBGROUP | PROPERTY_USAGE_CATEGORY)) != 0:
			continue
		out.append(p)
	return out


func _bad_values(p: Dictionary) -> Array:
	var name := str(p["name"])
	var hint := int(p.get("hint", 0))
	var hs := str(p.get("hint_string", ""))
	var out: Array = []
	match int(p["type"]):
		TYPE_FLOAT:
			out.append_array([NAN, INF, -INF])
		TYPE_VECTOR2:
			out.append(Vector2(NAN, 0.0))
		TYPE_COLOR:
			out.append(Color(NAN, 0.0, 0.0, 1.0))
	if int(p["type"]) == TYPE_INT and hint == PROPERTY_HINT_ENUM and hs != "":
		var ids: Array = []
		var next := 0
		for opt in hs.split(","):
			var parts := opt.split(":")
			var id := int(parts[1]) if parts.size() > 1 else next
			ids.append(id)
			next = id + 1
		out.append(int(ids.max()) + 1)
		out.append(int(ids.min()) - 1)
	if hint == PROPERTY_HINT_RANGE and not RANGE_IS_A_SLIDER.has(name) and (int(p["type"]) == TYPE_FLOAT or int(p["type"]) == TYPE_INT):
		var parts := hs.split(",")
		if parts.size() >= 2:
			var lo := float(parts[0])
			var hi := float(parts[1])
			var span := maxf(absf(hi - lo), 1.0)
			var step := 1.0 if int(p["type"]) == TYPE_INT else span * 0.25
			if not hs.contains("or_less"):
				out.append(lo - step if int(p["type"]) == TYPE_FLOAT else int(lo) - 1)
			if not hs.contains("or_greater"):
				out.append(hi + step if int(p["type"]) == TYPE_FLOAT else int(hi) + 1)
	return out


func _same(a: Variant, b: Variant) -> bool:
	if typeof(a) == TYPE_FLOAT and typeof(b) == TYPE_FLOAT:
		return a == b or (is_nan(a) and is_nan(b))
	return a == b


func test_every_data_setter_rejects_bad_values_loudly_and_keeps_the_old_one() -> void:
	var problems: Array = []
	var probed := 0
	for cls in CLASSES:
		var obj: Object = ClassDB.instantiate(cls)
		for p in _own_props(cls):
			var name := str(p["name"])
			for bad in _bad_values(p):
				var before: Variant = obj.get(name)
				expect_no_errors("clean before %s.%s" % [cls, name])
				obj.set(name, bad)
				var after: Variant = obj.get(name)
				probed += 1
				var texts: Array = []
				for err in get_errors():
					if not err.handled:
						err.handled = true
						texts.append(str(err.code))
				var label := "%s.%s = %s" % [cls, name, bad]
				if texts.size() != 1:
					problems.append("%s: expected one error, got %s" % [label, texts])
				elif not (texts[0] as String).begins_with("%s: %s " % [cls, name]) or not (texts[0] as String).ends_with(KEEP_TEXT):
					problems.append("%s: wording %s" % [label, texts[0]])
				if not _same(before, after):
					problems.append("%s: value changed %s -> %s" % [label, before, after])
	assert_gt(probed, 100, "the sweep reached the data classes (%d probes)" % probed)
	assert_eq("\n".join(problems), "", "every data setter rejects loudly with the standard wording and keeps the old value")
