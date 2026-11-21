Verdict: FALSE-POSITIVE

new-p22-63: "range.insert_time uses a negative default length for an inverted range" (src/core/ControlCommandsRange.cpp:276)

## Decision

The stated mechanism cannot happen. The default length `range.end() - range.start()` is only used while the range is active, and an active range always has `end > start`, so the default is always at least one tick. No product code changed. One guard test was added to pin the invariant that makes the finding unreachable.

## Evidence (file:line)

- **The only writers of the range ends order them.** `ArrangerRange::set` stores `m_start = max(0, min(start, end))` and `m_end = max(0, max(start, end))` at [src/core/ArrangementEdits.cpp:534-535](src/core/ArrangementEdits.cpp:534). `clear()` sets both to 0 at [src/core/ArrangementEdits.cpp:543](src/core/ArrangementEdits.cpp:543). Grep of `m_start`/`m_end` writers in `src/core/ArrangementEdits.cpp` finds only these lines (`logs/evidence-1.log`).
- **`range.set` passes its arguments straight into `set()`.** [src/core/ControlCommandsRange.cpp:147](src/core/ControlCommandsRange.cpp:147). The other callers (`ControlCommandsRange.cpp:357` duplicate, `ControlCommandsRange.cpp:549` clear, and the GUI and sections callers `ArrangerOverlays.cpp:247`, `ControlCommandsViewMemory.cpp:285`, `FolderSummaryLane.cpp:87/98`, `ControlCommandsSections.cpp:140`) all reach the same `set()`. Caller list in `logs/evidence-3.log`.
- **An inactive range is refused before any shift.** `active()` is `m_end > m_start` at [include/ArrangementEdits.h:208](include/ArrangementEdits.h:208). `beginEdit` refuses when `!range.active()` at [src/core/ControlCommandsRange.cpp:108-110](src/core/ControlCommandsRange.cpp:108), and `insertTime` calls `beginEdit` at [src/core/ControlCommandsRange.cpp:284](src/core/ControlCommandsRange.cpp:284), before `shiftClipsFrom` at :289. The default length at :276 is computed before that call, but it is only used when the range is active.
- **So the two states the finding names cannot reach the shift.** Inverted (`start > end`): `set()` swaps the ends, so the stored range is ordered. Empty (`start == end`): `active()` is false and `beginEdit` refuses with `Refused`. A negative default needs `end < start` with an active range, and no code path produces that.
- **The explicit `length` path is unchanged.** The `< 1` refusal at [src/core/ControlCommandsRange.cpp:278](src/core/ControlCommandsRange.cpp:278) still covers an explicit `length`. Its doc comment ("No time to insert: refused before the edge split") is honoured for the default path too, because the default path is refused by `beginEdit` before the split.

## Guard test added

`tests/src/core/RangeEditsTest.cpp:181` `insertTimeDefaultLengthIsNeverNegative()`:
- `range.set` with start 2 bars, end 1 bar → `range.get` returns start 1 bar, end 2 bars (the ordering contract).
- `range.insert_time` with no length → ok, `length` = 1 bar, spans as expected; `control.undo` restores.
- `range.set` with start == end → `range.insert_time` is `Refused`; spans unchanged.

It passes on the unmodified branch, as expected for a not-real finding. It is a guard, not a regression proof for a fix. Its value is that it fails if the ordering in `set()` is ever removed. That is the mutation check below.

## Commands and exit codes

Logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p22-63.r1/logs/`.

| Step | Command (run from the worktree unless noted) | EXIT | Log |
| --- | --- | --- | --- |
| Evidence: writers of range ends | `rg -n "m_start = \|m_end = " src/core/ArrangementEdits.cpp` | 0 | evidence-1.log |
| Evidence: `active()` | `rg -n "bool active\(\) const" include/ArrangementEdits.h` | 0 | evidence-2.log |
| Evidence: `range.set` / `ArrangerRange` callers | `rg -n "range.set\|ArrangerRange::instance\(\)\.(set\|clear)" src --glob '*.cpp'` | 0 | evidence-3.log |
| Evidence: insert_time users | `rg -n "insert_time" tests src --glob '*.cpp'` | 0 | evidence-4.log |
| Gate: feature_check.py | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | 1 (crashed before the table: `PermissionError` creating `<worktree>-feature-check` outside the write-jail) | gate-1.log |
| Gate: fork-sources | `bash tests/fork-sources-gate.sh` | 0 | gate-fork-sources-10.log |
| Gate: all-sources-reproduce | `bash tests/all-sources-reproduce.sh` | 0 | gate-all-sources-reproduce-11.log |
| Gate: no-upstream-regression | `bash tests/no-upstream-regression-gate.sh` | 0 | gate-no-upstream-regression-12.log |
| Gate: check-namespace | `tests/scripted/check-namespace` | 0 | gate-check-namespace-13.log |
| Gate: no-tautology | `bash tests/no-tautology-gate.sh` | 0 | gate-no-tautology-14.log |
| Gate: doc-figures | `python3 tests/doc-figures-gate.py` | 0 | gate-doc-figures-15.log |
| Gate ratchet: complexity | `bash tests/complexity-gate.sh --check` | 1 (full-tree REGRESSION lines, none in a file this branch changes) | gate-complexity-16.log |
| Gate ratchet: file-length | `bash tests/file-length-gate.sh --check` | 1 (full-tree REGRESSION lines, none in a file this branch changes) | gate-file-length-17.log |
| Build (first try) | `cmake --build build --target RangeEditsTest -j4` | 2 (ccache could not write its temp file under `dsh/home`) | build-1.log |
| Build (retry) | `CCACHE_TEMPDIR=/home/kruzzzzy/.cache/zene-tmp/ccache-tmp cmake --build build --target RangeEditsTest -j4` | 0 | build-2.log |
| Regression run (no mutation) | `cd build/tests && ctest -R RangeEditsTest --output-on-failure` | 0 (1 test) | test-1.log |
| Regression run (no mutation) | `./RangeEditsTest` (in `build/tests`) | 0 (15 passed, 0 failed) | test-2.log |
| Mutation: drop ordering in `ArrangerRange::set` | edit `src/core/ArrangementEdits.cpp:534-535`; diff saved | - | mutation.patch |
| Mutation build (first try) | same build command, no ccache override | 2 (same ccache error; the binary was stale, so the run below was discarded) | build-3.log |
| Mutation build | same build, ccache override | 0 (`ArrangementEdits.cpp.o` recompiled) | build-4.log |
| RED (mutation) | `./RangeEditsTest` (in `build/tests`) | 1 (`FAIL! insertTimeDefaultLengthIsNeverNegative()` at `RangeEditsTest.cpp:185`; 14 passed, 1 failed) | test-3-red.log |
| Restore original line | edit `src/core/ArrangementEdits.cpp:534-535` back | - | `git diff --stat` shows only the test file |
| GREEN (restored) build | same build, ccache override | 0 | build-5.log |
| GREEN (restored) | `./RangeEditsTest` (in `build/tests`) | 0 (15 passed, 0 failed) | test-4-green.log |
| GREEN ctest family | `cd build/tests && ctest -R RangeEditsTest --output-on-failure` | 0 (100% passed, 1 test) | test-5-ctest.log |

Affected family: `RangeEditsTest` is one ctest test with 15 QTest functions, all passing after the restore.

Gate note: `feature_check.py` could not run from this write-jail, because it writes its logs to `<worktree>-feature-check` in the parent directory. I ran its eight commands directly (table above). The two ratchet failures are full-tree lines (6 complexity, 22 file-length). None names `RangeEditsTest`, `ControlCommandsRange`, `ArrangementEdits` or `docs/`, which are the files this branch changes (the grep is in the gate logs). So under the script's rule, both ratchet rows would be PASS. The script itself still needs to run outside the jail to confirm this.

## Self-check

- **Test FAILS with the product change reverted and PASSES with it.** No product fix exists for a not-real finding, so there is nothing to revert. The equivalent check was the mutation: removing the ordering in `ArrangerRange::set` makes the test fail (test-3-red.log, EXIT 1, `RangeEditsTest.cpp:185`), and restoring it makes it pass (test-4-green.log, EXIT 0). The test passes on the unmodified branch (test-2.log), as expected for a guard.
- **Deterministic.** No timing, sleeps or threads. The test uses fixed tick values (`Bar = 192`) and the existing fixture (`init()` builds a track with one 3-bar clip and an active range).
- **Fix at the cause and complete.** Not applicable: no product change. I checked every caller of `ArrangerRange::set` and `beginEdit` (evidence-3.log, ControlCommandsRange.cpp:108-110 and :284). All go through the same normalisation and the same `active()` check.
- **No collateral damage.** No product file changed. The test file gained 21 lines and no existing assertion changed. `RangeEditsTest.cpp` is already registered in `tests/fork-sources.txt` (line 2538), so no registration change is needed. It is not an inherited file, so `tests/upstream-modifications.txt` does not apply.
- **Verdict evidence.** `ArrangementEdits.cpp:534-535`, `include/ArrangementEdits.h:208`, `ControlCommandsRange.cpp:108-110`, `:284`, `:276-278`, as above.

Known issues during this run (not fixed, out of scope):
- `feature_check.py` cannot write its logs from this write-jail (see the gate note).
- The first build and the first mutation build failed on ccache's temp directory under `dsh/home`. The fix was a `CCACHE_TEMPDIR` override for this shell command only. Nothing in the repo or in the cache config was changed.
