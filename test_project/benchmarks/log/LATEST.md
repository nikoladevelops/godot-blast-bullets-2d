# BlastBullets2D benchmark: latest run

- run: `2026-10-06T05-30-29Z` commit `cedbd11`
- machine: AMD Ryzen 7 8840HS w/ Radeon 780M Graphics (16 threads), Linux-7.2.9-1-cachyos-x86_64-with-glibc2.44
- godot: 4.7.2-stable (arch_linux), debug build: True, repeats: 5 (median)
- baseline: `cedbd11` (2026-10-06T05-29-10Z)

Times are milliseconds of CPU per frame (simulated time, `--fixed-fps 60`).
`frame` = step + engine; `step` = the scenario's own plugin calls (spawns...);
`engine` = physics server step + factory tick + drain + render; `tick` = factory physics tick only.

| scenario | frame p50 | frame p95 | frame p99 | frame max | step max | engine max | tick p50 | tick p99 | bullets | mem peak MB | objects +/- | vs baseline |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---|
| attachments_500 | 0.044 | 0.052 | 0.127 | 0.329 | 0.206 | 0.127 | 0.025 | 0.078 | 453 | 32.9 | +0 | frame p50 -2%, tick p50 -4% |
| bounce_box_2k | 0.116 | 0.122 | 0.127 | 0.133 | 0.001 | 0.132 | 0.053 | 0.058 | 2000 | 29.7 | +0 | frame p50 +3%, tick p50 +2% |
| cold_spawn_scaling | 0.004 | 0.005 | 0.007 | 0.007 | 0.001 | 0.007 | 0.000 | 0.000 | 0 | 51.3 | +0 | frame p50 +0%, tick p50 +0% |
| collision_storm | 0.662 | 1.261 | 1.306 | 2.345 | 0.147 | 2.290 | 0.142 | 0.305 | 1400 | 30.5 | +0 | frame p50 -3%, tick p50 -1% |
| factory_warm_spawn_5k | 11.978 | 13.332 | 15.161 | 16.047 | 2.798 | 14.748 | 3.010 | 3.837 | 90000 | 64.9 | +0 | frame p50 -9%, tick p50 -5% |
| hit_handlers_2k | 1.333 | 2.025 | 2.513 | 3.274 | 0.451 | 3.133 | 0.276 | 0.573 | 1870 | 35.6 | +318 | frame p50 +2%, tick p50 -7% |
| homing_2k_moving_targets | 0.169 | 0.176 | 0.180 | 0.218 | 0.006 | 0.217 | 0.103 | 0.111 | 2000 | 29.7 | +0 | frame p50 +2%, tick p50 +1% |
| kitchen_sink | 0.470 | 0.587 | 0.679 | 1.764 | 0.004 | 1.763 | 0.272 | 0.316 | 7488 | 32.7 | +171 | frame p50 -1%, tick p50 -1% |
| lifetime_signal_10k | 0.575 | 0.758 | 4.325 | 5.096 | 2.722 | 2.471 | 0.257 | 2.114 | 7375 | 38.3 | +0 | frame p50 -1%, tick p50 -2% |
| mass_expiry_10k | 0.578 | 0.756 | 4.080 | 4.608 | 2.293 | 2.498 | 0.260 | 0.707 | 7375 | 38.3 | +0 | frame p50 -1%, tick p50 -0% |
| pool_churn | 0.761 | 0.819 | 0.869 | 2.720 | 0.101 | 2.636 | 0.263 | 0.299 | 9000 | 33.4 | +0 | frame p50 -0%, tick p50 -2% |
| spawner_aimed_regen_2k | 2.590 | 2.793 | 4.464 | 4.683 | 2.066 | 4.197 | 1.126 | 1.304 | 36000 | 41.4 | +0 | frame p50 -1%, tick p50 -1% |
| spawner_moving_preview_1500 | 0.551 | 1.173 | 2.215 | 2.823 | 0.002 | 2.823 | 0.307 | 0.423 | 11550 | 34.2 | +15 | frame p50 -5%, tick p50 -5% |
| spawner_path_move_1500 | 0.838 | 1.542 | 2.830 | 3.200 | 0.001 | 3.199 | 0.468 | 0.597 | 17780 | 36.7 | +15 | frame p50 -6%, tick p50 -5% |
| spawner_spin_preview_1500 | 0.575 | 1.413 | 4.050 | 8.255 | 0.001 | 8.255 | 0.310 | 0.455 | 11550 | 39.1 | +15 | frame p50 -1%, tick p50 -1% |
| spawner_warm_shot_5k | 12.334 | 14.101 | 16.532 | 17.163 | 3.454 | 15.343 | 3.232 | 5.024 | 90000 | 158.5 | +0 | frame p50 -3%, tick p50 -2% |
| trails_fx_2k | 0.347 | 0.599 | 3.474 | 3.636 | 3.016 | 0.647 | 0.279 | 0.538 | 1967 | 30.9 | +0 | frame p50 +2%, tick p50 +2% |
| volley_10k_flight | 0.546 | 0.565 | 0.576 | 0.582 | 0.001 | 0.582 | 0.258 | 0.269 | 10000 | 32.3 | +0 | frame p50 -0%, tick p50 -0% |

## Scenario extras

- **cold_spawn_scaling**: cold_1000_ms=0.523, cold_2000_ms=1.192, cold_4000_ms=3.245, cold_8000_ms=9.345, scaling_8k_over_1k=17.821
- **collision_storm**: records_per_frame=200.000
- **hit_handlers_2k**: handler_calls=38157, healed=4769
- **lifetime_signal_10k**: bullets_listed=30000, handler_calls=30
- **spawner_path_move_1500**: pattern_cache_hits=47, pattern_cache_misses=2, preview_rebuilds=1

## Scenarios

- **attachments_500**: 500 bullets with node attachments, refired every 90 frames
- **bounce_box_2k**: 2k bullets bouncing inside a box (bounce resolution every hit)
- **cold_spawn_scaling**: cold (pool-miss) spawn time for 1k/2k/4k/8k-bullet volleys
- **collision_storm**: 200-bullet volley/frame dying on walls (collision drain + pool churn)
- **factory_warm_spawn_5k**: 5000-bullet TypedArray spawn every frame (script API), warm pool
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
