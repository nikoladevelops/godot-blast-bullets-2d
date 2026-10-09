# BlastBullets2D benchmark: latest run

- run: `2026-10-09T08-07-30Z` commit `2f5c362` (dirty src/)
- machine: AMD Ryzen 7 8840HS w/ Radeon 780M Graphics (16 threads), Linux-7.2.9-2-cachyos-x86_64-with-glibc2.44
- godot: 4.7.2-stable (arch_linux), debug build: True, repeats: 5 (median)
- baseline: `cedbd11` (2026-10-06T05-29-10Z)

Times are milliseconds of CPU per frame (simulated time, `--fixed-fps 60`).
`frame` = step + engine; `step` = the scenario's own plugin calls (spawns...);
`engine` = physics server step + factory tick + drain + render; `tick` = factory physics tick only.

| scenario | frame p50 | frame p95 | frame p99 | frame max | step max | engine max | tick p50 | tick p99 | bullets | mem peak MB | objects +/- | vs baseline |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---|
| sound_flight_10k | 1.559 | 1.631 | 1.884 | 3.378 | 0.003 | 3.378 | 1.256 | 1.555 | 10000 | 32.9 | +2 | new |
| sound_hit_storm | 0.677 | 1.280 | 1.370 | 2.508 | 0.167 | 2.327 | 0.148 | 0.322 | 1400 | 31.0 | +0 | new |

## Scenarios

- **sound_flight_10k**: 10k bullets in flight, On Flight hum following every bullet
- **sound_hit_storm**: 200-bullet volley/frame dying on walls with an On Hit sound
