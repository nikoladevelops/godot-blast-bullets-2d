extends BlastTest
## Repo hygiene: every runnable suite must live under res://tests/ (meta
## canaries under res://tests_meta/, third-party under res://addons/). A
## stray test_*.gd anywhere else is invisible to the runner (.gutconfig dirs
## plus tools/run_tests.py discovery walk tests/ only) and would silently stop
## running. This guard fails listing any such orphan.

const ALLOW_PREFIXES := ["res://tests/", "res://tests_meta/", "res://addons/"]


func test_no_orphan_suites() -> void:
	var orphans: Array = []
	_scan("res://", orphans)
	assert_true(orphans.is_empty(), "no orphan suites outside tests/: %s" % str(orphans))


func _scan(dir_path: String, out: Array) -> void:
	var d := DirAccess.open(dir_path)
	if d == null:
		return
	d.list_dir_begin()
	var entry := d.get_next()
	while entry != "":
		if entry == "." or entry == "..":
			entry = d.get_next()
			continue
		var full := dir_path.path_join(entry)
		if d.current_is_dir():
			if entry != ".godot":
				_scan(full, out)
		elif entry.begins_with("test_") and entry.ends_with(".gd"):
			var allowed := false
			for prefix in ALLOW_PREFIXES:
				if full.begins_with(prefix):
					allowed = true
			if not allowed:
				out.append(full)
		entry = d.get_next()
	d.list_dir_end()
