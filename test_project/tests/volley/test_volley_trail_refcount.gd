extends BlastTest
## Trail shard refcount: a shard hides exactly when its last bullet leaves
## (O(1) per disable instead of a rescan). A one-frame trail puts both
## bullets on shard 0, so the ordering below is deterministic.

var v: DirectionalBullets2D


func _bake(vol: DirectionalBullets2D) -> Dictionary:
	var bakes: Array = vol.debug_get_effect_layers_info().get("trail_bakes", [])
	return bakes[0] if not bakes.is_empty() else {}


func before_each() -> void:
	await super()
	var d := H.make_still_data(2)
	d.effect_layers = [H.make_effect_layer(BulletEffectLayerData2D.EFFECT_TRAIL_FOLLOW)]
	v = factory.spawn_controllable_directional_bullets(d)
	await physics(4)


func test_shared_shard_lifecycle() -> void:
	assert_true(v.has_trail_effects(), "trail baked")
	assert_eq(int(_bake(v).get("bullets_tracked", -1)), 2, "both bullets tracked")
	assert_gte(int(_bake(v).get("shards_visible", -1)), 1, "shard visible")
	v.disable_bullet(0)
	await physics()
	assert_eq(int(_bake(v).get("bullets_tracked", -1)), 1, "one bullet tracked")
	assert_gte(int(_bake(v).get("shards_visible", -1)), 1, "shard stays visible for the survivor")
	v.disable_bullet(1)
	await physics()
	assert_eq(int(_bake(v).get("bullets_tracked", -1)), 0, "no bullets tracked")
	assert_eq(int(_bake(v).get("shards_visible", 1)), 0, "shard hidden")
	assert_eq(factory.debug_get_bullets_pool_amount(0), 1, "volley pooled")


func test_wake_retracks_and_reshows() -> void:
	v.disable_bullet(0)
	v.disable_bullet(1)
	await idle(1)
	v.wake_bullet(0)
	v.wake_bullet(1)
	assert_push_warning("woke a pooled volley")
	await physics(4)
	assert_eq(int(_bake(v).get("bullets_tracked", -1)), 2, "both bullets tracked again")
	assert_gte(int(_bake(v).get("shards_visible", -1)), 1, "shard visible again")
