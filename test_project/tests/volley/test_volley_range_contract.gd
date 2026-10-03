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
	return null


func test_every_range_method_rejects_out_of_range() -> void:
	var v: BulletVolley2D = quick_volley(4, 100.0, 30.0)
	var methods := _range_methods(v)
	assert_gt(methods.size(), 20, "found the all_bullets_* range APIs (%d)" % methods.size())
	for m in methods:
		var args: Array = m["args"]
		var call_args: Array = []
		for i in args.size() - 2:
			call_args.append(_arg_for(args[i]))
		for bad in [[99, 99], [-5, 2], [0, 99], [3, 1]]:
			v.callv(m["name"], call_args + bad)
			var hits := 0
			for err in get_errors():
				if not err.handled and err.contains_text("Invalid index range in " + str(m["name"])):
					err.handled = true
					hits += 1
			assert_eq(hits, 1, "%s%s fails loud exactly once" % [m["name"], bad])
		# Anything else the hostile leading args provoked (null resources...)
		# is not this test's contract.
		swallow_rejections()


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
