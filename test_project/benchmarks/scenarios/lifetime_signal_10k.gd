extends BlastBenchmark
## 10,000 bullets (10 x 1000) expiring on the same frame with life_time_over
## on: each volley's handler runs live inside the tick and reads every listed
## bullet's custom data while it is still alive, then the plugin kills them.
## Three waves; compare with mass_expiry_10k (same burst, no signal).
var _data: Array[BulletVolleyData2D] = []
var _listed := 0
var _calls := 0
var _tag := Resource.new()

func describe() -> String:
	return "10k bullets expiring on one frame with a live life_time_over handler per volley (3 waves)"

func setup() -> void:
	warmup_frames = 10
	measure_frames = 240
	var customs: Array = []
	for i in 1000:
		customs.append(_tag)
	for i in 10:
		var d := ring_data(1000, Vector2(960 * (i % 5), 1200 * (i / 5)), 40.0, 50.0, 1.0)
		d.is_life_time_over_signal_enabled = true
		d.all_bullets_custom_data = customs
		_data.append(d)
	# Pre-warm like mass_expiry_10k: measure the expiry burst, not cold allocation.
	for d in _data:
		factory.populate_bullets_pool(BulletFactory2D.debug_expected_pool_key(d), d, 1)
	factory.life_time_over.connect(_on_expired)

func _on_expired(v: BulletVolley2D, indexes: Array) -> void:
	_calls += 1
	for i in indexes:
		if v.bullet_get_custom_data(i) == _tag:
			_listed += 1

func step(frame: int) -> void:
	if frame % 80 == 10:
		for d in _data:
			factory.spawn_volley(d)


func results() -> Dictionary:
	var r := super()
	extra["handler_calls"] = _calls
	extra["bullets_listed"] = _listed
	if _listed < 20000:
		push_error("lifetime_signal_10k: only %d bullets reached the handler - the waves did not expire" % _listed)
	return r
