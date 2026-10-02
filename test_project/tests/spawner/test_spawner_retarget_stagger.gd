extends BlastTest
## Retarget stagger: unrelated homing knob changes must not re-arm a running
## retarget countdown (N spawners would re-sync their scene scans).


func test_unrelated_knob_keeps_countdown() -> void:
	var d := H.make_directional_data(1, 0.0, 60.0)
	var sp := make_spawner(d, BulletSpawner2D.PATTERN_FROM_HELPER_RING, 1)
	sp.homing_enabled = true
	sp.homing_target_source = BulletSpawner2D.HOMING_SOURCE_GLOBAL_POSITION
	sp.homing_global_position = Vector2(500, 0)
	sp.homing_retarget_mode = 1 # ON_INTERVAL
	sp.homing_retarget_interval_sec = 10.0
	sp.homing_retarget_phase = 5.0
	await idle(1)
	await physics(120)
	var before: float = sp.debug_get_retarget_countdown()
	assert_between(before, 0.5, 4.5, "countdown running down")
	sp.homing_smoothing = 3.0
	await idle(10)
	assert_lt(sp.debug_get_retarget_countdown(), 4.9, "countdown not re-armed to the phase")
	sp.homing_enabled = false
	await idle(1)
	assert_false(sp.homing_enabled, "disable accepted")
