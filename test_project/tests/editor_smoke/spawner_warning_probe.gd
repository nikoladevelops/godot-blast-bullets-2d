@tool
extends Node
## Editor smoke probe (tools/run_editor_smoke.py): runs INSIDE the headless
## editor on spawner_warning_smoke.tscn and checks that the scene dock's warning
## triangle follows edits made INSIDE a spawner's resources, not only its own
## setters: an empty spawn_data warns "no sprite_frames" until a sprite is given
## to that same resource, an empty split_data until it gets a stage, a laser_data
## aiming with Track without a target source. Each edit must refresh the node's
## configuration warnings (SceneTree.node_configuration_warning_changed), or the
## editor keeps showing a warning that no longer applies. Prints "SMOKE OK", or
## "SMOKE FAIL: <step>" lines, then quits the editor.
##
## INERT unless the editor was started by the smoke runner (user argument
## --blast-editor-smoke).

const RUNNER_FLAG := "--blast-editor-smoke"

var frame := 0
var failures: Array = []
var refreshed := {}  # node -> times its warnings were refreshed
var bullets: BulletSpawner2D
var lasers: LaserSpawner2D


func _process(_delta: float) -> void:
	if not Engine.is_editor_hint() or not OS.get_cmdline_user_args().has(RUNNER_FLAG):
		set_process(false)
		return
	frame += 1
	match frame:
		1:
			bullets = get_parent().get_node_or_null("Bullets")
			lasers = get_parent().get_node_or_null("Lasers")
			if bullets == null or lasers == null:
				_fail("the smoke scene lost its spawners")
				_finish()
				return
			get_tree().node_configuration_warning_changed.connect(func(n): refreshed[n] = refreshed.get(n, 0) + 1)
			bullets.spawn_data = BulletVolleyData2D.new()
			bullets.split_enabled = true
			bullets.split_data = BulletSplitData2D.new()
			lasers.laser_data = LaserData2D.new()
		10:
			_expect_warning(bullets, "no sprite_frames", true, "an empty spawn_data")
			_expect_warning(bullets, "split_data has no stages", true, "an empty split_data")
			refreshed.clear()
			bullets.spawn_data.sprite_frames = SpriteFrames.new()
		20:
			_expect_warning(bullets, "no sprite_frames", false, "spawn_data with a sprite")
			_expect_refresh(bullets, "giving spawn_data a sprite_frames")
			refreshed.clear()
			bullets.split_data.stages = [BulletSplitStage2D.new()]
		30:
			_expect_warning(bullets, "split_data has no stages", false, "split_data with a stage")
			_expect_refresh(bullets, "adding a stage to split_data")
			refreshed.clear()
			lasers.laser_data.aim = LaserData2D.LASER_AIM_TRACK
		40:
			_expect_warning(lasers, "aims with Track or Lock On", true, "a Track laser_data without targets")
			_expect_refresh(lasers, "setting laser_data.aim")
			_finish()


func _expect_warning(spawner: Node, fragment: String, present: bool, what: String) -> void:
	var found := false
	for w in spawner.get_setup_warnings():
		found = found or fragment in w
	if found != present:
		_fail("%s: warning \"%s\" %s" % [what, fragment, "missing" if present else "still listed"])


func _expect_refresh(spawner: Node, what: String) -> void:
	if int(refreshed.get(spawner, 0)) < 1:
		_fail("%s did not refresh %s's editor warnings (the triangle goes stale)" % [what, spawner.name])


func _fail(message: String) -> void:
	failures.append(message)


func _finish() -> void:
	if failures.is_empty():
		print("SMOKE OK")
	else:
		for f in failures:
			print("SMOKE FAIL: " + f)
	get_tree().quit()
