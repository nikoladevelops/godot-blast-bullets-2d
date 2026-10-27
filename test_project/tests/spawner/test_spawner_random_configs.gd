extends BlastTest
## Random VALID configurations: every inspector property of BulletSpawner2D,
## BulletVolleyData2D, BulletFlock2D and GrazeZone2D set to a random
## value inside its own inspector hint (enums, ranges, layers, bools,
## colors), then shots, auto-fire and real frames against a wall, an area
## and grazing targets. A value the inspector offers is never rejected, no
## combination of them makes the engine complain, makes a bullet non-finite
## or unbalances the factory's books. The only texts allowed are the
## documented configuration warnings a random setup earns (empty sources,
## unhandled signals, mismatched masks, orphan reminders).

const SEEDS := 40
# Documented warnings a random configuration may legitimately earn.
const KNOWN := [
	"orbiting_enabled needs homing_enabled",
	"but nothing is connected",
	"collect_spawn_transforms:",
	"shoot_once: pattern_source",
	"resolve_homing_targets:",
	"bounce_mask has bits outside collision_mask",
	"after that spawner was freed",
	"movement_path is not a Path2D",
]
const SKIP := ["shooting_enabled", "editor_description", "process_mode", "process_priority",
	"process_physics_priority", "process_thread_group", "physics_interpolation_mode",
	"auto_translate_mode", "visible", "top_level", "show_behind_parent", "clip_children",
	"light_mask", "visibility_layer", "z_index", "z_as_relative", "y_sort_enabled",
	"texture_filter", "texture_repeat", "use_parent_material", "unique_name_in_owner"]

var rng := RandomNumberGenerator.new()
var problems: Array = []
var volleys: Array = []


func _ids(hs: String) -> Array:
	var ids: Array = []
	var next := 0
	for opt in hs.split(","):
		var parts := opt.split(":")
		var id := int(parts[1]) if parts.size() > 1 else next
		ids.append(id)
		next = id + 1
	return ids


## A random value inside the property's own inspector hint, or null.
func _value(p: Dictionary) -> Variant:
	var hint := int(p["hint"])
	var hs := str(p["hint_string"])
	match int(p["type"]):
		TYPE_BOOL:
			return rng.randf() < 0.5
		TYPE_INT:
			if hint == PROPERTY_HINT_ENUM:
				var ids := _ids(hs)
				return ids[rng.randi() % ids.size()]
			if hint == PROPERTY_HINT_RANGE:
				var parts := hs.split(",")
				var lo := int(float(parts[0]))
				return rng.randi_range(lo, mini(int(float(parts[1])), lo + 64)) # amounts kept small
			if hint == PROPERTY_HINT_LAYERS_2D_PHYSICS:
				return rng.randi() & 15
		TYPE_FLOAT:
			if hint == PROPERTY_HINT_RANGE:
				var parts := hs.split(",")
				var lo := float(parts[0])
				return rng.randf_range(lo, minf(float(parts[1]), lo + 500.0))
		TYPE_COLOR:
			return Color(rng.randf(), rng.randf(), rng.randf(), rng.randf())
	return null


func _known(text: String) -> bool:
	for k in KNOWN:
		if text.contains(k):
			return true
	return false


## Claims every pending error; anything not a documented warning is a problem.
func _audit(where: String) -> void:
	for err in get_errors():
		if err.handled:
			continue
		err.handled = true
		if not _known(str(err.code)):
			problems.append("%s: %s" % [where, err.code])


func _randomize(obj: Object, where: String) -> void:
	for p in obj.get_property_list():
		var usage := int(p["usage"])
		var name := str(p["name"])
		if (usage & PROPERTY_USAGE_STORAGE) == 0 or (usage & PROPERTY_USAGE_EDITOR) == 0 or name in SKIP:
			continue
		var v: Variant = _value(p)
		if v == null:
			continue
		obj.set(name, v)
		# A value the inspector offers must be accepted (a warning about a
		# missing companion node is fine, a rejection is not).
		for err in get_errors():
			if not err.handled:
				err.handled = true
				if str(err.code).contains("keeping the old value") or not _known(str(err.code)):
					problems.append("%s.%s = %s: %s" % [where, name, v, err.code])


func _check_bullets(where: String) -> void:
	volleys = volleys.filter(func(v): return is_instance_valid(v))
	for v in volleys:
		if v.debug_get_life_state() != "active":
			continue
		for i in v.get_amount_bullets():
			if v.is_bullet_status_enabled(i) and (not v.get_bullet_transform(i).is_finite() or not v.get_bullet_velocity(i).is_finite()):
				problems.append("%s: non-finite bullet %d" % [where, i])
				return


func test_random_valid_configurations_stay_clean() -> void:
	make_graze_target(Vector2(100, 50), &"fuzz_targets")
	make_graze_target(Vector2(300, 100), &"fuzz_targets")
	make_wall(Vector2(260, 0), Vector2(20, 900), 15)
	make_area(Vector2(-150, 0), Vector2(40, 900), 15)
	for sd in SEEDS:
		rng.seed = 7000 + sd
		seed(7000 + sd)
		var data := H.make_volley_data(1, 120.0, 1.0)
		data.all_bullet_speed_data = []
		data.shared_bullet_speed_data = H.make_speed(rng.randf_range(0, 300), rng.randf_range(0, 600), rng.randf_range(-100, 100))
		_randomize(data, "seed %d data" % sd)
		data.sprite_frames = H.make_sprite_frames()
		data.max_life_time = clampf(data.max_life_time, 0.2, 3.0)
		var sp := make_spawner(data, BulletSpawner2D.PATTERN_FROM_HELPER_RING, 8)
		sp.volley_fired.connect(func(v: BulletVolley2D, _n: int) -> void: volleys.append(v))
		_randomize(sp, "seed %d spawner" % sd)
		var flock := BulletFlock2D.new()
		_randomize(flock, "seed %d flock" % sd)
		sp.flock = flock
		var zone := GrazeZone2D.new()
		_randomize(zone, "seed %d zone" % sd)
		sp.graze_zones = [zone]
		sp.homing_node_group = &"fuzz_targets"
		sp.graze_node_group = &"fuzz_targets"
		_audit("seed %d setup" % sd)
		for k in 6:
			sp.shoot_once()
			await physics(3)
			await idle(1)
			_audit("seed %d shot %d" % [sd, k])
			_check_bullets("seed %d shot %d" % [sd, k])
		sp.set_shooting_enabled(true)
		for k in 30:
			await physics(1)
			await idle(1)
		_audit("seed %d auto-fire" % sd)
		_check_bullets("seed %d auto-fire" % sd)
		var books: Dictionary = factory.debug_assert_no_dangling()
		if not books["ok"]:
			problems.append("seed %d: %s" % [sd, books["error"]])
		sp.queue_free()
		await idle(1)
		factory.reset()
		await idle(2)
		_audit("seed %d teardown" % sd)
		if problems.size() > 6:
			break
	assert_eq("\n".join(problems.slice(0, 6)), "", "random valid configurations stay clean")
