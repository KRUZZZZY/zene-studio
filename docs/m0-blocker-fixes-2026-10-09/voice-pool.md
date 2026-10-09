# M0 blocker fixes — voice pool

Date: 2026-10-09
Branch: `design/m0-voice-pool`
Base: `c405233f8`
Design: [voice-pool design](../m0-design/voice-pool.md)

| Cluster | Verdict | Fix commit | Regression test and red → green evidence | Gate exits |
|---|---|---|---|---|
| 4467 | FIXED | `66717c2c9` | `SlicerTPlayheadTest`: restoring render-time `new`/`delete` fails the allocation probe (1 allocation vs 0; red exit 8). Preallocated 128-slot state pool passes (green exit 0). | complexity 0; file length 0; fork sources 0; no-upstream 0; RT sweep 0 |
| 5127 | FIXED | `e88364fcc` | `FrozenTakeHandoffTest`: restoring the allocating click path fails scheduling at 8 allocations vs 0 (red exit 8). Pooled scheduling, completion retirement, fresh-render comparison and capacity checks pass (green exit 0). | complexity 0; file length 0; fork sources 0; no-upstream 0; RT sweep 0 |
| 5414 | FIXED | `e88364fcc` | `FrozenTakeHandoffTest`: restoring per-pass `Sample`/handle/bus allocation fails scheduling at 7 allocations vs 0 (red exit 8). Re-arming, nonzero `sourceIn`, overlapping windows, 8-per-track and 480-global bounds, and the legacy-bus mix comparison pass (green exit 0; max error ≤ `1e-6`). | complexity 0; file length 0; fork sources 0; no-upstream 0; RT sweep 0 |

The red runs deliberately restored the old unpooled paths. Their intended allocation assertions fail as recorded; later QtTest slots can also report failures because those mutations leave legacy handles active. With the pool restored, all slots pass.

Final build: `zene`, `FrozenTakeHandoffTest`, `SlicerTPlayheadTest`, and `ControlRegistryTest` all built successfully (exit 0). Final targeted run: `ctest -R '^(FrozenTakeHandoffTest|SlicerTPlayheadTest|ControlRegistryTest)$' --output-on-failure` from `build/tests` passed 3/3 (exit 0). `git diff --check` exited 0.

The real-time-safety sweep measured 46 declared path/symbol pairs. It found 0 allocation hits, 2 locking hits and 2 growth hits, all existing allowlisted hits; there were 0 fresh hits and no new allowances.

## Commits

- `231c307b5` — document bounded voice-pool design for clusters 5127, 5414 and 4467.
- `631c0d4e7` — document shared-bus summing decision.
- `66717c2c9` — fix SlicerT render-path playback-state allocation (cluster 4467).
- `e88364fcc` — preallocate render-path sample voices (clusters 5127 and 5414).
- This report is committed separately after the implementation commit.
