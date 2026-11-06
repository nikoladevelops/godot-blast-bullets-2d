# BlastBullets2D benchmark: latest run

- run: `2026-11-06T11-33-54Z` commit `33f0df0`
- machine: AMD Ryzen 7 8840HS w/ Radeon 780M Graphics (16 threads), Linux-7.3.1-1-cachyos-x86_64-with-glibc2.44
- godot: 4.7.2-stable (arch_linux), debug build: True, repeats: 5 (median)
- baseline: `a23dc0a` (2026-10-28T07-52-07Z)

Times are milliseconds of CPU per frame (simulated time, `--fixed-fps 60`).
`frame` = step + engine; `step` = the scenario's own plugin calls (spawns...);
`engine` = physics server step + factory tick + drain + render; `tick` = factory physics tick only.

| scenario | frame p50 | frame p95 | frame p99 | frame max | step max | engine max | tick p50 | tick p99 | bullets | mem peak MB | objects +/- | vs baseline |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---|
| attachments_500 | 0.048 | 0.056 | 0.177 | 0.419 | 0.281 | 0.191 | 0.028 | 0.106 | 453 | 37.3 | +0 | frame p50 +2%, tick p50 +4% |
| bounce_box_2k | 0.122 | 0.130 | 0.133 | 0.142 | 0.001 | 0.142 | 0.056 | 0.062 | 2000 | 33.8 | +0 | frame p50 -2%, tick p50 -5% |
| cold_spawn_scaling | 0.004 | 0.004 | 0.005 | 0.005 | 0.001 | 0.004 | 0.000 | 0.001 | 0 | 55.2 | +0 | frame p50 +0%, tick p50 +0% |
| cold_volleys_in_physics | 1.230 | 1.765 | 3.741 | 6.009 | 0.004 | 6.008 | 0.079 | 0.416 | 1735 | 50.9 | -28200 | frame p50 +2%, tick p50 -5% |
| collision_storm | 0.707 | 1.081 | 1.142 | 2.552 | 0.232 | 2.413 | 0.175 | 0.311 | 1400 | 34.7 | +0 | frame p50 +1%, tick p50 +1% |
| colors_10k_flight | 0.590 | 0.609 | 0.654 | 2.237 | 0.001 | 2.237 | 0.271 | 0.323 | 10000 | 36.8 | +0 | frame p50 +1%, tick p50 -6% |
| curvy_graze_300 | 1.182 | 1.296 | 1.384 | 2.980 | 0.168 | 2.979 | 0.631 | 0.749 | 0 | 33.5 | +0 | new |
| curvy_lasers_256 | 0.820 | 0.867 | 0.896 | 1.525 | 0.197 | 1.524 | 0.287 | 0.318 | 0 | 32.8 | +0 | **REGRESSION**: frame_ms.p50 0.660->0.820 ms (+24%) |
| debugger_10k_flight | 0.583 | 0.602 | 0.617 | 0.627 | 0.001 | 0.627 | 0.264 | 0.283 | 10000 | 37.0 | +0 | frame p50 -4%, tick p50 -8% |
| emitter_storm | 2.880 | 3.996 | 6.154 | 7.740 | 0.171 | 7.736 | 1.817 | 4.023 | 12532 | 55.0 | +0 | frame p50 +2%, tick p50 +3% |
| factory_warm_spawn_5k | 12.196 | 15.590 | 17.051 | 18.693 | 4.220 | 16.307 | 3.210 | 4.368 | 90000 | 69.1 | +0 | frame p50 -2%, tick p50 -3% |
| flock_10k_flight | 4.739 | 4.965 | 6.668 | 7.082 | 0.009 | 7.081 | 4.354 | 6.169 | 10000 | 36.5 | +0 | frame p50 -2%, tick p50 -2% |
| flock_10k_interval | 2.582 | 2.709 | 3.002 | 4.593 | 0.008 | 4.591 | 2.193 | 2.529 | 10000 | 36.5 | +0 | frame p50 -3%, tick p50 -3% |
| flock_2k_swarm | 0.862 | 0.961 | 1.006 | 2.705 | 0.004 | 2.704 | 0.781 | 0.914 | 2000 | 33.9 | +0 | frame p50 -3%, tick p50 -3% |
| flock_5k_stacked | 2.527 | 2.632 | 2.715 | 4.464 | 0.005 | 4.463 | 2.320 | 2.461 | 5000 | 48.3 | +0 | frame p50 -3%, tick p50 -3% |
| flock_homing_bursts | 1.150 | 1.273 | 1.326 | 2.851 | 0.122 | 2.849 | 1.079 | 1.163 | 1466 | 34.2 | -5 | frame p50 -1%, tick p50 -1% |
| graze_10k_flight | 0.584 | 0.599 | 0.608 | 0.622 | 0.002 | 0.622 | 0.297 | 0.311 | 10000 | 36.5 | +0 | frame p50 -5%, tick p50 -7% |
| graze_64_targets | 0.656 | 0.689 | 0.702 | 0.709 | 0.017 | 0.698 | 0.356 | 0.400 | 10000 | 36.6 | +0 | frame p50 -5%, tick p50 -8% |
| graze_storm | 0.959 | 4.568 | 8.611 | 16.459 | 0.970 | 16.459 | 0.259 | 1.425 | 7867 | 42.3 | +0 | frame p50 -4%, tick p50 -6% |
| hit_handlers_2k | 1.340 | 1.840 | 1.962 | 3.173 | 0.222 | 3.096 | 0.346 | 0.591 | 1870 | 40.0 | +318 | frame p50 -7%, tick p50 -7% |
| homing_2k_moving_targets | 0.146 | 0.151 | 0.155 | 0.174 | 0.004 | 0.173 | 0.082 | 0.088 | 2000 | 33.9 | +0 | frame p50 -5%, tick p50 -5% |
| kitchen_sink | 0.494 | 0.586 | 0.643 | 0.937 | 0.002 | 0.936 | 0.290 | 0.340 | 7459 | 36.9 | +162 | frame p50 -2%, tick p50 -6% |
| laser_beams_256 | 0.409 | 0.431 | 0.440 | 1.696 | 0.005 | 1.690 | 0.361 | 0.390 | 0 | 33.1 | +0 | frame p50 +4%, tick p50 +3% |
| laser_looks_256 | 0.255 | 0.261 | 0.264 | 0.270 | 0.001 | 0.269 | 0.169 | 0.176 | 0 | 33.3 | +0 | frame p50 +4%, tick p50 +3% |
| lifetime_signal_10k | 0.573 | 0.728 | 4.399 | 5.873 | 4.006 | 2.514 | 0.263 | 2.131 | 7375 | 42.5 | +0 | frame p50 -4%, tick p50 -7% |
| mass_expiry_10k | 0.572 | 0.725 | 4.151 | 4.722 | 2.369 | 2.553 | 0.260 | 0.776 | 7375 | 42.5 | +0 | frame p50 -4%, tick p50 -8% |
| multi_spawner_pooled_8x | 0.908 | 1.215 | 1.634 | 3.210 | 0.005 | 3.210 | 0.454 | 0.947 | 10054 | 41.6 | +0 | frame p50 -4%, tick p50 -6% |
| pickup_convert_5k | 0.774 | 1.536 | 4.612 | 5.856 | 2.457 | 3.804 | 0.220 | 1.205 | 3352 | 44.3 | +0 | frame p50 -4%, tick p50 -6% |
| pool_churn | 0.739 | 0.829 | 0.845 | 2.526 | 0.092 | 2.100 | 0.268 | 0.323 | 9000 | 37.6 | +0 | frame p50 -6%, tick p50 -8% |
| sound_flight_10k | 1.450 | 1.514 | 1.588 | 3.233 | 0.001 | 3.233 | 1.167 | 1.254 | 10000 | 36.5 | +2 | frame p50 -3%, tick p50 -3% |
| sound_hit_storm | 0.759 | 1.062 | 1.103 | 2.479 | 0.284 | 2.426 | 0.173 | 0.295 | 1400 | 34.7 | +0 | frame p50 +5%, tick p50 -2% |
| spawner_aimed_regen_2k | 2.604 | 3.012 | 4.122 | 4.767 | 0.862 | 4.315 | 1.157 | 1.589 | 36000 | 45.6 | +0 | frame p50 -5%, tick p50 -7% |
| spawner_moving_preview_1500 | 0.560 | 1.081 | 1.398 | 1.686 | 0.003 | 1.681 | 0.313 | 0.403 | 11465 | 37.3 | +12 | frame p50 -6%, tick p50 -9% |
| spawner_override_shots | 1.839 | 2.033 | 2.376 | 3.928 | 2.282 | 2.375 | 0.302 | 0.525 | 3840 | 38.8 | +0 | frame p50 -1%, tick p50 +1% |
| spawner_path_move_1500 | 0.853 | 1.433 | 1.769 | 2.671 | 0.001 | 2.670 | 0.474 | 0.614 | 17595 | 39.9 | +12 | frame p50 -4%, tick p50 -7% |
| spawner_spin_preview_1500 | 0.576 | 1.243 | 1.938 | 2.679 | 0.001 | 2.679 | 0.315 | 0.419 | 11465 | 45.5 | +12 | frame p50 -4%, tick p50 -7% |
| spawner_warm_shot_5k | 12.127 | 15.550 | 17.195 | 20.228 | 1.438 | 19.312 | 3.414 | 4.762 | 90000 | 161.7 | +0 | frame p50 -2%, tick p50 -4% |
| split_storm | 0.226 | 2.716 | 5.743 | 6.076 | 0.187 | 6.074 | 0.163 | 4.911 | 1675 | 63.4 | +0 | frame p50 +4%, tick p50 +5% |
| trails_fx_2k | 0.352 | 0.678 | 3.490 | 3.660 | 3.028 | 0.704 | 0.287 | 0.631 | 1967 | 35.0 | +0 | frame p50 -2%, tick p50 -1% |
| volley_10k_flight | 0.546 | 0.557 | 0.569 | 0.580 | 0.001 | 0.579 | 0.260 | 0.272 | 10000 | 36.4 | +0 | frame p50 -6%, tick p50 -9% |

## Scenario extras

- **cold_spawn_scaling**: cold_1000_ms=0.332, cold_2000_ms=0.690, cold_4000_ms=1.486, cold_8000_ms=8.423, scaling_8k_over_1k=25.160
- **collision_storm**: records_per_frame=200.000
- **curvy_graze_300**: grazes=2447
- **curvy_lasers_256**: lasers=8
- **emitter_storm**: emitted=10212
- **flock_10k_flight**: flock_pass_usec=3405, neighbor_checks=327819
- **flock_10k_interval**: flock_pass_usec=1484, neighbor_checks=108825, queried_members=3330
- **flock_2k_swarm**: flock_pass_usec=730, neighbor_checks=82784
- **flock_5k_stacked**: flock_pass_usec=1954, neighbor_checks=160598
- **flock_homing_bursts**: flock_pass_usec=656, neighbor_checks=51321
- **graze_10k_flight**: grazes=500
- **graze_64_targets**: grazes=7178
- **graze_storm**: events=142000
- **hit_handlers_2k**: handler_calls=38157, healed=4769
- **laser_beams_256**: hits=8324
- **laser_looks_256**: hits=763
- **lifetime_signal_10k**: bullets_listed=30000, handler_calls=30
- **pickup_convert_5k**: conversions=7
- **spawner_override_shots**: override_empty_us=23.405, override_knob_us=61.030, plain_us=15.620
- **spawner_path_move_1500**: pattern_cache_hits=47, pattern_cache_misses=2, preview_rebuilds=1

## Scenarios

- **attachments_500**: 500 bullets with node attachments, refired every 90 frames
- **bounce_box_2k**: 2k bullets bouncing inside a box (bounce resolution every hit)
- **cold_spawn_scaling**: cold (pool-miss) spawn time for 1k/2k/4k/8k-bullet volleys
- **cold_volleys_in_physics**: 40 cold monitorable volleys per wave spawned inside _physics_process
- **collision_storm**: 200-bullet volley/frame dying on walls (collision drain + pool churn)
- **colors_10k_flight**: 10k bullets in flight with per-bullet colors (stride 12)
- **curvy_graze_300**: 256 curvy lasers grazing among 300 targets (2-ring zone)
- **curvy_lasers_256**: 256 curvy tracking lasers (8 x 32), a wave every 2 s, 1.9 s lives
- **debugger_10k_flight**: 10k bullets in flight, collision debugger on
- **emitter_storm**: 256 emitter bullets firing 8-rings every 0.25 s (~8k children)
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
- **laser_beams_256**: 256 sweeping beams (8 lasers), walls, graze, 24 pierce targets
- **laser_looks_256**: 256 sweeping beams (8 lasers) with body, muzzle, tip and pierce animations
- **lifetime_signal_10k**: 10k bullets expiring on one frame with a live life_time_over handler per volley (3 waves)
- **mass_expiry_10k**: 10k bullets expiring on the same frame (3 waves)
- **multi_spawner_pooled_8x**: 8 auto-fire spawners (spin, homing, split, graze) into one warm pool, ~10k live
- **pickup_convert_5k**: 5k bullets converted to pickups every 60 frames, magnet to a target, collected on arrival
- **pool_churn**: one 300-bullet volley per frame, 0.5 s life (warm-pool spawn + expiry)
- **sound_flight_10k**: 10k bullets in flight, On Flight hum following every bullet
- **sound_hit_storm**: 200-bullet volley/frame dying on walls with an On Hit sound
- **spawner_aimed_regen_2k**: 2000-bullet aimed spawner regenerating its pattern every frame (uncacheable source)
- **spawner_moving_preview_1500**: 1500-bullet spawner on a moving parent, runtime preview on, firing 4/s
- **spawner_override_shots**: 40 spawners x shoot_once_with() per frame (empty / one-knob overrides)
- **spawner_path_move_1500**: 1500-bullet star spawner moving along a Path2D, preview on, firing 8/s
- **spawner_spin_preview_1500**: 1500-bullet heart spawner, spin + runtime preview, firing 4/s
- **spawner_warm_shot_5k**: 5000-bullet heart spawner, shoot_once every frame, warm pool
- **split_storm**: 500 parents -> 2000 children -> 4000 grandchildren (two-stage split)
- **trails_fx_2k**: 2k bullets with trail + spawn-flash layers, refired every 60 frames
- **volley_10k_flight**: 10k bullets in flight, no overlaps
