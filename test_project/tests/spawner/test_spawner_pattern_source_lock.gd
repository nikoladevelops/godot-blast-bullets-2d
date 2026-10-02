extends BlastTest
## pattern_source is serialized as a plain int in .tscn, so every enum value
## must keep its exact integer: a renumber silently repoints saved scenes at
## the wrong generator. Pins all 33 ids through the inspector hint (the
## runtime mapping the editor and saved scenes use), the setter range, and
## the C++ enum constants.

const HINT_LOCK := {
	"From Children": 0, "From Self": 1, "Grid": 2, "Ring": 3, "Fan": 4,
	"Spiral": 5, "Line": 6, "Aimed": 7, "Flower": 8, "Ellipse": 9, "Rain": 10,
	"Scatter": 11, "Star Polygon": 12, "Multi Spiral": 13, "Cross": 14,
	"Star": 15, "Heart": 16, "Wave": 17, "Waterfall": 18, "Lattice": 19,
	"Rose": 20, "Counter Spiral": 21, "Corridor": 22, "Lissajous": 23,
	"Custom": 24, "Circle": 25, "Rectangle": 26, "Square": 27, "Polygon": 28,
	"Path2D": 29, "Triangle": 30, "Trapezoid": 31, "Diamond": 32,
}
const ENUM_LOCK := {
	"PATTERN_FROM_CHILDREN": 0, "PATTERN_FROM_SELF": 1, "PATTERN_FROM_HELPER_GRID": 2,
	"PATTERN_FROM_HELPER_RING": 3, "PATTERN_FROM_HELPER_FAN": 4, "PATTERN_FROM_HELPER_SPIRAL": 5,
	"PATTERN_FROM_HELPER_LINE": 6, "PATTERN_FROM_HELPER_AIMED": 7, "PATTERN_FROM_HELPER_FLOWER": 8,
	"PATTERN_FROM_HELPER_ELLIPSE": 9, "PATTERN_FROM_HELPER_RAIN": 10, "PATTERN_FROM_HELPER_SCATTER": 11,
	"PATTERN_FROM_HELPER_STAR_POLYGON": 12, "PATTERN_FROM_HELPER_MULTISPIRAL": 13, "PATTERN_FROM_HELPER_CROSS": 14,
	"PATTERN_FROM_HELPER_STAR": 15, "PATTERN_FROM_HELPER_HEART": 16, "PATTERN_FROM_HELPER_WAVE": 17,
	"PATTERN_FROM_HELPER_WATERFALL": 18, "PATTERN_FROM_HELPER_LATTICE": 19, "PATTERN_FROM_HELPER_ROSE": 20,
	"PATTERN_FROM_HELPER_COUNTER_SPIRAL": 21, "PATTERN_FROM_HELPER_CORRIDOR": 22, "PATTERN_FROM_HELPER_LISSAJOUS": 23,
	"PATTERN_FROM_HELPER_CUSTOM": 24, "PATTERN_FROM_HELPER_CIRCLE": 25, "PATTERN_FROM_HELPER_RECTANGLE": 26,
	"PATTERN_FROM_HELPER_SQUARE": 27, "PATTERN_FROM_HELPER_POLYGON": 28, "PATTERN_FROM_HELPER_PATH2D": 29,
	"PATTERN_FROM_HELPER_TRIANGLE": 30, "PATTERN_FROM_HELPER_TRAPEZOID": 31, "PATTERN_FROM_HELPER_DIAMOND": 32,
}

var sp: BulletSpawner2D


func before_each() -> void:
	await super()
	sp = make_spawner()


func test_enum_constants_locked() -> void:
	for enum_name in ENUM_LOCK:
		assert_eq(ClassDB.class_get_integer_constant("BulletSpawner2D", enum_name), ENUM_LOCK[enum_name], "%s keeps its id" % enum_name)
	assert_eq(BulletSpawner2D.PATTERN_FROM_LAST, 33, "33 sources")


func test_inspector_hint_pins_every_id() -> void:
	var hints := ""
	for p in sp.get_property_list():
		if str(p.get("name", "")) == "pattern_source":
			hints = str(p.get("hint_string", ""))
	assert_ne(hints, "", "pattern_source exposes a hint string")
	var pairs := {}
	var seen_ids := {}
	for part in hints.split(","):
		var kv := str(part).split(":")
		if kv.size() == 2:
			pairs[kv[0].strip_edges()] = int(kv[1])
			assert_false(seen_ids.has(int(kv[1])), "id %s claimed once" % kv[1])
			seen_ids[int(kv[1])] = true
	assert_eq(pairs.size(), HINT_LOCK.size(), "hint lists every source")
	for display in HINT_LOCK:
		assert_true(pairs.has(display), "hint has '%s'" % display)
		assert_eq(pairs.get(display, -1), HINT_LOCK[display], "'%s' hint id" % display)


func test_setter_range() -> void:
	var accepted := 0
	for i in range(33):
		sp.pattern_source = i
		if int(sp.pattern_source) == i:
			accepted += 1
	assert_eq(accepted, 33, "all 33 legal values accepted")
	sp.pattern_source = 5
	for bad in [-1, 33, 9999]:
		sp.pattern_source = bad
		expect_any_error()
		assert_eq(int(sp.pattern_source), 5, "value %d refused" % bad)
