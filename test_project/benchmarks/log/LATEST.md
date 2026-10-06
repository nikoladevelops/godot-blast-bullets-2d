# BlastBullets2D benchmark: latest run

- run: `2026-10-06T10-35-41Z` commit `b0acf31`
- machine: AMD Ryzen 7 8840HS w/ Radeon 780M Graphics (16 threads), Linux-7.2.9-1-cachyos-x86_64-with-glibc2.44
- godot: 4.7.2-stable (arch_linux), debug build: True, repeats: 5 (median)
- baseline: `cedbd11` (2026-10-06T05-29-10Z)

Times are milliseconds of CPU per frame (simulated time, `--fixed-fps 60`).
`frame` = step + engine; `step` = the scenario's own plugin calls (spawns...);
`engine` = physics server step + factory tick + drain + render; `tick` = factory physics tick only.

| scenario | frame p50 | frame p95 | frame p99 | frame max | step max | engine max | tick p50 | tick p99 | bullets | mem peak MB | objects +/- | vs baseline |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---|
| attachments_500 | 0.046 | 0.053 | 0.125 | 0.333 | 0.206 | 0.127 | 0.026 | 0.080 | 453 | 33.1 | +0 | frame p50 +2%, tick p50 +0% |
| bounce_box_2k | 0.118 | 0.129 | 0.141 | 1.000 | 0.002 | 0.998 | 0.055 | 0.072 | 2000 | 29.8 | +0 | frame p50 +4%, tick p50 +6% |
| cold_spawn_scaling | 0.004 | 0.004 | 0.005 | 0.005 | 0.001 | 0.004 | 0.000 | 0.001 | 0 | 51.4 | +0 | frame p50 +0%, tick p50 +0% |
| collision_storm | 0.676 | 1.259 | 1.305 | 2.350 | 0.134 | 2.302 | 0.145 | 0.311 | 1400 | 30.6 | +0 | frame p50 -1%, tick p50 +1% |
| factory_warm_spawn_5k | 13.650 | 15.560 | 16.348 | 17.741 | 3.874 | 15.577 | 3.331 | 4.171 | 90000 | 65.0 | +0 | frame p50 +4%, tick p50 +5% |
| graze_10k_flight | 0.587 | 0.612 | 0.657 | 0.716 | 0.004 | 0.714 | 0.294 | 0.324 | 10000 | 32.4 | +0 | new |
| graze_storm | 0.929 | 4.744 | 7.953 | 10.632 | 0.538 | 10.632 | 0.238 | 1.298 | 7867 | 38.2 | +0 | new |
| hit_handlers_2k | 1.321 | 2.032 | 2.409 | 3.510 | 0.475 | 3.360 | 0.284 | 0.587 | 1870 | 35.8 | +318 | frame p50 +1%, tick p50 -4% |
| homing_2k_moving_targets | 0.169 | 0.177 | 0.180 | 0.193 | 0.004 | 0.192 | 0.104 | 0.111 | 2000 | 29.8 | +0 | frame p50 +2%, tick p50 +2% |
| kitchen_sink | 0.464 | 0.592 | 0.674 | 1.514 | 0.002 | 1.510 | 0.271 | 0.327 | 7488 | 32.9 | +171 | frame p50 -2%, tick p50 -1% |
| lifetime_signal_10k | 0.586 | 0.760 | 4.353 | 4.943 | 2.583 | 2.489 | 0.264 | 2.117 | 7375 | 38.5 | +0 | frame p50 +1%, tick p50 +1% |
| mass_expiry_10k | 0.580 | 0.738 | 3.987 | 4.492 | 2.283 | 2.440 | 0.261 | 0.704 | 7375 | 38.4 | +0 | frame p50 -0%, tick p50 +0% |
| pool_churn | 0.768 | 0.810 | 0.845 | 2.279 | 0.111 | 1.657 | 0.268 | 0.298 | 9000 | 33.5 | +0 | frame p50 +1%, tick p50 +0% |
| spawner_aimed_regen_2k | 2.586 | 2.760 | 2.938 | 4.639 | 0.463 | 4.211 | 1.124 | 1.249 | 36000 | 41.6 | +0 | frame p50 -1%, tick p50 -1% |
| spawner_moving_preview_1500 | 0.557 | 1.191 | 2.190 | 2.845 | 0.003 | 2.844 | 0.311 | 0.433 | 11550 | 34.4 | +15 | frame p50 -4%, tick p50 -3% |
| spawner_path_move_1500 | 0.853 | 1.599 | 2.945 | 3.365 | 0.003 | 3.365 | 0.475 | 0.628 | 17780 | 36.9 | +15 | frame p50 -4%, tick p50 -4% |
| spawner_spin_preview_1500 | 0.581 | 1.448 | 4.060 | 8.284 | 0.001 | 8.284 | 0.315 | 0.480 | 11550 | 39.2 | +15 | frame p50 +0%, tick p50 +0% |
| spawner_warm_shot_5k | 12.555 | 14.487 | 15.848 | 17.676 | 3.430 | 15.392 | 3.257 | 3.761 | 90000 | 158.6 | +0 | frame p50 -1%, tick p50 -1% |
| trails_fx_2k | 0.357 | 0.611 | 3.723 | 3.884 | 3.240 | 0.662 | 0.287 | 0.549 | 1967 | 31.0 | +0 | frame p50 +5%, tick p50 +5% |
| volley_10k_flight | 0.561 | 0.591 | 0.617 | 0.678 | 0.001 | 0.677 | 0.264 | 0.293 | 10000 | 32.4 | +0 | frame p50 +2%, tick p50 +2% |

## Scenario extras

- **cold_spawn_scaling**: cold_1000_ms=0.557, cold_2000_ms=1.319, cold_4000_ms=3.303, cold_8000_ms=9.733, scaling_8k_over_1k=17.824
- **collision_storm**: records_per_frame=200.000
- **graze_10k_flight**: grazes=500
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
