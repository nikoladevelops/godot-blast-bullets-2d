# BlastBullets2D benchmark: latest run

- run: `2026-10-20T12-51-48Z` commit `f62e6d5`
- machine: AMD Ryzen 7 8840HS w/ Radeon 780M Graphics (16 threads), Linux-7.2.10-1-cachyos-x86_64-with-glibc2.44
- godot: 4.7.2-stable (arch_linux), debug build: True, repeats: 5 (median)
- baseline: `1249ed9` (2026-10-20T10-45-08Z)

Times are milliseconds of CPU per frame (simulated time, `--fixed-fps 60`).
`frame` = step + engine; `step` = the scenario's own plugin calls (spawns...);
`engine` = physics server step + factory tick + drain + render; `tick` = factory physics tick only.

| scenario | frame p50 | frame p95 | frame p99 | frame max | step max | engine max | tick p50 | tick p99 | bullets | mem peak MB | objects +/- | vs baseline |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---|
| attachments_500 | 0.047 | 0.052 | 0.156 | 0.360 | 0.233 | 0.156 | 0.027 | 0.098 | 453 | 34.3 | +0 | frame p50 +0%, tick p50 +0% |
| bounce_box_2k | 0.123 | 0.131 | 0.138 | 0.402 | 0.001 | 0.401 | 0.059 | 0.064 | 2000 | 31.1 | +0 | frame p50 +1%, tick p50 +4% |
| cold_spawn_scaling | 0.004 | 0.004 | 0.005 | 0.005 | 0.001 | 0.004 | 0.000 | 0.001 | 0 | 52.6 | +0 | frame p50 +0%, tick p50 +0% |
| collision_storm | 0.795 | 1.064 | 2.136 | 2.562 | 0.267 | 2.422 | 0.179 | 0.301 | 1400 | 31.9 | +0 | frame p50 +9%, tick p50 +2% |
| debugger_10k_flight | 0.610 | 0.640 | 0.718 | 2.285 | 0.002 | 2.285 | 0.289 | 0.309 | 10000 | 34.3 | +0 | frame p50 +2%, tick p50 +4% |
| factory_warm_spawn_5k | 12.760 | 17.060 | 18.553 | 19.277 | 4.474 | 16.537 | 3.332 | 5.683 | 90000 | 66.3 | +0 | frame p50 +0%, tick p50 +2% |
| flock_10k_flight | 4.811 | 5.206 | 6.732 | 6.874 | 0.007 | 6.873 | 4.415 | 6.282 | 10000 | 33.7 | +0 | frame p50 +0%, tick p50 -0% |
| flock_10k_interval | 2.657 | 2.777 | 4.425 | 4.529 | 0.007 | 4.527 | 2.265 | 3.966 | 10000 | 33.7 | +0 | frame p50 +0%, tick p50 +1% |
| flock_2k_swarm | 0.893 | 0.978 | 1.019 | 2.586 | 0.011 | 2.584 | 0.808 | 0.930 | 2000 | 31.1 | +0 | frame p50 +0%, tick p50 +0% |
| flock_5k_stacked | 2.601 | 2.676 | 4.286 | 4.434 | 0.007 | 4.433 | 2.387 | 4.031 | 5000 | 45.5 | +0 | frame p50 -0%, tick p50 -0% |
| flock_homing_bursts | 1.167 | 1.283 | 2.650 | 2.966 | 0.163 | 2.952 | 1.099 | 1.886 | 1466 | 31.0 | -4 | frame p50 +1%, tick p50 +2% |
| graze_10k_flight | 0.611 | 0.642 | 0.662 | 2.267 | 0.004 | 2.266 | 0.318 | 0.345 | 10000 | 33.7 | +0 | frame p50 -0%, tick p50 +0% |
| graze_64_targets | 0.693 | 0.747 | 0.796 | 2.364 | 0.037 | 2.352 | 0.386 | 0.438 | 10000 | 33.8 | +0 | frame p50 +0%, tick p50 +1% |
| graze_storm | 0.979 | 5.444 | 8.227 | 10.780 | 1.218 | 10.779 | 0.275 | 1.444 | 7867 | 39.5 | +0 | frame p50 -1%, tick p50 +1% |
| hit_handlers_2k | 1.343 | 1.817 | 2.856 | 3.312 | 0.425 | 3.084 | 0.389 | 0.670 | 1870 | 37.2 | +318 | frame p50 -0%, tick p50 +4% |
| homing_2k_moving_targets | 0.152 | 0.160 | 0.173 | 0.318 | 0.003 | 0.317 | 0.086 | 0.098 | 2000 | 31.1 | +0 | frame p50 -13%, tick p50 -21% |
| kitchen_sink | 0.501 | 0.641 | 0.937 | 2.227 | 0.006 | 2.227 | 0.305 | 0.380 | 7488 | 34.2 | +171 | frame p50 +2%, tick p50 +5% |
| lifetime_signal_10k | 0.601 | 0.789 | 4.425 | 5.199 | 2.672 | 2.614 | 0.283 | 2.205 | 7375 | 39.7 | +0 | frame p50 +0%, tick p50 +2% |
| mass_expiry_10k | 0.599 | 0.790 | 4.154 | 4.637 | 2.757 | 2.452 | 0.282 | 0.794 | 7375 | 39.7 | +0 | frame p50 +1%, tick p50 +1% |
| multi_spawner_pooled_8x | 0.891 | 1.278 | 1.527 | 2.881 | 0.004 | 2.879 | 0.463 | 0.817 | 10157 | 38.7 | +0 | frame p50 -1%, tick p50 +4% |
| pickup_convert_5k | 0.810 | 1.565 | 5.061 | 7.386 | 4.315 | 3.844 | 0.228 | 1.212 | 3353 | 41.5 | +0 | frame p50 +7%, tick p50 +1% |
| pool_churn | 0.796 | 0.868 | 0.976 | 2.504 | 0.460 | 2.426 | 0.290 | 0.332 | 9000 | 34.8 | +0 | frame p50 +2%, tick p50 +3% |
| sound_flight_10k | 1.499 | 1.543 | 2.520 | 3.238 | 0.002 | 3.238 | 1.208 | 1.430 | 10000 | 33.8 | +0 | frame p50 +0%, tick p50 +1% |
| sound_hit_storm | 0.738 | 1.070 | 1.103 | 2.586 | 0.127 | 2.497 | 0.177 | 0.304 | 1400 | 31.9 | +0 | frame p50 -6%, tick p50 +1% |
| spawner_aimed_regen_2k | 2.741 | 3.339 | 4.622 | 4.886 | 0.749 | 4.436 | 1.242 | 2.187 | 36000 | 42.9 | +0 | frame p50 +0%, tick p50 +1% |
| spawner_moving_preview_1500 | 0.588 | 1.260 | 2.327 | 3.825 | 0.005 | 3.824 | 0.343 | 0.499 | 11550 | 35.7 | +15 | frame p50 +1%, tick p50 +3% |
| spawner_path_move_1500 | 0.894 | 1.688 | 2.939 | 3.827 | 0.002 | 3.827 | 0.516 | 0.700 | 17780 | 38.1 | +15 | frame p50 +2%, tick p50 +3% |
| spawner_spin_preview_1500 | 0.606 | 1.480 | 4.207 | 8.522 | 0.001 | 8.522 | 0.339 | 0.526 | 11550 | 40.5 | +15 | frame p50 -0%, tick p50 +0% |
| spawner_warm_shot_5k | 13.368 | 18.434 | 21.110 | 21.905 | 3.525 | 19.957 | 3.582 | 6.007 | 90000 | 159.9 | +0 | frame p50 +0%, tick p50 +2% |
| split_storm | 0.232 | 1.622 | 8.123 | 18.910 | 0.246 | 18.909 | 0.170 | 7.295 | 1675 | 59.3 | +0 | frame p50 +10%, tick p50 +16% |
| trails_fx_2k | 0.355 | 0.626 | 3.584 | 3.837 | 3.158 | 2.037 | 0.286 | 0.567 | 1967 | 32.3 | +0 | frame p50 -1%, tick p50 -1% |
| volley_10k_flight | 0.596 | 0.650 | 0.717 | 2.307 | 0.002 | 2.307 | 0.297 | 0.351 | 10000 | 33.7 | +0 | frame p50 +4%, tick p50 +5% |

## Scenario extras

- **cold_spawn_scaling**: cold_1000_ms=0.537, cold_2000_ms=1.247, cold_4000_ms=3.225, cold_8000_ms=10.089, scaling_8k_over_1k=18.964
- **collision_storm**: records_per_frame=200.000
- **flock_10k_flight**: flock_pass_usec=3415, neighbor_checks=327819
- **flock_10k_interval**: flock_pass_usec=1486, neighbor_checks=108825, queried_members=3330
- **flock_2k_swarm**: flock_pass_usec=745, neighbor_checks=82784
- **flock_5k_stacked**: flock_pass_usec=2010, neighbor_checks=160598
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
