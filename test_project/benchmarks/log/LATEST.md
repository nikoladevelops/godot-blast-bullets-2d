# BlastBullets2D benchmark: latest run

- run: `2026-10-19T07-19-21Z` commit `02dcaab`
- machine: AMD Ryzen 7 8840HS w/ Radeon 780M Graphics (16 threads), Linux-7.2.10-1-cachyos-x86_64-with-glibc2.44
- godot: 4.7.2-stable (arch_linux), debug build: True, repeats: 5 (median)
- baseline: `cedbd11` (2026-10-06T05-29-10Z)

Times are milliseconds of CPU per frame (simulated time, `--fixed-fps 60`).
`frame` = step + engine; `step` = the scenario's own plugin calls (spawns...);
`engine` = physics server step + factory tick + drain + render; `tick` = factory physics tick only.

| scenario | frame p50 | frame p95 | frame p99 | frame max | step max | engine max | tick p50 | tick p99 | bullets | mem peak MB | objects +/- | vs baseline |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---|
| attachments_500 | 0.046 | 0.051 | 0.146 | 0.349 | 0.224 | 0.146 | 0.026 | 0.097 | 453 | 34.3 | +0 | frame p50 +2%, tick p50 +0% |
| bounce_box_2k | 0.117 | 0.126 | 0.133 | 0.140 | 0.001 | 0.139 | 0.055 | 0.060 | 2000 | 31.1 | +0 | frame p50 +4%, tick p50 +6% |
| cold_spawn_scaling | 0.004 | 0.004 | 0.005 | 0.005 | 0.001 | 0.004 | 0.000 | 0.001 | 0 | 52.6 | +0 | frame p50 +0%, tick p50 +0% |
| collision_storm | 0.694 | 1.032 | 1.654 | 2.524 | 0.523 | 2.436 | 0.156 | 0.269 | 1400 | 31.8 | +0 | frame p50 +2%, tick p50 +8% |
| factory_warm_spawn_5k | 12.489 | 17.013 | 18.523 | 20.436 | 4.322 | 17.103 | 3.171 | 5.441 | 90000 | 66.2 | +0 | frame p50 -5%, tick p50 +0% |
| flock_10k_flight | 4.795 | 5.156 | 6.658 | 6.846 | 0.016 | 6.845 | 4.402 | 6.206 | 10000 | 33.7 | +0 | new |
| flock_10k_interval | 2.624 | 2.728 | 4.361 | 4.510 | 0.007 | 4.508 | 2.236 | 3.921 | 10000 | 33.7 | +0 | new |
| flock_2k_swarm | 0.884 | 0.962 | 0.991 | 2.636 | 0.013 | 2.635 | 0.801 | 0.904 | 2000 | 31.0 | +0 | new |
| flock_5k_stacked | 2.582 | 2.661 | 4.300 | 4.451 | 0.005 | 4.449 | 2.368 | 4.069 | 5000 | 45.5 | +0 | new |
| flock_homing_bursts | 1.139 | 1.257 | 1.590 | 2.875 | 0.147 | 2.873 | 1.070 | 1.212 | 1466 | 31.0 | -4 | new |
| graze_10k_flight | 0.595 | 0.620 | 0.637 | 2.250 | 0.005 | 2.245 | 0.301 | 0.320 | 10000 | 33.7 | +0 | frame p50 +1%, tick p50 +2% |
| graze_64_targets | 0.669 | 0.725 | 0.748 | 2.376 | 0.117 | 2.361 | 0.364 | 0.417 | 10000 | 33.8 | +0 | frame p50 -2%, tick p50 -5% |
| graze_storm | 0.967 | 5.261 | 8.347 | 10.832 | 1.136 | 10.832 | 0.261 | 1.382 | 7867 | 39.5 | +0 | frame p50 +4%, tick p50 +10% |
| hit_handlers_2k | 1.317 | 1.784 | 2.728 | 3.114 | 0.380 | 3.075 | 0.347 | 0.610 | 1870 | 37.1 | +318 | frame p50 +0%, tick p50 +17% |
| homing_2k_moving_targets | 0.174 | 0.183 | 0.193 | 0.438 | 0.004 | 0.436 | 0.109 | 0.117 | 2000 | 31.0 | +0 | frame p50 +5%, tick p50 +7% |
| kitchen_sink | 0.482 | 0.619 | 0.853 | 2.277 | 0.002 | 2.276 | 0.286 | 0.341 | 7488 | 34.1 | +171 | frame p50 +2%, tick p50 +4% |
| lifetime_signal_10k | 0.586 | 0.762 | 4.505 | 6.393 | 4.459 | 2.546 | 0.268 | 2.204 | 7375 | 39.7 | +0 | frame p50 +1%, tick p50 +2% |
| mass_expiry_10k | 0.586 | 0.774 | 4.106 | 5.948 | 2.995 | 2.565 | 0.268 | 0.784 | 7375 | 39.7 | +0 | frame p50 +1%, tick p50 +3% |
| multi_spawner_pooled_8x | 0.868 | 1.249 | 1.517 | 2.893 | 0.006 | 2.893 | 0.435 | 0.732 | 10157 | 38.7 | +0 | new |
| pickup_convert_5k | 0.764 | 1.516 | 5.081 | 6.174 | 2.979 | 3.793 | 0.223 | 1.136 | 3352 | 41.5 | +0 | new |
| pool_churn | 0.773 | 0.857 | 0.927 | 2.485 | 0.614 | 2.414 | 0.276 | 0.308 | 9000 | 34.8 | +0 | frame p50 +1%, tick p50 +3% |
| sound_flight_10k | 1.550 | 1.597 | 3.225 | 3.312 | 0.001 | 3.312 | 1.260 | 2.596 | 10000 | 33.8 | +2 | frame p50 +3%, tick p50 +4% |
| sound_hit_storm | 0.746 | 1.035 | 2.261 | 2.454 | 0.522 | 2.394 | 0.155 | 0.270 | 1400 | 31.8 | +0 | frame p50 +7%, tick p50 +4% |
| spawner_aimed_regen_2k | 2.672 | 3.264 | 4.663 | 5.025 | 1.139 | 4.409 | 1.181 | 1.764 | 36000 | 42.8 | +0 | frame p50 +2%, tick p50 +4% |
| spawner_moving_preview_1500 | 0.573 | 1.250 | 2.641 | 3.055 | 0.005 | 3.054 | 0.323 | 0.495 | 11550 | 35.6 | +15 | frame p50 -1%, tick p50 +0% |
| spawner_path_move_1500 | 0.871 | 1.619 | 3.090 | 4.411 | 0.003 | 4.411 | 0.486 | 0.676 | 17780 | 38.1 | +15 | frame p50 -2%, tick p50 -2% |
| spawner_spin_preview_1500 | 0.592 | 1.487 | 4.152 | 8.658 | 0.002 | 8.658 | 0.325 | 0.535 | 11550 | 40.5 | +15 | frame p50 +2%, tick p50 +4% |
| spawner_warm_shot_5k | 13.177 | 18.148 | 19.978 | 21.752 | 3.483 | 19.143 | 3.432 | 5.809 | 90000 | 159.9 | +0 | **REGRESSION**: frame_ms.p95 14.745->18.148 ms (+23%); factory_tick_ms.p95 3.594->4.725 ms (+31%) |
| split_storm | 0.203 | 1.349 | 7.223 | 18.429 | 0.224 | 18.429 | 0.142 | 6.439 | 1675 | 59.2 | +0 | new |
| trails_fx_2k | 0.353 | 0.610 | 3.589 | 4.037 | 3.367 | 2.073 | 0.283 | 0.550 | 1967 | 32.3 | +0 | frame p50 +4%, tick p50 +3% |
| volley_10k_flight | 0.568 | 0.596 | 0.611 | 2.131 | 0.002 | 2.129 | 0.271 | 0.310 | 10000 | 33.7 | +0 | frame p50 +4%, tick p50 +5% |

## Scenario extras

- **cold_spawn_scaling**: cold_1000_ms=0.539, cold_2000_ms=1.236, cold_4000_ms=3.222, cold_8000_ms=9.824, scaling_8k_over_1k=18.250
- **collision_storm**: records_per_frame=200.000
- **flock_10k_flight**: flock_pass_usec=3436, neighbor_checks=327819
- **flock_10k_interval**: flock_pass_usec=1476, neighbor_checks=108825, queried_members=3330
- **flock_2k_swarm**: flock_pass_usec=738, neighbor_checks=82784
- **flock_5k_stacked**: flock_pass_usec=2026, neighbor_checks=160598
- **flock_homing_bursts**: flock_pass_usec=677, neighbor_checks=51321
- **graze_10k_flight**: grazes=500
- **graze_64_targets**: grazes=7178
- **graze_storm**: events=142000
- **hit_handlers_2k**: handler_calls=38157, healed=4769
- **lifetime_signal_10k**: bullets_listed=30000, handler_calls=30
- **pickup_convert_5k**: conversions=7
- **spawner_path_move_1500**: pattern_cache_hits=47, pattern_cache_misses=2, preview_rebuilds=1

## Scenarios

- **attachments_500**: 500 bullets with node attachments, refired every 90 frames
- **bounce_box_2k**: 2k bullets bouncing inside a box (bounce resolution every hit)
- **cold_spawn_scaling**: cold (pool-miss) spawn time for 1k/2k/4k/8k-bullet volleys
- **collision_storm**: 200-bullet volley/frame dying on walls (collision drain + pool churn)
- **factory_warm_spawn_5k**: 5000-bullet TypedArray spawn every frame (script API), warm pool
- **flock_10k_flight**: 10k bullets in flight, 10 Per Volley flocks (dense, capped)
- **flock_10k_interval**: 10k bullets, 10 Per Volley flocks, steering refreshed every 3 ticks
- **flock_2k_swarm**: 2k bullets, one Shared flock (default rules)
- **flock_5k_stacked**: 5k bullets stacked on one point, one flock (cap bound)
- **flock_homing_bursts**: 55-bullet homing bursts every 0.15 s, 128 px random sizes, wobble, one default flock
- **graze_10k_flight**: 10k bullets in flight, graze armed (3 rings, 1 moving target)
- **graze_64_targets**: 10k bullets in flight, graze armed (3 rings, 64 moving targets)
- **graze_storm**: graze storm: 2k-bullet rings spawned on the player every 0.25 s
- **hit_handlers_2k**: 100-bullet volley/frame into walls, a GDScript hit handler per bullet (live emit + kill decision)
- **homing_2k_moving_targets**: 2k homing bullets chasing 4 moving targets
- **kitchen_sink**: 3 spawners (spin spiral, fan, homing ring) + moving player + walls
- **lifetime_signal_10k**: 10k bullets expiring on one frame with a live life_time_over handler per volley (3 waves)
- **mass_expiry_10k**: 10k bullets expiring on the same frame (3 waves)
- **multi_spawner_pooled_8x**: 8 auto-fire spawners (spin, homing, split, graze) into one warm pool, ~10k live
- **pickup_convert_5k**: 5k bullets converted to pickups every 60 frames, magnet to a target, collected on arrival
- **pool_churn**: one 300-bullet volley per frame, 0.5 s life (warm-pool spawn + expiry)
- **sound_flight_10k**: 10k bullets in flight, On Flight hum following every bullet
- **sound_hit_storm**: 200-bullet volley/frame dying on walls with an On Hit sound
- **spawner_aimed_regen_2k**: 2000-bullet aimed spawner regenerating its pattern every frame (uncacheable source)
- **spawner_moving_preview_1500**: 1500-bullet spawner on a moving parent, runtime preview on, firing 4/s
- **spawner_path_move_1500**: 1500-bullet star spawner moving along a Path2D, preview on, firing 8/s
- **spawner_spin_preview_1500**: 1500-bullet heart spawner, spin + runtime preview, firing 4/s
- **spawner_warm_shot_5k**: 5000-bullet heart spawner, shoot_once every frame, warm pool
- **split_storm**: 500 parents -> 2000 children -> 4000 grandchildren (two-stage split)
- **trails_fx_2k**: 2k bullets with trail + spawn-flash layers, refired every 60 frames
- **volley_10k_flight**: 10k bullets in flight, no overlaps
