extends BlastBenchmark
## Splitting: 500 bullets (2 x 250) expire together and burst into rings of
## 4, whose 2000 children expire 0.5 s later and fan into 2 each (4000
## grandchildren, a two-stage chain). One child volley per parent: 500 and
## then 2000 spawns in one flush. The data has per-bullet speed entries, so
## the children spawn from the cached per-bullet-free template. The first
## wave warms the pools; three measured waves.
func describe() -> String:
	return "500 parents -> 2000 children -> 4000 grandchildren (two-stage split)"

var _data: Array[BulletVolleyData2D] = []
var _chain: BulletSplitData2D


func setup() -> void:
	warmup_frames = 130
	measure_frames = 360
	var ring := BulletSplitStage2D.new()
	ring.count = 4
	ring.direction_mode = BulletSplitStage2D.DIRECTION_RING
	var fan := BulletSplitStage2D.new()
	fan.count = 2
	fan.spread_degrees = 30.0
	_chain = BulletSplitData2D.new()
	_chain.stages = [ring, fan]
	for i in 2:
		_data.append(ring_data(250, Vector2(600 + 900 * i, 500), 60.0, 120.0, 0.5))


func step(frame: int) -> void:
	if frame % 120 == 0:
		for d in _data:
			var v := factory.spawn_volley(d)
			v.split_set_data(_chain, d)
