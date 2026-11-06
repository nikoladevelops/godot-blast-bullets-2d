extends BlastTest
## Slice a of the pairwise feature-mix cover (common/feature_mix.gd): every
## pair of catalog features flies together in some mix (8 features each, plus
## the kitchen sink of everything the user named), cold and on the warm pool,
## and every MixLedger rule, drawn pose, book and pool check holds. A failure
## names its mix; rerun it alone with MIX_ONLY=<seed>:<index>, soak with
## MIX_SEEDS=<n>, bisect with MIX_SKIP=<feature,...>.


func test_mix(mix: Array = use_parameters(FeatureMix.slice(0, 3))) -> void:
	var label := "mix %d:%d %s" % [mix[0], mix[1], "+".join(mix[2])]
	var problems: Array = await FeatureMix.run_twice(self, mix[2], int(mix[0]) * 1000 + int(mix[1]), label)
	assert_eq("\n".join(problems.slice(0, 8)), "", label)
