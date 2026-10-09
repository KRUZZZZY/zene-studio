# M0 automation lock-free lane report — 2026-10-09

Branch `design/m0-automation-lockfree`, based on `c405233f8`. This report records stage 3 round 2 against the independent review at `scratchpad/luna-review-automation-stage3/review.md`.

| Item | Verdict | Fix commit(s) | Regression and render proof | Red exit | Green exit | Gate exits | Design |
|---|---|---|---|---:|---:|---|---|
| (a) Stage 1 follow-ups | ACCEPT (independent review) | `a3956c3b8`, `354a81d9b` | Compound automation edits publish once; the reader race and idle snapshot-reclamation tests are in `AutomationClipSnapshotTest`. No new stage 3 code. | — | Prior accepted proof; not rerun for this item. | C 0, L 0, F 0, U 0, RT 0 | [automation-lockfree.md](../m0-design/automation-lockfree.md) |
| (b) Project-generation fence | FIXED | `1d1f98a1b` | `AutomationRecordingGenerationTest` leaves an event pending across clear/load, reloads a clip with the same saved journal ID, and checks that the new clip is unchanged and the old event is counted stale. | 8 (generation comparison reversed) | 0 | C 0, L 0, F 0, U 0, RT 0 | [automation-lockfree.md](../m0-design/automation-lockfree.md) |
| (c) Exact recording overlay | FIXED | `d25ce7733` | `AutomationRecordingOverlayTest` compares pending cubic insertion and repeated-equal no-op/removal with synchronous `recordValue()`, then bit-compares 12-period sample-accurate cubic and repeated-value renders. | 8 (overlay collapse/tangent replay disabled; both renders mismatch) | 0 | C 0, L 0, F 0, U 0, RT 0 | [automation-lockfree.md](../m0-design/automation-lockfree.md) |
| (d) Render allocations | FIXED | `d25ce7733` | The allocation probe measures eight real render periods with automation playing and recording, and verifies drained recording points. Render collection now uses control-grown raw arrays for clips and values and a raw-array restore cache. | 8 (old clip-vector collection reintroduced; 13 render allocations) | 0 (zero allocations; no overlay or scratch overflows) | C 0, L 0, F 0, U 0, RT 0 | [automation-lockfree.md](../m0-design/automation-lockfree.md) |
| (e) Qt 5 compatibility | ACCEPT (independent review) | — | Static review found no Qt 6-only API in the stage 3 changes. No new compatibility-specific code. | — | Review accepted. | C 0, L 0, F 0, U 0, RT 0 | [automation-lockfree.md](../m0-design/automation-lockfree.md) |

Gate abbreviations: C = complexity, L = file length, F = fork sources, U = no upstream regression, RT = `rt-safety-sweep.py --check`. All gate commands exited 0. The RT sweep found four existing allowlisted hits in three keys and no fresh hits. The named regression run passed 5/5: `AutomationClipSnapshotTest`, `AutomationRecordingTest`, `AutomationRecordingGenerationTest`, `AutomationRecordingOverlayTest`, and `SampleAccurateAutomationTest`.

The independent review began with ACCEPT 2 and REWORK 3. Round 2 closes findings (b)–(d); the stage 3 disposition is now ACCEPT.

## Round 2 commits

- `74f02755d` — `docs(m0): extend automation design for review round 2 (cluster 5388)`
- `1d1f98a1b` — `fix(automation): fence queued events by project generation (cluster 5388)`
- `d25ce7733` — `fix(automation): match replay semantics and preallocate render scratch (cluster 5388)`
- This report commit — final stage 3 round 2 validation record.

## Earlier lane decisions

Cluster 2647 remains blocked on a complete render-model registry and coherent lock-free value publication. Cluster 5528's compound-publication and idle-reclamation follow-ups remain fixed by `a3956c3b8` and `354a81d9b`.
