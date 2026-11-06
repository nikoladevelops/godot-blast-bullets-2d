extends CanvasLayer
## The examples' HUD, built in code (no scene to keep in sync):
## - a weapon bar (bottom centre): one slot per weapon with its key, name and
##   an icon of its pattern; the active slot is lit, a banner names a new pick
## - health bars (top centre) for a boss and the player, shown on first use
## - a one-line controls hint (top right)

const FONT := preload("res://shared/ui/fonts/Exo2.ttf")
const ACCENT := Color(0.35, 0.85, 1.0)
const DIM := Color(0.55, 0.62, 0.75, 0.55)
const PANEL := Color(0.06, 0.08, 0.14, 0.78)

var _slots: Array[PanelContainer] = []
var _banner: Label
var _banner_tween: Tween
var _bars := {}


func _init() -> void:
	layer = 10
	name = "HUD"


## Builds the weapon bar for `names` (key 1 = names[0]) and the hint line.
func build(names: Array) -> void:
	var bar := HBoxContainer.new()
	bar.add_theme_constant_override("separation", 10)
	bar.anchor_left = 0.5
	bar.anchor_right = 0.5
	bar.anchor_top = 1.0
	bar.anchor_bottom = 1.0
	bar.grow_horizontal = Control.GROW_DIRECTION_BOTH
	bar.grow_vertical = Control.GROW_DIRECTION_BEGIN
	bar.offset_bottom = -14
	add_child(bar)
	for i in names.size():
		var slot := PanelContainer.new()
		slot.custom_minimum_size = Vector2(108, 70)
		var box := VBoxContainer.new()
		box.alignment = BoxContainer.ALIGNMENT_CENTER
		box.add_theme_constant_override("separation", 2)
		var icon := _WeaponIcon.new()
		icon.kind = i
		icon.custom_minimum_size = Vector2(96, 34)
		box.add_child(icon)
		box.add_child(_label("%d  %s" % [i + 1, names[i]], 14, HORIZONTAL_ALIGNMENT_CENTER))
		slot.add_child(box)
		bar.add_child(slot)
		_slots.append(slot)
	_banner = _label("", 30, HORIZONTAL_ALIGNMENT_CENTER)
	_banner.anchor_left = 0.5
	_banner.anchor_right = 0.5
	_banner.anchor_top = 1.0
	_banner.anchor_bottom = 1.0
	_banner.grow_horizontal = Control.GROW_DIRECTION_BOTH
	_banner.offset_top = -150
	_banner.modulate.a = 0.0
	add_child(_banner)
	# Top right: the scene's own tip text owns the bottom left.
	var hint := _label("WASD / arrows: move    Space / left click: fire    1-4: weapon", 13, HORIZONTAL_ALIGNMENT_RIGHT)
	hint.anchor_left = 1.0
	hint.anchor_right = 1.0
	hint.grow_horizontal = Control.GROW_DIRECTION_BEGIN
	hint.offset_right = -16
	hint.offset_top = 12
	hint.modulate = Color(1, 1, 1, 0.6)
	add_child(hint)


## Lights slot `index`; with `announce` a banner names it for a moment.
func select(index: int, announce := true) -> void:
	for i in _slots.size():
		var on := i == index
		var style := StyleBoxFlat.new()
		style.bg_color = PANEL if not on else Color(0.08, 0.2, 0.3, 0.9)
		style.border_color = ACCENT if on else Color(0.25, 0.3, 0.42, 0.8)
		style.set_border_width_all(3 if on else 1)
		style.set_corner_radius_all(8)
		style.shadow_color = Color(ACCENT, 0.5) if on else Color(0, 0, 0, 0)
		style.shadow_size = 8 if on else 0
		_slots[i].add_theme_stylebox_override("panel", style)
		_slots[i].modulate = Color(1.6, 1.6, 1.6) if on else Color(1, 1, 1, 0.75)
		(_slots[i].get_child(0).get_child(0) as _WeaponIcon).active = on
		_slots[i].get_child(0).get_child(0).queue_redraw()
	if announce and index >= 0 and index < _slots.size():
		var text: String = (_slots[index].get_child(0).get_child(1) as Label).text.substr(3)
		_banner.text = text.to_upper()
		if _banner_tween != null:
			_banner_tween.kill()
		_banner.modulate = Color(1.8, 1.8, 1.8, 1.0)
		_banner_tween = create_tween()
		_banner_tween.tween_interval(0.5)
		_banner_tween.tween_property(_banner, "modulate:a", 0.0, 0.4)


## A health bar (created on first use): `key` names it, `label` titles it.
func set_health(key: String, label: String, value: float, max_value: float, color: Color) -> void:
	if not _bars.has(key):
		var holder := VBoxContainer.new()
		holder.anchor_left = 0.5
		holder.anchor_right = 0.5
		holder.grow_horizontal = Control.GROW_DIRECTION_BOTH
		holder.offset_top = 12 + 46 * _bars.size()
		holder.custom_minimum_size = Vector2(420, 0)
		var title := _label(label, 14, HORIZONTAL_ALIGNMENT_CENTER)
		var bar := ProgressBar.new()
		bar.show_percentage = false
		bar.custom_minimum_size = Vector2(420, 14)
		var bg := StyleBoxFlat.new()
		bg.bg_color = PANEL
		bg.set_corner_radius_all(6)
		bg.border_color = Color(color, 0.6)
		bg.set_border_width_all(1)
		var fill := StyleBoxFlat.new()
		fill.bg_color = color * 1.4
		fill.set_corner_radius_all(6)
		bar.add_theme_stylebox_override("background", bg)
		bar.add_theme_stylebox_override("fill", fill)
		holder.add_child(title)
		holder.add_child(bar)
		add_child(holder)
		_bars[key] = bar
	var bar: ProgressBar = _bars[key]
	bar.max_value = max_value
	create_tween().tween_property(bar, "value", value, 0.15)


func health_value(key: String) -> float:
	return (_bars[key] as ProgressBar).value if _bars.has(key) else -1.0


func _label(text: String, size: int, align: HorizontalAlignment) -> Label:
	var l := Label.new()
	l.text = text
	l.horizontal_alignment = align
	l.add_theme_font_override("font", FONT)
	l.add_theme_font_size_override("font_size", size)
	l.add_theme_constant_override("outline_size", 4)
	l.add_theme_color_override("font_outline_color", Color(0, 0, 0, 0.8))
	return l


## A small drawing of each weapon's pattern (the shot goes up).
class _WeaponIcon extends Control:
	var kind := 0
	var active := false

	func _draw() -> void:
		var c := size / 2.0 + Vector2(0, 6)
		var col := ACCENT if active else DIM
		match kind:
			0: # Rapid: two tight streams
				for x in [-6.0, 6.0]:
					for k in 3:
						draw_circle(c + Vector2(x, -k * 9.0), 2.6, col)
			1: # Spread: a wide fan
				for k in 7:
					var a := -PI / 2 + (k - 3) * 0.17
					draw_circle(c + Vector2.from_angle(a) * 18.0, 2.6, col)
			2: # Homing: curving trails toward a target
				for side in [-1.0, 1.0]:
					var pts := PackedVector2Array()
					for t in 9:
						var f := t / 8.0
						pts.append(c + Vector2(side * 14.0 * (1.0 - f) * (1.0 - f), -f * 22.0))
					draw_polyline(pts, col, 2.0, true)
				draw_circle(c + Vector2(0, -24), 4.0, Color(1.0, 0.45, 0.45, col.a))
			3: # Nova: a ring
				for k in 12:
					draw_circle(c + Vector2(0, -8) + Vector2.from_angle(TAU * k / 12.0) * 13.0, 2.2, col)


var _counter: Label


## A big centred banner for a moment ("PHASE 2", "RAGE!").
func announce(text: String, color := Color(1, 1, 1)) -> void:
	var banner := _label(text, 46, HORIZONTAL_ALIGNMENT_CENTER)
	banner.anchor_left = 0.0
	banner.anchor_right = 1.0
	banner.anchor_top = 0.4
	banner.anchor_bottom = 0.4
	banner.modulate = Color(color.r * 1.8, color.g * 1.8, color.b * 1.8, 0.0)
	add_child(banner)
	var t := create_tween()
	t.tween_property(banner, "modulate:a", 1.0, 0.15)
	t.tween_interval(1.0)
	t.tween_property(banner, "modulate:a", 0.0, 0.5)
	t.tween_callback(banner.queue_free)


## A counter at the top left ("Gems: 12").
func set_counter(text: String) -> void:
	if _counter == null:
		_counter = _label("", 22, HORIZONTAL_ALIGNMENT_LEFT)
		_counter.offset_left = 16
		_counter.offset_top = 12
		add_child(_counter)
	_counter.text = text


## A full-screen end card: title, a line under it and how to restart.
func show_end_screen(title: String, subtitle: String, color: Color) -> void:
	var dim := ColorRect.new()
	dim.color = Color(0.02, 0.03, 0.07, 0.0)
	dim.set_anchors_preset(Control.PRESET_FULL_RECT)
	add_child(dim)
	var box := VBoxContainer.new()
	box.set_anchors_preset(Control.PRESET_CENTER)
	box.grow_horizontal = Control.GROW_DIRECTION_BOTH
	box.grow_vertical = Control.GROW_DIRECTION_BOTH
	box.alignment = BoxContainer.ALIGNMENT_CENTER
	var head := _label(title, 64, HORIZONTAL_ALIGNMENT_CENTER)
	head.modulate = Color(color.r * 1.6, color.g * 1.6, color.b * 1.6)
	box.add_child(head)
	box.add_child(_label(subtitle, 20, HORIZONTAL_ALIGNMENT_CENTER))
	var again := _label("Press R to play again", 16, HORIZONTAL_ALIGNMENT_CENTER)
	again.modulate = Color(1, 1, 1, 0.7)
	box.add_child(again)
	box.modulate.a = 0.0
	add_child(box)
	var t := create_tween().set_parallel()
	t.tween_property(dim, "color:a", 0.7, 0.6)
	t.tween_property(box, "modulate:a", 1.0, 0.6)
