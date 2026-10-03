extends BlastTest
## Documented setter/signal split: with auto-fire configured, the shot that
## trips max_volleys emits shooting_finished exactly once; set_max_volleys()
## only reports start/stop transitions (lowering onto the count -> stopped,
## never a second finished). Manual-only spawners never report a finish
## (test_spawner_cadence).


func test_cap_trip_by_shot_emits_finished_once() -> void:
	var sp := make_spawner(H.make_directional_data(1, 0.0, 60.0), BulletSpawner2D.PATTERN_FROM_SELF, 1)
	sp.max_volleys = 2
	sp.set_shooting_enabled(true) # auto-fire configured: the cap trip is a finish
	watch_signals(sp)
	assert_true(sp.shoot_once())
	assert_true(sp.shoot_once())
	assert_signal_emit_count(sp, "shooting_finished", 1, "finished exactly once")
	assert_true(sp.shoot_once(), "manual shot past the cap still fires")
	assert_signal_emit_count(sp, "shooting_finished", 1, "no second finish past the cap")


func test_lowering_then_raising_the_cap() -> void:
	var sp := make_spawner(H.make_directional_data(1, 0.0, 60.0), BulletSpawner2D.PATTERN_FROM_SELF, 1)
	sp.max_volleys = -1
	sp.set_shooting_enabled(true)
	assert_true(sp.shoot_once())
	assert_true(sp.shoot_once())
	watch_signals(sp)
	sp.max_volleys = sp.get_volleys_fired()
	assert_signal_emit_count(sp, "shooting_stopped", 1, "stopped on the crossing")
	assert_signal_emit_count(sp, "shooting_finished", 0, "no finish from the setter")
	assert_false(sp.is_shooting_active(), "inactive at the lowered cap")
	sp.max_volleys = sp.get_volleys_fired() + 3
	assert_signal_emit_count(sp, "shooting_started", 1, "started on raising")
	assert_true(sp.is_shooting_active(), "active again")
