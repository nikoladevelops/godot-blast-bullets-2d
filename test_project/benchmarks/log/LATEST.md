# BlastBullets2D benchmark: latest run

- run: `2026-10-03T09-03-58Z` commit `ae0b9b8`
- machine: AMD Ryzen 7 8840HS w/ Radeon 780M Graphics (16 threads), Linux-7.2.8-2-cachyos-x86_64-with-glibc2.44
- godot: 4.7.2-stable (arch_linux), debug build: True, repeats: 5 (median)
- baseline: `5ed265d` (2026-10-02T16-53-28Z)

Times are milliseconds of CPU per frame (simulated time, `--fixed-fps 60`).
`frame` = step + engine; `step` = the scenario's own plugin calls (spawns...);
`engine` = physics server step + factory tick + drain + render; `tick` = factory physics tick only.

| scenario | frame p50 | frame p95 | frame p99 | frame max | step max | engine max | tick p50 | tick p99 | bullets | mem peak MB | objects +/- | vs baseline |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---|
| attachments_500 | 0.043 | 0.048 | 0.134 | 0.351 | 0.231 | 0.134 | 0.024 | 0.109 | 453 | 33.0 | +0 | frame p50 -7%, tick p50 -4% |
| block_10k_flight | 0.497 | 0.555 | 0.610 | 1.828 | 0.002 | 1.826 | 0.216 | 0.247 | 10000 | 32.6 | +0 | frame p50 -1%, tick p50 -1% |
| bounce_box_2k | 0.116 | 0.125 | 0.139 | 1.056 | 0.002 | 1.054 | 0.054 | 0.068 | 2000 | 29.8 | +0 | frame p50 -7%, tick p50 -2% |
| cold_spawn_scaling | 0.004 | 0.004 | 0.005 | 0.005 | 0.001 | 0.004 | 0.000 | 0.001 | 0 | 51.3 | +0 | frame p50 +0%, tick p50 +0% |
| collision_storm | 0.683 | 1.298 | 2.237 | 2.661 | 0.509 | 2.594 | 0.147 | 0.330 | 1400 | 30.5 | +0 | frame p50 -8%, tick p50 -18% |
| dir_10k_flight | 0.549 | 0.581 | 0.609 | 2.259 | 0.002 | 2.258 | 0.264 | 0.293 | 10000 | 32.8 | +0 | frame p50 -3%, tick p50 -1% |
| factory_warm_spawn_5k | 13.330 | 15.260 | 16.108 | 16.773 | 2.931 | 14.800 | 3.542 | 4.457 | 90000 | 69.7 | +0 | frame p50 -23%, tick p50 -18% |
| homing_2k_moving_targets | 0.161 | 0.171 | 0.179 | 0.191 | 0.005 | 0.189 | 0.098 | 0.109 | 2000 | 29.7 | +0 | frame p50 -4%, tick p50 +1% |
| kitchen_sink | 0.465 | 0.618 | 0.788 | 1.288 | 0.004 | 1.284 | 0.272 | 0.333 | 7488 | 33.1 | +171 | frame p50 -6%, tick p50 -2% |
| mass_expiry_10k | 0.574 | 0.759 | 4.644 | 5.395 | 3.023 | 2.519 | 0.260 | 1.434 | 7375 | 38.8 | +0 | frame p50 -3%, tick p50 -3% |
| pool_churn | 0.790 | 0.845 | 0.893 | 1.796 | 0.160 | 1.411 | 0.284 | 0.313 | 9000 | 33.9 | +0 | frame p50 -12%, tick p50 -10% |
| spawner_moving_preview_1500 | 0.560 | 1.429 | 2.644 | 3.305 | 0.005 | 3.304 | 0.315 | 0.573 | 11550 | 34.9 | +15 | frame p50 -56%, tick p50 -1% |
| spawner_path_move_1500 | 0.843 | 1.737 | 3.343 | 3.761 | 0.002 | 3.761 | 0.474 | 0.742 | 17780 | 37.7 | +15 | new |
| spawner_spin_preview_1500 | 0.572 | 1.619 | 4.593 | 9.167 | 0.003 | 9.166 | 0.315 | 0.582 | 11550 | 39.8 | +15 | frame p50 -41%, tick p50 -1% |
| spawner_warm_shot_5k | 13.778 | 16.464 | 17.789 | 18.353 | 3.886 | 16.306 | 3.836 | 5.626 | 90000 | 163.7 | +0 | new |
| trails_fx_2k | 0.375 | 0.756 | 3.713 | 3.877 | 3.214 | 1.941 | 0.309 | 0.721 | 1967 | 31.0 | +0 | frame p50 -7%, tick p50 -7% |

## Scenario extras

- **cold_spawn_scaling**: cold_1000_ms=0.600, cold_2000_ms=1.375, cold_4000_ms=3.646, cold_8000_ms=10.696, scaling_8k_over_1k=17.873
- **collision_storm**: records_per_frame=200.000
- **spawner_path_move_1500**: pattern_cache_hits=47, pattern_cache_misses=2, preview_rebuilds=1

## Scenarios

- **attachments_500**: 500 bullets with node attachments, refired every 90 frames
- **block_10k_flight**: 10k block bullets in flight (rigid volleys)
- **bounce_box_2k**: 2k bullets bouncing inside a box (bounce resolution every hit)
- **cold_spawn_scaling**: cold (pool-miss) spawn time for 1k/2k/4k/8k-bullet volleys
- **collision_storm**: 200-bullet volley/frame dying on walls (collision drain + pool churn)
- **dir_10k_flight**: 10k directional bullets in flight, no overlaps
- **factory_warm_spawn_5k**: 5000-bullet TypedArray spawn every frame (script API), warm pool
- **homing_2k_moving_targets**: 2k homing bullets chasing 4 moving targets
- **kitchen_sink**: 3 spawners (spin spiral, fan, homing ring) + moving player + walls
- **mass_expiry_10k**: 10k bullets expiring on the same frame (3 waves)
- **pool_churn**: one 300-bullet volley per frame, 0.5 s life (warm-pool spawn + expiry)
- **spawner_moving_preview_1500**: 1500-bullet spawner on a moving parent, runtime preview on, firing 4/s
- **spawner_path_move_1500**: 1500-bullet star spawner moving along a Path2D, preview on, firing 8/s
- **spawner_spin_preview_1500**: 1500-bullet heart spawner, spin + runtime preview, firing 4/s
- **spawner_warm_shot_5k**: 5000-bullet heart spawner, shoot_once every frame, warm pool
- **trails_fx_2k**: 2k bullets with trail + spawn-flash layers, refired every 60 frames
