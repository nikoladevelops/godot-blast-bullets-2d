extends Node
## Entry scene for tools/run_benchmarks.py:
##   godot --headless --fixed-fps 60 --path test_project res://benchmarks/bench_main.tscn \
##         -- --scenario=res://benchmarks/scenarios/<name>.gd --out=/abs/result.json
## Writes the scenario's results() as JSON and quits (exit 0 = success).


func _ready() -> void:
	var scenario_path := ""
	var out_path := ""
	for arg in OS.get_cmdline_user_args():
		if arg.begins_with("--scenario="):
			scenario_path = arg.substr(11)
		elif arg.begins_with("--out="):
			out_path = arg.substr(6)
	if scenario_path == "" or out_path == "":
		push_error("bench_main: pass -- --scenario=res://... --out=/abs/path.json")
		get_tree().quit(2)
		return
	var script: Script = load(scenario_path)
	if script == null:
		push_error("bench_main: cannot load " + scenario_path)
		get_tree().quit(2)
		return
	var bench: BlastBenchmark = script.new()
	add_child(bench)
	var result: Dictionary = await bench.run()
	result["scenario"] = scenario_path.get_file().get_basename()
	result["godot"] = Engine.get_version_info().get("string", "")
	result["debug_build"] = OS.is_debug_build()
	result["physics_ticks_per_second"] = Engine.physics_ticks_per_second
	var f := FileAccess.open(out_path, FileAccess.WRITE)
	if f == null:
		push_error("bench_main: cannot write " + out_path)
		get_tree().quit(3)
		return
	f.store_string(JSON.stringify(result, "  "))
	f.close()
	bench.queue_free()
	await get_tree().process_frame
	get_tree().quit(0)
