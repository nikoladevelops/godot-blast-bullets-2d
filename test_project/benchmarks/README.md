# BlastBullets2D benchmarks

Headless, deterministic performance scenarios with a regression log.

```sh
python3 tools/run_benchmarks.py                    # all scenarios, 3 repeats (median)
python3 tools/run_benchmarks.py --scenario churn   # substring filter
python3 tools/run_benchmarks.py --gate             # exit 1 on a regression vs log/baseline.json
python3 tools/run_benchmarks.py --update-baseline  # accept this run as the new reference
```

- `scenarios/*.gd` extend `BlastBenchmark` (`blast_benchmark.gd`): `setup()`
  builds the scene, `step(frame)` runs per frame, `extra` holds scenario
  numbers. `bench_main.tscn` loads one scenario per Godot process.
- Time is simulated (`--fixed-fps 60`): a frame's wall time is its CPU cost.
  `frame_ms` = whole engine frame, `factory_tick_ms` = BulletFactory2D
  physics tick only (from `get_frame_stats()`).
- `log/LATEST.md` — this machine's latest run vs `log/baseline.json` (regressions
  flagged). `log/history.csv` — append-only log, one row per scenario per run.
  `log/results/` — full JSON per run (machine, commit, build type).
- Numbers only compare on the same machine + build type. Re-baseline only
  for an accepted change (and say so in the commit message).

Adding a scenario: copy one in `scenarios/`, keep it deterministic (use
`rng`, seeded), keep per-frame scenario work cheap, describe it in
`describe()`. The interactive human benchmark lives in `benchmark_scene/`.
