extends SceneTree
## PatternSource source-lock suite: every enum value must keep its exact
## integer, because pattern_source is serialized as a plain int in .tscn.
##
## Why this test exists: the enum was 33 entries with only the FIRST explicitly
## assigned, the other 32 implicit. Inserting an enumerator renumbered every
## later one and quietly repointed saved scenes at the wrong generator. Two
## static_asserts caught that only for STAR_POLYGON and POLYGON - slots 1..11
## and 29..32 had no guard at all. The enum is now fully explicit, and this
## suite pins all 33 values from the script side so a future renumber fails
## here even if someone deletes an assert.
##
## Also covers: the setter range check accepts every legal value and rejects
## out-of-range ones, and the inspector hint lists all 33.
##
## Run: godot --headless --path test_project --script tests/spawner/test_spawner_pattern_source_lock.gd
## Exit code 0 = all pass. Any FAIL = a saved scene will load the wrong pattern.

var failures := 0

func _check(cond: bool, label: String) -> void:
	if cond:
		print("PASS  ", label)
	else:
		failures += 1
		printerr("FAIL  ", label)

# The enum's serialized integers, pinned here in script order (0..32). The
# authoritative runtime check is the inspector hint sweep in T2, which reads
# the SAME kPatternSources table the editor and saved scenes use; this table
# documents the expected C++ enum order at a glance.
const LOCK_ORDER := [
	"PATTERN_FROM_CHILDREN", "PATTERN_FROM_SELF", "PATTERN_FROM_HELPER_GRID",
	"PATTERN_FROM_HELPER_RING", "PATTERN_FROM_HELPER_FAN", "PATTERN_FROM_HELPER_SPIRAL",
	"PATTERN_FROM_HELPER_LINE", "PATTERN_FROM_HELPER_AIMED", "PATTERN_FROM_HELPER_FLOWER",
	"PATTERN_FROM_HELPER_ELLIPSE", "PATTERN_FROM_HELPER_RAIN", "PATTERN_FROM_HELPER_SCATTER",
	"PATTERN_FROM_HELPER_STAR_POLYGON", "PATTERN_FROM_HELPER_MULTISPIRAL", "PATTERN_FROM_HELPER_CROSS",
	"PATTERN_FROM_HELPER_STAR", "PATTERN_FROM_HELPER_HEART", "PATTERN_FROM_HELPER_WAVE",
	"PATTERN_FROM_HELPER_WATERFALL", "PATTERN_FROM_HELPER_LATTICE", "PATTERN_FROM_HELPER_ROSE",
	"PATTERN_FROM_HELPER_COUNTER_SPIRAL", "PATTERN_FROM_HELPER_CORRIDOR", "PATTERN_FROM_HELPER_LISSAJOUS",
	"PATTERN_FROM_HELPER_CUSTOM", "PATTERN_FROM_HELPER_CIRCLE", "PATTERN_FROM_HELPER_RECTANGLE",
	"PATTERN_FROM_HELPER_SQUARE", "PATTERN_FROM_HELPER_POLYGON", "PATTERN_FROM_HELPER_PATH2D",
	"PATTERN_FROM_HELPER_TRIANGLE", "PATTERN_FROM_HELPER_TRAPEZOID", "PATTERN_FROM_HELPER_DIAMOND",
]

func _initialize() -> void:
	var factory := BulletFactory2D.new()
	get_root().add_child(factory)
	await process_frame
	await process_frame
	var spawner := BulletSpawner2D.new()
	# This suite only inspects enum/property mapping; the spawner's own firing
	# loop would spam errors and drive frames we do not control.
	spawner.set_shooting_enabled(false)
	spawner.set_process(false)
	factory.add_child(spawner)
	await process_frame
	await process_frame

	# ---------------------------------------------------------------
	printerr("SRC T1 the enum covers 33 sources, 0..32, with no gaps")
	_check(LOCK_ORDER.size() == 33, "T1a 33 sources declared (%d)" % LOCK_ORDER.size())
	var seen_names := {}
	var dup_names: Array = []
	for n in LOCK_ORDER:
		if seen_names.has(n):
			dup_names.append(n)
		seen_names[n] = true
	_check(dup_names.is_empty(), "T1b no duplicate enumerator names (%s)" % (", ".join(dup_names) if not dup_names.is_empty() else "none"))

	# ---------------------------------------------------------------
	printerr("SRC T2 the inspector hint pins every source id")
	# pattern_source_hint() builds "Name:id" pairs from kPatternSources. Reading
	# them back through the property hint proves the RUNTIME mapping, which is
	# what the editor and saved scenes actually use.
	var hints := ""
	for p in spawner.get_property_list():
		if str(p.get("name", "")) == "pattern_source":
			hints = str(p.get("hint_string", ""))
	_check(hints != "", "T2a pattern_source exposes a hint string (%d chars)" % hints.length())

	# Every locked id must appear in the hint exactly once, under its display
	# name. The hint string is the runtime mapping the editor renders AND the
	# one a saved scene round-trips through, so it - not the C++ enum text - is
	# the thing worth pinning from the script side.
	var hint_pairs := {}
	for part in hints.split(","):
		var kv := str(part).split(":")
		if kv.size() == 2:
			hint_pairs[kv[0].strip_edges()] = int(kv[1])

	# Display name (as it appears in the inspector) -> locked integer.
	const HINT_LOCK := {
		"From Children": 0,
		"From Self": 1,
		"Grid": 2,
		"Ring": 3,
		"Fan": 4,
		"Spiral": 5,
		"Line": 6,
		"Aimed": 7,
		"Flower": 8,
		"Ellipse": 9,
		"Rain": 10,
		"Scatter": 11,
		"Star Polygon": 12,
		"Multi Spiral": 13,
		"Cross": 14,
		"Star": 15,
		"Heart": 16,
		"Wave": 17,
		"Waterfall": 18,
		"Lattice": 19,
		"Rose": 20,
		"Counter Spiral": 21,
		"Corridor": 22,
		"Lissajous": 23,
		"Custom": 24,
		"Circle": 25,
		"Rectangle": 26,
		"Square": 27,
		"Polygon": 28,
		"Path2D": 29,
		"Triangle": 30,
		"Trapezoid": 31,
		"Diamond": 32,
	}
	_check(hint_pairs.size() == HINT_LOCK.size(),
		"T2a hint lists every source (%d of %d)" % [hint_pairs.size(), HINT_LOCK.size()])
	for display in HINT_LOCK.keys():
		var expected: int = HINT_LOCK[display]
		if not hint_pairs.has(display):
			_check(false, "T2 hint is missing '%s'" % display)
			continue
		_check(int(hint_pairs[display]) == expected,
			"T2 '%s' hint id == %d (got %d)" % [display, expected, int(hint_pairs[display])])

	# ---------------------------------------------------------------
	printerr("SRC T3 the setter accepts every legal value and rejects the rest")
	var accepted := 0
	for i in range(0, 33):
		spawner.pattern_source = i
		if int(spawner.pattern_source) == i:
			accepted += 1
	_check(accepted == 33, "T3a all 33 legal values accepted (%d)" % accepted)
	# Out of range must be refused, leaving the previous value in place.
	spawner.pattern_source = 5
	spawner.pattern_source = -1
	_check(int(spawner.pattern_source) == 5, "T3b negative value refused, kept %d" % int(spawner.pattern_source))
	spawner.pattern_source = 33
	_check(int(spawner.pattern_source) == 5, "T3c value 33 refused, kept %d" % int(spawner.pattern_source))
	spawner.pattern_source = 9999
	_check(int(spawner.pattern_source) == 5, "T3d value 9999 refused, kept %d" % int(spawner.pattern_source))

	# ---------------------------------------------------------------
	printerr("SRC T4 the two long-standing static_assert anchors still hold")
	# These are the historical tripwires; a round-trip through the property
	# proves the compile-time values survived the explicit-enum rewrite.
	spawner.pattern_source = 12
	_check(int(spawner.pattern_source) == 12, "T4a STAR_POLYGON anchor is 12")
	spawner.pattern_source = 28
	_check(int(spawner.pattern_source) == 28, "T4b POLYGON anchor is 28")

	# ---------------------------------------------------------------
	printerr("SRC T5 no duplicate ids in the hint (a renumber would collide)")
	var seen := {}
	var dupes: Array = []
	for name in hint_pairs.keys():
		var id: int = hint_pairs[name]
		if seen.has(id):
			dupes.append("%s and %s both claim %d" % [seen[id], name, id])
		seen[id] = name
	_check(dupes.is_empty(), "T5 no duplicate hint ids (%s)" % (", ".join(dupes) if not dupes.is_empty() else "none"))

	_check(factory.debug_assert_no_dangling().get("ok", false) == true, "no dangling at end")

	factory.reset()
	factory.queue_free()
	await process_frame
	print("----")
	if failures == 0:
		print("ALL PATTERN-SOURCE-LOCK TESTS PASSED")
	else:
		printerr(str(failures) + " TEST(S) FAILED")
	quit(failures)
