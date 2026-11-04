extends BlastTest
## The class reference in doc_classes/ (compiled into the binary, shown in the
## editor's help) matches the bound API: every plugin class has a page, and
## every [method]/[member]/[signal]/[constant]/[enum] link in every page
## resolves through ClassDB (a stale link renders as a red error in the
## editor's help). Every documented method, member, signal and constant has
## a description. Reads the XML files next to the test project, so it runs
## wherever the repository is checked out.

const LINK := "\\[(method|member|signal|constant|enum) ([A-Za-z0-9_@]+)(?:\\.([A-Za-z0-9_]+))?\\]"
# Godot's own documentation links these; ClassDB does not list them.
const ENGINE_SPECIALS := ["Object.free"]

var doc_dir := ""


func before_all() -> void:
	super()
	doc_dir = ProjectSettings.globalize_path("res://").path_join("../doc_classes").simplify_path()


func _doc_files() -> PackedStringArray:
	var files := PackedStringArray()
	for f in DirAccess.get_files_at(doc_dir):
		if f.ends_with(".xml"):
			files.append(f)
	return files


func _builtin_type_names() -> Array:
	var names := []
	for t in TYPE_MAX:
		names.append(type_string(t))
	return names


func _resolves(kind: String, cls: String, node_name: String) -> bool:
	match kind:
		"method":
			for m in ClassDB.class_get_method_list(cls): # includes virtuals and inherited methods
				if m.name == node_name:
					return true
			return false
		"member":
			for p in ClassDB.class_get_property_list(cls):
				if p.name == node_name:
					return true
			return false
		"signal":
			return ClassDB.class_has_signal(cls, node_name)
		"constant":
			return node_name in ClassDB.class_get_integer_constant_list(cls)
		"enum":
			return ClassDB.class_has_enum(cls, node_name)
	return false


func test_the_doc_folder_is_found() -> void:
	assert_gt(_doc_files().size(), 20, "doc_classes/ at %s" % doc_dir)


func test_every_plugin_class_has_a_page() -> void:
	var files := _doc_files()
	var missing := []
	for cls in ClassDB.get_class_list():
		if ClassDB.class_get_api_type(cls) == ClassDB.API_EXTENSION and not (cls + ".xml") in files:
			missing.append(cls)
	assert_eq(missing, [], "plugin classes without a doc_classes page")


func test_every_link_in_every_page_resolves() -> void:
	var re := RegEx.create_from_string(LINK)
	var builtins := _builtin_type_names()
	var broken := []
	var total := 0
	for f in _doc_files():
		var own := f.get_basename()
		for m in re.search_all(FileAccess.get_file_as_string(doc_dir.path_join(f))):
			total += 1
			var qualified := m.get_string(3) != ""
			var cls := m.get_string(2) if qualified else own
			var link_name := m.get_string(3) if qualified else m.get_string(2)
			if (cls + "." + link_name) in ENGINE_SPECIALS or cls in builtins or cls.begins_with("@"):
				continue
			if not ClassDB.class_exists(cls) or not _resolves(m.get_string(1), cls, link_name):
				broken.append("%s: %s" % [f, m.get_string(0)])
	assert_gt(total, 1000, "the pages were read")
	assert_eq(broken, [], "links that resolve to nothing")


func test_every_documented_entry_has_a_description() -> void:
	var empty := []
	for f in _doc_files():
		var parser := XMLParser.new()
		assert_eq(parser.open(doc_dir.path_join(f)), OK, f)
		var entry := ""
		var text := ""
		while parser.read() == OK:
			var type := parser.get_node_type()
			if type == XMLParser.NODE_ELEMENT and parser.get_node_name() in ["method", "member", "signal", "constant"]:
				entry = "%s %s" % [parser.get_node_name(), parser.get_named_attribute_value_safe("name")]
				text = ""
				if parser.get_node_name() in ["member", "constant"] and parser.is_empty():
					empty.append("%s: %s" % [f, entry])
					entry = ""
			elif type == XMLParser.NODE_TEXT and entry != "":
				text += parser.get_node_data()
			elif type == XMLParser.NODE_ELEMENT_END and entry != "" and parser.get_node_name() in ["method", "member", "signal", "constant"]:
				if text.strip_edges() == "":
					empty.append("%s: %s" % [f, entry])
				entry = ""
	assert_eq(empty, [], "entries without a description")
