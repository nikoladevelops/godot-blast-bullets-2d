# BlastBullets2D benchmark: latest run

- run: `2026-10-12T18-10-21Z` commit `8a0ca5b`
- machine: AMD Ryzen 7 8840HS w/ Radeon 780M Graphics (16 threads), Linux-7.2.9-2-cachyos-x86_64-with-glibc2.44
- godot: 4.7.2-stable (arch_linux), debug build: True, repeats: 5 (median)
- baseline: `cedbd11` (2026-10-06T05-29-10Z)

Times are milliseconds of CPU per frame (simulated time, `--fixed-fps 60`).
`frame` = step + engine; `step` = the scenario's own plugin calls (spawns...);
`engine` = physics server step + factory tick + drain + render; `tick` = factory physics tick only.

| scenario | frame p50 | frame p95 | frame p99 | frame max | step max | engine max | tick p50 | tick p99 | bullets | mem peak MB | objects +/- | vs baseline |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---|
| attachments_500 | 0.088 | 0.128 | 0.290 | 0.668 | 0.434 | 0.292 | 0.051 | 0.185 | 453 | 34.0 | +0 | frame p50 +96%, tick p50 +96% |
| bounce_box_2k | 0.246 | 0.289 | 0.318 | 0.341 | 0.003 | 0.340 | 0.113 | 0.150 | 2000 | 30.7 | +0 | **REGRESSION**: frame_ms.p50 0.113->0.246 ms (+118%); factory_tick_ms.p50 0.052->0.113 ms (+117%) |
| cold_spawn_scaling | 0.005 | 0.006 | 0.007 | 0.007 | 0.001 | 0.006 | 0.000 | 0.001 | 0 | 52.3 | +0 | frame p50 +25%, tick p50 +0% |
| collision_storm | 1.258 | 1.311 | 1.342 | 2.860 | 0.148 | 2.754 | 0.295 | 0.331 | 1400 | 31.5 | +0 | **REGRESSION**: frame_ms.p50 0.682->1.258 ms (+84%); factory_tick_ms.p50 0.144->0.295 ms (+105%) |
| factory_warm_spawn_5k | 18.319 | 19.230 | 19.541 | 19.936 | 2.067 | 18.212 | 4.562 | 4.952 | 90000 | 65.9 | +0 | **REGRESSION**: frame_ms.p50 13.107->18.319 ms (+40%); frame_ms.p95 14.554->19.230 ms (+32%); factory_tick_ms.p50 3.163->4.562 ms (+44%); factory_tick_ms.p95 3.695->4.850 ms (+31%) |
| flock_10k_flight | 4.858 | 5.444 | 5.581 | 5.651 | 0.010 | 5.649 | 4.254 | 4.942 | 10000 | 33.3 | +0 | new |
| flock_10k_interval | 3.154 | 3.469 | 3.565 | 3.631 | 0.011 | 3.629 | 2.559 | 2.960 | 10000 | 33.3 | +0 | new |
| flock_2k_swarm | 1.398 | 1.478 | 1.511 | 1.672 | 0.009 | 1.669 | 1.275 | 1.364 | 2000 | 30.7 | +0 | new |
| flock_5k_stacked | 2.377 | 2.491 | 2.560 | 2.608 | 0.008 | 2.606 | 2.051 | 2.186 | 5000 | 34.3 | +0 | new |
| graze_10k_flight | 0.890 | 1.020 | 1.095 | 1.112 | 0.009 | 1.110 | 0.452 | 0.533 | 10000 | 33.3 | +0 | **REGRESSION**: frame_ms.p50 0.587->0.890 ms (+52%); frame_ms.p95 0.612->1.020 ms (+67%); factory_tick_ms.p50 0.294->0.452 ms (+54%) |
| graze_64_targets | 1.009 | 1.143 | 1.215 | 1.265 | 0.050 | 1.235 | 0.549 | 0.659 | 10000 | 33.5 | +0 | **REGRESSION**: frame_ms.p50 0.686->1.009 ms (+47%); frame_ms.p95 0.759->1.143 ms (+51%); factory_tick_ms.p50 0.383->0.549 ms (+43%) |
| graze_storm | 1.461 | 7.140 | 12.400 | 16.776 | 0.856 | 16.775 | 0.389 | 2.010 | 7867 | 39.1 | +0 | **REGRESSION**: frame_ms.p50 0.929->1.461 ms (+57%); frame_ms.p95 4.744->7.140 ms (+51%); factory_tick_ms.p50 0.238->0.389 ms (+63%); factory_tick_ms.p95 1.260->1.977 ms (+57%) |
| hit_handlers_2k | 2.140 | 2.502 | 2.686 | 3.907 | 0.461 | 3.610 | 0.545 | 0.713 | 1870 | 36.8 | +318 | **REGRESSION**: frame_ms.p50 1.311->2.140 ms (+63%); frame_ms.p95 2.020->2.502 ms (+24%); factory_tick_ms.p50 0.297->0.545 ms (+84%) |
| homing_2k_moving_targets | 0.331 | 0.370 | 0.391 | 0.403 | 0.011 | 0.397 | 0.206 | 0.237 | 2000 | 30.7 | +0 | **REGRESSION**: frame_ms.p50 0.166->0.331 ms (+99%); factory_tick_ms.p50 0.102->0.206 ms (+102%) |
| kitchen_sink | 0.855 | 1.151 | 1.424 | 1.782 | 0.009 | 1.781 | 0.489 | 0.673 | 7488 | 33.8 | +171 | **REGRESSION**: frame_ms.p50 0.473->0.855 ms (+81%); frame_ms.p95 0.615->1.151 ms (+87%); factory_tick_ms.p50 0.275->0.489 ms (+78%) |
| lifetime_signal_10k | 0.919 | 1.315 | 6.617 | 8.719 | 4.543 | 4.361 | 0.411 | 3.331 | 7375 | 39.4 | +0 | **REGRESSION**: frame_ms.p50 0.579->0.919 ms (+59%); frame_ms.p95 0.767->1.315 ms (+71%); factory_tick_ms.p50 0.262->0.411 ms (+57%) |
| mass_expiry_10k | 0.903 | 1.246 | 6.393 | 7.980 | 3.859 | 4.121 | 0.406 | 1.131 | 7375 | 39.4 | +0 | **REGRESSION**: frame_ms.p50 0.581->0.903 ms (+55%); frame_ms.p95 0.758->1.246 ms (+64%); factory_tick_ms.p50 0.261->0.406 ms (+56%) |
| pickup_convert_5k | 1.165 | 2.219 | 7.745 | 9.197 | 3.664 | 7.198 | 0.338 | 1.715 | 3352 | 41.2 | +0 | new |
| pool_churn | 1.167 | 1.369 | 1.429 | 1.477 | 0.198 | 1.358 | 0.411 | 0.524 | 9000 | 34.5 | +0 | **REGRESSION**: frame_ms.p50 0.762->1.167 ms (+53%); frame_ms.p95 0.821->1.369 ms (+67%); factory_tick_ms.p50 0.268->0.411 ms (+53%) |
| sound_flight_10k | 2.288 | 2.398 | 2.455 | 2.529 | 0.004 | 2.529 | 1.849 | 1.929 | 10000 | 33.4 | +5 | **REGRESSION**: frame_ms.p50 1.507->2.288 ms (+52%); frame_ms.p95 1.575->2.398 ms (+52%); factory_tick_ms.p50 1.212->1.849 ms (+53%); factory_tick_ms.p95 1.242->1.902 ms (+53%) |
| sound_hit_storm | 1.267 | 1.315 | 1.340 | 1.456 | 0.151 | 1.321 | 0.296 | 0.337 | 1400 | 31.5 | +0 | **REGRESSION**: frame_ms.p50 0.694->1.267 ms (+83%); factory_tick_ms.p50 0.149->0.296 ms (+99%) |
| spawner_aimed_regen_2k | 4.056 | 4.432 | 4.486 | 4.604 | 0.731 | 3.970 | 1.750 | 2.086 | 36000 | 42.5 | +0 | **REGRESSION**: frame_ms.p50 2.623->4.056 ms (+55%); frame_ms.p95 2.871->4.432 ms (+54%); factory_tick_ms.p50 1.137->1.750 ms (+54%); factory_tick_ms.p95 1.233->2.046 ms (+66%) |
| spawner_moving_preview_1500 | 0.853 | 1.862 | 3.493 | 4.464 | 0.007 | 4.463 | 0.480 | 0.682 | 11550 | 35.3 | +15 | **REGRESSION**: frame_ms.p50 0.578->0.853 ms (+48%); frame_ms.p95 1.286->1.862 ms (+45%); factory_tick_ms.p50 0.322->0.480 ms (+49%) |
| spawner_path_move_1500 | 1.286 | 2.409 | 4.395 | 4.958 | 0.004 | 4.957 | 0.723 | 1.018 | 17780 | 37.8 | +15 | **REGRESSION**: frame_ms.p50 0.888->1.286 ms (+45%); frame_ms.p95 1.776->2.409 ms (+36%); factory_tick_ms.p50 0.494->0.723 ms (+46%) |
| spawner_spin_preview_1500 | 0.879 | 2.184 | 6.763 | 13.014 | 0.004 | 13.014 | 0.479 | 0.721 | 11550 | 40.1 | +15 | **REGRESSION**: frame_ms.p50 0.580->0.879 ms (+52%); frame_ms.p95 1.440->2.184 ms (+52%); factory_tick_ms.p50 0.314->0.479 ms (+53%) |
| spawner_warm_shot_5k | 18.338 | 19.950 | 20.769 | 21.442 | 2.662 | 18.980 | 4.885 | 5.445 | 90000 | 159.5 | +0 | **REGRESSION**: frame_ms.p50 12.730->18.338 ms (+44%); frame_ms.p95 14.745->19.950 ms (+35%); factory_tick_ms.p50 3.286->4.885 ms (+49%); factory_tick_ms.p95 3.594->5.319 ms (+48%) |
| trails_fx_2k | 0.534 | 0.912 | 5.415 | 5.647 | 4.658 | 0.995 | 0.432 | 0.821 | 1967 | 31.9 | +0 | **REGRESSION**: frame_ms.p50 0.341->0.534 ms (+57%); frame_ms.p95 0.589->0.912 ms (+55%); factory_tick_ms.p50 0.274->0.432 ms (+58%) |
| volley_10k_flight | 0.842 | 0.973 | 1.048 | 1.060 | 0.004 | 1.059 | 0.408 | 0.489 | 10000 | 33.3 | +0 | **REGRESSION**: frame_ms.p50 0.548->0.842 ms (+54%); frame_ms.p95 0.567->0.973 ms (+72%); factory_tick_ms.p50 0.259->0.408 ms (+58%) |

## Scenario extras

- **cold_spawn_scaling**: cold_1000_ms=0.888, cold_2000_ms=1.980, cold_4000_ms=5.129, cold_8000_ms=14.590, scaling_8k_over_1k=16.430
- **collision_storm**: records_per_frame=200.000
- **flock_10k_flight**: flock_pass_usec=3782, neighbor_checks=370801
- **flock_10k_interval**: flock_pass_usec=1748, neighbor_checks=122852, queried_members=3330
- **flock_2k_swarm**: flock_pass_usec=1043, neighbor_checks=72174
- **flock_5k_stacked**: flock_pass_usec=1564, neighbor_checks=165966
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
- **trails_fx_2k**: 2k bullets with trail + spawn-flash layers, refired every 60 frames
- **volley_10k_flight**: 10k bullets in flight, no overlaps
