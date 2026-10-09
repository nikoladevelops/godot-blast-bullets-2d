extends BlastBenchmark
## 10,000 bullets in flight, each with a followed On Flight hum (one voice per
## bullet, pooled by the interval). Exercises the per-bullet flight offers, the
## follow-dedup check and the mixer flush over the sweep's winners.
var _sound: BulletSoundData2D


func describe() -> String:
	return "10k bullets in flight, On Flight hum following every bullet"


func setup() -> void:
	_sound = H.make_sound(BulletSoundData2D.SOUND_ON_FLIGHT, 8)
	_sound.min_interval_sec = 0.05
	_sound.follow_bullet = true
	_sound.fade_out_sec = 0.1
	for i in 10:
		var v: BulletVolley2D = factory.spawn_volley(ring_data(1000, Vector2(960 * (i % 5), 1200 * (i / 5)), 50.0, 60.0, 1000.0))
		v.sound_set_effects([_sound])
