@tool
extends Node
## Editor smoke probe (tools/run_editor_smoke.py): runs INSIDE the headless
## editor on sound_preview_smoke.tscn and walks the editor-only sound paths:
## preview() starts a playing voice with the mixed volume/pitch through a
## transient player under the tree root (never the edited scene), a second
## call stops the first, preview_sound_effect(index) plays the spawner's entry,
## wrong index / null / streamless entries warn without crashing. Prints one
## "SMOKE OK" line, or "SMOKE FAIL: <step>" lines, then quits the editor.
##
## INERT unless the editor was started by the smoke runner (user argument
## --blast-editor-smoke): opened in anyone's editor (a restored tab, a scene
## thumbnail, a curious click) it touches nothing and never quits.

const RUNNER_FLAG := "--blast-editor-smoke"

var frame := 0
var failures: Array = []
var root: Node
var enemy: BulletSpawner2D
var entry: BulletSoundData2D


func _process(_delta: float) -> void:
	if not Engine.is_editor_hint() or not OS.get_cmdline_user_args().has(RUNNER_FLAG):
		set_process(false)
		return
	frame += 1
	if frame == 1:
		root = get_parent()
		enemy = root.get_node_or_null("Enemy")
		if enemy == null:
			_fail("the Enemy spawner is missing")
			_finish()
			return
		if enemy.sound_effects.is_empty():
			_fail("the Enemy spawner has no sound entries")
			_finish()
			return
		entry = enemy.sound_effects[0] as BulletSoundData2D
		entry.streams = [_silent_loop()]
		entry.min_interval_sec = 0.0
	if enemy == null or not is_instance_valid(enemy):
		return
	match frame:
		30:
			entry.preview()
			var p := _preview_node()
			if p == null:
				_fail("preview() created no transient player")
			elif not p.is_playing():
				_fail("preview() voice is not playing")
			elif not is_equal_approx(p.volume_db, -6.0):
				_fail("preview() volume %f, expected -6" % p.volume_db)
			elif not is_equal_approx(p.pitch_scale, 1.5):
				_fail("preview() pitch %f, expected 1.5" % p.pitch_scale)
			elif p.get_parent() != get_tree().root:
				_fail("preview() player is not under the tree root")
		60:
			var first := _preview_node()
			entry.preview()
			if first != null and is_instance_valid(first):
				_fail("a second preview() left the first voice playing")
			var second := _preview_node()
			if second == null or not second.is_playing():
				_fail("a second preview() is not playing")
		90:
			enemy.preview_sound_effect(0)
			var plays := _preview_node()
			if plays == null or not plays.is_playing():
				_fail("preview_sound_effect(0) is not playing")
		120:
			enemy.sound_effects = [null]
			enemy.preview_sound_effect(0)
			if _preview_node() == null or not _preview_node().is_playing():
				_fail("null entry handling stopped the playing voice")
		150:
			_stop_preview()
		180:
			var streamless := BulletSoundData2D.new()
			enemy.sound_effects = [streamless]
			enemy.preview_sound_effect(0)
			if _preview_node() != null:
				_fail("streamless entry left a preview player behind")
			_finish()


func _preview_node() -> AudioStreamPlayer2D:
	return entry.debug_get_preview_player() as AudioStreamPlayer2D


## Silent looping stream: 8-bit WAV is unsigned, silence is 128, not 0.
func _silent_loop() -> AudioStreamWAV:
	var w := AudioStreamWAV.new()
	w.format = AudioStreamWAV.FORMAT_8_BITS
	w.mix_rate = 8000
	var bytes := PackedByteArray()
	bytes.resize(2000)
	bytes.fill(128)
	w.data = bytes
	w.loop_mode = AudioStreamWAV.LOOP_FORWARD
	w.loop_end = bytes.size()
	return w


func _stop_preview() -> void:
	var p := _preview_node()
	if p != null and is_instance_valid(p):
		p.stop()
		p.queue_free()


func _fail(message: String) -> void:
	failures.append(message)


func _finish() -> void:
	_stop_preview()
	if failures.is_empty():
		print("SMOKE OK")
	else:
		for f in failures:
			print("SMOKE FAIL: " + f)
	get_tree().quit()
