# BlastBullets2D benchmark: latest run

- run: `2026-10-03T20-48-46Z` commit `de48529`
- machine: AMD Ryzen 7 8840HS w/ Radeon 780M Graphics (16 threads), Linux-7.2.8-2-cachyos-x86_64-with-glibc2.44
- godot: 4.7.2-stable (arch_linux), debug build: True, repeats: 5 (median)
- baseline: `5ed265d` (2026-10-02T16-53-28Z)

Times are milliseconds of CPU per frame (simulated time, `--fixed-fps 60`).
`frame` = step + engine; `step` = the scenario's own plugin calls (spawns...);
`engine` = physics server step + factory tick + drain + render; `tick` = factory physics tick only.

| scenario | frame p50 | frame p95 | frame p99 | frame max | step max | engine max | tick p50 | tick p99 | bullets | mem peak MB | objects +/- | vs baseline |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---|
| attachments_500 | 0.045 | 0.053 | 0.144 | 0.371 | 0.243 | 0.144 | 0.026 | 0.114 | 453 | 32.9 | +0 | frame p50 -2%, tick p50 +4% |
| bounce_box_2k | 0.117 | 0.125 | 0.133 | 0.137 | 0.001 | 0.137 | 0.055 | 0.062 | 2000 | 29.7 | +0 | frame p50 -6%, tick p50 +0% |
| cold_spawn_scaling | 0.004 | 0.004 | 0.004 | 0.004 | 0.001 | 0.004 | 0.000 | 0.001 | 0 | 51.2 | +0 | frame p50 +0%, tick p50 +0% |
| collision_storm | 0.685 | 1.305 | 1.761 | 2.744 | 0.200 | 2.618 | 0.150 | 0.334 | 1400 | 30.4 | +0 | frame p50 -7%, tick p50 -17% |
| factory_warm_spawn_5k | 13.037 | 15.435 | 16.034 | 16.579 | 3.526 | 14.706 | 3.545 | 5.510 | 90000 | 69.6 | +0 | frame p50 -24%, tick p50 -18% |
| homing_2k_moving_targets | 0.170 | 0.184 | 0.212 | 1.071 | 0.010 | 1.061 | 0.104 | 0.130 | 2000 | 29.6 | +0 | frame p50 +2%, tick p50 +7% |
| kitchen_sink | 0.475 | 0.643 | 1.026 | 2.198 | 0.005 | 2.198 | 0.281 | 0.391 | 7488 | 33.0 | +171 | frame p50 -4%, tick p50 +1% |
| mass_expiry_10k | 0.584 | 0.770 | 4.710 | 5.363 | 3.014 | 2.502 | 0.269 | 1.448 | 7375 | 38.7 | +0 | frame p50 -1%, tick p50 +0% |
| pool_churn | 0.811 | 0.868 | 1.046 | 2.589 | 0.768 | 2.487 | 0.294 | 0.344 | 9000 | 33.8 | +0 | frame p50 -10%, tick p50 -6% |
| spawner_moving_preview_1500 | 0.568 | 1.443 | 2.733 | 3.584 | 0.005 | 3.583 | 0.322 | 0.569 | 11550 | 34.8 | +15 | frame p50 -56%, tick p50 +1% |
| spawner_path_move_1500 | 0.851 | 1.780 | 3.355 | 3.841 | 0.001 | 3.841 | 0.483 | 0.759 | 17780 | 37.6 | +15 | new |
| spawner_spin_preview_1500 | 0.585 | 1.680 | 4.796 | 9.215 | 0.002 | 9.215 | 0.322 | 0.607 | 11550 | 39.7 | +15 | frame p50 -39%, tick p50 +2% |
| spawner_warm_shot_5k | 13.956 | 16.468 | 17.758 | 18.540 | 4.053 | 16.402 | 3.845 | 5.835 | 90000 | 163.6 | +0 | new |
| trails_fx_2k | 0.358 | 0.621 | 3.715 | 3.957 | 3.328 | 2.086 | 0.289 | 0.683 | 1967 | 30.9 | +0 | frame p50 -12%, tick p50 -13% |
| volley_10k_flight | 0.557 | 0.587 | 0.616 | 2.287 | 0.002 | 2.287 | 0.271 | 0.300 | 10000 | 32.7 | +0 | frame p50 -1%, tick p50 +1% |

## Scenario extras

- **cold_spawn_scaling**: cold_1000_ms=0.608, cold_2000_ms=1.381, cold_4000_ms=3.561, cold_8000_ms=10.766, scaling_8k_over_1k=17.081
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
