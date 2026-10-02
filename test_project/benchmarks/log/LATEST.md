# BlastBullets2D benchmark: latest run

- run: `2026-10-02T17-25-04Z` commit `50e8ed4` (dirty src/)
- machine: AMD Ryzen 7 8840HS w/ Radeon 780M Graphics (16 threads), Linux-7.2.8-2-cachyos-x86_64-with-glibc2.44
- godot: 4.7.2-stable (arch_linux), debug build: True, repeats: 5 (median)
- baseline: `5ed265d` (2026-10-02T16-53-28Z)

Times are milliseconds of CPU per frame (simulated time, `--fixed-fps 60`).
`frame` = step + engine; `step` = the scenario's own plugin calls (spawns...);
`engine` = physics server step + factory tick + drain + render; `tick` = factory physics tick only.

| scenario | frame p50 | frame p95 | frame p99 | frame max | step max | engine max | tick p50 | tick p99 | bullets | mem peak MB | objects +/- | vs baseline |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---|
| cold_spawn_scaling | 0.004 | 0.005 | 0.005 | 0.005 | 0.001 | 0.004 | 0.000 | 0.001 | 0 | 51.2 | +0 | frame p50 +0%, tick p50 +0% |
| factory_warm_spawn_5k | 15.316 | 18.916 | 20.179 | 20.592 | 4.898 | 17.620 | 3.865 | 5.636 | 90000 | 69.6 | +0 | frame p50 -11%, tick p50 -10% |
| spawner_moving_preview_1500 | 0.569 | 1.537 | 2.824 | 3.648 | 0.009 | 3.647 | 0.319 | 0.624 | 11550 | 34.9 | +15 | frame p50 -56%, tick p50 +0% |
| spawner_path_move_1500 | 0.862 | 1.947 | 3.827 | 4.323 | 0.003 | 4.322 | 0.482 | 0.852 | 17780 | 37.6 | +15 | new |
| spawner_spin_preview_1500 | 0.585 | 1.734 | 4.898 | 9.368 | 0.002 | 9.367 | 0.318 | 0.676 | 11550 | 39.7 | +15 | frame p50 -39%, tick p50 +0% |
| spawner_warm_shot_5k | 14.694 | 18.680 | 20.340 | 21.747 | 3.986 | 19.590 | 3.918 | 5.893 | 90000 | 163.6 | +0 | new |

## Scenario extras

- **cold_spawn_scaling**: cold_1000_ms=0.628, cold_2000_ms=1.539, cold_4000_ms=4.009, cold_8000_ms=11.214, scaling_8k_over_1k=17.945
- **spawner_path_move_1500**: pattern_cache_hits=47, pattern_cache_misses=2, preview_rebuilds=1

## Scenarios

- **cold_spawn_scaling**: cold (pool-miss) spawn time for 1k/2k/4k/8k-bullet volleys
- **factory_warm_spawn_5k**: 5000-bullet TypedArray spawn every frame (script API), warm pool
- **spawner_moving_preview_1500**: 1500-bullet spawner on a moving parent, runtime preview on, firing 4/s
- **spawner_path_move_1500**: 1500-bullet star spawner moving along a Path2D, preview on, firing 8/s
- **spawner_spin_preview_1500**: 1500-bullet heart spawner, spin + runtime preview, firing 4/s
- **spawner_warm_shot_5k**: 5000-bullet heart spawner, shoot_once every frame, warm pool
