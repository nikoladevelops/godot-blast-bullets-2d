extends BlastBenchmark
## 2,000 bullets ricocheting forever inside a closed box of bounce walls.
func describe() -> String:
	return "2k bullets bouncing inside a box (bounce resolution every hit)"

func setup() -> void:
	var c := Vector2(960, 540)
	make_static_box(c + Vector2(0, -500), Vector2(1800, 40), 8)
	make_static_box(c + Vector2(0, 500), Vector2(1800, 40), 8)
	make_static_box(c + Vector2(-900, 0), Vector2(40, 1040), 8)
	make_static_box(c + Vector2(900, 0), Vector2(40, 1040), 8)
	await get_tree().physics_frame
	for v in 10:
		var d := ring_data(200, c + Vector2(-600 + 130 * v, 0), 40.0, 400.0, 1000.0)
		d.set_collision_mask_from_array([4])
		d.set_bounce_mask_from_array([4])
		d.bullet_max_collision_count = 0
		factory.spawn_controllable_directional_bullets(d)
