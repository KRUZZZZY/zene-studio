# M0 blocker fixes — voice pool

Date: 2026-10-09
Branch: `design/m0-voice-pool`
Base: `c405233f8`
Design: [voice-pool design](../m0-design/voice-pool.md)

## Round 1 verdict correction

The previous report marked clusters 4467, 5127 and 5414 `FIXED`. The independent review returned `REWORK` for all three, so those claims overstated the evidence. The shared-bus comparison was accepted; the missing SlicerT comparison and pooled-handle thread affinity, track lifetime and capacity defects remained open.

| Cluster | Corrected round 1 status | Review finding |
|---|---|---|
| 4467 | REWORK | Allocation was bounded, but the required reused-state versus fresh-state audio comparison was absent. |
| 5127 | REWORK | Pooled handles kept construction-thread affinity. A backend callback could not retire completed clicks, eventually exhausting all 32 slots. |
| 5414 | REWORK | The affinity issue also stranded frozen voices; deleting an InstrumentTrack could leave a voice referring to it; the eight-pass cap could reject a long take looping over a short region. |

## Round 2 results

| Cluster | Round 2 result | Fix / test commits | Red → green evidence |
|---|---|---|---|
| 4467 | FIXED | `a25e6ae38` | `SlicerTPlayheadTest` compares rendered output from reused pooled state with fresh state bit-for-bit across 32 notes. Fault-injecting the missing state reset fails at note 1 (ctest exit 8); restoring it passes. Active SlicerT state deletion is also covered. |
| 5127 | FIXED | `0fb831a36`; `e2464d695` | The worker-thread regression renders 40 metronome clicks, each audible and retired. The prior behavior exhausted the 32-slot pool before click 40. A second worker-thread case renders 500 frozen voices without pool overflow or queue invariant failures. |
| 5414 | FIXED | `0fb831a36`; `e2464d695` | A 192-tick take over a 4-tick loop derives capacity for 48 overlapping passes: all 48 are accepted and the 49th refusal is counted and surfaced. The old eight-pass bound rejected before 48. Mid-playback deletion tests cover InstrumentTrack and SampleTrack; the SlicerT deletion case covers its pooled state. |

Frozen capacity is recomputed on the control thread when loop geometry changes and before playback. It uses the take/loop overlap bound, with the existing minimum allowance and the global 480-voice ceiling. Pooled handles do not retain construction-thread affinity. Track teardown removes associated pooled voices under the engine change lock before the track is destroyed.

## Verification

Build command: `cmake --build /tmp/voicepool-r2-build --target zene FrozenTakeHandoffTest FrozenTakeRoundTwoTest SlicerTPlayheadTest slicert -- -j4` (exit 0).

Targeted run from `/tmp/voicepool-r2-build/tests`: `ctest -R '^(FrozenTakeHandoffTest|FrozenTakeRoundTwoTest|SlicerTPlayheadTest)$' --output-on-failure` passed 3/3 (exit 0). Deletion paths were exercised in these tests; no ASan target was built.

| Gate | Exit | Result |
|---|---:|---|
| `complexity-gate.sh --check` | 0 | No regressions |
| `file-length-gate.sh --check` | 0 | No regressions |
| `fork-sources-gate.sh` | 0 | Manifests reproduce; no stale entries |
| `no-upstream-regression-gate.sh` | 0 | All inherited changes declared |
| `rt-safety-sweep.py --check` | 0 | 46 declared pairs, 1,636 lines measured; 0 allocation hits, 2 lock hits and 2 growth hits, all existing allowlisted hits; 0 fresh keys |
| `git diff --check` | 0 | Clean |

## Round 2 commits

- `5fe7e21f7` — revise voice-pool design for round two.
- `e2464d695` — correct frozen overlap bound and add round-two regression coverage.
- `0fb831a36` — fix pooled voice thread affinity, capacity and teardown lifecycle.
- `a25e6ae38` — compare reused SlicerT state with fresh renders.
- This report is committed separately after verification.
