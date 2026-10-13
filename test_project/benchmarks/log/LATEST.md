# BlastBullets2D benchmark: latest run

- run: `2026-10-13T13-09-18Z` commit `cae8fde`
- machine: AMD Ryzen 7 8840HS w/ Radeon 780M Graphics (16 threads), Linux-7.2.9-2-cachyos-x86_64-with-glibc2.44
- godot: 4.7.2-stable (arch_linux), debug build: True, repeats: 5 (median)
- baseline: `cedbd11` (2026-10-06T05-29-10Z)

Times are milliseconds of CPU per frame (simulated time, `--fixed-fps 60`).
`frame` = step + engine; `step` = the scenario's own plugin calls (spawns...);
`engine` = physics server step + factory tick + drain + render; `tick` = factory physics tick only.

| scenario | frame p50 | frame p95 | frame p99 | frame max | step max | engine max | tick p50 | tick p99 | bullets | mem peak MB | objects +/- | vs baseline |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---|
| attachments_500 | 0.087 | 0.115 | 0.430 | 1.079 | 0.787 | 0.430 | 0.049 | 0.226 | 453 | 34.0 | +0 | frame p50 +93%, tick p50 +88% |
| bounce_box_2k | 0.181 | 0.203 | 0.213 | 0.232 | 0.001 | 0.232 | 0.084 | 0.104 | 2000 | 30.7 | +0 | **REGRESSION**: frame_ms.p50 0.113->0.181 ms (+60%) |
| cold_spawn_scaling | 0.005 | 0.006 | 0.007 | 0.007 | 0.001 | 0.006 | 0.000 | 0.001 | 0 | 52.3 | +0 | frame p50 +25%, tick p50 +0% |
| collision_storm | 1.333 | 1.486 | 1.667 | 2.245 | 0.380 | 2.064 | 0.310 | 0.436 | 1400 | 31.5 | +0 | **REGRESSION**: frame_ms.p50 0.682->1.333 ms (+95%); factory_tick_ms.p50 0.144->0.310 ms (+115%) |
| factory_warm_spawn_5k | 22.171 | 24.785 | 25.695 | 26.051 | 5.458 | 21.625 | 5.127 | 6.163 | 90000 | 65.9 | +0 | **REGRESSION**: frame_ms.p50 13.107->22.171 ms (+69%); frame_ms.p95 14.554->24.785 ms (+70%); factory_tick_ms.p50 3.163->5.127 ms (+62%); factory_tick_ms.p95 3.695->5.845 ms (+58%) |
| flock_10k_flight | 4.959 | 5.585 | 5.827 | 5.948 | 0.019 | 5.945 | 4.271 | 4.975 | 10000 | 33.3 | +0 | new |
| flock_10k_interval | 3.164 | 3.512 | 3.619 | 3.683 | 0.013 | 3.679 | 2.559 | 2.947 | 10000 | 33.3 | +0 | new |
| flock_2k_swarm | 1.397 | 1.466 | 1.504 | 1.747 | 0.008 | 1.739 | 1.272 | 1.371 | 2000 | 30.7 | +0 | new |
| flock_5k_stacked | 2.388 | 2.496 | 2.562 | 2.728 | 0.012 | 2.720 | 2.052 | 2.168 | 5000 | 34.3 | +0 | new |
| graze_10k_flight | 0.905 | 0.987 | 1.031 | 1.113 | 0.010 | 1.110 | 0.455 | 0.514 | 10000 | 33.3 | +0 | **REGRESSION**: frame_ms.p50 0.587->0.905 ms (+54%); frame_ms.p95 0.612->0.987 ms (+61%); factory_tick_ms.p50 0.294->0.455 ms (+55%) |
| graze_64_targets | 1.019 | 1.142 | 1.191 | 1.379 | 0.056 | 1.356 | 0.554 | 0.664 | 10000 | 33.5 | +0 | **REGRESSION**: frame_ms.p50 0.686->1.019 ms (+49%); frame_ms.p95 0.759->1.142 ms (+50%); factory_tick_ms.p50 0.383->0.554 ms (+45%) |
| graze_storm | 1.477 | 7.481 | 12.836 | 16.653 | 1.674 | 16.652 | 0.394 | 2.043 | 7867 | 39.1 | +0 | **REGRESSION**: frame_ms.p50 0.929->1.477 ms (+59%); frame_ms.p95 4.744->7.481 ms (+58%); factory_tick_ms.p50 0.238->0.394 ms (+66%); factory_tick_ms.p95 1.260->1.999 ms (+59%) |
| hit_handlers_2k | 2.226 | 2.820 | 3.265 | 3.791 | 0.529 | 3.659 | 0.596 | 1.094 | 1870 | 36.8 | +318 | **REGRESSION**: frame_ms.p50 1.311->2.226 ms (+70%); frame_ms.p95 2.020->2.820 ms (+40%); factory_tick_ms.p50 0.297->0.596 ms (+101%); factory_tick_ms.p95 0.559->0.973 ms (+74%) |
| homing_2k_moving_targets | 0.336 | 0.383 | 0.406 | 0.457 | 0.012 | 0.455 | 0.207 | 0.255 | 2000 | 30.7 | +0 | **REGRESSION**: frame_ms.p50 0.166->0.336 ms (+102%); factory_tick_ms.p50 0.102->0.207 ms (+103%) |
| kitchen_sink | 0.738 | 1.032 | 1.231 | 1.799 | 0.011 | 1.797 | 0.431 | 0.620 | 7488 | 33.8 | +171 | **REGRESSION**: frame_ms.p50 0.473->0.738 ms (+56%); frame_ms.p95 0.615->1.032 ms (+68%); factory_tick_ms.p50 0.275->0.431 ms (+57%) |
| lifetime_signal_10k | 0.903 | 1.218 | 6.766 | 10.054 | 7.163 | 3.762 | 0.404 | 3.304 | 7375 | 39.4 | +0 | **REGRESSION**: frame_ms.p50 0.579->0.903 ms (+56%); frame_ms.p95 0.767->1.218 ms (+59%); factory_tick_ms.p50 0.262->0.404 ms (+54%) |
| mass_expiry_10k | 0.899 | 1.216 | 7.246 | 9.613 | 6.622 | 3.835 | 0.401 | 1.249 | 7375 | 39.4 | +0 | **REGRESSION**: frame_ms.p50 0.581->0.899 ms (+55%); frame_ms.p95 0.758->1.216 ms (+60%); factory_tick_ms.p50 0.261->0.401 ms (+54%) |
| pickup_convert_5k | 1.135 | 2.161 | 8.044 | 11.076 | 6.575 | 5.470 | 0.341 | 1.804 | 3352 | 41.2 | +0 | new |
| pool_churn | 1.203 | 1.359 | 1.422 | 1.465 | 0.289 | 1.336 | 0.417 | 0.497 | 9000 | 34.5 | +0 | **REGRESSION**: frame_ms.p50 0.762->1.203 ms (+58%); frame_ms.p95 0.821->1.359 ms (+66%); factory_tick_ms.p50 0.268->0.417 ms (+56%) |
| sound_flight_10k | 2.284 | 2.413 | 2.453 | 2.499 | 0.004 | 2.499 | 1.833 | 1.925 | 10000 | 33.4 | +5 | **REGRESSION**: frame_ms.p50 1.507->2.284 ms (+52%); frame_ms.p95 1.575->2.413 ms (+53%); factory_tick_ms.p50 1.212->1.833 ms (+51%); factory_tick_ms.p95 1.242->1.900 ms (+53%) |
| sound_hit_storm | 1.290 | 1.366 | 1.398 | 2.792 | 0.196 | 2.662 | 0.310 | 0.348 | 1400 | 31.5 | +0 | **REGRESSION**: frame_ms.p50 0.694->1.290 ms (+86%); frame_ms.p95 1.025->1.366 ms (+33%); factory_tick_ms.p50 0.149->0.310 ms (+108%) |
| spawner_aimed_regen_2k | 4.221 | 4.792 | 4.992 | 5.182 | 0.895 | 4.543 | 1.836 | 2.264 | 36000 | 42.5 | +0 | **REGRESSION**: frame_ms.p50 2.623->4.221 ms (+61%); frame_ms.p95 2.871->4.792 ms (+67%); factory_tick_ms.p50 1.137->1.836 ms (+61%); factory_tick_ms.p95 1.233->2.068 ms (+68%) |
| spawner_moving_preview_1500 | 0.910 | 1.977 | 4.109 | 5.859 | 0.010 | 5.858 | 0.509 | 0.802 | 11550 | 35.3 | +15 | **REGRESSION**: frame_ms.p50 0.578->0.910 ms (+57%); frame_ms.p95 1.286->1.977 ms (+54%); factory_tick_ms.p50 0.322->0.509 ms (+58%) |
| spawner_path_move_1500 | 1.299 | 2.537 | 5.004 | 5.903 | 0.004 | 5.902 | 0.737 | 1.039 | 17780 | 37.8 | +15 | **REGRESSION**: frame_ms.p50 0.888->1.299 ms (+46%); frame_ms.p95 1.776->2.537 ms (+43%); factory_tick_ms.p50 0.494->0.737 ms (+49%); factory_tick_ms.p95 0.651->0.959 ms (+47%) |
| spawner_spin_preview_1500 | 0.880 | 2.246 | 6.574 | 13.247 | 0.004 | 13.247 | 0.483 | 0.741 | 11550 | 40.1 | +15 | **REGRESSION**: frame_ms.p50 0.580->0.880 ms (+52%); frame_ms.p95 1.440->2.246 ms (+56%); factory_tick_ms.p50 0.314->0.483 ms (+54%) |
| spawner_warm_shot_5k | 20.053 | 23.511 | 24.792 | 25.769 | 2.955 | 23.255 | 5.122 | 5.981 | 90000 | 159.5 | +0 | **REGRESSION**: frame_ms.p50 12.730->20.053 ms (+58%); frame_ms.p95 14.745->23.511 ms (+59%); factory_tick_ms.p50 3.286->5.122 ms (+56%); factory_tick_ms.p95 3.594->5.782 ms (+61%) |
| trails_fx_2k | 0.528 | 0.906 | 5.644 | 5.968 | 4.962 | 1.041 | 0.424 | 0.810 | 1967 | 31.9 | +0 | **REGRESSION**: frame_ms.p50 0.341->0.528 ms (+55%); frame_ms.p95 0.589->0.906 ms (+54%); factory_tick_ms.p50 0.274->0.424 ms (+55%) |
| volley_10k_flight | 0.865 | 1.006 | 1.083 | 1.113 | 0.003 | 1.113 | 0.424 | 0.501 | 10000 | 33.3 | +0 | **REGRESSION**: frame_ms.p50 0.548->0.865 ms (+58%); frame_ms.p95 0.567->1.006 ms (+77%); factory_tick_ms.p50 0.259->0.424 ms (+64%) |

## Scenario extras

- **cold_spawn_scaling**: cold_1000_ms=0.837, cold_2000_ms=2.093, cold_4000_ms=6.349, cold_8000_ms=16.874, scaling_8k_over_1k=20.160
- **collision_storm**: records_per_frame=200.000
- **flock_10k_flight**: flock_pass_usec=3842, neighbor_checks=370801
- **flock_10k_interval**: flock_pass_usec=1786, neighbor_checks=122852, queried_members=3330
- **flock_2k_swarm**: flock_pass_usec=1027, neighbor_checks=72174
- **flock_5k_stacked**: flock_pass_usec=1571, neighbor_checks=165966
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
