# Cluster new-p9-434-port — FIXED (port of bugs/qwen-new-p9-434, commit d1b6882e1)

**Verdict:** FIXED. The bug was real on `bugs/qwen-new-p9-434-port` (branch off `050/m0-train-5`
`b02a0ff2b`): the SMF writer refused an unsupported global tempo but not an unsupported global
time signature, so `0/4`, `33/4`, `3/3` and `3/64` were written as other metres.
The fix was re-applied by hand (no merge, cherry-pick or rebase). It is not a verbatim copy:
the tempo check and the new metre check now share one helper, because the new branch in
`writeConductorTrack` pushed its complexity past the CCN 10 limit.

**Gates: both FAIL (exit 1), and the failures are pre-existing.** See "Gates" below.

## What was wrong (current tree, before the port)

- `src/core/SmfInterchange.cpp` `writeConductorTrack` checked only `globalTempo`; nothing checked
  `globalSignature` before `exportEvents()` built the tick-0 seed from it.
- `src/core/SmfInterchange.cpp:124` `timeSignaturePayload()` masks the numerator with `& 0xFF` and
  encodes `log2(denominator)`, so an unsupported metre was silently re-encoded (`3/3` became `3/2`,
  `0/4` was accepted).
- `include/TempoMap.h:49-58` already defines the engine's range (`TempoMapMinNumerator`..
  `TempoMapMaxNumerator`, power-of-two denominators up to `TempoMapLargestDenominator`), enforced by
  `TempoMap::validEvent` (`src/core/TempoMap.cpp:97-104`).

## The fix (final)

- `src/core/SmfInterchange.cpp:337` `needsGlobalTimeSignature()` — the global metre is read only when
  the map is off or has no time signature at tick 0 (the same condition under which the seed is built).
- `src/core/SmfInterchange.cpp:346` `globalTimeSignatureExportable()` — when the global metre is
  needed, it is checked by `TempoMap::validEvent` on a tick-0 time-signature-only event, i.e. the
  engine's own rule for that half, not a copy of it.
- `src/core/SmfInterchange.cpp:361` `globalSeedRepresentable()` — refuses an out-of-range global
  tempo (the original check, same message) and an unrepresentable global metre (new message), with
  no file written. `writeConductorTrack` calls it at `:459` in place of the inline tempo check.
- `src/core/SmfInterchange.cpp:60` (`#include <span>`) for `map.all()`.
- `tests/src/core/SmfInterchangeTest.cpp:467` `aGlobalTimeSignatureTheEngineDoesNotHoldIsRefused()` —
  RED before the fix, GREEN after. It asserts refusal (and no file) for `0/4`, `33/4`, `3/3`, `3/64`,
  and that `3/4` still writes and reads back as `3/4`.
- `tests/upstream-modifications.txt` — one added line for the test file. The file already has an
  entry from cluster new-p8-231, and the registry already holds several entries per path, so this
  follows the existing convention.
- No new source files, no header changes, no new control commands, no canary-bearing docs changed.

## Evidence

Log folder: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-434-port.r1/logs`.

Build note: the first build failed with `ccache: error: failed to create temporary file for
/run/user/1000/ccache-tmp/...: Permission denied` (`build-1.log`, EXIT=2). This is environmental, not
code. Every later build used `CCACHE_TEMPDIR=/tmp/ccache-tmp-434` (no config or repo change).

| Step | Exact command (run from the worktree root unless noted) | Exit | Result |
|---|---|---:|---|
| Build, pre-fix (test only changed) | `CCACHE_TEMPDIR=/tmp/ccache-tmp-434 cmake --build build --target SmfInterchangeTest > $L/build-2.log 2>&1; echo EXIT=$?` | 0 | Test target built. |
| **RED (before fix)** | `cd build/tests && ctest -R '^SmfInterchangeTest$' --output-on-failure > $L/test-1.log 2>&1; echo EXIT=$?` | **8** | `aGlobalTimeSignatureTheEngineDoesNotHoldIsRefused()`: `global signature 0/4 was accepted`. QTest: 11 passed, 1 failed. |
| Build, with fix v1 | `cmake --build build --target zene SmfInterchangeTest SmfInterchangeRoundTripTest > $L/build-3.log 2>&1; echo EXIT=$?` | 0 | |
| GREEN v1 | `ctest -R '^SmfInterchange(Test\|RoundTripTest)$' --output-on-failure > $L/test-2.log 2>&1; echo EXIT=$?` | 0 | 2/2 passed (superseded by the helper refactor below). |
| Sensitivity v1 | `git diff -- src/core/SmfInterchange.cpp > $L/fix.patch && git apply -R $L/fix.patch` ; build `build-4.log` EXIT=0 ; ctest `test-3.log` | 0 / 8 | Reversed product patch: RED again (`0/4 was accepted`). |
| Complexity gate v1 | `bash tests/complexity-gate.sh --check > $L/gate-1.log 2>&1; echo EXIT=$?` | 1 | New `writeConductorTrack` at CCN 12 (my two extra branches). Fixed by the refactor. |
| File-length gate v1 | `bash tests/file-length-gate.sh --check > $L/gate-2.log 2>&1; echo EXIT=$?` | 1 | Pre-existing, see Gates. |
| **Final sensitivity cycle** (final product code) | `git diff -- src/core/SmfInterchange.cpp > $L/fix.patch && git apply -R $L/fix.patch` | 0 | Product fix reversed. |
| Build, reversed | `CCACHE_TEMPDIR=/tmp/ccache-tmp-434 cmake --build build --target zene SmfInterchangeTest SmfInterchangeRoundTripTest > $L/build-7.log 2>&1; echo EXIT=$?` | 0 | |
| **RED, final** | `cd build/tests && ctest -R '^SmfInterchange(Test\|RoundTripTest)$' --output-on-failure > $L/test-5.log 2>&1; echo EXIT=$?` | **8** | 1 of 2 tests failed: `global signature 0/4 was accepted`. |
| Restore | `git apply $L/fix.patch; echo EXIT=$?` | 0 | Final fix restored. |
| Build, restored | `CCACHE_TEMPDIR=/tmp/ccache-tmp-434 cmake --build build --target zene SmfInterchangeTest SmfInterchangeRoundTripTest > $L/build-8.log 2>&1; echo EXIT=$?` | 0 | |
| **GREEN, final** | `cd build/tests && ctest -R '^SmfInterchange(Test\|RoundTripTest)$' --output-on-failure > $L/test-6.log 2>&1; echo EXIT=$?` | **0** | 2/2 ctest tests passed. |
| QTest totals, final | `cd build/tests && ./SmfInterchangeTest > $L/test-7.log 2>&1; echo EXIT=$?` and `./SmfInterchangeRoundTripTest > $L/test-8.log 2>&1; echo EXIT=$?` | 0 / 0 | `Totals: 12 passed, 0 failed` / `Totals: 17 passed, 0 failed`. |

**Affected family:** `SmfInterchangeTest` (12 QTest functions) and `SmfInterchangeRoundTripTest`
(17 QTest functions): 2 ctest tests, 29 QTest functions, 0 failed after the fix.
The only other caller is `src/core/ControlCommandsInterchange.cpp:253` (`interchange.smf_export`);
no ctest drives that command, so it is not covered by the run above. For in-range inputs the
writer's output is unchanged.

## Gates

| Gate | Command | Exit (branch) | Exit (HEAD, exported to /tmp, same script) |
|---|---|---:|---:|
| Complexity | `bash tests/complexity-gate.sh --check > $L/gate-3.log 2>&1; echo EXIT=$?` | **1** | **1** (`gate-head-ccn.log`) |
| File length | `bash tests/file-length-gate.sh --check > $L/gate-4.log 2>&1; echo EXIT=$?` | **1** | **1** (`gate-head-len.log`) |

- Complexity: `writeConductorTrack` and the new helpers are **not** flagged (CCN <= 10). The gate still
  reports six functions in files this commit does not touch (`dotCentreAlong`, `parseNotes`, `adjust`,
  `setStrip`, `rotateProjectRevision`, `importedOverrides`). Those six are not in
  `tests/complexity-baseline.tsv` (unchanged from HEAD).
- File length: `SmfInterchange.cpp` is under the 500-line limit. The 14 reported files
  (`SmfInterchangeReader.cpp` 504, `TakeLane.cpp` 520, and 12 test files) are untouched by this commit.
- The REGRESSION/FAIL lines are identical to the lines the same scripts print on an exported copy of
  HEAD (`diff` of the `REGRESSION|FAIL` lines: identical for both gates).
- I did not change any gate, limit, or baseline, and did not re-anchor. Clearing these needs the
  owner's decision on the baseline (a `--reanchor` is a deliberate, recorded act), not a change in this fix.

## Files committed

- `src/core/SmfInterchange.cpp`
- `tests/src/core/SmfInterchangeTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p9-434-port.md`
