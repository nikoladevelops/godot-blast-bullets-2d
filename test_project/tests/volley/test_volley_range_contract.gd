extends BlastTest
## Range contract for EVERY all_bullets_* method (found via get_method_list,
## so new range APIs are covered automatically): the default (0, -1) covers
## the whole volley, -1 as the end means "through the last bullet", and an
## out-of-range or inverted range fails loud and applies nothing.


func _range_methods(v: Object) -> Array:
	var out: Array = []
	for m in v.get_method_list():
		var name: String = m["name"]
		if not name.begins_with("all_bullets_"):
			continue
		var args: Array = m["args"]
		var n := args.size()
		if n >= 2 and str(args[n - 2]["name"]) == "bullet_index_start" and str(args[n - 1]["name"]) == "bullet_index_end_inclusive":
			out.append(m)
	return out


## Plausible value for a leading (non-range) argument.
func _arg_for(arg: Dictionary) -> Variant:
	match int(arg["type"]):
		TYPE_BOOL:
			return true
		TYPE_INT:
			return 0
		TYPE_FLOAT:
			return 1.0
		TYPE_VECTOR2:
			return Vector2(1, 0)
		TYPE_TRANSFORM2D:
			return Transform2D(0.0, Vector2(5, 5))
		TYPE_COLOR:
			return Color.RED
		TYPE_ARRAY:
			return [Vector2(1, 1)] # a real entry: an array applied by mistake shows
	return null


func test_every_range_method_rejects_out_of_range() -> void:
	var v: BulletVolley2D = quick_volley(4, 100.0, 30.0)
	var methods := _range_methods(v)
	assert_gt(methods.size(), 20, "found the all_bullets_* range APIs (%d)" % methods.size())
	var changed: Array = []
	for m in methods:
		var args: Array = m["args"]
		var call_args: Array = []
		for i in args.size() - 2:
			call_args.append(_arg_for(args[i]))
		for bad in [[99, 99], [-5, 2], [0, 99], [3, 1]]:
			var before := _fingerprint(v)
			v.callv(m["name"], call_args + bad)
			var hits := 0
			var others: Array = []
			for err in get_errors():
				if err.handled:
					continue
				err.handled = true
				if err.contains_text("Invalid index range in " + str(m["name"])):
					hits += 1
				else:
					others.append(err.code)
			assert_eq(hits, 1, "%s%s fails loud exactly once" % [m["name"], bad])
			assert_eq(others, [], "%s%s reports nothing else" % [m["name"], bad])
			if _fingerprint(v) != before:
				changed.append("%s%s" % [m["name"], bad])
	assert_eq(changed, [], "a rejected range applies nothing")


## Every per-bullet state a script can read, for every bullet (getters taking
## only a bullet index, plus the volley-wide range getters).
func _fingerprint(v: BulletVolley2D) -> String:
	var parts: Array = []
	for m in v.get_method_list():
		var n := str(m["name"])
		var args: Array = m["args"]
		if args.size() == 1 and str(args[0]["name"]) == "bullet_index" and (n.begins_with("get_") or n.begins_with("bullet_get_") or n.begins_with("has_") or n.begins_with("bullet_check_") or n.begins_with("bullet_homing_check_") or n.begins_with("is_bullet_")):
			for i in v.get_amount_bullets():
				parts.append([n, i, v.call(n, i)])
	parts.sort()
	for err in get_errors():
		err.handled = true # a getter's own refusal is not this contract
	return var_to_str(parts)


## Every all_bullets_* method names its range arguments the same way, so the
## sweep above sees all of them.
func test_every_range_method_names_its_range_arguments_alike() -> void:
	var v: BulletVolley2D = quick_volley(2, 100.0, 30.0)
	var odd: Array = []
	for m in v.get_method_list():
		var name: String = m["name"]
		if not name.begins_with("all_bullets_"):
			continue
		for a in m["args"]:
			var arg := str(a["name"])
			if int(a["type"]) == TYPE_INT and "index" in arg and ("start" in arg or "end" in arg) and arg not in ["bullet_index_start", "bullet_index_end_inclusive"]:
				odd.append("%s(%s)" % [name, arg])
	assert_eq(odd, [], "range arguments are bullet_index_start / bullet_index_end_inclusive")


## A rejected range never reaches the bullets through a nested call: the
## movement pattern stays on every bullet, the homing queues stay empty.
func test_a_rejected_range_never_applies_through_a_nested_call() -> void:
	var v: BulletVolley2D = quick_volley(3, 100.0, 30.0)
	var curve := Curve2D.new()
	curve.add_point(Vector2.ZERO)
	curve.add_point(Vector2(50, 0))
	v.all_bullets_set_movement_pattern_from_curve(curve)
	v.all_bullets_set_movement_pattern_from_curve(null, false, true, 99, 99)
	expect_error_sequence(["Invalid index range in all_bullets_set_movement_pattern_from_curve (99..99 outside 0..2; use -1 as the end for \"through the last bullet\"). Nothing was applied."])
	v.all_bullets_set_movement_pattern_from_path(null, false, true, 5, 1)
	expect_error_sequence(["Invalid index range in all_bullets_set_movement_pattern_from_path (5..1 outside 0..2; use -1 as the end for \"through the last bullet\"). Nothing was applied."])
	for i in 3:
		assert_true(v.has_bullet_movement_pattern(i), "bullet %d keeps its movement pattern" % i)
	v.all_bullets_push_back_homing_targets_array([Vector2(5, 5)], 99, 99)
	expect_error_sequence(["Invalid index range in all_bullets_push_back_homing_targets_array (99..99 outside 0..2; use -1 as the end for \"through the last bullet\"). Nothing was applied."])
	v.all_bullets_replace_homing_targets_with_new_target_array([Vector2(5, 5)], 3, 1)
	expect_error_sequence(["Invalid index range in all_bullets_replace_homing_targets_with_new_target_array (3..1 outside 0..2; use -1 as the end for \"through the last bullet\"). Nothing was applied."])
	v.all_bullets_assign_homing_targets_array([Vector2(5, 5)], 99, 99)
	expect_error_sequence(["Invalid index range in all_bullets_assign_homing_targets_array (99..99 outside 0..2; use -1 as the end for \"through the last bullet\"). Nothing was applied."])
	assert_eq(Array(v.all_bullets_get_homing_targets_amount()), [0, 0, 0], "no bullet took a target")


func test_defaults_cover_the_whole_volley() -> void:
	var v: BulletVolley2D = quick_volley(5, 100.0, 30.0)
	v.all_bullets_set_direction(Vector2(0, 1))
	for i in 5:
		assert_eq(v.get_bullet_direction(i), Vector2(0, 1), "default range covers bullet %d" % i)
	v.all_bullets_set_direction(Vector2(-1, 0), 3, -1)
	assert_eq(v.get_bullet_direction(2), Vector2(0, 1), "start 3 leaves bullet 2")
	assert_eq(v.get_bullet_direction(4), Vector2(-1, 0), "end -1 reaches the last bullet")


func swallow_rejections() -> void:
	for err in get_errors():
		if not err.handled and not err.contains_text("Invalid index range"):
			err.handled = true
