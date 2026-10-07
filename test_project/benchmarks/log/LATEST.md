# BlastBullets2D benchmark: latest run

- run: `2026-10-07T18-35-28Z` commit `05b62d4`
- machine: AMD Ryzen 7 8840HS w/ Radeon 780M Graphics (16 threads), Linux-7.2.9-1-cachyos-x86_64-with-glibc2.44
- godot: 4.7.2-stable (arch_linux), debug build: True, repeats: 5 (median)
- baseline: `cedbd11` (2026-10-06T05-29-10Z)

Times are milliseconds of CPU per frame (simulated time, `--fixed-fps 60`).
`frame` = step + engine; `step` = the scenario's own plugin calls (spawns...);
`engine` = physics server step + factory tick + drain + render; `tick` = factory physics tick only.

| scenario | frame p50 | frame p95 | frame p99 | frame max | step max | engine max | tick p50 | tick p99 | bullets | mem peak MB | objects +/- | vs baseline |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---|
| graze_10k_flight | 0.609 | 0.640 | 0.683 | 2.313 | 0.006 | 2.312 | 0.308 | 0.344 | 10000 | 32.7 | +0 | frame p50 +4%, tick p50 +5% |
| volley_10k_flight | 0.573 | 0.604 | 0.800 | 2.304 | 0.002 | 2.304 | 0.275 | 0.315 | 10000 | 32.7 | +0 | frame p50 +5%, tick p50 +6% |

## Scenario extras

- **graze_10k_flight**: grazes=500

## Scenarios

- **graze_10k_flight**: 10k bullets in flight, graze armed (3 rings, 1 moving target)
- **volley_10k_flight**: 10k bullets in flight, no overlaps
