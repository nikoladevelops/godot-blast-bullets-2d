# BlastBullets2D benchmark: latest run

- run: `2026-10-13T14-20-34Z` commit `7fe53f4` (dirty src/)
- machine: AMD Ryzen 7 8840HS w/ Radeon 780M Graphics (16 threads), Linux-7.2.9-2-cachyos-x86_64-with-glibc2.44
- godot: 4.7.2-stable (arch_linux), debug build: True, repeats: 5 (median)
- baseline: `cedbd11` (2026-10-06T05-29-10Z)

Times are milliseconds of CPU per frame (simulated time, `--fixed-fps 60`).
`frame` = step + engine; `step` = the scenario's own plugin calls (spawns...);
`engine` = physics server step + factory tick + drain + render; `tick` = factory physics tick only.

| scenario | frame p50 | frame p95 | frame p99 | frame max | step max | engine max | tick p50 | tick p99 | bullets | mem peak MB | objects +/- | vs baseline |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---|
| flock_10k_flight | 5.675 | 6.334 | 7.306 | 7.704 | 0.016 | 7.701 | 5.072 | 6.611 | 10000 | 33.4 | +0 | new |
| flock_10k_interval | 3.454 | 3.810 | 3.954 | 4.102 | 0.011 | 4.099 | 2.839 | 3.232 | 10000 | 33.4 | +0 | new |
| flock_2k_swarm | 1.564 | 1.645 | 1.743 | 2.984 | 0.007 | 2.982 | 1.435 | 1.573 | 2000 | 30.7 | +0 | new |
| flock_5k_stacked | 2.657 | 2.816 | 2.929 | 3.052 | 0.013 | 3.050 | 2.309 | 2.493 | 5000 | 34.3 | +0 | new |

## Scenario extras

- **flock_10k_flight**: flock_pass_usec=4659, neighbor_checks=360928
- **flock_10k_interval**: flock_pass_usec=2011, neighbor_checks=119620, queried_members=3330
- **flock_2k_swarm**: flock_pass_usec=1125, neighbor_checks=63800
- **flock_5k_stacked**: flock_pass_usec=1875, neighbor_checks=163679

## Scenarios

- **flock_10k_flight**: 10k bullets in flight, 10 Per Volley flocks (dense, capped)
- **flock_10k_interval**: 10k bullets, 10 Per Volley flocks, steering refreshed every 3 ticks
- **flock_2k_swarm**: 2k bullets, one Shared flock (default rules)
- **flock_5k_stacked**: 5k bullets stacked on one point, one flock (cap bound)
