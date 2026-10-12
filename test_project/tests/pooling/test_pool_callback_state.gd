extends BlastTest
## Spawn-time attachment callbacks see the same volley state on a cold spawn
## and on a pooled reuse: the bullets already report enabled (a disable_bullet
## from the callback must take effect on both paths).


func _probe_scene() -> PackedScene:
	var probe: Node = AttachmentStatusProbe2D.new()
	var ps := PackedScene.new()
	assert_eq(ps.pack(probe), OK, "probe scene packs")
	probe.free()
	return ps


func before_each() -> void:
	await super()
	AttachmentStatusProbe2D.volley = null
	AttachmentStatusProbe2D.spawn_seen.clear()
	AttachmentStatusProbe2D.enable_seen.clear()


func test_enable_callback_on_pooled_reuse_sees_enabled_bullets() -> void:
	var data := H.make_volley_data(2, 0.0, 30.0)
	data.shared_bullet_attachment = _probe_scene()
	var cold: BulletVolley2D = factory.spawn_volley(data)
	await idle(1)
	factory.clear_active_bullets()
	await idle(1)
	# The pooled volley IS this instance: point the probe at it before the
	# respawn, because the callbacks run inside spawn_volley.
	AttachmentStatusProbe2D.volley = cold
	var reused: BulletVolley2D = factory.spawn_volley(data)
	assert_same(reused, cold, "the pooled volley was reused")
	assert_eq(AttachmentStatusProbe2D.enable_seen.size(), 2, "one on_bullet_enable per pooled attachment")
	assert_eq(str(AttachmentStatusProbe2D.enable_seen), "[true, true]", "bullet 0 already reads enabled inside the callback")
