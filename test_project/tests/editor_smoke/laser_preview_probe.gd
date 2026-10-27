@tool
extends Node
## Editor smoke probe (tools/run_editor_smoke.py): runs INSIDE the headless
## editor on laser_preview_smoke.tscn and walks the editor-only LaserSpawner2D
## preview: one arrow per slot drawn at the laser's length, a length edit
## redraws, the editor-only LaserData2D `preset` property redraws, a pattern
## change redraws, a reparented spawner keeps its preview, an empty
## laser_data falls back to the plain facing arrows, a spawner removed from
## the scene takes its preview off an external generator (it used to stay
## drawn there). Prints one "SMOKE OK"
## line, or "SMOKE FAIL: <step>" lines, then quits the editor.
##
## INERT unless the editor was started by the smoke runner (user argument
## --blast-editor-smoke): opened in anyone's editor it touches nothing and
## never quits.

const RUNNER_FLAG := "--blast-editor-smoke"

var frame := 0
var failures: Array = []
var root: Node
var lasers: LaserSpawner2D


func _process(_delta: float) -> void:
	if not Engine.is_editor_hint() or not OS.get_cmdline_user_args().has(RUNNER_FLAG):
		set_process(false)
		return
	frame += 1
	if frame == 1:
		root = get_parent()
		lasers = root.get_node_or_null("Lasers")
		if lasers == null:
			_fail("the Lasers spawner is missing")
			_finish()
			return
	if lasers == null or not is_instance_valid(lasers):
		return
	match frame:
		30:
			_expect(6, 300.0, "one arrow per slot at the laser's length")
			lasers.laser_data.length = 450.0
		60:
			_expect(6, 450.0, "a length edit redraws")
			lasers.laser_data.set("preset", LaserData2D.PRESET_WALL_BEAM)
		90:
			_expect(6, 1200.0, "the editor preset property redraws")
			lasers.helper_bullets_amount = 3
		120:
			_expect(3, 1200.0, "a pattern change redraws")
			var holder := Node2D.new()
			holder.name = "Holder"
			root.add_child(holder)
			lasers.reparent(holder)
		150:
			_expect(3, 1200.0, "a reparented spawner keeps its preview")
			lasers.laser_data = null
		180:
			_expect(3, lasers.preview_arrow_length, "no laser_data: the plain facing arrows")
			var gen := Node2D.new()
			gen.name = "Gen"
			gen.position = Vector2(400, 300)
			root.add_child(gen)
			lasers.set_transforms_generator(gen)
		210:
			var gen := root.get_node("Gen")
			if _preview_holders(gen) != 1:
				_fail("the preview did not move under the external generator")
			lasers.get_parent().remove_child(lasers)
		240:
			if _preview_holders(root.get_node("Gen")) != 0:
				_fail("a spawner removed from the scene left its preview under the generator")
			lasers.free()
			_finish()


func _preview_holders(parent: Node) -> int:
	var n := 0
	for c in parent.get_children(true):
		if c.has_meta("_blast_preview_owner") and c.visible and not c.is_queued_for_deletion():
			n += 1
	return n


func _expect(count: int, length: float, step: String) -> void:
	var arrows: Dictionary = lasers.debug_get_preview_arrows()
	var tails: PackedVector2Array = arrows["tails"]
	if tails.size() != count:
		_fail("%s: %d arrows, expected %d" % [step, tails.size(), count])
		return
	# A laser's beam: its band runs the laser's length from the muzzle; plain
	# facing arrows have no band.
	var drawn := float(arrows["beam_length"]) if float(arrows["beam_width"]) > 0.0 else float(arrows["length"])
	if not is_equal_approx(drawn, length):
		_fail("%s: drawn length %s, expected %s" % [step, drawn, length])


func _fail(message: String) -> void:
	failures.append(message)


func _finish() -> void:
	if failures.is_empty():
		print("SMOKE OK")
	else:
		for f in failures:
			print("SMOKE FAIL: " + f)
	get_tree().quit()
