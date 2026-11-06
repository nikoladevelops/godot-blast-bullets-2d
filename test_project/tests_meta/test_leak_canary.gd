extends GutTest
## Runner self-test only (tools/run_tests.py --self-test): deliberately leaks
## an Object, a Node outside the tree, a Resource and a physics RID so the
## --verbose leak detector proves it fires. Never part of the normal run.

var _keep_alive: Array = []

func test_leaks_on_purpose() -> void:
	var obj := Object.new()
	var orphan := Node2D.new()
	var res := Resource.new()
	_keep_alive.append(res)
	PhysicsServer2D.area_create()
	assert_true(obj != null and orphan != null, "canary leaked objects")
