# BlastBullets2D benchmark: latest run

- run: `2026-10-20T10-49-50Z` commit `1249ed9`
- machine: AMD Ryzen 7 8840HS w/ Radeon 780M Graphics (16 threads), Linux-7.2.10-1-cachyos-x86_64-with-glibc2.44
- godot: 4.7.2-stable (arch_linux), debug build: True, repeats: 5 (median)
- baseline: `1249ed9` (2026-10-20T10-45-08Z)

Times are milliseconds of CPU per frame (simulated time, `--fixed-fps 60`).
`frame` = step + engine; `step` = the scenario's own plugin calls (spawns...);
`engine` = physics server step + factory tick + drain + render; `tick` = factory physics tick only.

| scenario | frame p50 | frame p95 | frame p99 | frame max | step max | engine max | tick p50 | tick p99 | bullets | mem peak MB | objects +/- | vs baseline |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---|
| attachments_500 | 0.047 | 0.052 | 0.159 | 0.363 | 0.239 | 0.159 | 0.027 | 0.098 | 453 | 34.3 | +0 | frame p50 +0%, tick p50 +0% |
| bounce_box_2k | 0.122 | 0.130 | 0.140 | 0.179 | 0.001 | 0.179 | 0.058 | 0.067 | 2000 | 31.1 | +0 | frame p50 +0%, tick p50 +2% |
| cold_spawn_scaling | 0.004 | 0.004 | 0.005 | 0.005 | 0.001 | 0.004 | 0.000 | 0.001 | 0 | 52.6 | +0 | frame p50 +0%, tick p50 +0% |
| collision_storm | 0.722 | 1.070 | 2.269 | 2.562 | 0.117 | 2.500 | 0.176 | 0.309 | 1400 | 31.9 | +0 | frame p50 -1%, tick p50 +1% |
| debugger_10k_flight | 0.601 | 0.637 | 0.666 | 2.255 | 0.002 | 2.255 | 0.279 | 0.306 | 10000 | 34.2 | +0 | frame p50 +0%, tick p50 +0% |
| factory_warm_spawn_5k | 12.666 | 17.178 | 18.361 | 20.522 | 4.790 | 17.041 | 3.250 | 5.595 | 90000 | 66.3 | +0 | frame p50 -1%, tick p50 -0% |
| flock_10k_flight | 4.798 | 5.098 | 6.675 | 6.799 | 0.007 | 6.798 | 4.418 | 6.223 | 10000 | 33.7 | +0 | frame p50 -0%, tick p50 +0% |
| flock_10k_interval | 2.641 | 2.751 | 4.373 | 4.611 | 0.014 | 4.610 | 2.253 | 3.921 | 10000 | 33.7 | +0 | frame p50 -0%, tick p50 +0% |
| flock_2k_swarm | 0.891 | 0.975 | 1.377 | 2.663 | 0.014 | 2.661 | 0.807 | 1.108 | 2000 | 31.1 | +0 | frame p50 +0%, tick p50 +0% |
| flock_5k_stacked | 2.592 | 2.678 | 4.329 | 4.464 | 0.005 | 4.462 | 2.380 | 4.102 | 5000 | 45.5 | +0 | frame p50 -0%, tick p50 -0% |
| flock_homing_bursts | 1.143 | 1.261 | 1.602 | 2.921 | 0.199 | 2.913 | 1.075 | 1.346 | 1466 | 31.0 | -4 | frame p50 -1%, tick p50 -0% |
| graze_10k_flight | 0.607 | 0.640 | 0.678 | 2.291 | 0.005 | 2.290 | 0.314 | 0.356 | 10000 | 33.7 | +0 | frame p50 -1%, tick p50 -1% |
| graze_64_targets | 0.686 | 0.737 | 0.863 | 2.401 | 0.030 | 2.388 | 0.381 | 0.439 | 10000 | 33.8 | +0 | frame p50 -1%, tick p50 +0% |
| graze_storm | 0.975 | 5.249 | 8.187 | 10.730 | 1.195 | 10.729 | 0.269 | 1.474 | 7867 | 39.5 | +0 | frame p50 -2%, tick p50 -1% |
| hit_handlers_2k | 1.355 | 1.836 | 2.942 | 3.247 | 1.096 | 3.159 | 0.386 | 0.554 | 1870 | 37.2 | +318 | frame p50 +1%, tick p50 +3% |
| homing_2k_moving_targets | 0.177 | 0.190 | 0.195 | 0.212 | 0.005 | 0.210 | 0.109 | 0.122 | 2000 | 31.1 | +0 | frame p50 +1%, tick p50 +0% |
| kitchen_sink | 0.494 | 0.627 | 0.884 | 2.290 | 0.007 | 2.289 | 0.295 | 0.348 | 7488 | 34.2 | +171 | frame p50 +1%, tick p50 +1% |
| lifetime_signal_10k | 0.599 | 0.776 | 4.470 | 5.452 | 3.548 | 2.649 | 0.278 | 2.243 | 7375 | 39.7 | +0 | frame p50 +0%, tick p50 +0% |
| mass_expiry_10k | 0.597 | 0.785 | 4.189 | 5.336 | 2.452 | 2.566 | 0.277 | 0.793 | 7375 | 39.7 | +0 | frame p50 +0%, tick p50 -0% |
| multi_spawner_pooled_8x | 0.890 | 1.283 | 1.650 | 2.923 | 0.009 | 2.922 | 0.444 | 0.844 | 10157 | 38.7 | +0 | frame p50 -1%, tick p50 +0% |
| pickup_convert_5k | 0.754 | 1.497 | 5.078 | 8.286 | 4.719 | 3.901 | 0.226 | 1.191 | 3352 | 41.5 | +0 | frame p50 +0%, tick p50 +0% |
| pool_churn | 0.790 | 0.861 | 1.086 | 2.517 | 0.292 | 2.450 | 0.287 | 0.329 | 9000 | 34.8 | +0 | frame p50 +1%, tick p50 +2% |
| sound_flight_10k | 1.490 | 1.553 | 3.134 | 3.245 | 0.002 | 3.245 | 1.197 | 2.202 | 10000 | 33.8 | +14 | frame p50 -0%, tick p50 -0% |
| sound_hit_storm | 0.825 | 1.070 | 1.785 | 2.585 | 0.532 | 2.476 | 0.182 | 0.306 | 1400 | 31.9 | +0 | frame p50 +6%, tick p50 +3% |
| spawner_aimed_regen_2k | 2.753 | 3.326 | 4.825 | 5.007 | 2.008 | 4.553 | 1.237 | 2.882 | 36000 | 42.9 | +0 | frame p50 +1%, tick p50 +1% |
| spawner_moving_preview_1500 | 0.584 | 1.264 | 2.331 | 3.232 | 0.005 | 3.231 | 0.332 | 0.507 | 11550 | 35.6 | +15 | frame p50 +1%, tick p50 -0% |
| spawner_path_move_1500 | 0.880 | 1.638 | 2.907 | 3.308 | 0.002 | 3.308 | 0.505 | 0.716 | 17780 | 38.1 | +15 | frame p50 +0%, tick p50 +1% |
| spawner_spin_preview_1500 | 0.598 | 1.470 | 4.377 | 8.709 | 0.002 | 8.709 | 0.332 | 0.494 | 11550 | 40.5 | +15 | frame p50 -1%, tick p50 -2% |
| spawner_warm_shot_5k | 13.126 | 18.041 | 20.059 | 21.354 | 3.595 | 18.887 | 3.492 | 5.759 | 90000 | 159.9 | +0 | frame p50 -1%, tick p50 -1% |
| split_storm | 0.208 | 1.335 | 7.220 | 21.096 | 0.213 | 21.095 | 0.145 | 6.631 | 1675 | 59.3 | +0 | frame p50 -1%, tick p50 -1% |
| trails_fx_2k | 0.357 | 0.632 | 3.617 | 3.902 | 3.272 | 2.097 | 0.289 | 0.577 | 1967 | 32.3 | +0 | frame p50 -1%, tick p50 -0% |
| volley_10k_flight | 0.576 | 0.606 | 0.633 | 2.247 | 0.002 | 2.247 | 0.283 | 0.312 | 10000 | 33.7 | +0 | frame p50 +0%, tick p50 +0% |

## Scenario extras

- **cold_spawn_scaling**: cold_1000_ms=0.536, cold_2000_ms=1.242, cold_4000_ms=3.275, cold_8000_ms=9.688, scaling_8k_over_1k=17.823
- **collision_storm**: records_per_frame=200.000
- **flock_10k_flight**: flock_pass_usec=3447, neighbor_checks=327819
- **flock_10k_interval**: flock_pass_usec=1476, neighbor_checks=108825, queried_members=3330
- **flock_2k_swarm**: flock_pass_usec=744, neighbor_checks=82784
- **flock_5k_stacked**: flock_pass_usec=2007, neighbor_checks=160598
- **flock_homing_bursts**: flock_pass_usec=674, neighbor_checks=51321
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
