extends BlastTest
## User handlers that FREE things re-entrantly (free(), not queue_free()):
## the plugin must never touch freed memory afterwards. Each test frees the
## object from inside the signal that hands it out, then keeps ticking.
## A regression here is a crash (CRASH status in the runner) or, at best,
## a dangling slot caught by the after_each no-dangling check.

var freed_count := 0


func _attached_data(lifetime := 30.0, max_hits := 1) -> DirectionalBulletsData2D:
	var d := H.make_directional_data(2, 600.0, lifetime)
	d.transforms = [Transform2D(), Transform2D(0.0, Vector2(0, 40))]
	d.monitorable = true
	d.set_collision_mask_from_array([3])
	d.collision_shape = H.make_circle_shape(6.0)
	d.bullet_max_collision_count = max_hits
	return d


func _free_attachment_of(volley: Object, idx: int) -> void:
	var a: Node = (volley as DirectionalBullets2D).bullet_get_attachment(idx)
	if a != null:
		a.free()
		freed_count += 1


var body_hits: Array = []


func _on_body_free_attachment(_body: Object, volley: DirectionalBullets2D, idx: int) -> void:
	body_hits.append(idx)
	_free_attachment_of(volley, idx)


func _on_lifetime_free_attachments(volley: Object, indexes: Array) -> void:
	for i in indexes:
		_free_attachment_of(volley, int(i))


func before_each() -> void:
	await super()
	freed_count = 0
	body_hits.clear()


func test_free_attachment_inside_body_entered_killing_hit() -> void:
	# S1: the post-signal disable path used to call get_instance_id() on the
	# captured attachment pointer AFTER the handler freed it (use-after-free).
	make_wall(Vector2(200, 0))
	await physics()
	factory.directional_body_entered.connect(_on_body_free_attachment)
	var v: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(_attached_data())
	v.all_bullets_set_attachment(make_probe_scene(), Vector2.ZERO, true)
	for i in 60:
		await physics()
		if freed_count >= 2:
			break
	assert_eq(body_hits, [0, 1], "one body_entered per bullet")
	assert_eq(freed_count, 2, "both attachments freed inside the killing-hit handler")
	assert_null(v.bullet_get_attachment(0), "slot 0 dropped (no dangling pointer)")
	assert_null(v.bullet_get_attachment(1), "slot 1 dropped (no dangling pointer)")
	await physics(5)
	assert_true(factory.debug_assert_no_dangling().get("ok", false), "factory consistent after re-entrant frees")


func test_free_attachment_inside_body_entered_non_killing_hit() -> void:
	# Same, but the bullet survives the hit (infinite collisions): the slot
	# must be dropped and the bullet keeps flying with no attachment.
	make_wall(Vector2(200, 0))
	await physics()
	factory.directional_body_entered.connect(_on_body_free_attachment)
	var v: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(_attached_data(30.0, 0))
	v.all_bullets_set_attachment(make_probe_scene(), Vector2.ZERO, true)
	for i in 60:
		await physics()
		if freed_count >= 2:
			break
	assert_eq(freed_count, 2, "both attachments freed inside the non-killing handler")
	assert_true(v.is_bullet_status_enabled(0), "bullet survives the hit")
	assert_null(v.bullet_get_attachment(0), "slot dropped")
	await physics(5)


func test_free_attachment_inside_life_time_over() -> void:
	# S2: expiry queued a deferred attachment-disable carrying the RAW
	# attachment pointer; the life_time_over handler (flushed first) frees it,
	# then the deferred call's argument conversion touched freed memory.
	factory.directional_life_time_over.connect(_on_lifetime_free_attachments)
	var d := _attached_data(0.1)
	d.is_life_time_over_signal_enabled = true
	var v: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d)
	v.all_bullets_set_attachment(make_probe_scene(), Vector2.ZERO, true)
	for i in 30:
		await physics()
		if freed_count >= 2:
			break
	await idle(2)
	assert_eq(freed_count, 2, "both attachments freed inside life_time_over")
	await physics(5)
	await idle(2)
	assert_true(factory.debug_assert_no_dangling().get("ok", false), "factory consistent after expiry frees")



var seen_attached: Array = []


func _on_body_probe(_body: Object, volley: DirectionalBullets2D, idx: int) -> void:
	seen_attached.append([idx, volley.bullet_get_attachment(idx) != null])


func test_last_bullet_killing_hit_handler_sees_its_attachment() -> void:
	# S4: a killing blow on the volley's LAST live bullet deactivates the
	# volley; its reset used to release the guarded slot BEFORE the signal,
	# so only the final bullet's handler saw a null attachment.
	seen_attached.clear()
	make_wall(Vector2(200, 0))
	await physics()
	factory.directional_body_entered.connect(_on_body_probe)
	var v: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(_attached_data())
	v.all_bullets_set_attachment(make_probe_scene(), Vector2.ZERO, true)
	for i in 60:
		await physics()
		if seen_attached.size() >= 2:
			break
	assert_eq(seen_attached, [[0, true], [1, true]], "every killing-hit handler sees its own attachment")
	await idle(2)
	assert_null(v.bullet_get_attachment(0), "slot 0 released after its handler")
	assert_null(v.bullet_get_attachment(1), "slot 1 released after its handler")
	assert_eq(factory.debug_get_active_attachments_amount(), 0, "no attachment left active")
	assert_eq(factory.debug_get_attachments_pool_amount(), 2, "both attachments parked in the pool")
