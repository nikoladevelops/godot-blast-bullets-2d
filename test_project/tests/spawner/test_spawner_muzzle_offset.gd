extends BlastTest
## spawn_position_offset applies to EVERY volley exactly once (it used to be
## applied only on the homing/orbit path, i.e. ignored for plain spawners);
## a zero offset is a no-op.

var sp: BulletSpawner2D
var _volleys: Array = []


func _on_volley(volley: Object, _idx: int) -> void:
	_volleys.append(volley)


func before_each() -> void:
	await super()
	_volleys.clear()
	var d := H.make_directional_data(1, 0.0, 60.0)
	d.collision_shape = H.make_circle_shape(6.0)
	d.bullet_max_collision_count = 0
	sp = make_spawner(d, BulletSpawner2D.PATTERN_FROM_SELF, 1)
	sp.volley_fired.connect(_on_volley)


func _first_origin() -> Vector2:
	return (_volleys[0] as DirectionalBullets2D).get_bullet_transform(0).get_origin()


func test_plain_spawner_applies_offset() -> void:
	sp.spawn_position_offset = Vector2(30, -12)
	assert_true(sp.shoot_once())
	assert_eq(_volleys.size(), 1)
	assert_almost_eq(_first_origin(), sp.global_position + Vector2(30, -12), Vector2(0.5, 0.5), "bullet at spawner + offset")


func test_homing_spawner_shifts_exactly_once() -> void:
	sp.spawn_position_offset = Vector2(30, -12)
	sp.homing_enabled = true
	sp.homing_target_source = BulletSpawner2D.HOMING_SOURCE_GLOBAL_POSITION
	sp.homing_global_position = Vector2(500, 0)
	assert_true(sp.shoot_once())
	assert_almost_eq(_first_origin(), sp.global_position + Vector2(30, -12), Vector2(0.5, 0.5), "single offset under homing")


func test_zero_offset_is_noop() -> void:
	sp.spawn_position_offset = Vector2.ZERO
	assert_true(sp.shoot_once())
	assert_almost_eq(_first_origin(), sp.global_position, Vector2(0.5, 0.5), "bullet on the spawner")
