# cluster new-p12-204 — a project sample with a zero sample rate reached a division by zero

**Verdict: FIXED** (BUG 6, `src/core/SampleClip.cpp`, confidence "high" — confirmed real).

## The finding, and where it lives

`windowTicksFor()` (src/core/SampleClip.cpp:516-525) evaluated
`window.length() / clipFramesPerTick()`, and `clipFramesPerTick()`
(src/core/SampleClip.cpp:528-541) is exactly `0.0f` when the clip's sample rate is
zero. The float division is a well-defined infinity; the following
`static_cast<int>(...)` narrows that infinity to `int`, which is undefined
behaviour — and produced `-2147483648` on this toolchain (observed live, see the
RED evidence below).

The rate reaches the clip from project data with no validation at all:
`SampleClip::loadSettings()` (src/core/SampleClip.cpp:824-832 before the fix)
read `sample_rate` with `QString::toInt()` — which hands back `0` for `"0"`, for
a negative value's text and for any unparseable text — and passed it straight to
`SampleBuffer::fromBase64(data, sampleRate)`. `clipFramesPerTick()` is then
zero, and the next auto-resize update or tempo change
(`updateLength()` → `sampleLength()` → `windowTicksFor()`, src/core/SampleClip.cpp:454-469
and 506-513) performs the division. `SampleBuffer`'s constructors
(src/core/SampleBuffer.cpp:35-48) do not validate the rate either.

## The fix (smallest change, both halves of the finding's prescription)

1. `SampleClip::loadSettings()` — a stated `sample_rate` that is not positive
   now loads the way an absent one already did: the engine's output rate
   (`Engine::audioEngine()->outputSampleRate()`). A project can no longer
   install a zero-rate (or negative-rate) buffer.
2. `SampleClip::windowTicksFor()` — a nonpositive frames-per-tick maps to `0`
   ticks instead of dividing. This is the rule `leadOutputFramesAt()`
   (src/core/SampleClip.cpp:496-497) and `restoreState()`
   (src/core/SampleClip.cpp:605-609) already apply to that same value, and it
   covers any zero-rate buffer that did not come from a project load.

No header changed (`loadSettings`/`windowTicksFor` signatures untouched); only
`src/core/SampleClip.cpp` and the regression test were edited.

## The regression test (fails before the fix, passes after)

`tests/src/tracks/SampleClipWarpTest.cpp` (ctest `-R '^SampleClipWarpTest$'`):

- `zeroSampleRateIsRejectedOnProjectLoad()` — saves a clip with embedded
  `data`, rewrites its `sample_rate` to `"0"`, restores it into a fresh clip
  (the project-load path), and pins: the buffer's rate is the output rate,
  `clipFramesPerTick()` is finite and positive, `sampleLength()` equals the
  window's real tick length, and `updateLength()` (auto-resize) lands on that
  same length.
- `nonpositiveRateMapsToNoLength()` — installs a rate-0 buffer directly and
  pins that the mapping answers a defined `0` instead of an infinity narrowed
  to int.

## Evidence

All logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-204.r1/logs/`.

| Step | Command (run from the worktree root unless noted) | EXIT |
|---|---|---|
| Build test, original product code | `cmake --build build --target SampleClipWarpTest > LOGS/build-1.log 2>&1` | 0 |
| **RED** (original code) | `cd build/tests && ctest -R '^SampleClipWarpTest$' --output-on-failure > LOGS/test-1-red.log 2>&1` | **8** |
| Build product + test with fix | `cmake --build build --target zene SampleClipWarpTest > LOGS/build-2.log 2>&1` | 0 |
| **GREEN** (fix applied) | `cd build/tests && ctest -R '^SampleClipWarpTest$' --output-on-failure > LOGS/test-2-green.log 2>&1` | **0** |
| Family, first attempt (5 suites ***Not Run — their binaries were not built yet, not a product failure) | `cd build/tests && ctest -R 'SampleClip\|ClipSerialisation\|ClipWarpPersistence' --output-on-failure > LOGS/test-3-family.log 2>&1` | 8 |
| Build the six family binaries | `cmake --build build --target ClipSerialisationTest ClipWarpPersistenceTest SampleClipStretchTest SampleClipRubberBandTest SampleClipWarpTest SampleClipWindowTest > LOGS/build-3.log 2>&1` | 0 |
| **Family GREEN** — 6 tests executed, 6 passed (`100% tests passed out of 6`) | `cd build/tests && ctest -R 'SampleClip\|ClipSerialisation\|ClipWarpPersistence' --output-on-failure > LOGS/test-4-family.log 2>&1` | **0** |
| Proof the test catches the bug: revert the product fix only | `git diff -- src/core/SampleClip.cpp > LOGS/fix.patch && git apply -R LOGS/fix.patch` | 0 |
| Rebuild on the unfixed product code | `cmake --build build --target zene SampleClipWarpTest > LOGS/build-5.log 2>&1` | 0 |
| **RED proof** (test only, product code original) | `cd build/tests && ctest -R '^SampleClipWarpTest$' --output-on-failure > LOGS/test-5-unfixed.log 2>&1` | **8** |
| Re-apply the fix | `git apply LOGS/fix.patch` | 0 |
| Rebuild with the fix | `cmake --build build --target zene SampleClipWarpTest > LOGS/build-6.log 2>&1` | 0 |
| **GREEN proof** | `cd build/tests && ctest -R '^SampleClipWarpTest$' --output-on-failure > LOGS/test-6-refixed.log 2>&1` | **0** |
| Family on the final tree — 6 tests executed, 6 passed | `cd build/tests && ctest -R 'SampleClip\|ClipSerialisation\|ClipWarpPersistence' --output-on-failure > LOGS/test-7-family-final.log 2>&1` | **0** |

RED failure (LOGS/test-1-red.log and LOGS/test-5-unfixed.log, identical):

```
FAIL!  : SampleClipWarpTest::zeroSampleRateIsRejectedOnProjectLoad() Compared values are not the same
   Actual   (static_cast<int>(loaded->sample().sampleRate())): 0
   Expected (m_rate)                                         : 44100
FAIL!  : SampleClipWarpTest::nonpositiveRateMapsToNoLength() Compared values are not the same
   Actual   (static_cast<int>(clip->sampleLength().getTicks())): -2147483648
   Expected (0)                                                : 0
Totals: 15 passed, 2 failed, 0 skipped, 0 blacklisted
```

The `-2147483648` is the finding's undefined behaviour observed: the infinity
produced by `352800 / 0.0f` narrowed to `int`.

## Gates

| Gate | Command | EXIT |
|---|---|---|
| Per-method complexity (CCN ratchet) | `bash tests/complexity-gate.sh --check` (LOGS/gate-complexity.log, rerun on the final tree: LOGS/gate-complexity-final.log) | 1 — PRE-EXISTING |
| Per-file length ratchet | `bash tests/file-length-gate.sh --check` (LOGS/gate-file-length.log, rerun on the final tree: LOGS/gate-file-length-final.log) | 1 — PRE-EXISTING |
| Complexity at clean HEAD, no fix present | `git archive HEAD \| tar -x -C <checkout> && cd <checkout> && bash tests/complexity-gate.sh --check` (LOGS/gate-complexity-head.log) | 1 |
| File length at clean HEAD, no fix present | same clean checkout, `bash tests/file-length-gate.sh --check` (LOGS/gate-file-length-head.log) | 1 |

Both gates were run after the final edit. Both are red **on this branch without
my change**: run from a clean `git archive HEAD` checkout
(LOGS/gate-complexity-head.log, LOGS/gate-file-length-head.log) they exit 1 with
the byte-identical regression lists —

- complexity, 6 entries, all in files this fix never touches:
  `dotCentreAlong@tests/src/gui/XyPadPairTest.cpp` (CCN 16),
  `parseNotes@src/core/DawProjectReadTracks.cpp` (13),
  `lmms::adjust@src/core/ControlCommandsMixerTools.cpp` (12),
  `setStrip@src/core/ControlCommandsMixerStrip.cpp` (11),
  `rotateProjectRevision@src/core/ProjectRevisions.cpp` (11),
  `importedOverrides@src/gui/ControlCommandsShortcuts.cpp` (11);
- file length, 14 entries, likewise untouched by this fix
  (`src/core/SmfInterchangeReader.cpp` 504, `src/core/TakeLane.cpp` 520,
  and 12 tests/src/core files).

`diff` of the two runs' `^REGRESSION` lines is empty in both gates
(`COMPLEXITY-REGRESSIONS-IDENTICAL`, `FILELENGTH-REGRESSIONS-IDENTICAL` in the
session record above), so this fix adds **zero** gate regressions. Neither limit
nor baseline was changed, and the pre-existing debt is not this cluster's to
fix; it is reported here rather than papered over with a re-anchor.

## Files in this fix

- `src/core/SampleClip.cpp` (the fix; registered in tests/upstream-modifications.txt)
- `tests/src/tracks/SampleClipWarpTest.cpp` (the regression test)
- `tests/upstream-modifications.txt`, `tests/fork-sources.txt` (registrations)
- `docs/qwen-fixes-2026-10/cluster-new-p12-204.md` (this report)

Nothing else was touched; no control commands were added (the
ControlCommandsSnapshot canary is unmoved), and both changed functions stay
allocation-free and lock-free, so the audio-thread callers of the mapping are
unaffected by the change's shape.
