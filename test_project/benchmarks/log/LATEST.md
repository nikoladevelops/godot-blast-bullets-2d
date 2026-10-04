# BlastBullets2D benchmark: latest run

- run: `2026-10-04T05-21-56Z` commit `78f4155` (dirty src/)
- machine: AMD Ryzen 7 8840HS w/ Radeon 780M Graphics (16 threads), Linux-7.2.8-2-cachyos-x86_64-with-glibc2.44
- godot: 4.7.2-stable (arch_linux), debug build: True, repeats: 5 (median)
- baseline: `5ed265d` (2026-10-02T16-53-28Z)

Times are milliseconds of CPU per frame (simulated time, `--fixed-fps 60`).
`frame` = step + engine; `step` = the scenario's own plugin calls (spawns...);
`engine` = physics server step + factory tick + drain + render; `tick` = factory physics tick only.

| scenario | frame p50 | frame p95 | frame p99 | frame max | step max | engine max | tick p50 | tick p99 | bullets | mem peak MB | objects +/- | vs baseline |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---|
| bounce_box_2k | 0.119 | 0.130 | 0.138 | 0.999 | 0.002 | 0.999 | 0.056 | 0.070 | 2000 | 29.6 | +0 | frame p50 -5%, tick p50 +2% |
| collision_storm | 0.676 | 1.274 | 2.236 | 2.747 | 0.550 | 2.600 | 0.140 | 0.312 | 1400 | 30.4 | +0 | frame p50 -9%, tick p50 -22% |
| homing_2k_moving_targets | 0.166 | 0.174 | 0.190 | 1.218 | 0.009 | 1.209 | 0.101 | 0.118 | 2000 | 29.6 | +0 | frame p50 -1%, tick p50 +4% |
| kitchen_sink | 0.474 | 0.638 | 0.829 | 1.966 | 0.005 | 1.965 | 0.283 | 0.356 | 7488 | 33.0 | +171 | frame p50 -4%, tick p50 +2% |
| mass_expiry_10k | 0.586 | 0.766 | 4.824 | 5.584 | 3.084 | 2.563 | 0.269 | 1.556 | 7375 | 38.7 | +0 | frame p50 -1%, tick p50 +0% |
| pool_churn | 0.813 | 0.875 | 0.961 | 2.650 | 0.154 | 2.499 | 0.298 | 0.356 | 9000 | 33.8 | +0 | frame p50 -9%, tick p50 -5% |
| volley_10k_flight | 0.561 | 0.585 | 0.612 | 2.292 | 0.001 | 2.292 | 0.271 | 0.293 | 10000 | 32.7 | +0 | frame p50 -1%, tick p50 +1% |

## Scenario extras

- **collision_storm**: records_per_frame=200.000

## Scenarios

- **bounce_box_2k**: 2k bullets bouncing inside a box (bounce resolution every hit)
- **collision_storm**: 200-bullet volley/frame dying on walls (collision drain + pool churn)
- **homing_2k_moving_targets**: 2k homing bullets chasing 4 moving targets
- **kitchen_sink**: 3 spawners (spin spiral, fan, homing ring) + moving player + walls
- **mass_expiry_10k**: 10k bullets expiring on the same frame (3 waves)
- **pool_churn**: one 300-bullet volley per frame, 0.5 s life (warm-pool spawn + expiry)
- **volley_10k_flight**: 10k bullets in flight, no overlaps
