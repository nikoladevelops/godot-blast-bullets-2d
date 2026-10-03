extends BlastBenchmark
## 2,000 bullets with a trail layer + spawn flash, re-fired every second so
## trails, one-shot effects and their shards stay busy.
func describe() -> String:
	return "2k bullets with trail + spawn-flash layers, refired every 60 frames"

var _data: Array[BulletVolleyData2D] = []

func setup() -> void:
	for i in 4:
		var d := ring_data(500, Vector2(500 + 300 * i, 540), 30.0, 180.0, 1.0)
		d.effect_layers = [H.make_effect_layer(BulletEffectLayerData2D.EFFECT_TRAIL_FOLLOW, 4), H.make_effect_layer(BulletEffectLayerData2D.EFFECT_ON_SPAWN, 4)]
		_data.append(d)

func step(frame: int) -> void:
	if frame % 60 == 0:
		for d in _data:
			factory.spawn_volley(d)
