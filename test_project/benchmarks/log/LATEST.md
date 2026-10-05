# BlastBullets2D benchmark: latest run

- run: `2026-10-05T09-50-52Z` commit `3ffa5b1`
- machine: AMD Ryzen 7 8840HS w/ Radeon 780M Graphics (16 threads), Linux-7.2.9-1-cachyos-x86_64-with-glibc2.44
- godot: 4.7.2-stable (arch_linux), debug build: True, repeats: 5 (median)
- baseline: `5ed265d` (2026-10-02T16-53-28Z)

Times are milliseconds of CPU per frame (simulated time, `--fixed-fps 60`).
`frame` = step + engine; `step` = the scenario's own plugin calls (spawns...);
`engine` = physics server step + factory tick + drain + render; `tick` = factory physics tick only.

| scenario | frame p50 | frame p95 | frame p99 | frame max | step max | engine max | tick p50 | tick p99 | bullets | mem peak MB | objects +/- | vs baseline |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---|
| attachments_500 | 0.047 | 0.051 | 0.126 | 0.358 | 0.236 | 0.126 | 0.027 | 0.080 | 453 | 32.9 | +0 | frame p50 +2%, tick p50 +8% |
| bounce_box_2k | 0.117 | 0.126 | 0.129 | 0.138 | 0.001 | 0.137 | 0.056 | 0.061 | 2000 | 29.7 | +0 | frame p50 -6%, tick p50 +2% |
| cold_spawn_scaling | 0.004 | 0.004 | 0.005 | 0.005 | 0.001 | 0.004 | 0.000 | 0.001 | 0 | 51.2 | +0 | frame p50 +0%, tick p50 +0% |
| collision_storm | 0.675 | 1.281 | 1.515 | 2.418 | 0.432 | 2.342 | 0.145 | 0.317 | 1400 | 30.5 | +0 | frame p50 -9%, tick p50 -19% |
| factory_warm_spawn_5k | 12.690 | 15.336 | 16.016 | 16.662 | 3.420 | 15.052 | 3.171 | 5.135 | 90000 | 69.6 | +0 | frame p50 -26%, tick p50 -26% |
| homing_2k_moving_targets | 0.170 | 0.181 | 0.197 | 1.304 | 0.010 | 1.303 | 0.106 | 0.124 | 2000 | 29.7 | +0 | frame p50 +2%, tick p50 +9% |
| kitchen_sink | 0.471 | 0.622 | 0.799 | 2.254 | 0.003 | 2.253 | 0.279 | 0.350 | 7488 | 33.1 | +171 | frame p50 -5%, tick p50 +1% |
| mass_expiry_10k | 0.585 | 0.774 | 4.698 | 5.296 | 2.912 | 2.593 | 0.270 | 0.740 | 7375 | 38.8 | +0 | frame p50 -1%, tick p50 +1% |
| pool_churn | 0.786 | 0.865 | 0.910 | 2.521 | 0.619 | 2.417 | 0.276 | 0.314 | 9000 | 33.8 | +0 | frame p50 -12%, tick p50 -12% |
| spawner_moving_preview_1500 | 0.569 | 1.315 | 2.647 | 3.462 | 0.004 | 3.461 | 0.324 | 0.466 | 11550 | 34.9 | +15 | frame p50 -56%, tick p50 +2% |
| spawner_path_move_1500 | 0.860 | 1.671 | 3.386 | 3.871 | 0.002 | 3.870 | 0.489 | 0.663 | 17780 | 37.6 | +15 | new |
| spawner_spin_preview_1500 | 0.589 | 1.549 | 4.618 | 9.189 | 0.001 | 9.189 | 0.323 | 0.494 | 11550 | 39.8 | +15 | frame p50 -39%, tick p50 +2% |
| spawner_warm_shot_5k | 13.679 | 16.481 | 17.263 | 17.754 | 3.871 | 15.752 | 3.421 | 5.345 | 90000 | 163.6 | +0 | new |
| trails_fx_2k | 0.350 | 0.610 | 3.636 | 3.994 | 3.377 | 1.613 | 0.284 | 0.549 | 1967 | 30.9 | +0 | frame p50 -14%, tick p50 -15% |
| volley_10k_flight | 0.557 | 0.587 | 0.613 | 2.007 | 0.001 | 2.007 | 0.271 | 0.297 | 10000 | 32.7 | +0 | frame p50 -1%, tick p50 +1% |

## Scenario extras

- **cold_spawn_scaling**: cold_1000_ms=0.607, cold_2000_ms=1.427, cold_4000_ms=3.637, cold_8000_ms=10.782, scaling_8k_over_1k=17.529
- **collision_storm**: records_per_frame=200.000
- **spawner_path_move_1500**: pattern_cache_hits=47, pattern_cache_misses=2, preview_rebuilds=1

## Scenarios

- **attachments_500**: 500 bullets with node attachments, refired every 90 frames
- **bounce_box_2k**: 2k bullets bouncing inside a box (bounce resolution every hit)
- **cold_spawn_scaling**: cold (pool-miss) spawn time for 1k/2k/4k/8k-bullet volleys
- **collision_storm**: 200-bullet volley/frame dying on walls (collision drain + pool churn)
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
- **volley_10k_flight**: 10k bullets in flight, no overlaps
