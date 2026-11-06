@tool
extends Node
## Editor smoke probe (tools/run_editor_smoke.py): runs INSIDE the headless
## editor on despawn_preview_smoke.tscn and walks the factory's despawn bounds
## preview: hidden while the bounds are off, drawn in the editor as soon as
## they are on (no runtime flag needed there), follows rect edits, hides when
## disabled or when the preview toggle is off, and is an internal owner-less
## child (never saved). Prints one "SMOKE OK" line, or "SMOKE FAIL: <step>"
## lines, then quits the editor.
##
## INERT unless the editor was started by the smoke runner (user argument
## --blast-editor-smoke).

const RUNNER_FLAG := "--blast-editor-smoke"

var frame := 0
var failures: Array = []
var factory: BulletFactory2D


func _process(_delta: float) -> void:
	if not Engine.is_editor_hint() or not OS.get_cmdline_user_args().has(RUNNER_FLAG):
		set_process(false)
		return
	frame += 1
	if frame == 1:
		factory = get_parent().get_node_or_null("BulletFactory2D")
		if factory == null:
			_fail("the BulletFactory2D is missing")
			_finish()
			return
	if factory == null or not is_instance_valid(factory):
		return
	match frame:
		20:
			if bool(factory.debug_get_despawn_bounds_preview()["visible"]):
				_fail("the preview is visible while the bounds are off")
			factory.despawn_bounds_rect = Rect2(10, 20, 300, 200)
			factory.despawn_bounds_enabled = true
		40:
			var info: Dictionary = factory.debug_get_despawn_bounds_preview()
			if not bool(info["visible"]):
				_fail("the preview is not drawn in the editor with the bounds on")
			elif info["rect"] != Rect2(10, 20, 300, 200):
				_fail("the preview rect is %s" % [info["rect"]])
			elif info["color"] != Color(1, 0, 0, 0.25):
				_fail("the default color is %s, expected translucent red" % [info["color"]])
			elif int(info["draws"]) < 1:
				_fail("the preview was never drawn")
			var layer := factory.get_node_or_null("~BlastBulletsDespawnPreview")
			if layer == null:
				_fail("the preview layer node is missing")
			else:
				if layer.owner != null:
					_fail("the preview layer has an owner (it would be saved)")
				if layer in factory.get_children():
					_fail("the preview layer is a public child")
			factory.despawn_bounds_rect = Rect2(0, 0, 50, 60)
		60:
			if factory.debug_get_despawn_bounds_preview()["rect"] != Rect2(0, 0, 50, 60):
				_fail("a rect edit did not reach the preview")
			factory.despawn_bounds_enabled = false
		80:
			if bool(factory.debug_get_despawn_bounds_preview()["visible"]):
				_fail("disabling the bounds left the preview visible")
			factory.despawn_bounds_enabled = true
		100:
			if not bool(factory.debug_get_despawn_bounds_preview()["visible"]):
				_fail("re-enabling the bounds did not show the preview")
			factory.despawn_bounds_preview = false
		120:
			if bool(factory.debug_get_despawn_bounds_preview()["visible"]):
				_fail("despawn_bounds_preview = false left the preview visible")
			_finish()


func _fail(message: String) -> void:
	failures.append(message)


func _finish() -> void:
	if failures.is_empty():
		print("SMOKE OK")
	else:
		for f in failures:
			print("SMOKE FAIL: " + f)
	get_tree().quit()
