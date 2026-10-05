extends BlastTest
## reset_finished: emitted exactly once per accepted reset (full, scoped and
## deferred), never for a rejected one, after every volley is gone. A handler
## may respawn right away (the "next wave" pattern): its volley spawns, ticks
## and is not wiped by the reset that announced itself.


func test_every_accepted_reset_emits_once_and_a_rejected_one_never() -> void:
	watch_signals(factory)
	factory.spawn_volley(H.make_still_data(2))
	factory.reset()
	assert_signal_emit_count(factory, "reset_finished", 1, "a full reset")
	assert_eq(factory.get_active_bullet_count(), 0, "every bullet gone when it fires")
	var scoped := H.make_still_data(3)
	factory.spawn_volley(scoped)
	factory.reset(BulletFactory2D.debug_expected_pool_key(scoped))
	assert_signal_emit_count(factory, "reset_finished", 2, "a scoped reset")
	await physics()
	factory.reset()
	expect_errors_containing("reset", 1, "structural calls are rejected inside the physics step")
	assert_signal_emit_count(factory, "reset_finished", 2, "a rejected reset emits nothing")
	await idle(1)
	factory.reset_deferred()
	assert_signal_emit_count(factory, "reset_finished", 2, "deferred: not yet")
	for i in 5:
		await idle(1)
		if get_signal_emit_count(factory, "reset_finished") > 2:
			break
	assert_signal_emit_count(factory, "reset_finished", 3, "the deferred reset emits once")


func test_a_reset_finished_handler_can_spawn_the_next_wave() -> void:
	var next: Array = []
	factory.spawn_volley(H.make_volley_data(2, 60.0))
	factory.reset_finished.connect(func(): next.append(factory.spawn_volley(H.make_volley_data(2, 60.0))))
	factory.reset()
	assert_eq(next.size(), 1, "the handler ran once")
	assert_not_null(next[0], "the next wave spawned inside the handler")
	assert_eq(factory.get_active_bullet_count(), 2, "and is alive after the reset returns")
	await idle(3)
	assert_almost_eq((next[0] as BulletVolley2D).get_curves_elapsed_time(), 3.0 / 60.0, 1e-6, "and ticks normally")
