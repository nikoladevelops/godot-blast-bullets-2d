# BlastBullets2D benchmark: latest run

- run: `2026-10-13T19-05-13Z` commit `7c5f364` (dirty src/)
- machine: AMD Ryzen 7 8840HS w/ Radeon 780M Graphics (16 threads), Linux-7.2.9-2-cachyos-x86_64-with-glibc2.44
- godot: 4.7.2-stable (arch_linux), debug build: True, repeats: 5 (median)
- baseline: `cedbd11` (2026-10-06T05-29-10Z)

Times are milliseconds of CPU per frame (simulated time, `--fixed-fps 60`).
`frame` = step + engine; `step` = the scenario's own plugin calls (spawns...);
`engine` = physics server step + factory tick + drain + render; `tick` = factory physics tick only.

| scenario | frame p50 | frame p95 | frame p99 | frame max | step max | engine max | tick p50 | tick p99 | bullets | mem peak MB | objects +/- | vs baseline |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---|
| flock_10k_flight | 4.873 | 5.067 | 5.129 | 5.248 | 0.008 | 5.245 | 4.459 | 4.642 | 10000 | 33.4 | +0 | new |
| flock_10k_interval | 2.667 | 2.782 | 2.849 | 2.984 | 0.007 | 2.982 | 2.266 | 2.383 | 10000 | 33.4 | +0 | new |
| flock_2k_swarm | 0.893 | 0.974 | 0.991 | 1.019 | 0.005 | 1.017 | 0.809 | 0.902 | 2000 | 30.8 | +0 | new |
| flock_5k_stacked | 2.629 | 2.671 | 2.679 | 2.705 | 0.005 | 2.704 | 2.412 | 2.458 | 5000 | 45.3 | +0 | new |
| flock_homing_bursts | 1.145 | 1.270 | 1.297 | 1.450 | 0.140 | 1.448 | 1.077 | 1.145 | 1466 | 30.8 | -4 | new |
| volley_10k_flight | 0.570 | 0.614 | 0.634 | 0.644 | 0.001 | 0.644 | 0.272 | 0.304 | 10000 | 33.4 | +0 | frame p50 +4%, tick p50 +5% |

## Scenario extras

- **flock_10k_flight**: flock_pass_usec=3478, neighbor_checks=327819
- **flock_10k_interval**: flock_pass_usec=1505, neighbor_checks=108825, queried_members=3330
- **flock_2k_swarm**: flock_pass_usec=754, neighbor_checks=82784
- **flock_5k_stacked**: flock_pass_usec=2035, neighbor_checks=160598
- **flock_homing_bursts**: flock_pass_usec=681, neighbor_checks=51321

## Scenarios

- **flock_10k_flight**: 10k bullets in flight, 10 Per Volley flocks (dense, capped)
- **flock_10k_interval**: 10k bullets, 10 Per Volley flocks, steering refreshed every 3 ticks
- **flock_2k_swarm**: 2k bullets, one Shared flock (default rules)
- **flock_5k_stacked**: 5k bullets stacked on one point, one flock (cap bound)
- **flock_homing_bursts**: 55-bullet homing bursts every 0.15 s, 128 px random sizes, wobble, one default flock
- **volley_10k_flight**: 10k bullets in flight, no overlaps
