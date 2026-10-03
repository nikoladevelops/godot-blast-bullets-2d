extends BlastTest
## An expiry signal must survive a same-frame pool reuse of the expired
## volley (a physics spawn after the factory, or a call_deferred spawn queued
## before the flush), fire exactly once with the right volley + indexes, and
## attachments must still be readable inside the handler.

## Runs a callable in _physics_process AFTER the factory (high priority).
class PhysicsWatcher extends Node:
	var cb: Callable
	func _physics_process(_d: float) -> void:
		if cb.is_valid():
			cb.call()

var signals: Array = []
var attach_seen: Array = []
var attach_target: Object = null


func _data() -> BulletVolleyData2D:
	var d := H.make_volley_data(2, 0.0, 0.1)
	d.is_life_time_over_signal_enabled = true
	return d


func _on_lifetime(volley: Object, indexes: Array) -> void:
	signals.append([volley, indexes.duplicate()])
	if volley != attach_target:
		return
	var seen := []
	for i in indexes:
		seen.append((volley as BulletVolley2D).bullet_get_attachment(int(i)) != null)
	attach_seen.append(seen)


func before_each() -> void:
	await super()
	signals.clear()
	attach_seen.clear()
	attach_target = null
	factory.life_time_over.connect(_on_lifetime)


func _hits_for(v: Object) -> int:
	var n := 0
	for s in signals:
		if s[0] == v:
			n += 1
	return n


func test_same_frame_physics_spawn_after_expiry() -> void:
	var respawned: Array = []
	var first: BulletVolley2D = factory.spawn_volley(_data())
	var watcher := PhysicsWatcher.new()
	watcher.process_physics_priority = 1000
	watcher.cb = func():
		if respawned.is_empty() and not first.debug_get_volley_info().get("is_active", true):
			respawned.append(factory.spawn_volley(_data()))
	add(watcher)
	for i in 30:
		await physics()
		if not respawned.is_empty():
			break
	watcher.cb = Callable()
	await idle()
	assert_eq(respawned.size(), 1, "respawn ran in the expiry frame")
	assert_not_null(respawned[0] if not respawned.is_empty() else null, "respawn returned a volley")
	assert_eq(signals.size(), 1, "expiry signal delivered exactly once")
	if signals.size() >= 1:
		assert_eq(signals[0][0], first, "signal names the expired volley")
		assert_eq((signals[0][1] as Array).size(), 2, "both expired indexes reported")


func test_call_deferred_spawn_before_flush() -> void:
	var second: BulletVolley2D = factory.spawn_volley(_data())
	for i in 30:
		await physics()
		if not second.debug_get_volley_info().get("is_active", true):
			break
		# A same-key spawn queued in the expiry frame flushes before the
		# deferred signal.
		factory.call_deferred("spawn_volley", _data())
	await idle()
	assert_eq(_hits_for(second), 1, "deferred respawn does not eat the expiry signal")


func test_attachments_visible_inside_handler() -> void:
	var third: BulletVolley2D = factory.spawn_volley(_data())
	attach_target = third
	third.all_bullets_set_attachment(make_probe_scene(), Vector2.ZERO, true)
	var probe0 = third.bullet_get_attachment(0)
	for i in 30:
		await physics()
		if not attach_seen.is_empty():
			break
	await idle()
	assert_eq(attach_seen, [[true, true]], "handler sees both attachments")
	assert_null(third.bullet_get_attachment(0), "attachments released after the handler")
	assert_not_null(probe0)
	if probe0 != null:
		assert_eq(int(probe0.get("disable_calls")), 1, "exactly one on_bullet_disable per expiry")
		assert_eq(int(probe0.get("enable_calls")), 0, "no enable churn during expiry")
