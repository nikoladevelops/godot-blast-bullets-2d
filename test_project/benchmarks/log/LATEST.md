# BlastBullets2D benchmark: latest run

- run: `2026-10-03T10-29-14Z` commit `b03ebcd` (dirty src/)
- machine: AMD Ryzen 7 8840HS w/ Radeon 780M Graphics (16 threads), Linux-7.2.8-2-cachyos-x86_64-with-glibc2.44
- godot: 4.7.2-stable (arch_linux), debug build: True, repeats: 3 (median)
- baseline: `5ed265d` (2026-10-02T16-53-28Z)

Times are milliseconds of CPU per frame (simulated time, `--fixed-fps 60`).
`frame` = step + engine; `step` = the scenario's own plugin calls (spawns...);
`engine` = physics server step + factory tick + drain + render; `tick` = factory physics tick only.

| scenario | frame p50 | frame p95 | frame p99 | frame max | step max | engine max | tick p50 | tick p99 | bullets | mem peak MB | objects +/- | vs baseline |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---|
| spawner_moving_preview_1500 | 0.563 | 1.446 | 2.612 | 3.279 | 0.003 | 3.278 | 0.318 | 0.582 | 11550 | 34.9 | +15 | frame p50 -56%, tick p50 +0% |
| spawner_path_move_1500 | 0.844 | 1.790 | 3.328 | 3.713 | 0.002 | 3.712 | 0.475 | 0.749 | 17780 | 37.7 | +15 | new |
| spawner_spin_preview_1500 | 0.574 | 1.601 | 4.687 | 9.101 | 0.001 | 9.101 | 0.314 | 0.590 | 11550 | 39.8 | +15 | frame p50 -40%, tick p50 -1% |
| spawner_warm_shot_5k | 13.808 | 16.388 | 17.470 | 17.735 | 3.875 | 15.717 | 3.811 | 5.756 | 90000 | 163.6 | +0 | new |

## Scenario extras

- **spawner_path_move_1500**: pattern_cache_hits=47, pattern_cache_misses=2, preview_rebuilds=1

## Scenarios

- **spawner_moving_preview_1500**: 1500-bullet spawner on a moving parent, runtime preview on, firing 4/s
- **spawner_path_move_1500**: 1500-bullet star spawner moving along a Path2D, preview on, firing 8/s
- **spawner_spin_preview_1500**: 1500-bullet heart spawner, spin + runtime preview, firing 4/s
- **spawner_warm_shot_5k**: 5000-bullet heart spawner, shoot_once every frame, warm pool
