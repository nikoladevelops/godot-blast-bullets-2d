extends BlastTest
## Every ```gdscript block of README.md and QUICKSTART.md compiles against the
## real plugin classes: the names a snippet assumes from the reader's game
## (`spawner`, `factory`, `player`, ...) are TYPED variables here, so a
## misspelled property, method, constant or signal is a compile error that
## names the block. Behavior of the snippets is pinned by test_readme_examples.

const DOCS := ["README.md", "QUICKSTART.md"]

## What the snippets take from the reader's own scene, with the type they mean.
const HEADER := """extends Node2D
var spawner: BulletSpawner2D
var factory: BulletFactory2D
var player: Node2D
var boss: Node2D
var volley: BulletVolley2D
var data: BulletVolleyData2D
var laser: LaserVolley2D
var lasers: LaserSpawner2D
var sparks: BulletSpawner2D
var coin: BulletPickupData2D
var score: int
func spawn_spark(_at: Vector2, _normal: Vector2) -> void: pass
func _next_phase() -> void: pass
"""


func _blocks(file_name: String) -> Array:
	var path := ProjectSettings.globalize_path("res://").path_join("..").path_join(file_name)
	var text := FileAccess.get_file_as_string(path)
	assert_ne(text, "", "read " + file_name)
	var out := []
	var lines := text.split("\n")
	var i := 0
	while i < lines.size():
		if lines[i].strip_edges() == "```gdscript":
			var start := i + 1
			var body := PackedStringArray()
			i += 1
			while i < lines.size() and lines[i].strip_edges() != "```":
				body.append(lines[i])
				i += 1
			out.append({"where": "%s:%d" % [file_name, start], "code": "\n".join(body)})
		i += 1
	return out


## A block with top-level functions stays at class level; any other block
## becomes the body of one function (preload -> load: the reader's files).
func _source(code: String) -> String:
	code = code.replace("preload(", "load(")
	if code.begins_with("extends "): # a whole script: its own base, then the shared names
		var first := code.split("\n")[0]
		var shared := HEADER.substr(HEADER.find("\n") + 1)
		return first + "\n" + shared + code.substr(first.length()) + "\n"
	var has_funcs := false
	for line in code.split("\n"):
		if line.begins_with("func "):
			has_funcs = true
	if has_funcs:
		return HEADER + "\n" + code + "\n"
	var body := PackedStringArray()
	for line in code.split("\n"):
		body.append("\t" + line)
	return HEADER + "\nfunc _snippet() -> void:\n" + "\n".join(body) + "\n\tpass\n"


## The type of each name a snippet uses: the HEADER's reader variables plus
## its own `var x := Class.new(...)` / `var x: Class` / `var x := Class.create_preset(...)`.
func _types(code: String) -> Dictionary:
	var rx := RegEx.create_from_string("var ([a-z_][a-z0-9_]*)\\s*(?::=\\s*([A-Z][A-Za-z0-9]*)\\.(?:new|create_preset)\\(|:\\s*([A-Z][A-Za-z0-9]*))")
	var types := {}
	for m in rx.search_all(HEADER):
		types[m.get_string(1)] = m.get_string(3)
	# A name the snippet declares itself shadows the reader variable...
	for m in RegEx.create_from_string("var ([a-z_][a-z0-9_]*)").search_all(code):
		types.erase(m.get_string(1))
	# ...and is typed again when its class is visible.
	for m in rx.search_all(code):
		var cls := m.get_string(2) if m.get_string(2) != "" else m.get_string(3)
		if ClassDB.class_exists(cls):
			types[m.get_string(1)] = cls
	return types


func _has_member(cls: String, member: String) -> bool:
	if member == "new" or ClassDB.class_has_method(cls, member) or ClassDB.class_has_signal(cls, member) or ClassDB.class_has_integer_constant(cls, member):
		return true
	for prop in ClassDB.class_get_property_list(cls):
		if prop.name == member:
			return true
	return false


## `name.member` on a typed name and `Class.member` on a plugin or engine class.
func _unknown_members(code: String) -> Array:
	var types := _types(code)
	var out := []
	var rx := RegEx.create_from_string("(?<![\\w./\"])([A-Za-z_][A-Za-z0-9_]*)\\.([A-Za-z_][A-Za-z0-9_]*)")
	for m in rx.search_all(code):
		var base := m.get_string(1)
		var cls: String = types.get(base, base if ClassDB.class_exists(base) else "")
		if cls != "" and not _has_member(cls, m.get_string(2)):
			out.append("%s.%s" % [base, m.get_string(2)])
	return out


func test_every_docs_snippet_names_only_real_members() -> void:
	var unknown := []
	for doc in DOCS:
		for block in _blocks(doc):
			for name in _unknown_members(block.code):
				unknown.append("%s %s" % [block.where, name])
	assert_eq(unknown, [], "members the plugin does not have: " + ", ".join(unknown))


func test_every_docs_snippet_compiles_against_the_plugin() -> void:
	var broken := []
	var count := 0
	for doc in DOCS:
		for block in _blocks(doc):
			count += 1
			var script := GDScript.new()
			script.source_code = _source(block.code)
			if script.reload() != OK:
				broken.append(block.where)
	assert_gt(count, 10, "the docs hold snippets")
	assert_eq(broken, [], "snippets that do not compile (the parse error above names the line)")
