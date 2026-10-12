class_name AttachmentTimeProbe2D
extends BulletAttachment2D
## Fixture for the time-scale suites: children of every kind the plugin
## scales (particles, animated sprite, animation player) with NON-trivial
## original speed_scale values, and a log of on_time_scale_changed calls.

var scale_calls: Array = []

func on_time_scale_changed(scale: float) -> void:
	scale_calls.append(scale)

func gpu() -> GPUParticles2D:
	return get_node("Gpu") as GPUParticles2D

func cpu() -> CPUParticles2D:
	return get_node("Cpu") as CPUParticles2D

func sprite() -> AnimatedSprite2D:
	return get_node("Sprite") as AnimatedSprite2D

func player() -> AnimationPlayer:
	return get_node("Player") as AnimationPlayer

func speeds() -> Array:
	return [gpu().speed_scale, cpu().speed_scale, sprite().speed_scale, player().speed_scale]

## PackedScene of this fixture: Gpu 2.0, Cpu 1.5, Sprite 0.5, Player 3.0 and a
## nested child (Deep/Gpu2, 4.0) to prove the walk is recursive.
static func make_scene() -> PackedScene:
	var root := AttachmentTimeProbe2D.new()
	var specs := [["Gpu", GPUParticles2D.new(), 2.0], ["Cpu", CPUParticles2D.new(), 1.5], ["Sprite", AnimatedSprite2D.new(), 0.5], ["Player", AnimationPlayer.new(), 3.0]]
	for s in specs:
		var n: Node = s[1]
		n.name = s[0]
		n.speed_scale = s[2]
		root.add_child(n)
		n.owner = root
	var deep := Node2D.new()
	deep.name = "Deep"
	root.add_child(deep)
	deep.owner = root
	var gpu2 := GPUParticles2D.new()
	gpu2.name = "Gpu2"
	gpu2.speed_scale = 4.0
	deep.add_child(gpu2)
	gpu2.owner = root
	var ps := PackedScene.new()
	ps.pack(root)
	root.free()
	return ps
