# Cluster new-p12-152-port

Verdict: **FIXED** (port of `bugs/qwen-new-p12-152` commit `d122344f4` onto `050/m0-train-5`).

Not ALREADY-FIXED: the newest line still had the default `qlen` value. Caveat: both ratchet gates exit 1 on this branch, and they exit 1 identically on an untouched HEAD export (see "Gates"). This fix adds no gate entries.

## Bug

`loadUnquantisedTiming` (`src/core/Note.cpp:89`) documents that both `qpos` and `qlen` are required, but it read `qlen` with a default of `"1"`:

- `src/core/Note.cpp:97` (before the fix): `element.attribute( "qlen", "1" ).toInt( &lengthOk )`. A missing `qlen` parsed as `1` with `lengthOk == true`, so `<note qpos="192">` stored timing `(192, 1)`.
- `note.restore_timing` then moved the note to tick 192 and set its length to one tick.

## Fix

- `src/core/Note.cpp:97` (after the fix): `element.attribute( "qlen" ).toInt( &lengthOk )`. A missing attribute is a null string, which `toInt` reports as not ok, so the existing early return (`src/core/Note.cpp:98`) rejects the whole pair. The `qlen` default is the only change.

## Regression test

- `tests/src/core/NoteEditRangeTest.cpp:179` `savedTimingWithoutLengthIsIgnored()`: saves a note, sets `qpos="192"` with no `qlen`, restores it, runs `note.restore_timing`, and checks that zero notes are restored and that position 24 and length 48 are kept.
- Assertion that fails before the fix: `NoteEditRangeTest.cpp:191`, `restored` is `1`, expected `0`.
- `#include <QDomDocument>` added at `NoteEditRangeTest.cpp:33`.

## Registration

- `tests/upstream-modifications.txt:814`: the existing `src/core/Note.cpp` entry gains a `new-p12-152` reason. No new files, so `tests/fork-sources.txt` is unchanged.

## Build environment note

The first build failed with `ccache: error: failed to create temporary file for /run/user/1000/ccache-tmp/...: Permission denied` (`build-1.log`, EXIT=2). Every build after that used `CCACHE_TEMPDIR=/tmp/ccache-tmp-p12` (`build-2.log` onward). That is an environment workaround only; no build setting in the tree changed.

## Evidence table

Logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-152-port.r1/logs/`.

| Step | Exact command (run from worktree root unless noted) | EXIT | Result / log |
|---|---|---:|---|
| Build attempt 1 (no ccache fix) | `cmake --build build --target zene NoteEditRangeTest -j2 > LOGS/build-1.log 2>&1; echo EXIT=$?` | 2 | ccache temp-file permission error; not a code error (`build-1.log`) |
| RED build (new test, original `Note.cpp`) | `CCACHE_TEMPDIR=/tmp/ccache-tmp-p12 cmake --build build --target zene NoteEditRangeTest -j2 > LOGS/build-2.log 2>&1; echo EXIT=$?` | 0 | built (`build-2.log`) |
| RED test | `cd build/tests && ctest -R '^NoteEditRangeTest$' --output-on-failure > LOGS/test-red-1.log 2>&1; echo EXIT=$?` | 8 | `FAIL! : NoteEditRangeTest::savedTimingWithoutLengthIsIgnored()`, `Actual (restored...): 1`, `Expected (0): 0`, `Loc: [NoteEditRangeTest.cpp(191)]`, `Totals: 6 passed, 1 failed` |
| GREEN build (fix applied) | `CCACHE_TEMPDIR=/tmp/ccache-tmp-p12 cmake --build build --target zene NoteEditRangeTest -j2 > LOGS/build-3.log 2>&1; echo EXIT=$?` | 0 | built (`build-3.log`) |
| GREEN test | `cd build/tests && ctest -R '^NoteEditRangeTest$' --output-on-failure > LOGS/test-green-1.log 2>&1; echo EXIT=$?` | 0 | `100% tests passed, 0 tests failed out of 1` |
| Reversal proof: save fix | `git diff -- src/core/Note.cpp > LOGS/fix.patch && git apply -R LOGS/fix.patch` | 0 | production fix reverted |
| Reversal proof: rebuild | `CCACHE_TEMPDIR=/tmp/ccache-tmp-p12 cmake --build build --target zene NoteEditRangeTest -j2 > LOGS/build-4.log 2>&1; echo EXIT=$?` | 0 | built (`build-4.log`) |
| Reversal proof: RED test | `cd build/tests && ctest -R '^NoteEditRangeTest$' --output-on-failure > LOGS/test-red-2.log 2>&1; echo EXIT=$?` | 8 | same failure at `NoteEditRangeTest.cpp(191)`, `Totals: 6 passed, 1 failed` |
| Reversal proof: reapply | `git apply LOGS/fix.patch` | 0 | production fix restored |
| Reversal proof: rebuild | `CCACHE_TEMPDIR=/tmp/ccache-tmp-p12 cmake --build build --target zene NoteEditRangeTest -j2 > LOGS/build-5.log 2>&1; echo EXIT=$?` | 0 | built (`build-5.log`) |
| Reversal proof: GREEN test | `cd build/tests && ctest -R '^NoteEditRangeTest$' --output-on-failure > LOGS/test-green-2.log 2>&1; echo EXIT=$?` | 0 | `100% tests passed out of 1` |
| Affected family build | `CCACHE_TEMPDIR=/tmp/ccache-tmp-p12 cmake --build build --target MidiProbabilityPersistenceTest NoteEditingCommandsTest NoteCurvesAcrossTest -j2 > LOGS/build-6.log 2>&1; echo EXIT=$?` | 0 | built (`build-6.log`) |
| Affected family test | `cd build/tests && ctest -R '^(MidiProbabilityPersistenceTest\|NoteEditingCommandsTest\|NoteCurvesAcrossTest\|NoteEditRangeTest)$' --output-on-failure > LOGS/test-family-1.log 2>&1; echo EXIT=$?` | 0 | `100% tests passed out of 4` (the family count is 4) |
| Complexity gate (branch) | `bash tests/complexity-gate.sh --check > LOGS/gate-complexity-1.log 2>&1; echo EXIT=$?` | 1 | 6 `REGRESSION: new function over target` lines in unrelated files (`XyPadPairTest.cpp`, `DawProjectReadTracks.cpp`, `ControlCommandsMixerTools.cpp`, `ControlCommandsMixerStrip.cpp`, `ProjectRevisions.cpp`, `ControlCommandsShortcuts.cpp`) |
| File-length gate (branch) | `bash tests/file-length-gate.sh --check > LOGS/gate-filelength-1.log 2>&1; echo EXIT=$?` | 1 | 14 `REGRESSION: new file over 500 lines` lines in unrelated files (`SmfInterchangeReader.cpp`, `TakeLane.cpp`, `VideoTrackTest.cpp`, `ArrangerTimelineTest.cpp`, ...); none is `Note.cpp` or `NoteEditRangeTest.cpp` |
| Complexity gate (untouched HEAD) | `git archive HEAD \| tar -x -C /tmp/p12-base-gates`, then `cd /tmp/p12-base-gates && bash tests/complexity-gate.sh --check > LOGS/gate-complexity-base.log 2>&1; echo EXIT=$?` | 1 | 6 regression lines |
| File-length gate (untouched HEAD) | `cd /tmp/p12-base-gates && bash tests/file-length-gate.sh --check > LOGS/gate-filelength-base.log 2>&1; echo EXIT=$?` | 1 | 14 regression lines |

Comparison of the regression lines: `diff` of the `REGRESSION:` lines between the HEAD export and the branch is empty for both gates. The branch adds no new over-target function and no new over-500-line file. The gates were not re-anchored and the limits and baselines were not changed.

## Gates

Both required gates exit 1 on this branch. They also exit 1 on an untouched HEAD export (`/tmp/p12-base-gates`, from `git archive HEAD`), and the regression sets are identical. The failures are in files this fix does not touch, so they are pre-existing. I did not fix them here, since that would mean changing unrelated code in other clusters.

## Other notes

- The brief's "Luna round 0" section lists no separate points; only the "Lessons from recent independent QA" section was present. Its guidance was applied: RED before the fix, the reversal proof, and the family count reported above.
- Note the assertion is driven through `note.restore_timing`, which is the user-visible path in the finding.

## Files committed

- `src/core/Note.cpp`
- `tests/src/core/NoteEditRangeTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p12-152-port.md`
