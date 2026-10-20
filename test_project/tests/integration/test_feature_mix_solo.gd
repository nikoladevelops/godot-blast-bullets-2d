extends BlastTest
## Every feature of the feature-mix catalog (common/feature_mix.gd) alone,
## through the real spawner path: it visibly does what it is for (the
## evidence), every MixLedger rule holds, what is drawn matches the bullets,
## every bullet leaves and every volley is pooled, parked or freed, and a
## second run on the warm pool allocates nothing new and ends every bullet
## the same way.


func _solos() -> Array:
	var out: Array = []
	for f in FeatureMix.FEATURES:
		out.append(FeatureMix._with_needs([f]))
	return out


func test_each_feature_alone_works_and_balances(features: Array = use_parameters(_solos())) -> void:
	var problems: Array = await FeatureMix.run_twice(self, features, 1, "", true)
	assert_eq("\n".join(problems.slice(0, 8)), "", "+".join(features))
