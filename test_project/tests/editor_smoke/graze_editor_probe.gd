@tool
extends Node
## Editor smoke probe (tools/run_editor_smoke.py): runs INSIDE the headless
## editor on graze_editor_smoke.tscn and walks the editor-only graze paths:
## rings drawn around the edited scene's player, the player deleted (rings
## drop, no crash), a replacement added (rings return), a zone edited, the
## spawner reparented, the Node Name source (rings follow the named nodes,
## one added later included), graze switched off (layer hidden). Prints one
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
	if enemy == null or not is_instance_valid(enemy):
		return
	match frame:
		30:
			_expect(2, Vector2(300, 6), "rings around the player")
			root.get_node("Player").free()
		60:
			_expect(0, Vector2.ZERO, "the deleted player's rings drop")
			var replacement := Node2D.new()
			replacement.name = "Replacement"
			replacement.position = Vector2(100, 0)
			root.add_child(replacement)
			replacement.add_to_group(&"player")
		90:
			_expect(2, Vector2(100, 0), "a replacement target gets rings")
			var zone := enemy.graze_zones[0] as BulletGrazeZone2D
			zone.ring_count = 1
			# A runtime-only switch: the editor keeps drawing through the
			# spawner and the (never ready) factory draws nothing.
			zone.preview_during_runtime = true
		120:
			_expect(1, Vector2(100, 0), "a zone edit redraws (runtime preview flag ignored in the editor)")
			var factory := root.get_node("BulletFactory2D")
			if factory.get_node_or_null("~BlastBulletsGrazeRuntimePreview") != null or factory.debug_get_graze_runtime_preview()["awake"]:
				_fail("the factory runtime preview woke inside the editor")
			var holder := Node2D.new()
			holder.name = "Holder"
			root.add_child(holder)
			enemy.reparent(holder)
		150:
			_expect(1, Vector2(100, 0), "a reparented spawner keeps its rings")
			var named := Node2D.new()
			named.name = "NamedTarget"
			named.position = Vector2(50, 50)
			root.add_child(named)
			enemy.graze_node_name = "Named"
			enemy.graze_target_source = BulletSpawner2D.GRAZE_SOURCE_NODE_NAME
		180:
			_expect(1, Vector2(50, 50), "the Node Name source rings the named node, not the group")
			var later := Node2D.new()
			later.name = "NamedLater"
			later.position = Vector2(-50, 0)
			root.add_child(later)
		210:
			_expect(2, Vector2(50, 50), "a named node added later is ringed too")
			enemy.graze_enabled = false
		240:
			var stats: Dictionary = enemy.debug_get_graze_preview_stats()
			if stats["visible"] or enemy.debug_get_graze_preview_circles().size() != 0:
				_fail("graze off still shows rings: %s" % stats)
			_finish()


func _expect(count: int, center: Vector2, step: String) -> void:
	var circles: Array = enemy.debug_get_graze_preview_circles()
	if circles.size() != count:
		_fail("%s: %d circles, expected %d" % [step, circles.size(), count])
		return
	if count > 0 and not (circles[0]["center"] as Vector2).is_equal_approx(center):
		_fail("%s: centered at %s, expected %s" % [step, circles[0]["center"], center])


func _fail(message: String) -> void:
	failures.append(message)


func _finish() -> void:
	if failures.is_empty():
		print("SMOKE OK")
	else:
		for f in failures:
			print("SMOKE FAIL: " + f)
	get_tree().quit()
