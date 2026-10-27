# BlastBullets2D benchmark: latest run

- run: `2026-10-27T06-36-04Z` commit `9f7f8e9` (dirty src/)
- machine: AMD Ryzen 7 8840HS w/ Radeon 780M Graphics (16 threads), Linux-7.3.0-5-cachyos-x86_64-with-glibc2.44
- godot: 4.7.2-stable (arch_linux), debug build: True, repeats: 5 (median)
- baseline: `1249ed9` (2026-10-20T10-45-08Z)

Times are milliseconds of CPU per frame (simulated time, `--fixed-fps 60`).
`frame` = step + engine; `step` = the scenario's own plugin calls (spawns...);
`engine` = physics server step + factory tick + drain + render; `tick` = factory physics tick only.

| scenario | frame p50 | frame p95 | frame p99 | frame max | step max | engine max | tick p50 | tick p99 | bullets | mem peak MB | objects +/- | vs baseline |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---|
| curvy_lasers_256 | 0.682 | 0.694 | 0.730 | 2.448 | 0.099 | 2.446 | 0.298 | 0.339 | 0 | 30.7 | +0 | new |
| laser_beams_256 | 0.399 | 0.421 | 0.454 | 2.275 | 0.002 | 2.274 | 0.355 | 0.407 | 0 | 31.1 | +0 | new |

## Scenario extras

- **curvy_lasers_256**: lasers=8
- **laser_beams_256**: hits=8324

## Scenarios

- **curvy_lasers_256**: 256 curvy tracking lasers (8 x 32), a wave every 2 s, 1.9 s lives
- **laser_beams_256**: 256 sweeping beams (8 lasers), walls, graze, 24 pierce targets
