extends BlastTest
## set_gravity() fills genuine gaps only (presence bits): authored per-bullet
## gravity (bullet_set_gravity or seeded) survives a shared write.


func _data() -> DirectionalBulletsData2D:
	var d := H.make_still_data(2)
	d.gravity = Vector2(0, 100)
	return d


func test_authored_slot_survives_gap_fills() -> void:
	var v: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(_data())
	v.bullet_set_gravity(0, Vector2(50, 0))
	v.set_gravity(Vector2(0, 300))
	assert_eq(v.bullet_get_gravity(0), Vector2(50, 0), "authored gravity kept")
	assert_eq(v.bullet_get_gravity(1), Vector2(0, 300), "gap filled by shared")
	await physics(5)
	assert_true(v.get_bullet_velocity(0).is_finite() and v.get_bullet_velocity(1).is_finite(), "velocities finite under gravity")


func test_seeded_entries_claim_presence() -> void:
	var d := _data()
	d.all_bullet_gravity = [Vector2(10, 0), Vector2(20, 0)]
	var v: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d)
	v.set_gravity(Vector2(0, 300))
	assert_eq(v.bullet_get_gravity(0), Vector2(10, 0), "seeded slot 0 kept")
	assert_eq(v.bullet_get_gravity(1), Vector2(20, 0), "seeded slot 1 kept")
