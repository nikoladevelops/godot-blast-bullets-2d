# BlastBullets2D benchmark: latest run

- run: `2026-10-17T19-01-35Z` commit `0d2e9f1` (dirty src/)
- machine: AMD Ryzen 7 8840HS w/ Radeon 780M Graphics (16 threads), Linux-7.2.10-1-cachyos-x86_64-with-glibc2.44
- godot: 4.7.2-stable (arch_linux), debug build: True, repeats: 5 (median)
- baseline: `cedbd11` (2026-10-06T05-29-10Z)

Times are milliseconds of CPU per frame (simulated time, `--fixed-fps 60`).
`frame` = step + engine; `step` = the scenario's own plugin calls (spawns...);
`engine` = physics server step + factory tick + drain + render; `tick` = factory physics tick only.

| scenario | frame p50 | frame p95 | frame p99 | frame max | step max | engine max | tick p50 | tick p99 | bullets | mem peak MB | objects +/- | vs baseline |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---|
| attachments_500 | 0.046 | 0.052 | 0.153 | 0.350 | 0.227 | 0.153 | 0.026 | 0.096 | 453 | 34.3 | +0 | frame p50 +2%, tick p50 +0% |
| bounce_box_2k | 0.116 | 0.125 | 0.129 | 0.135 | 0.001 | 0.135 | 0.055 | 0.060 | 2000 | 31.0 | +0 | frame p50 +3%, tick p50 +6% |
| cold_spawn_scaling | 0.004 | 0.004 | 0.005 | 0.005 | 0.001 | 0.004 | 0.000 | 0.001 | 0 | 52.6 | +0 | frame p50 +0%, tick p50 +0% |
| collision_storm | 0.688 | 1.021 | 1.077 | 2.479 | 0.111 | 2.395 | 0.153 | 0.261 | 1400 | 31.8 | +0 | frame p50 +1%, tick p50 +6% |
| factory_warm_spawn_5k | 12.401 | 16.738 | 18.397 | 19.321 | 4.346 | 16.603 | 3.166 | 5.014 | 90000 | 66.2 | +0 | frame p50 -5%, tick p50 +0% |
| flock_10k_flight | 4.794 | 5.101 | 6.662 | 6.735 | 0.014 | 6.732 | 4.397 | 6.225 | 10000 | 33.6 | +0 | new |
| flock_10k_interval | 2.631 | 2.724 | 4.373 | 4.504 | 0.008 | 4.501 | 2.236 | 3.878 | 10000 | 33.6 | +0 | new |
| flock_2k_swarm | 0.884 | 0.966 | 0.997 | 2.634 | 0.005 | 2.632 | 0.800 | 0.900 | 2000 | 31.0 | +0 | new |
| flock_5k_stacked | 2.555 | 2.654 | 4.243 | 4.376 | 0.006 | 4.374 | 2.344 | 3.737 | 5000 | 45.5 | +0 | new |
| flock_homing_bursts | 1.123 | 1.246 | 1.324 | 2.844 | 0.134 | 2.842 | 1.053 | 1.121 | 1466 | 31.0 | -4 | new |
| graze_10k_flight | 0.595 | 0.625 | 0.649 | 2.332 | 0.003 | 2.330 | 0.301 | 0.332 | 10000 | 33.6 | +0 | frame p50 +1%, tick p50 +2% |
| graze_64_targets | 0.674 | 0.726 | 0.762 | 2.313 | 0.117 | 2.292 | 0.369 | 0.411 | 10000 | 33.7 | +0 | frame p50 -2%, tick p50 -4% |
| graze_storm | 0.967 | 5.220 | 8.326 | 11.128 | 1.188 | 11.127 | 0.259 | 1.336 | 7867 | 39.4 | +0 | frame p50 +4%, tick p50 +9% |
| hit_handlers_2k | 1.328 | 1.825 | 2.674 | 3.324 | 0.413 | 3.120 | 0.364 | 0.631 | 1870 | 37.1 | +318 | **REGRESSION**: factory_tick_ms.p50 0.297->0.364 ms (+23%) |
| homing_2k_moving_targets | 0.174 | 0.184 | 0.187 | 0.226 | 0.004 | 0.225 | 0.108 | 0.115 | 2000 | 31.0 | +0 | frame p50 +5%, tick p50 +6% |
| kitchen_sink | 0.483 | 0.616 | 0.719 | 2.198 | 0.003 | 2.197 | 0.285 | 0.327 | 7488 | 34.1 | +171 | frame p50 +2%, tick p50 +4% |
| lifetime_signal_10k | 0.583 | 0.760 | 4.487 | 5.140 | 2.769 | 2.558 | 0.267 | 2.169 | 7375 | 39.7 | +0 | frame p50 +1%, tick p50 +2% |
| mass_expiry_10k | 0.589 | 0.778 | 4.147 | 6.988 | 4.361 | 2.553 | 0.267 | 0.777 | 7375 | 39.6 | +0 | frame p50 +1%, tick p50 +2% |
| pickup_convert_5k | 0.765 | 1.615 | 5.273 | 7.723 | 4.620 | 3.535 | 0.222 | 1.158 | 3352 | 41.4 | +0 | new |
| pool_churn | 0.748 | 0.808 | 0.831 | 0.877 | 0.110 | 0.803 | 0.266 | 0.294 | 9000 | 34.8 | +0 | frame p50 -2%, tick p50 -1% |
| sound_flight_10k | 1.546 | 1.583 | 3.224 | 3.336 | 0.002 | 3.336 | 1.254 | 2.843 | 10000 | 33.7 | +1 | frame p50 +3%, tick p50 +3% |
| sound_hit_storm | 0.716 | 1.029 | 1.182 | 2.476 | 0.210 | 2.395 | 0.155 | 0.265 | 1400 | 31.8 | +0 | frame p50 +3%, tick p50 +4% |
| spawner_aimed_regen_2k | 2.682 | 3.157 | 4.607 | 5.151 | 1.546 | 4.298 | 1.183 | 2.280 | 36000 | 42.8 | +0 | frame p50 +2%, tick p50 +4% |
| spawner_moving_preview_1500 | 0.566 | 1.215 | 2.373 | 2.890 | 0.003 | 2.890 | 0.320 | 0.461 | 11550 | 35.6 | +15 | frame p50 -2%, tick p50 -1% |
| spawner_path_move_1500 | 0.849 | 1.539 | 2.845 | 3.205 | 0.002 | 3.205 | 0.478 | 0.626 | 17780 | 38.1 | +15 | frame p50 -4%, tick p50 -3% |
| spawner_spin_preview_1500 | 0.576 | 1.407 | 4.056 | 8.326 | 0.001 | 8.326 | 0.316 | 0.465 | 11550 | 40.4 | +15 | frame p50 -1%, tick p50 +1% |
| spawner_warm_shot_5k | 12.578 | 14.381 | 17.848 | 19.999 | 3.296 | 17.354 | 3.305 | 4.484 | 90000 | 159.8 | +0 | frame p50 -1%, tick p50 +1% |
| split_storm | 0.226 | 1.167 | 5.274 | 16.150 | 0.184 | 16.150 | 0.165 | 4.874 | 1675 | 59.0 | +0 | new |
| trails_fx_2k | 0.358 | 0.603 | 3.457 | 3.710 | 3.085 | 0.654 | 0.284 | 0.550 | 1967 | 32.2 | +0 | frame p50 +5%, tick p50 +4% |
| volley_10k_flight | 0.556 | 0.580 | 0.594 | 0.604 | 0.001 | 0.604 | 0.267 | 0.279 | 10000 | 33.6 | +0 | frame p50 +1%, tick p50 +3% |

## Scenario extras

- **cold_spawn_scaling**: cold_1000_ms=0.534, cold_2000_ms=1.224, cold_4000_ms=3.290, cold_8000_ms=9.571, scaling_8k_over_1k=17.643
- **collision_storm**: records_per_frame=200.000
- **flock_10k_flight**: flock_pass_usec=3382, neighbor_checks=327819
- **flock_10k_interval**: flock_pass_usec=1481, neighbor_checks=108825, queried_members=3330
- **flock_2k_swarm**: flock_pass_usec=739, neighbor_checks=82784
- **flock_5k_stacked**: flock_pass_usec=1953, neighbor_checks=160598
- **flock_homing_bursts**: flock_pass_usec=655, neighbor_checks=51321
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
