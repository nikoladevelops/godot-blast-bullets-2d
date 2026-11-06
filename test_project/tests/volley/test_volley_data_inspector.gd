extends BlastTest
## Inspector contract of the merged classes, checked generically by walking
## get_property_list() so every future property is covered automatically:
## every property BulletVolleyData2D / BulletVolley2D add sits under a group
## (nothing floats above the first group), names are unique (the two merged
## classes never collide), every group title appears once, and
## calculate_bitmask keeps its layer contract.


## Properties the class itself adds (its own inspector category), each with
## the group it renders under.
func _own_properties(obj: Object) -> Array:
	var out: Array = []
	var own_category := obj.get_class()
	var in_own := false
	var group := ""
	for p in obj.get_property_list():
		var usage: int = int(p.get("usage", 0))
		var node_name := str(p.get("name", ""))
		if (usage & PROPERTY_USAGE_CATEGORY) != 0:
			in_own = node_name == own_category
			continue
		if not in_own:
			continue
		if (usage & PROPERTY_USAGE_GROUP) != 0:
			group = node_name
			continue
		if (usage & PROPERTY_USAGE_SUBGROUP) != 0:
			continue
		if (usage & PROPERTY_USAGE_EDITOR) == 0:
			continue
		out.append({"name": node_name, "group": group})
	return out


func _groups(obj: Object) -> Array:
	var out: Array = []
	var own_category := obj.get_class()
	var in_own := false
	for p in obj.get_property_list():
		var usage: int = int(p.get("usage", 0))
		var node_name := str(p.get("name", ""))
		if (usage & PROPERTY_USAGE_CATEGORY) != 0:
			in_own = node_name == own_category
		elif in_own and (usage & PROPERTY_USAGE_GROUP) != 0:
			out.append(node_name)
	return out


func _check(obj: Object, what: String) -> void:
	var props := _own_properties(obj)
	assert_gt(props.size(), 20, what + " exposes its properties")
	var ungrouped: Array = []
	var seen := {}
	var dups: Array = []
	for p in props:
		if str(p["group"]) == "":
			ungrouped.append(p["name"])
		if seen.has(p["name"]):
			dups.append(p["name"])
		seen[p["name"]] = true
	assert_eq(ungrouped, [], what + ": every property sits under a group")
	assert_eq(dups, [], what + ": property names are unique")
	var groups := _groups(obj)
	var group_dups: Array = []
	var gseen := {}
	for g in groups:
		if gseen.has(g):
			group_dups.append(g)
		gseen[g] = true
	assert_eq(group_dups, [], what + ": every group title appears once")


func test_spawn_data_inspector_is_fully_grouped() -> void:
	_check(BulletVolleyData2D.new(), "BulletVolleyData2D")


func test_live_volley_inspector_is_fully_grouped() -> void:
	var v: BulletVolley2D = quick_volley(2)
	_check(v, "BulletVolley2D")
	# The dedup knob used to float ungrouped above every group.
	var group_of := {}
	for p in _own_properties(v):
		group_of[p["name"]] = p["group"]
	assert_eq(str(group_of.get("collision_dedup_by_object", "")), "Collision", "dedup knob lives with Collision")


func test_calculate_bitmask_keeps_the_layer_contract() -> void:
	assert_eq(BulletVolleyData2D.calculate_bitmask([1, 3]), 5, "layers 1 and 3 -> bits 0 and 2")
	assert_eq(BulletVolleyData2D.calculate_bitmask([]), 0, "no layers -> 0")
	assert_eq(BulletVolleyData2D.calculate_bitmask([2, 2]), 2, "duplicates are idempotent")
	assert_eq(BulletVolleyData2D.calculate_bitmask([1, 0, 33]), 1, "out-of-range layers are skipped")
	expect_error_sequence([
		"Invalid layer number 0 in calculate_bitmask. Valid range is 1..32. Ignoring.",
		"Invalid layer number 33 in calculate_bitmask. Valid range is 1..32. Ignoring.",
	])
	# GDScript ints are 64-bit: a number past the int32 range is out of
	# range too, never wrapped onto a real layer (2^32 + 1 is not layer 1).
	assert_eq(BulletVolleyData2D.calculate_bitmask([4294967297, -4294967294]), 0, "huge layers are skipped")
	expect_error_sequence([
		"Invalid layer number 4294967297 in calculate_bitmask. Valid range is 1..32. Ignoring.",
		"Invalid layer number -4294967294 in calculate_bitmask. Valid range is 1..32. Ignoring.",
	])


## The light mask and visibility layer have array setters too (scripts that
## build the layer list at runtime): same helper, same bitmask in the property.
func test_light_and_visibility_array_setters_use_the_bitmask_helper() -> void:
	var d := BulletVolleyData2D.new()
	d.set_light_mask_from_array([1, 32])
	assert_eq(d.light_mask, BulletVolleyData2D.calculate_bitmask([1, 32]), "light mask")
	d.set_visibility_layer_from_array([2, 3])
	assert_eq(d.visibility_layer, BulletVolleyData2D.calculate_bitmask([2, 3]), "visibility layer")
	d.set_light_mask_from_array([1, 0])
	expect_error_sequence(["Invalid layer number 0 in calculate_bitmask. Valid range is 1..32. Ignoring."])
	assert_eq(d.light_mask, 1, "the valid entry still lands")
