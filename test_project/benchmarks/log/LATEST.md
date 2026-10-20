# BlastBullets2D benchmark: latest run

- run: `2026-10-20T16-50-20Z` commit `5de793e`
- machine: AMD Ryzen 7 8840HS w/ Radeon 780M Graphics (16 threads), Linux-7.2.10-1-cachyos-x86_64-with-glibc2.44
- godot: 4.7.2-stable (arch_linux), debug build: True, repeats: 5 (median)
- baseline: `1249ed9` (2026-10-20T10-45-08Z)

Times are milliseconds of CPU per frame (simulated time, `--fixed-fps 60`).
`frame` = step + engine; `step` = the scenario's own plugin calls (spawns...);
`engine` = physics server step + factory tick + drain + render; `tick` = factory physics tick only.

| scenario | frame p50 | frame p95 | frame p99 | frame max | step max | engine max | tick p50 | tick p99 | bullets | mem peak MB | objects +/- | vs baseline |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---|
| attachments_500 | 0.048 | 0.055 | 0.159 | 0.393 | 0.259 | 0.159 | 0.028 | 0.105 | 453 | 34.4 | +0 | frame p50 +2%, tick p50 +4% |
| bounce_box_2k | 0.124 | 0.134 | 0.152 | 0.164 | 0.002 | 0.164 | 0.059 | 0.070 | 2000 | 31.1 | +0 | frame p50 +2%, tick p50 +4% |
| cold_spawn_scaling | 0.004 | 0.004 | 0.005 | 0.005 | 0.001 | 0.004 | 0.000 | 0.000 | 0 | 52.7 | +0 | frame p50 +0%, tick p50 +0% |
| collision_storm | 0.758 | 1.069 | 1.658 | 2.594 | 0.218 | 2.508 | 0.183 | 0.297 | 1400 | 31.9 | +0 | frame p50 +4%, tick p50 +5% |
| debugger_10k_flight | 0.612 | 0.636 | 0.660 | 2.288 | 0.001 | 2.287 | 0.286 | 0.310 | 10000 | 34.3 | +0 | frame p50 +2%, tick p50 +3% |
| factory_warm_spawn_5k | 12.771 | 17.049 | 18.459 | 19.657 | 5.097 | 17.004 | 3.341 | 5.569 | 90000 | 66.3 | +0 | frame p50 +0%, tick p50 +2% |
| flock_10k_flight | 4.838 | 5.147 | 6.695 | 6.984 | 0.015 | 6.982 | 4.448 | 6.208 | 10000 | 33.7 | +0 | frame p50 +1%, tick p50 +1% |
| flock_10k_interval | 2.669 | 2.784 | 4.407 | 4.526 | 0.012 | 4.524 | 2.275 | 3.940 | 10000 | 33.7 | +0 | frame p50 +1%, tick p50 +1% |
| flock_2k_swarm | 0.891 | 0.977 | 1.208 | 2.667 | 0.007 | 2.665 | 0.805 | 1.035 | 2000 | 31.1 | +0 | frame p50 +0%, tick p50 +0% |
| flock_5k_stacked | 2.614 | 2.681 | 4.313 | 4.390 | 0.005 | 4.389 | 2.401 | 4.052 | 5000 | 45.6 | +0 | frame p50 +0%, tick p50 +1% |
| flock_homing_bursts | 1.167 | 1.285 | 1.579 | 2.967 | 0.151 | 2.909 | 1.098 | 1.406 | 1466 | 31.0 | -4 | frame p50 +1%, tick p50 +2% |
| graze_10k_flight | 0.613 | 0.639 | 0.661 | 2.299 | 0.002 | 2.299 | 0.319 | 0.343 | 10000 | 33.7 | +0 | frame p50 +0%, tick p50 +1% |
| graze_64_targets | 0.693 | 0.744 | 0.790 | 2.410 | 0.050 | 2.388 | 0.387 | 0.447 | 10000 | 33.8 | +0 | frame p50 +0%, tick p50 +2% |
| graze_storm | 0.985 | 5.202 | 8.135 | 11.123 | 1.292 | 11.121 | 0.274 | 1.467 | 7867 | 39.5 | +0 | frame p50 -1%, tick p50 +1% |
| hit_handlers_2k | 1.339 | 1.832 | 3.023 | 3.399 | 0.373 | 3.251 | 0.376 | 0.589 | 1870 | 37.2 | +318 | frame p50 -1%, tick p50 +1% |
| homing_2k_moving_targets | 0.154 | 0.162 | 0.177 | 0.203 | 0.005 | 0.202 | 0.086 | 0.098 | 2000 | 31.1 | +0 | frame p50 -12%, tick p50 -21% |
| kitchen_sink | 0.502 | 0.643 | 0.917 | 2.199 | 0.003 | 2.198 | 0.305 | 0.368 | 7488 | 34.2 | +171 | frame p50 +3%, tick p50 +5% |
| lifetime_signal_10k | 0.606 | 0.815 | 4.521 | 7.465 | 4.710 | 2.523 | 0.284 | 2.229 | 7375 | 39.8 | +0 | frame p50 +1%, tick p50 +2% |
| mass_expiry_10k | 0.602 | 0.790 | 4.148 | 5.999 | 2.595 | 2.607 | 0.282 | 0.790 | 7375 | 39.7 | +0 | frame p50 +1%, tick p50 +1% |
| multi_spawner_pooled_8x | 0.893 | 1.278 | 1.888 | 2.893 | 0.008 | 2.891 | 0.452 | 0.834 | 10157 | 38.7 | +0 | frame p50 -1%, tick p50 +2% |
| pickup_convert_5k | 0.813 | 1.581 | 5.069 | 5.969 | 2.911 | 3.408 | 0.228 | 1.237 | 3352 | 41.5 | +0 | frame p50 +8%, tick p50 +1% |
| pool_churn | 0.792 | 0.886 | 1.080 | 2.563 | 0.318 | 2.474 | 0.288 | 0.331 | 9000 | 34.8 | +0 | frame p50 +1%, tick p50 +2% |
| sound_flight_10k | 1.506 | 1.546 | 3.142 | 3.255 | 0.001 | 3.255 | 1.210 | 2.098 | 10000 | 33.8 | -1 | frame p50 +1%, tick p50 +1% |
| sound_hit_storm | 0.709 | 1.074 | 1.111 | 2.469 | 0.484 | 2.379 | 0.176 | 0.301 | 1400 | 31.9 | +0 | frame p50 -9%, tick p50 +0% |
| spawner_aimed_regen_2k | 2.788 | 3.484 | 4.793 | 5.667 | 1.641 | 5.138 | 1.269 | 2.359 | 36000 | 42.9 | +0 | frame p50 +2%, tick p50 +3% |
| spawner_moving_preview_1500 | 0.593 | 1.265 | 2.311 | 3.159 | 0.004 | 3.159 | 0.343 | 0.509 | 11550 | 35.7 | +15 | frame p50 +2%, tick p50 +3% |
| spawner_path_move_1500 | 0.894 | 1.667 | 3.160 | 3.784 | 0.002 | 3.784 | 0.512 | 0.714 | 17780 | 38.1 | +15 | frame p50 +2%, tick p50 +2% |
| spawner_spin_preview_1500 | 0.608 | 1.507 | 4.183 | 8.947 | 0.001 | 8.947 | 0.340 | 0.526 | 11550 | 40.5 | +15 | frame p50 +0%, tick p50 +1% |
| spawner_warm_shot_5k | 13.370 | 18.268 | 20.360 | 22.712 | 3.573 | 19.632 | 3.569 | 5.845 | 90000 | 159.9 | +0 | frame p50 +0%, tick p50 +1% |
| split_storm | 0.217 | 1.677 | 7.296 | 20.488 | 0.193 | 20.488 | 0.153 | 6.400 | 1675 | 59.4 | +0 | frame p50 +3%, tick p50 +4% |
| trails_fx_2k | 0.360 | 0.635 | 3.609 | 4.174 | 3.230 | 1.923 | 0.290 | 0.575 | 1967 | 32.3 | +0 | frame p50 +0%, tick p50 +0% |
| volley_10k_flight | 0.579 | 0.603 | 0.633 | 2.264 | 0.001 | 2.264 | 0.287 | 0.313 | 10000 | 33.7 | +0 | frame p50 +1%, tick p50 +1% |

## Scenario extras

- **cold_spawn_scaling**: cold_1000_ms=0.556, cold_2000_ms=1.258, cold_4000_ms=3.277, cold_8000_ms=9.821, scaling_8k_over_1k=17.709
- **collision_storm**: records_per_frame=200.000
- **flock_10k_flight**: flock_pass_usec=3441, neighbor_checks=327819
- **flock_10k_interval**: flock_pass_usec=1485, neighbor_checks=108825, queried_members=3330
- **flock_2k_swarm**: flock_pass_usec=741, neighbor_checks=82784
- **flock_5k_stacked**: flock_pass_usec=2020, neighbor_checks=160598
- **flock_homing_bursts**: flock_pass_usec=664, neighbor_checks=51321
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
- **debugger_10k_flight**: 10k bullets in flight, collision debugger on
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
