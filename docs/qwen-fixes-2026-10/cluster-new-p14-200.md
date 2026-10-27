Verdict: FIXED

new-p14-200: REAL. Restoring a saved quantise timing (`qpos`/`qlen`) accepted values that place the note outside the song, and the restored end tick could overflow `int`.

## Evidence

- **Defect, before the fix:** `loadUnquantisedTiming()` (`src/core/Note.cpp:94-104`) checked only that both attributes parse, that `qpos >= 0` and that `qlen >= 1`. Any pair that fits an `int` was accepted.
- **The song limit:** `MaxSongLength = 9999 * DefaultTicksPerBar` (`include/Song.h:81`; `DefaultTicksPerBar = 192`, `include/TimePos.h:39`) = 1,919,808 ticks.
- **Sibling clamp:** `Note::loadSettings()` bounds the note's own position to `[0, MaxSongLength-1]` and its length to `MaxSongLength - pos` (`src/core/Note.cpp:650-651`). The restored timing had no such bound, and `Note::restoreTiming()` (`src/core/Note.cpp:288-293`) copied it straight into `m_pos`/`m_length`.
- **Overflow:** `Note::endPos()` (`include/Note.h:343-347`) computes `pos() + length()` in `int`. With the old check, `qpos=2147483647 qlen=2147483647` loads and the sum wraps. Values above `INT_MAX` already fail `toInt` and are rejected.
- **Fix** (`src/core/Note.cpp:104`): after the existing parse and sign checks, reject `storedPos >= MaxSongLength` or `storedPos + storedLength > MaxSongLength`. The sum is computed as `qint64`, so the check cannot itself overflow. The song's own bounds stay legal: `(0, MaxSongLength)` and `(MaxSongLength-1, 1)` still restore (test part (g), second loop).
- **Callers:** the only load path is `Note::loadSettings()` at `src/core/Note.cpp:710`. `rememberTiming()` writes the pre-quantise pos/len of a note that is already in the song (`src/core/NoteTransform.cpp:281`, `:348`), and the editors bound note positions to `MaxSongLength` (`src/gui/shell/InspectorClipFields.cpp:214-217`, `src/gui/noteeditor/NoteProperties.cpp:57-58`). A note created by those paths therefore saves a pair that the new check accepts.
- **Collateral, stated:** a file whose saved `qpos`/`qlen` lies outside the song now has no restorable timing, and `note.restore_timing` reports `restored: 0` for that note. The note's own position and length are unchanged; `loadSettings` already clamped them before this fix. No in-song timing is affected.
- **Not checked:** I did not audit every in-memory path that could make `pos + len` exceed `MaxSongLength` before a save. If one exists, its saved pair would be dropped on reload. That is the same class of note `loadSettings` already clamps, so I have not changed the save side.

## Regression test

`tests/src/core/MidiProbabilityPersistenceTest.cpp`, new method `unquantisedTimingOutsideTheSongIsNotRestored()` (around line 485). It is an existing test file for this area, next to `unparseableUnquantisedTimingIsNotTreatedAsTickZero()`. The matrix:

- Rejected pairs: `(MaxSongLength, 1)`, `(INT_MAX, 1)`, `(96, MaxSongLength)`, `(MaxSongLength-1, 2)`, `(INT_MAX, INT_MAX)`. Each must give `hasUnquantisedTiming() == false`, `restoreTiming() == false`, and leave pos 24 / len 48 unchanged.
- Accepted boundary pairs: `(0, MaxSongLength)`, `(MaxSongLength-1, 1)`. Each must restore exactly.
- Deterministic: no timing, sleeps or races. The file gained `#include "Song.h"` for `MaxSongLength`.

## Commands and exit codes

Environment note: a build with the default ccache temp dir failed with `ccache: error: failed to create temporary file ... Permission denied` (`logs/build-1-red.log`, EXIT=2). That is the sandbox, not the code. Every build below sets `CCACHE_TEMPDIR=/home/kruzzzzy/.cache/zene-tmp/ccache-tmp`. The ctest that followed that failed build (`logs/test-1-red.log`, EXIT=8) reported `Not Run` because no binary existed, so it is not evidence and is not cited below.

| Step | Command (run from the worktree unless noted) | Log | EXIT | Result |
| --- | --- | --- | --- | --- |
| RED build | `cmake --build build --target MidiProbabilityPersistenceTest -j4 > .../logs/build-2-red.log 2>&1` | build-2-red.log | 0 | builds |
| RED run | `cd build/tests && ctest -R '^MidiProbabilityPersistenceTest$' --output-on-failure > .../logs/test-2-red.log 2>&1` | test-2-red.log | **8** | `FAIL! ... '!loaded.hasUnquantisedTiming()' returned FALSE. (qpos=1919808 qlen=1)`, `MidiProbabilityPersistenceTest.cpp(506)`; 10 passed, 1 failed |
| GREEN build | `cmake --build build --target zene MidiProbabilityPersistenceTest -j4 > .../logs/build-3-green.log 2>&1` | build-3-green.log | 0 | builds |
| GREEN run | `cd build/tests && ctest -R '^MidiProbabilityPersistenceTest$' --output-on-failure > .../logs/test-3-green.log 2>&1` | test-3-green.log | 0 | 100% tests passed, 1 of 1 |
| Proof: revert fix | `git diff -- src/core/Note.cpp > .../logs/fix.patch; git apply -R .../logs/fix.patch` | (none) | 0 / 0 | fix reverted |
| Proof: RED build | `cmake --build build --target zene MidiProbabilityPersistenceTest -j4 > .../logs/build-4-unfixed.log 2>&1` | build-4-unfixed.log | 0 | builds |
| Proof: RED run | `ctest -R '^MidiProbabilityPersistenceTest$' --output-on-failure > .../logs/test-4-unfixed.log` (from `build/tests`) | test-4-unfixed.log | **8** | same assertion, `qpos=1919808 qlen=1` |
| Proof: re-apply fix | `git apply .../logs/fix.patch` | (none) | 0 | fix restored |
| Proof: GREEN build | `cmake --build build --target zene MidiProbabilityPersistenceTest -j4 > .../logs/build-5-refixed.log 2>&1` | build-5-refixed.log | 0 | builds |
| Proof: GREEN run | `ctest -R '^MidiProbabilityPersistenceTest$' --output-on-failure > .../logs/test-5-refixed.log` (from `build/tests`) | test-5-refixed.log | 0 | 100% tests passed, 1 of 1 |
| Affected family build | `cmake --build build --target NoteEditRangeTest NoteEditingCommandsTest SlideNotesTest Lb302NotePathTest NoteCurvesAcrossTest -j4 > .../logs/build-6-family.log 2>&1` | build-6-family.log | 0 | builds |
| Affected family run | `ctest -R '^(MidiProbabilityPersistenceTest\|NoteEditRangeTest\|NoteEditingCommandsTest\|SlideNotesTest\|Lb302NotePathTest\|NoteCurvesAcrossTest)$' --output-on-failure` (from `build/tests`) | test-6-family.log | 0 | **100% tests passed, 6 of 6** |
| Gates | see the note below: `python3 -I .../runs/new-p14-200.r1/feature_check_logdir.py <worktree>` | gates-2.log, gates/*.log | 0 | RESULT: PASS, all eight rows PASS |

Gate note. The stock `feature_check.py` exited 1 (`logs/gates-1.log`, a `PermissionError`): it creates `<worktree>-feature-check/` next to the worktree, which is outside my write-jail. I ran a copy in the run folder that differs from the stock script in exactly two lines, both shown by `diff`: `ROOT` is pinned to the real qwen directory (so the FIX-BASE and gate-baselines paths resolve as before), and the log directory is `.../logs/gates`. The checks, limits and baselines are unchanged.

| Gate row | Result |
| --- | --- |
| fork-sources | PASS (exit 0) |
| all-sources-reproduce | PASS (exit 0) |
| no-upstream-regression | PASS (exit 0) |
| check-namespace | PASS (exit 0) |
| no-tautology | PASS (exit 0) |
| doc-figures | PASS (exit 0) |
| complexity | PASS, no new item in the changed files |
| file-length | PASS, no new item in the changed files |

## Files committed

- `src/core/Note.cpp` (the load-time range check)
- `tests/src/core/MidiProbabilityPersistenceTest.cpp` (the regression, plus `#include "Song.h"`)
- `tests/upstream-modifications.txt` (one line registering the inherited `Note.cpp` change with its reason)
- `docs/qwen-fixes-2026-10/cluster-new-p14-200.md` (this report)

## Self-check

- **Regression fails with the fix reverted and passes with it:** yes. Reverted run `test-4-unfixed.log` EXIT=8 (`FAIL!` on `qpos=1919808 qlen=1`). Restored run `test-5-refixed.log` EXIT=0, 1 of 1 passed. The first RED run was a `Not Run` (no binary), so it is not cited.
- **Deterministic:** yes. The test uses fixed integer `qpos`/`qlen` pairs in a `QDomElement` and a `Note` loaded with `restoreState()`. There is no clock, sleep, thread or race and no injection seam is needed.
- **Fix at the cause and complete:** the only disk load path is `Note::loadSettings()` (`src/core/Note.cpp:710`), which calls the changed function. The check mirrors the clamp the same function's sibling applies to the note's own pos and len (`src/core/Note.cpp:650-651`). `restoreTiming()` (`src/core/Note.cpp:288-293`) needed no change because it only trusts what the loader accepted. The save side (`rememberTiming`) was not changed, and the unchecked in-memory paths are stated under Evidence.
- **No collateral damage:** the rejected range is only pairs outside the song, which `loadSettings` already clamps. The boundary pairs `(0, MaxSongLength)` and `(MaxSongLength-1, 1)` still restore (the test's accepted loop). The affected family passes 6 of 6 (`test-6-family.log`, EXIT=0). Editor-produced notes stay within the bound (`InspectorClipFields.cpp:214-217`, `NoteProperties.cpp:57-58`).
- **Verdict is not claim-only:** FIXED, with a product change and a regression.
- **Gates, known environment limit:** the stock `feature_check.py` exits 1 with `PermissionError` because it creates `<worktree>-feature-check/` outside the write-jail (`logs/gates-1.log`). The same checks, run from a copy that changes only the log directory and pins `ROOT` (`logs/gates-2.log`, EXIT=0, 8 of 8 PASS), pass. `submit_check` runs the stock script, so its gate row will show FAIL for this reason only. I did not write outside the jail to work around it.
