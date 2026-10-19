extends BlastTest
## Three spawners share ONE spawn data, flock, graze zone, sound entry and
## split chain on one factory and one pool. One is freed mid-flight (Hand To
## Factory), one is reparented, one is frozen (time_scale 0). Each keeps its
## own routing (its splits and hits on itself; the orphan's on the factory),
## its own live-bullet count, its frozen bullets stay put, and a volley the
## pool hands from one spawner to another carries nothing of the old owner.

const DT := 1.0 / 60.0
const GROUP := &"shared_targets"

var data: BulletVolleyData2D
var flock: BulletFlock2D
var zone: BulletGrazeZone2D
var sound: BulletSoundData2D
var chain: BulletSplitData2D
var reports := {} # emitter name -> split reports


func before_each() -> void:
	await super()
	reports = {"a": 0, "b": 0, "c": 0, "factory": 0}
	data = H.make_volley_data(6, 80.0, 0.25)
	data.shared_bullet_speed_data = data.all_bullet_speed_data[0]
	data.all_bullet_speed_data = []
	flock = H.make_flock(2.0, 1.0, 1.0)
	zone = H.make_graze_zone([40.0])
	sound = H.make_sound(BulletSoundData2D.SOUND_ON_SPLIT)
	var stage := BulletSplitStage2D.new()
	stage.count = 2
	stage.child_data = H.make_volley_data(2, 40.0, 30.0)
	chain = BulletSplitData2D.new()
	chain.stages = [stage]
	make_graze_target(Vector2(0, 300), GROUP)
	factory.bullet_split.connect(func(_p, _c, _ch, _d) -> void: reports["factory"] += 1)
	factory.bullet_grazed.connect(func(_t, _v, _i, _z, _r) -> void: pass)


func _spawner(name: String, at: Vector2) -> BulletSpawner2D:
	var sp := make_spawner(data, BulletSpawner2D.PATTERN_FROM_HELPER_RING, 6)
	sp.name = name
	sp.global_position = at
	sp.flock_enabled = true
	sp.flock = flock
	sp.graze_enabled = true
	sp.graze_zones = [zone]
	sp.graze_node_group = GROUP
	sp.sound_enabled = true
	sp.sound_effects = [sound]
	sp.split_enabled = true
	sp.split_data = chain
	sp.bullet_split.connect(func(_p, _c, _ch, _d) -> void: reports[name] += 1)
	return sp


func _fire(sp: BulletSpawner2D) -> BulletVolley2D:
	var got: Array = []
	sp.volley_fired.connect(func(v: BulletVolley2D, _n: int) -> void: got.append(v), CONNECT_ONE_SHOT)
	assert_true(sp.shoot_once(), "%s fires" % sp.name)
	return got[0] if got.size() > 0 else null


func test_shared_resources_keep_every_spawner_apart() -> void:
	var a := _spawner("a", Vector2(-300, 0))
	var b := _spawner("b", Vector2(0, 0))
	var c := _spawner("c", Vector2(300, 0))
	a.orphaned_volleys = BulletSpawner2D.ORPHANED_VOLLEYS_HAND_TO_FACTORY
	c.time_scale = 0.0
	await idle(1)
	_fire(a)
	_fire(b)
	var frozen := _fire(c)
	var frozen_at: Array = origins(frozen)
	a.free() # mid-flight: its volley is the factory's now
	var other_parent := Node2D.new()
	add(other_parent)
	b.get_parent().remove_child(b)
	other_parent.add_child(b) # reparented: keeps its volleys and settings
	for i in 30: # the 0.25 s lifetime runs out, every live parent splits
		factory.debug_advance_time(DT)
	assert_eq(reports["b"], 6, "b's six parents split, reported on b")
	assert_eq(reports["factory"], 6, "the orphan's six splits are the factory's")
	assert_eq(reports["c"], 0, "c's frozen bullets never aged")
	assert_eq(origins(frozen), frozen_at, "c's bullets stayed put")
	assert_eq(c.get_active_live_bullet_count(), 6, "c counts its own six")
	assert_eq(b.get_active_live_bullet_count(), 12, "b counts its own children (6 x 2)")
	assert_true(factory.debug_assert_no_dangling()["ok"], "balanced books")
	# Pool hand-over: b's children (2-bullet volleys) expire into the pool...
	factory.clear_active_bullets()
	await idle(1)
	c.time_scale = 1.0
	var d2 := H.make_volley_data(2, 0.0, 30.0)
	var reused: BulletVolley2D = factory.spawn_volley(d2)
	assert_true(reused.debug_get_life_state() == "active" and reused.get_life_id() > 1, "the factory reused one of b's pooled children")
	assert_eq(int(reused.debug_get_volley_info()["owner_spawner_id"]), 0, "it belongs to the factory now")
	assert_null(reused.flock_get(), "no flock of b's life")
	assert_null(reused.split_get_data(), "no chain of b's life")
	assert_eq(reused.sound_get_effects().size(), 0, "no sound of b's life")
	assert_eq(reused.debug_check_invariants(), "", "a plain volley")
	expect_no_errors()
