# BlastBullets2D benchmark: latest run

- run: `2026-10-03T10-06-56Z` commit `8bf3549` (dirty src/)
- machine: AMD Ryzen 7 8840HS w/ Radeon 780M Graphics (16 threads), Linux-7.2.8-2-cachyos-x86_64-with-glibc2.44
- godot: 4.7.2-stable (arch_linux), debug build: True, repeats: 3 (median)
- baseline: `5ed265d` (2026-10-02T16-53-28Z)

Times are milliseconds of CPU per frame (simulated time, `--fixed-fps 60`).
`frame` = step + engine; `step` = the scenario's own plugin calls (spawns...);
`engine` = physics server step + factory tick + drain + render; `tick` = factory physics tick only.

| scenario | frame p50 | frame p95 | frame p99 | frame max | step max | engine max | tick p50 | tick p99 | bullets | mem peak MB | objects +/- | vs baseline |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---|
| cold_spawn_scaling | 0.004 | 0.004 | 0.005 | 0.005 | 0.001 | 0.005 | 0.000 | 0.000 | 0 | 51.3 | +0 | frame p50 +0%, tick p50 +0% |
| spawner_moving_preview_1500 | 0.556 | 1.405 | 2.659 | 3.432 | 0.003 | 3.432 | 0.313 | 0.565 | 11550 | 34.9 | +15 | frame p50 -57%, tick p50 -2% |
| spawner_path_move_1500 | 0.850 | 1.746 | 3.517 | 4.774 | 0.002 | 4.774 | 0.481 | 0.742 | 17780 | 37.7 | +15 | new |
| spawner_spin_preview_1500 | 0.575 | 1.654 | 4.672 | 9.141 | 0.001 | 9.141 | 0.314 | 0.648 | 11550 | 39.8 | +15 | frame p50 -40%, tick p50 -1% |
| spawner_warm_shot_5k | 13.710 | 16.550 | 17.530 | 18.864 | 3.890 | 15.592 | 3.802 | 5.866 | 90000 | 163.7 | +0 | new |

## Scenario extras

- **cold_spawn_scaling**: cold_1000_ms=0.608, cold_2000_ms=1.385, cold_4000_ms=3.744, cold_8000_ms=10.842, scaling_8k_over_1k=17.564
- **spawner_path_move_1500**: pattern_cache_hits=47, pattern_cache_misses=2, preview_rebuilds=1

## Scenarios

- **cold_spawn_scaling**: cold (pool-miss) spawn time for 1k/2k/4k/8k-bullet volleys
- **spawner_moving_preview_1500**: 1500-bullet spawner on a moving parent, runtime preview on, firing 4/s
- **spawner_path_move_1500**: 1500-bullet star spawner moving along a Path2D, preview on, firing 8/s
- **spawner_spin_preview_1500**: 1500-bullet heart spawner, spin + runtime preview, firing 4/s
- **spawner_warm_shot_5k**: 5000-bullet heart spawner, shoot_once every frame, warm pool
