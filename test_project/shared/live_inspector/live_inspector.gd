class_name LiveInspector
extends CanvasLayer
## A runtime inspector for the plugin: every inspector property of the factory, the spawners,
## their spawn / laser data and any nested Resource, edited live while the game runs.
## The editors are generated from get_property_list(), so new plugin properties show up
## without touching this script. F2 (or the button) shows and hides it.
##
## A setter that refuses a value keeps the old one (the plugin's contract): the row then
## flashes red and the status line says so; the reason is in the Output / debugger.

## Targets listed in the picker: [label, Callable returning the Object (or null)].
## Every BulletFactory2D and spawner of the running scene is found on its own (Rescan);
## add_target() adds anything else (a script's BulletVolleyData2D, say).
var targets: Array = []
var _manual_targets: Array = []

const PANEL_WIDTH := 430.0
const REFRESH_SEC := 0.25
## Base classes whose properties stay hidden (Node, CanvasItem, ... are not the plugin's API).
const ENGINE_CATEGORIES := ["Node", "CanvasItem", "Node2D", "Resource", "RefCounted", "Object", "CanvasLayer", "Area2D", "CollisionObject2D"]

var _stack: Array[Object] = [] # navigation: the target, then opened sub-resources
var _stack_names: PackedStringArray = []
var _rows: Array[Dictionary] = [] # {name, editor, kind, row, group_box}
var _groups: Dictionary = {} # group title -> {header, box, open}
var _open_groups: Dictionary = {} # remembered per class: "Class/Group" -> bool
var _refresh_left := REFRESH_SEC
var _signature := ""

var _panel: PanelContainer
var _picker: OptionButton
var _crumbs: Label
var _back: Button
var _search: LineEdit
var _list: VBoxContainer
var _status: Label
var _toggle: Button


func _ready() -> void:
	layer = 50
	_build_ui()
	_panel.visible = false


func _unhandled_key_input(event: InputEvent) -> void:
	var key := event as InputEventKey
	if key and key.pressed and not key.echo and key.keycode == KEY_F2:
		_set_open(not _panel.visible)
		get_viewport().set_input_as_handled()


## Adds a target to the picker. getter returns the Object to edit (it may change over time).
func add_target(label: String, getter: Callable) -> void:
	_manual_targets.append([label, getter])
	_discover()


## Lists the plugin nodes of the running scene (and the newest live volley / laser).
func _discover() -> void:
	var keep := _picker.get_item_text(_picker.selected) if _picker and _picker.selected >= 0 else ""
	targets.clear()
	var found: Array[Node] = []
	if is_inside_tree():
		_collect(get_tree().root, found)
	for n in found:
		var id := n.get_instance_id()
		targets.append(["%s  (%s)" % [n.name, n.get_class()], func(): return instance_from_id(id)])
	for f in found:
		if f is BulletFactory2D:
			var fid := f.get_instance_id()
			targets.append(["Newest live volley", func():
				var fac = instance_from_id(fid)
				var vs: Array = fac.get_active_volleys() if fac else []
				return vs.back() if not vs.is_empty() else null])
			break
	targets.append_array(_manual_targets)
	if _picker == null:
		return
	_picker.clear()
	for i in targets.size():
		_picker.add_item(targets[i][0])
		if targets[i][0] == keep:
			_picker.select(i)


func _collect(n: Node, out: Array[Node]) -> void:
	if n is BulletFactory2D or n is PatternSpawner2D:
		out.append(n)
	if n is BulletFactory2D:
		return # its children are the plugin's own (volleys, containers)
	for c in n.get_children():
		_collect(c, out)


func _process(delta: float) -> void:
	if not _panel.visible:
		return
	_refresh_left -= delta
	if _refresh_left > 0.0:
		return
	_refresh_left = REFRESH_SEC
	var obj := _current()
	if obj == null:
		var sel := _picker.selected
		if sel >= 0 and sel < targets.size() and targets[sel][1].call() is Object:
			_open_target(sel) # the target exists now (spawned, or the scene finished loading)
		return
	if _property_signature(obj) != _signature:
		_rebuild() # a gating property changed what the inspector shows
		return
	for r in _rows:
		_pull(obj, r)


# ------------------------------------------------------------------ UI skeleton
func _build_ui() -> void:
	var theme_res: Theme = load("res://shared/ui/test_lab_theme.tres")
	_toggle = Button.new()
	_toggle.text = "Inspector (F2)"
	_toggle.theme = theme_res
	_toggle.anchor_left = 1.0
	_toggle.anchor_right = 1.0
	_toggle.offset_left = -190.0
	_toggle.offset_right = -12.0
	_toggle.offset_top = 82.0
	_toggle.offset_bottom = 112.0
	_toggle.focus_mode = Control.FOCUS_NONE
	_toggle.pressed.connect(func(): _set_open(not _panel.visible))
	add_child(_toggle)

	_panel = PanelContainer.new()
	_panel.theme = theme_res
	_panel.anchor_left = 1.0
	_panel.anchor_right = 1.0
	_panel.anchor_bottom = 1.0
	_panel.offset_left = -PANEL_WIDTH
	_panel.offset_top = 118.0
	_panel.mouse_filter = Control.MOUSE_FILTER_STOP
	add_child(_panel)
	var v := VBoxContainer.new()
	v.add_theme_constant_override(&"separation", 6)
	_panel.add_child(v)

	var title := Label.new()
	title.text = "Live Inspector"
	title.add_theme_font_size_override(&"font_size", 18)
	v.add_child(title)
	var hint := Label.new()
	hint.text = "Every plugin property, live. Refused values keep the old one."
	hint.add_theme_font_size_override(&"font_size", 11)
	hint.modulate = Color(1, 1, 1, 0.6)
	v.add_child(hint)

	var pick_row := HBoxContainer.new()
	_picker = OptionButton.new()
	_picker.focus_mode = Control.FOCUS_NONE
	_picker.clip_text = true
	_picker.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_picker.item_selected.connect(_open_target)
	pick_row.add_child(_picker)
	var rescan := Button.new()
	rescan.text = "Rescan"
	rescan.focus_mode = Control.FOCUS_NONE
	rescan.pressed.connect(func():
		_discover()
		_open_target(maxi(_picker.selected, 0)))
	pick_row.add_child(rescan)
	v.add_child(pick_row)

	var nav := HBoxContainer.new()
	_back = Button.new()
	_back.text = "< Back"
	_back.focus_mode = Control.FOCUS_NONE
	_back.pressed.connect(_go_back)
	nav.add_child(_back)
	_crumbs = Label.new()
	_crumbs.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_crumbs.clip_text = true
	_crumbs.add_theme_font_size_override(&"font_size", 12)
	nav.add_child(_crumbs)
	v.add_child(nav)

	_search = LineEdit.new()
	_search.placeholder_text = "Search properties..."
	_search.clear_button_enabled = true
	_search.text_changed.connect(func(_t): _apply_filter())
	v.add_child(_search)

	var scroll := ScrollContainer.new()
	scroll.size_flags_vertical = Control.SIZE_EXPAND_FILL
	scroll.horizontal_scroll_mode = ScrollContainer.SCROLL_MODE_DISABLED
	v.add_child(scroll)
	_list = VBoxContainer.new()
	_list.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_list.add_theme_constant_override(&"separation", 2)
	scroll.add_child(_list)

	_status = Label.new()
	_status.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	_status.add_theme_font_size_override(&"font_size", 12)
	_status.custom_minimum_size.y = 34
	v.add_child(_status)


func _set_open(open: bool) -> void:
	_panel.visible = open
	if open and _stack.is_empty():
		_discover()
		_open_target(maxi(_picker.selected, 0))


# ------------------------------------------------------------------ navigation
func _current() -> Object:
	if _stack.is_empty():
		return null
	var obj: Object = _stack.back()
	return obj if is_instance_valid(obj) else null


func _open_target(index: int) -> void:
	_stack.clear()
	_stack_names.clear()
	if index < 0 or index >= targets.size():
		return
	var obj = targets[index][1].call()
	if obj is Object and is_instance_valid(obj):
		_stack.append(obj)
		_stack_names.append(targets[index][0])
	_rebuild()


func _open_child(obj: Object, label: String) -> void:
	_stack.append(obj)
	_stack_names.append(label)
	_search.text = ""
	_rebuild()


func _go_back() -> void:
	if _stack.size() > 1:
		_stack.pop_back()
		_stack_names.resize(_stack_names.size() - 1)
		_rebuild()


# ------------------------------------------------------------------ property list -> rows
func _editable_properties(obj: Object) -> Array[Dictionary]:
	var out: Array[Dictionary] = []
	var category := ""
	var group := ""
	var subgroup := ""
	var group_prefix := ""
	var subgroup_prefix := ""
	for p in obj.get_property_list():
		var usage: int = p.usage
		if usage & PROPERTY_USAGE_CATEGORY:
			category = p.name
			group = ""
			subgroup = ""
			group_prefix = ""
			subgroup_prefix = ""
			continue
		if usage & PROPERTY_USAGE_GROUP:
			group = p.name
			group_prefix = p.hint_string
			subgroup = ""
			subgroup_prefix = ""
			continue
		if usage & PROPERTY_USAGE_SUBGROUP:
			subgroup = p.name
			subgroup_prefix = p.hint_string
			continue
		if not (usage & PROPERTY_USAGE_EDITOR) or category in ENGINE_CATEGORIES:
			continue
		if p.name in ["script", "resource_local_to_scene", "resource_name", "resource_path"]:
			continue
		var d: Dictionary = p.duplicate()
		d["group"] = group if group != "" else category
		d["subgroup"] = subgroup if subgroup_prefix == "" or (p.name as String).begins_with(subgroup_prefix) else ""
		d["prefix"] = subgroup_prefix if d.subgroup != "" else group_prefix
		out.append(d)
	return out


func _property_signature(obj: Object) -> String:
	var names := PackedStringArray()
	for p in _editable_properties(obj):
		names.append(p.name)
	return ",".join(names)


func _rebuild() -> void:
	for c in _list.get_children():
		c.queue_free()
	_rows.clear()
	_groups.clear()
	var obj := _current()
	_back.disabled = _stack.size() <= 1
	_crumbs.text = " / ".join(_stack_names)
	if obj == null:
		_signature = ""
		_status.text = "Nothing to inspect here (not in this scene yet)."
		return
	var props := _editable_properties(obj)
	_signature = _property_signature(obj)
	var last_sub := ""
	for p in props:
		var box := _group_box(obj, p.group)
		if p.subgroup != last_sub:
			last_sub = p.subgroup
			if last_sub != "":
				var sub := Label.new()
				sub.text = "  " + last_sub
				sub.add_theme_font_size_override(&"font_size", 12)
				sub.modulate = Color(0.55, 0.95, 1.0)
				box.add_child(sub)
				_rows.append({"name": "", "row": sub, "group_title": p.group, "subgroup_label": true})
		var row := _make_row(obj, p)
		if row:
			box.add_child(row.row)
			row["group_title"] = p.group
			_rows.append(row)
	_add_actions(obj)
	_apply_filter()


func _group_box(obj: Object, title: String) -> VBoxContainer:
	if _groups.has(title):
		return _groups[title].box
	var key := obj.get_class() + "/" + title
	var open: bool = _open_groups.get(key, _groups.is_empty()) # the first group starts open
	var header := Button.new()
	header.text = ("v  " if open else ">  ") + title
	header.alignment = HORIZONTAL_ALIGNMENT_LEFT
	header.focus_mode = Control.FOCUS_NONE
	_list.add_child(header)
	var box := VBoxContainer.new()
	box.visible = open
	_list.add_child(box)
	_groups[title] = {"header": header, "box": box}
	header.pressed.connect(func():
		box.visible = not box.visible
		_open_groups[key] = box.visible
		header.text = ("v  " if box.visible else ">  ") + title)
	return box


func _apply_filter() -> void:
	var q := _search.text.strip_edges().to_lower()
	var visible_in: Dictionary = {}
	for r in _rows:
		if r.get("subgroup_label", false):
			r.row.visible = q == ""
			continue
		var hit := q == "" or (r.name as String).to_lower().contains(q) or (r.group_title as String).to_lower().contains(q)
		r.row.visible = hit
		if hit:
			visible_in[r.group_title] = true
	for title in _groups:
		var g: Dictionary = _groups[title]
		g.header.visible = q == "" or visible_in.has(title) or title == "Actions"
		if q != "":
			g.box.visible = visible_in.has(title)


# ------------------------------------------------------------------ one row per property
func _make_row(obj: Object, p: Dictionary) -> Dictionary:
	var row := HBoxContainer.new()
	var label := Label.new()
	var shown: String = p.name
	var prefix: String = p.prefix
	if prefix != "" and shown.begins_with(prefix) and shown.length() > prefix.length():
		shown = shown.substr(prefix.length())
	label.text = shown.capitalize()
	label.tooltip_text = "%s\n%s" % [p.name, type_string(p.type)]
	label.mouse_filter = Control.MOUSE_FILTER_PASS
	label.custom_minimum_size.x = 170
	label.clip_text = true
	label.add_theme_font_size_override(&"font_size", 12)
	row.add_child(label)
	var r := {"name": p.name, "row": row, "prop": p}
	var editor := _make_editor(obj, p, r)
	if editor == null:
		row.free()
		return {}
	editor.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	row.add_child(editor)
	r["editor"] = editor
	_pull(obj, r)
	return r


func _make_editor(obj: Object, p: Dictionary, r: Dictionary) -> Control:
	var prop_name: String = p.name
	match p.type:
		TYPE_BOOL:
			var cb := CheckBox.new()
			cb.focus_mode = Control.FOCUS_NONE
			cb.toggled.connect(func(on): _push(obj, r, on))
			r.kind = "bool"
			return cb
		TYPE_INT:
			if p.hint == PROPERTY_HINT_ENUM:
				var ob := OptionButton.new()
				ob.focus_mode = Control.FOCUS_NONE
				ob.clip_text = true
				var next := 0
				for part in (p.hint_string as String).split(","):
					var kv := part.split(":")
					var value := int(kv[1]) if kv.size() > 1 else next
					ob.add_item(kv[0].strip_edges(), value)
					next = value + 1
				ob.item_selected.connect(func(i): _push(obj, r, ob.get_item_id(i)))
				r.kind = "enum"
				return ob
			return _spin(obj, p, r, true)
		TYPE_FLOAT:
			return _spin(obj, p, r, false)
		TYPE_VECTOR2, TYPE_VECTOR2I:
			var h := HBoxContainer.new()
			var xs := _raw_spin(p, p.type == TYPE_VECTOR2I)
			var ys := _raw_spin(p, p.type == TYPE_VECTOR2I)
			var write := func(_v):
				var v = Vector2(xs.value, ys.value)
				var pushed: Variant = v
				if p.type == TYPE_VECTOR2I:
					pushed = Vector2i(v)
				_push(obj, r, pushed)
			xs.value_changed.connect(write)
			ys.value_changed.connect(write)
			h.add_child(xs)
			h.add_child(ys)
			r.kind = "vec2"
			r.x = xs
			r.y = ys
			return h
		TYPE_COLOR:
			var cp := ColorPickerButton.new()
			cp.custom_minimum_size.y = 24
			cp.color_changed.connect(func(c): _push(obj, r, c))
			r.kind = "color"
			return cp
		TYPE_STRING, TYPE_STRING_NAME, TYPE_NODE_PATH:
			var le := LineEdit.new()
			var commit := func():
				var v: Variant = le.text
				if p.type == TYPE_STRING_NAME:
					v = StringName(le.text)
				elif p.type == TYPE_NODE_PATH:
					v = NodePath(le.text)
				if is_instance_valid(obj) and obj.get(prop_name) != v:
					_push(obj, r, v)
			le.text_submitted.connect(func(_t): le.release_focus())
			le.focus_exited.connect(commit)
			r.kind = "text"
			return le
		TYPE_OBJECT:
			return _resource_editor(obj, p, r)
		TYPE_ARRAY:
			return _array_editor(obj, p, r)
		TYPE_PACKED_VECTOR2_ARRAY, TYPE_PACKED_FLOAT32_ARRAY, TYPE_PACKED_FLOAT64_ARRAY, TYPE_PACKED_INT32_ARRAY, TYPE_PACKED_INT64_ARRAY, TYPE_PACKED_COLOR_ARRAY, TYPE_DICTIONARY, TYPE_TRANSFORM2D, TYPE_RECT2:
			var l := Label.new()
			l.add_theme_font_size_override(&"font_size", 11)
			l.clip_text = true
			r.kind = "readonly"
			return l
	return null


func _raw_spin(p: Dictionary, integer: bool) -> SpinBox:
	var s := SpinBox.new()
	s.allow_greater = true
	s.allow_lesser = true
	s.step = 1.0 if integer else 0.001
	s.min_value = -1e6 if not integer else -1e9
	s.max_value = 1e6 if not integer else 1e9
	if p.hint == PROPERTY_HINT_RANGE:
		var parts := (p.hint_string as String).split(",")
		if parts.size() >= 2 and parts[0].is_valid_float() and parts[1].is_valid_float():
			s.min_value = float(parts[0])
			s.max_value = float(parts[1])
			s.allow_lesser = "or_less" in parts or "or_lesser" in parts
			s.allow_greater = "or_greater" in parts
		if parts.size() >= 3 and parts[2].is_valid_float() and float(parts[2]) > 0.0:
			s.step = float(parts[2])
		for part in parts:
			if part.begins_with("suffix:"):
				s.suffix = part.substr(7)
	s.select_all_on_focus = true
	s.update_on_text_changed = false
	s.custom_minimum_size.x = 80
	return s


func _spin(obj: Object, p: Dictionary, r: Dictionary, integer: bool) -> SpinBox:
	var s := _raw_spin(p, integer)
	s.value_changed.connect(func(v): _push(obj, r, int(v) if integer else v))
	r.kind = "number"
	return s


func _resource_editor(obj: Object, p: Dictionary, r: Dictionary) -> Control:
	var h := HBoxContainer.new()
	var open := Button.new()
	open.focus_mode = Control.FOCUS_NONE
	open.clip_text = true
	open.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	var cls: String = p.hint_string if p.hint == PROPERTY_HINT_RESOURCE_TYPE else ""
	open.pressed.connect(func():
		var value = obj.get(p.name)
		if value is Object and is_instance_valid(value):
			_open_child(value, "%s (%s)" % [p.name, value.get_class()])
		elif cls != "" and not cls.contains(",") and ClassDB.can_instantiate(cls):
			_push(obj, r, ClassDB.instantiate(cls))
			_status.text = "%s = new %s" % [p.name, cls])
	h.add_child(open)
	var clear := Button.new()
	clear.text = "x"
	clear.tooltip_text = "Set to null"
	clear.focus_mode = Control.FOCUS_NONE
	clear.pressed.connect(func(): _push(obj, r, null))
	h.add_child(clear)
	r.kind = "object"
	r.button = open
	r.cls = cls
	return h


func _array_editor(_obj: Object, p: Dictionary, r: Dictionary) -> Control:
	# Typed Resource arrays ("24/17:ClassName") get Open [i] and Add buttons; others are shown.
	var v := VBoxContainer.new()
	var cls := ""
	var hs: String = p.hint_string
	if hs.contains(":") and hs.begins_with("%d/" % TYPE_OBJECT):
		cls = hs.get_slice(":", 1)
	r.kind = "array"
	r.cls = cls
	r.box = v
	r.size = -1
	return v


func _rebuild_array(obj: Object, r: Dictionary, value: Array) -> void:
	var v: VBoxContainer = r.box
	for c in v.get_children():
		c.queue_free()
	var cls: String = r.cls
	var head := HBoxContainer.new()
	var count := Label.new()
	count.text = "%d item%s" % [value.size(), "" if value.size() == 1 else "s"]
	count.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	count.add_theme_font_size_override(&"font_size", 12)
	head.add_child(count)
	if cls != "" and ClassDB.can_instantiate(cls):
		var add := Button.new()
		add.text = "+ " + cls
		add.focus_mode = Control.FOCUS_NONE
		add.clip_text = true
		add.pressed.connect(func():
			var a: Array = obj.get(r.name)
			a.append(ClassDB.instantiate(cls))
			_push(obj, r, a))
		head.add_child(add)
	v.add_child(head)
	for i in value.size():
		var item = value[i]
		if item is Object and is_instance_valid(item):
			var line := HBoxContainer.new()
			var open := Button.new()
			open.text = "[%d] %s" % [i, item.get_class()]
			open.focus_mode = Control.FOCUS_NONE
			open.size_flags_horizontal = Control.SIZE_EXPAND_FILL
			open.pressed.connect(func(): _open_child(item, "%s[%d]" % [r.name, i]))
			line.add_child(open)
			var del := Button.new()
			del.text = "x"
			del.focus_mode = Control.FOCUS_NONE
			del.pressed.connect(func():
				var a: Array = obj.get(r.name)
				if i < a.size():
					a.remove_at(i)
					_push(obj, r, a))
			line.add_child(del)
			v.add_child(line)
		elif value.size() <= 8:
			var l := Label.new()
			l.text = "[%d] %s" % [i, str(item)]
			l.add_theme_font_size_override(&"font_size", 11)
			v.add_child(l)


# ------------------------------------------------------------------ values in and out
func _pull(obj: Object, r: Dictionary) -> void:
	if not r.has("editor"):
		return
	var value = obj.get(r.name)
	var e: Control = r.editor
	match r.kind:
		"bool":
			(e as CheckBox).set_pressed_no_signal(bool(value))
		"enum":
			var ob := e as OptionButton
			var idx := ob.get_item_index(int(value))
			if idx >= 0 and ob.selected != idx:
				ob.select(idx)
		"number":
			var s := e as SpinBox
			if not s.get_line_edit().has_focus():
				s.set_value_no_signal(float(value))
		"vec2":
			if not (r.x as SpinBox).get_line_edit().has_focus() and not (r.y as SpinBox).get_line_edit().has_focus():
				(r.x as SpinBox).set_value_no_signal(value.x)
				(r.y as SpinBox).set_value_no_signal(value.y)
		"color":
			(e as ColorPickerButton).color = value
		"text":
			var le := e as LineEdit
			if not le.has_focus():
				le.text = str(value)
		"object":
			var b: Button = r.button
			if value is Object and is_instance_valid(value):
				var path: String = value.resource_path if value is Resource else ""
				b.text = "%s  >" % (path.get_file() if path != "" and not path.contains("::") else value.get_class())
			else:
				b.text = ("+ New " + r.cls) if r.cls != "" and not (r.cls as String).contains(",") and ClassDB.can_instantiate(r.cls) else "(null)"
		"array":
			var a: Array = value if value is Array else []
			var sig := a.size()
			for item in a:
				sig = sig * 31 + (item.get_instance_id() if item is Object else 0)
			if sig != r.size:
				r.size = sig
				_rebuild_array(obj, r, a)
		"readonly":
			var text := str(value)
			(e as Label).text = text.substr(0, 60) + ("..." if text.length() > 60 else "")


func _push(obj: Object, r: Dictionary, value: Variant) -> void:
	if not is_instance_valid(obj):
		return
	var before = obj.get(r.name)
	obj.set(r.name, value)
	var after = obj.get(r.name)
	var accepted := _same(after, value)
	if accepted:
		_status.text = "%s = %s" % [r.name, _short(after)]
	else:
		_status.text = "%s refused %s: kept %s (the reason is in the Output)" % [r.name, _short(value), _short(before)]
		_flash(r.row)
	_pull(obj, r)


func _same(a: Variant, b: Variant) -> bool:
	if typeof(a) == TYPE_FLOAT and typeof(b) in [TYPE_FLOAT, TYPE_INT]:
		return is_equal_approx(a, float(b))
	if typeof(a) == TYPE_VECTOR2 and typeof(b) == TYPE_VECTOR2:
		return a.is_equal_approx(b)
	if typeof(a) == TYPE_ARRAY or typeof(b) == TYPE_ARRAY:
		return true # arrays are written whole; their size is shown
	return a == b


func _short(v: Variant) -> String:
	var s := str(v)
	return s.substr(0, 48) + ("..." if s.length() > 48 else "")


func _flash(c: Control) -> void:
	c.modulate = Color(1.0, 0.45, 0.45)
	var tw := c.create_tween()
	tw.tween_property(c, "modulate", Color.WHITE, 0.8)


# ------------------------------------------------------------------ actions (no-argument methods)
const ACTION_SKIP := ["free", "queue_free", "notify_property_list_changed", "emit_changed", "get_rid",
	"duplicate", "reset_state", "setup_local_to_scene", "get_setup_warnings"]


func _add_actions(obj: Object) -> void:
	var names := PackedStringArray()
	var cls := obj.get_class()
	while cls != "" and not cls in ENGINE_CATEGORIES:
		for m in ClassDB.class_get_method_list(cls, true):
			var n: String = m.name
			if n.begins_with("_") or n in ACTION_SKIP or n in names:
				continue
			if (m.args as Array).size() - (m.default_args as Array).size() > 0:
				continue
			if n.begins_with("set_") or ((n.begins_with("get_") or n.begins_with("is_") or n.begins_with("has_")) and _is_property_getter(obj, n)):
				continue
			names.append(n)
		cls = ClassDB.get_parent_class(cls)
	if names.is_empty():
		return
	names.sort()
	var box := _group_box(obj, "Actions")
	var flow := HFlowContainer.new()
	box.add_child(flow)
	for n in names:
		var b := Button.new()
		b.text = n
		b.focus_mode = Control.FOCUS_NONE
		b.add_theme_font_size_override(&"font_size", 11)
		b.pressed.connect(func():
			if not is_instance_valid(obj):
				return
			var result = obj.call(n)
			_status.text = "%s() -> %s" % [n, str(result).substr(0, 400)])
		flow.add_child(b)
	_rows.append({"name": "actions " + " ".join(names), "row": flow, "group_title": "Actions"})


func _is_property_getter(obj: Object, method: String) -> bool:
	var prop := method.trim_prefix("get_").trim_prefix("is_").trim_prefix("has_")
	return prop in obj or method.trim_prefix("get_") in obj
