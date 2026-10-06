# BlastBullets2D benchmark: latest run

- run: `2026-10-06T16-57-18Z` commit `fbafb7e`
- machine: AMD Ryzen 7 8840HS w/ Radeon 780M Graphics (16 threads), Linux-7.2.9-1-cachyos-x86_64-with-glibc2.44
- godot: 4.7.2-stable (arch_linux), debug build: True, repeats: 5 (median)
- baseline: `cedbd11` (2026-10-06T05-29-10Z)

Times are milliseconds of CPU per frame (simulated time, `--fixed-fps 60`).
`frame` = step + engine; `step` = the scenario's own plugin calls (spawns...);
`engine` = physics server step + factory tick + drain + render; `tick` = factory physics tick only.

| scenario | frame p50 | frame p95 | frame p99 | frame max | step max | engine max | tick p50 | tick p99 | bullets | mem peak MB | objects +/- | vs baseline |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---|
| attachments_500 | 0.047 | 0.061 | 0.136 | 0.498 | 0.206 | 0.284 | 0.027 | 0.081 | 453 | 33.2 | +0 | frame p50 +4%, tick p50 +4% |
| bounce_box_2k | 0.119 | 0.127 | 0.131 | 0.157 | 0.002 | 0.157 | 0.055 | 0.059 | 2000 | 29.9 | +0 | frame p50 +5%, tick p50 +6% |
| cold_spawn_scaling | 0.004 | 0.004 | 0.004 | 0.004 | 0.001 | 0.004 | 0.000 | 0.000 | 0 | 51.5 | +0 | frame p50 +0%, tick p50 +0% |
| collision_storm | 0.676 | 1.262 | 1.474 | 2.595 | 0.177 | 2.527 | 0.142 | 0.314 | 1400 | 30.7 | +0 | frame p50 -1%, tick p50 -1% |
| factory_warm_spawn_5k | 12.161 | 14.597 | 15.267 | 15.533 | 2.893 | 14.236 | 3.114 | 5.079 | 90000 | 65.1 | +0 | frame p50 -7%, tick p50 -2% |
| graze_10k_flight | 0.587 | 0.616 | 0.665 | 2.314 | 0.005 | 2.313 | 0.293 | 0.325 | 10000 | 32.5 | +0 | frame p50 +0%, tick p50 -0% |
| graze_64_targets | 0.686 | 0.759 | 0.781 | 2.446 | 0.108 | 2.431 | 0.383 | 0.444 | 10000 | 32.6 | +0 | frame p50 -1%, tick p50 -1% |
| graze_storm | 0.953 | 5.107 | 9.322 | 10.777 | 0.598 | 10.776 | 0.245 | 1.335 | 7867 | 38.3 | +0 | frame p50 +3%, tick p50 +3% |
| hit_handlers_2k | 1.332 | 2.092 | 2.762 | 3.559 | 0.573 | 3.453 | 0.282 | 0.601 | 1870 | 36.0 | +318 | frame p50 +2%, tick p50 -5% |
| homing_2k_moving_targets | 0.171 | 0.181 | 0.196 | 1.261 | 0.009 | 1.252 | 0.106 | 0.122 | 2000 | 29.9 | +0 | frame p50 +3%, tick p50 +4% |
| kitchen_sink | 0.475 | 0.602 | 0.863 | 2.263 | 0.004 | 2.262 | 0.278 | 0.328 | 7488 | 33.0 | +171 | frame p50 +0%, tick p50 +1% |
| lifetime_signal_10k | 0.586 | 0.784 | 4.320 | 4.985 | 2.648 | 2.541 | 0.267 | 2.180 | 7375 | 38.6 | +0 | frame p50 +1%, tick p50 +2% |
| mass_expiry_10k | 0.586 | 0.759 | 4.110 | 4.870 | 2.376 | 2.537 | 0.265 | 0.726 | 7375 | 38.5 | +0 | frame p50 +1%, tick p50 +2% |
| pool_churn | 0.775 | 0.838 | 0.874 | 2.586 | 0.327 | 2.455 | 0.269 | 0.312 | 9000 | 33.6 | +0 | frame p50 +2%, tick p50 +0% |
| spawner_aimed_regen_2k | 2.648 | 2.831 | 4.548 | 4.643 | 1.679 | 4.224 | 1.162 | 2.942 | 36000 | 41.7 | +0 | frame p50 +1%, tick p50 +2% |
| spawner_moving_preview_1500 | 0.570 | 1.238 | 2.406 | 3.038 | 0.004 | 3.037 | 0.322 | 0.455 | 11550 | 34.5 | +15 | frame p50 -1%, tick p50 +0% |
| spawner_path_move_1500 | 0.858 | 1.586 | 2.961 | 3.334 | 0.002 | 3.334 | 0.481 | 0.660 | 17780 | 36.9 | +15 | frame p50 -3%, tick p50 -3% |
| spawner_spin_preview_1500 | 0.589 | 1.453 | 4.221 | 8.861 | 0.002 | 8.861 | 0.323 | 0.472 | 11550 | 39.3 | +15 | frame p50 +2%, tick p50 +3% |
| spawner_warm_shot_5k | 12.739 | 15.362 | 16.579 | 17.355 | 3.529 | 15.756 | 3.359 | 5.336 | 90000 | 158.7 | +0 | frame p50 +0%, tick p50 +2% |
| trails_fx_2k | 0.355 | 0.611 | 3.472 | 3.633 | 3.020 | 1.994 | 0.287 | 0.551 | 1967 | 31.1 | +0 | frame p50 +4%, tick p50 +5% |
| volley_10k_flight | 0.560 | 0.592 | 0.632 | 2.324 | 0.002 | 2.323 | 0.269 | 0.294 | 10000 | 32.5 | +0 | frame p50 +2%, tick p50 +4% |

## Scenario extras

- **cold_spawn_scaling**: cold_1000_ms=0.540, cold_2000_ms=1.237, cold_4000_ms=3.243, cold_8000_ms=9.732, scaling_8k_over_1k=18.022
- **collision_storm**: records_per_frame=200.000
- **graze_10k_flight**: grazes=500
- **graze_64_targets**: grazes=7178
- **graze_storm**: events=142000
- **hit_handlers_2k**: handler_calls=38157, healed=4769
- **lifetime_signal_10k**: bullets_listed=30000, handler_calls=30
- **spawner_path_move_1500**: pattern_cache_hits=47, pattern_cache_misses=2, preview_rebuilds=1

## Scenarios

- **attachments_500**: 500 bullets with node attachments, refired every 90 frames
- **bounce_box_2k**: 2k bullets bouncing inside a box (bounce resolution every hit)
- **cold_spawn_scaling**: cold (pool-miss) spawn time for 1k/2k/4k/8k-bullet volleys
- **collision_storm**: 200-bullet volley/frame dying on walls (collision drain + pool churn)
- **factory_warm_spawn_5k**: 5000-bullet TypedArray spawn every frame (script API), warm pool
- **graze_10k_flight**: 10k bullets in flight, graze armed (3 rings, 1 moving target)
- **graze_64_targets**: 10k bullets in flight, graze armed (3 rings, 64 moving targets)
- **graze_storm**: graze storm: 2k-bullet rings spawned on the player every 0.25 s
- **hit_handlers_2k**: 100-bullet volley/frame into walls, a GDScript hit handler per bullet (live emit + kill decision)
- **homing_2k_moving_targets**: 2k homing bullets chasing 4 moving targets
- **kitchen_sink**: 3 spawners (spin spiral, fan, homing ring) + moving player + walls
- **lifetime_signal_10k**: 10k bullets expiring on one frame with a live life_time_over handler per volley (3 waves)
- **mass_expiry_10k**: 10k bullets expiring on the same frame (3 waves)
- **pool_churn**: one 300-bullet volley per frame, 0.5 s life (warm-pool spawn + expiry)
- **spawner_aimed_regen_2k**: 2000-bullet aimed spawner regenerating its pattern every frame (uncacheable source)
- **spawner_moving_preview_1500**: 1500-bullet spawner on a moving parent, runtime preview on, firing 4/s
- **spawner_path_move_1500**: 1500-bullet star spawner moving along a Path2D, preview on, firing 8/s
- **spawner_spin_preview_1500**: 1500-bullet heart spawner, spin + runtime preview, firing 4/s
- **spawner_warm_shot_5k**: 5000-bullet heart spawner, shoot_once every frame, warm pool
- **trails_fx_2k**: 2k bullets with trail + spawn-flash layers, refired every 60 frames
- **volley_10k_flight**: 10k bullets in flight, no overlaps
