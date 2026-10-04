extends BlastTest
## Pool neutrality: a volley that lived with non-default runtime settings,
## went back to the pool and was popped again must read exactly like a cold
## volley built from the same plain data. Runtime-only knobs (ones that spawn
## data never seeds) are the classic leak: they survive the reset unless the
## new life resets them explicitly.

const N := 3


func _plain() -> BulletVolleyData2D:
	return H.make_volley_data(N, 120.0, 5.0)


## Drains every bullet so the volley goes back to the pool, then spawns the
## same key again and returns the reused instance (asserted to be a pool hit).
func _drain_and_reuse(v: BulletVolley2D) -> BulletVolley2D:
	for i in v.get_amount_bullets():
		v.disable_bullet(i)
	await idle(1)
	var reused: BulletVolley2D = factory.spawn_volley(_plain())
	assert_same(reused, v, "same key reuses the pooled instance")
	return reused


func test_collision_dedup_mode_is_reset_for_the_next_life() -> void:
	var v: BulletVolley2D = factory.spawn_volley(_plain())
	v.set_collision_dedup_by_object(false)
	var reused := await _drain_and_reuse(v)
	assert_true(reused.get_collision_dedup_by_object(), "dedup mode is per life: the next owner starts object-level (default)")


## Properties the sweep leaves alone: engine bookkeeping the plugin owns
## (interpolation mode, multimesh, texture), lifecycle flags with their own
## suite (auto pooling: a pooling-off volley parks instead of pooling), and
## Node identity knobs that never affect bullets.
const SWEEP_SKIP := [
	"script", "multimesh", "texture", "visible", "physics_interpolation_mode",
	"process_thread_group", "auto_translate_mode", "unique_name_in_owner",
	"editor_description", "is_auto_pooling_enabled", "is_attachments_auto_pooling_enabled",
	"effect_layers", "bullets_current_collision_count",
]


## A value every setter accepts and that differs from `current`.
func _mutated(p: Dictionary, current: Variant) -> Variant:
	match int(p.type):
		TYPE_BOOL:
			return not bool(current)
		TYPE_INT:
			return int(current) + 1
		TYPE_FLOAT:
			return 0.37 if not is_equal_approx(float(current), 0.37) else 0.21
		TYPE_VECTOR2:
			return Vector2(3, 4)
		TYPE_COLOR:
			return Color(0.5, 0.4, 0.3, 0.9)
		TYPE_OBJECT:
			# Resource hints may list several classes ("A,B"): take the first.
			var cls: String = String(p.hint_string).get_slice(",", 0)
			if cls != "" and ClassDB.class_exists(cls) and ClassDB.can_instantiate(cls):
				return ClassDB.instantiate(cls)
	return null


func test_every_volley_property_returns_to_its_cold_value_after_reuse() -> void:
	var v: BulletVolley2D = factory.spawn_volley(_plain())
	var cold := {}
	var mutated: Array = []
	for p in v.get_property_list():
		var usage := int(p.usage)
		if (usage & (PROPERTY_USAGE_STORAGE | PROPERTY_USAGE_EDITOR)) == 0 or String(p.name) in SWEEP_SKIP:
			continue
		var current: Variant = v.get(p.name)
		var value: Variant = _mutated(p, current)
		if value == null:
			continue
		cold[p.name] = current
		v.set(p.name, value)
		mutated.append(p.name)
	expect_no_errors("every mutation was accepted")
	assert_gt(mutated.size(), 40, "the sweep really mutated the volley (%d properties)" % mutated.size())
	var reused := await _drain_and_reuse(v)
	var leaks: Array = []
	for name in mutated:
		var now: Variant = reused.get(name)
		var was: Variant = cold[name]
		var same: bool = (now == was) if typeof(was) != TYPE_FLOAT else is_equal_approx(float(now), float(was))
		if not same:
			leaks.append("%s: %s (cold %s)" % [name, str(now), str(was)])
	assert_eq(" | ".join(leaks), "", "no runtime setting of the previous life leaks into the reused volley")


func test_node_decorations_are_cleared_on_reuse() -> void:
	var v: BulletVolley2D = factory.spawn_volley(_plain())
	v.add_to_group("enemy_bullets")
	v.set_meta("owner_tag", 42)
	var reused := await _drain_and_reuse(v)
	assert_false(reused.is_in_group("enemy_bullets"), "user groups are dropped")
	assert_false(reused.has_meta("owner_tag"), "metadata is dropped")
	var internal_groups := 0
	for g in reused.get_groups():
		if String(g).begins_with("_"):
			internal_groups += 1
	assert_gt(internal_groups, 0, "engine-internal groups are kept")
