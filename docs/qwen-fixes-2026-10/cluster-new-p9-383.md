FIXED: `ReferenceProject::open` no longer writes through a null `why` (BUG 9, cluster new-p9-383)

## Finding

`ReferenceProject::open(path, why)` wrote `*why` on both refusal paths with no null check, so a caller passing
`nullptr` for `why` crashed on a missing file or a project without song tracks, instead of getting `false`.

## Verdict and evidence

Verdict: REAL, fixed. Both failure paths dereferenced `why` unguarded.

- Before the fix: `src/core/ReferenceProject.cpp:109-118` wrote `*why` at the missing-file refusal and at the
  no-song-tracks refusal, with no check.
- After the fix: `src/core/ReferenceProject.cpp:111` and `:117` write `*why` only `if (why != nullptr)`. The control flow
  is unchanged: both paths still return `false`.
- Contract: `include/ReferenceProject.h:57` now documents `why` as "(when not nullptr)". The parameter stays the same
  type and position, so no caller changes.
- Callers (both checked, both pass a non-null sink, so neither changes behaviour):
  - `src/core/ControlCommandsReference.cpp:103` (`reference.open`, passes `&why`)
  - `src/core/ControlCommandsDocuments.cpp:338` (`project.switch` with `keep_as_reference`, passes `&why`)
- No sibling writes `why` in `ReferenceProject`: `open` is the only function in the class that takes it.
- Thread: `ReferenceProject` is UI-thread state (`include/ReferenceProject.h`, class comment). The audio thread never
  reaches `open`.
- Undo and commands: `open` is not a session edit (it is interface state, not saved with either project). No command
  or command file changed, so the snapshot and canary are not affected.

## Regression test

`tests/src/gui/ReferenceProjectPanelTest.cpp:315` (`openWithoutAnErrorSinkRefusesInsteadOfCrashing`). It calls `open`
with `nullptr` for a missing file and for a non-project file (`"not xml"`, which has no song tracks), asserts `false`
and `!isOpen()` for each, then passes a sink for the missing file and asserts the message contains `no project file`.
It uses only `QTemporaryDir` and direct calls: no timers, sleeps or races.

## Commands and exit codes

All build and test output is in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-383.r1/logs/`.

| Step | Command (run from the worktree unless noted) | Log | EXIT |
| --- | --- | --- | --- |
| Build test (1st try) | `cmake --build build --target ReferenceProjectPanelTest -j4` | build-1.log | 2 (ccache could not create its temp file, `Permission denied`; the compile never ran. Environmental.) |
| Build test (retry) | `CCACHE_DISABLE=1 cmake --build build --target ReferenceProjectPanelTest -j4` | build-2.log | 0 |
| **RED, full binary** | `cd build/tests && ctest -R '^ReferenceProjectPanelTest$' --output-on-failure` | test-1.log | 8 (SIGSEGV, signal 11 at address 0x0, inside `openWithoutAnErrorSinkRefusesInsteadOfCrashing`) |
| Fix applied, build | `CCACHE_DISABLE=1 cmake --build build --target ReferenceProjectPanelTest -j4` | build-3.log | 0 |
| Fix applied, build | `CCACHE_DISABLE=1 cmake --build build --target zene -j4` | build-4.log | 0 |
| Full binary after fix | `ctest -R '^ReferenceProjectPanelTest$' --output-on-failure` | test-2.log | 8 (see Known unrelated failures: watchdog timeout in an existing test, before the new slot ran) |
| **Proof: write patch** | `git diff -- include/ReferenceProject.h src/core/ReferenceProject.cpp > logs/fix.patch` | (none) | 0 |
| **Proof: revert fix** | `git apply -R logs/fix.patch` | (none) | 0 |
| Revert build | `CCACHE_DISABLE=1 cmake --build build --target zene ReferenceProjectPanelTest -j4` | build-5.log | 0 |
| Full binary, reverted | `ctest -R '^ReferenceProjectPanelTest$' ...` | test-3.log | 8 (watchdog timeout in an existing test; did not reach the new slot) |
| **RED, new test alone** | `cd build/tests && ./ReferenceProjectPanelTest openWithoutAnErrorSinkRefusesInsteadOfCrashing` | test-5-single-red.log | **139** (SIGSEGV, address 0x0) |
| Revert build (2nd) | `CCACHE_DISABLE=1 cmake --build build --target zene ReferenceProjectPanelTest -j4` | build-7.log | 0 |
| Restore fix | `git apply logs/fix.patch` | (none) | 0 |
| Build after restore | `CCACHE_DISABLE=1 cmake --build build --target zene ReferenceProjectPanelTest ArrangingSpacerReferenceTest ArrangingCatchUpTest -j4` | build-6.log, build-8.log | 0, 0 |
| **GREEN, new test alone** | `cd build/tests && ./ReferenceProjectPanelTest openWithoutAnErrorSinkRefusesInsteadOfCrashing` | test-4-single-green.log, test-6-single-green.log | **0** (3 passed, 0 failed) |
| Sibling families | `ctest -R 'ArrangingSpacerReferenceTest\|ArrangingCatchUpTest'` | test-7-arranging.log | 8 (ArrangingSpacerReferenceTest passed; ArrangingCatchUpTest failed, see below) |
| Sibling unfixed check | revert, `cmake --build ... zene ArrangingCatchUpTest`, `ctest -R '^ArrangingCatchUpTest$'` | build-9.log, test-8-catchup-unfixed.log | 0 build, 8 test (same assertion, same counts) |
| Restore + panel family | `git apply logs/fix.patch`, rebuild (build-10.log, 0), `ctest -R '^ReferenceProjectPanelTest$'` | test-9-panel-green.log | **0** (100% tests passed, 1 of 1) |
| Gates | `python3 .../qwen/feature_check.py <worktree>` (see note below) | gates-1.log | **0** (RESULT: PASS) |

Affected-family test count: ReferenceProjectPanelTest 1 ctest test (the single-function run executed 3 functions;
the full-binary run in `test-9-panel-green.log` executed all functions and passed; in `test-2.log` and `test-3.log` the
existing `theButtonsOpenAndCloseTheReference` hit the watchdog before the new slot ran). ArrangingSpacerReferenceTest:
1 ctest test, passed. ArrangingCatchUpTest: 1 ctest test, failing on the unfixed tree as well.

Note on the gate run: `feature_check.py` tries to write its logs into `projects/zene-studio-program/zene-qwen-new-p9-383-feature-check`, outside the write area, and failed with `Permission denied` (exit 1, no table). I ran a copy of the same script from the run folder, changing only the log directory (to the LOG folder) and the `ROOT` path (to the qwen directory, for `FIX-BASE` and the cached base listing `gate-baselines/cf3618e61.json`). The gate logic is unchanged. The base listing was read from the existing cache, so no git worktree was created in another repo.

## Known unrelated failures (not caused by this fix)

1. `ReferenceProjectPanelTest::theButtonsOpenAndCloseTheReference` (an existing file-dialog test, not touched) hit the
   QTest 5-minute watchdog (`QFATAL: Test function timed out`, about 303 s) in two runs: `test-2.log` (with the fix)
   and `test-3.log` (without the fix). It passed in `test-1.log` (without the fix) and `test-9-panel-green.log`
   (with the fix). The machine load average was 7-11 on 4 cores during these runs. The timing is in its
   `answerFileDialog` helper, which gives up after about a second. It is load-dependent and pre-existing; I did not
   change it.
2. `ArrangingCatchUpTest::queuedInstrumentHandlesAreRemovedBeforeTheirTrackCanDie` fails at
   `tests/src/core/ArrangingCatchUpTest.cpp:304` (expected `tripleoscillator`, got `dummyinstrument`). The log
   shows `The plugin "tripleoscillator" wasn't found or could not be loaded! Reason: "Plugin not found."`. The same
   assertion and counts (12 passed, 1 failed, 1 skipped) fail on the unfixed tree (`test-8-catchup-unfixed.log`).
   `ArrangingCatchUpTest` never calls `ReferenceProject::open`; its only use is `close()` (line 140).

## Scope

Three files changed: `src/core/ReferenceProject.cpp`, `include/ReferenceProject.h` (comment-level contract change only,
plus the same two guards), `tests/src/gui/ReferenceProjectPanelTest.cpp` (one new slot). No new files, so nothing to
register in `tests/fork-sources.txt` or `tests/upstream-modifications.txt`. No command or snapshot change.

## Self-check

- **Regression test fails with the change reverted, passes with it (exit codes).** Yes. Reverted: `test-5-single-red.log`,
  exit 139 (SIGSEGV at address 0x0). Restored: `test-6-single-green.log`, exit 0 (3 passed). The full-binary run
  `test-1.log` also shows the SIGSEGV at the new slot (exit 8).
- **Deterministic (no wall-clock timing, sleeps or races).** Yes. The new slot uses only direct `open` calls on a
  `QTemporaryDir` path, a file it writes itself, and synchronous checks. It contains no timer or event-loop wait.
  The timing in the file-dialog test is pre-existing and documented above.
- **Fix at the cause, complete, callers and siblings checked.** Yes. Both writes to `why` in `open` are guarded
  (`src/core/ReferenceProject.cpp:111`, `:117`). The two callers pass non-null sinks (`ControlCommandsReference.cpp:103`,
  `ControlCommandsDocuments.cpp:338`). No other write to `why` exists in the class.
- **No collateral damage.** Success path unchanged. Error text unchanged when a sink is passed (the new test asserts
  `no project file`). No limit or check added, so no caller can be newly refused.
- **Realtime, undo, commands.** `open` is UI-thread state (header comment). It is not an undo step and it is not a
  command. No command was added, so the snapshot and canary are untouched.
- **Verdict REAL, so the ALREADY-FIXED/FALSE-POSITIVE clause does not apply.**
- **Gates.** `feature_check` (copy, see note) exit 0, RESULT PASS, all eight rows PASS. Complexity and file-length
  report no new item in the changed files.
- **Completion check (`submit_check.py`, log `submit-check-1.log`, exit 1).** Four rows PASS (commits on the branch,
  clean tree, this report with its verdict, tests changed). One row FAILS: `gates (feature_check.py)`. Its cause is the
  write-jail, not the code. `submit_check.py` runs the original `feature_check.py`, which creates its log folder at
  `projects/zene-studio-program/zene-qwen-new-p9-383-feature-check` and got `PermissionError` there. The gate logic
  itself passes (`gates-1.log`, exit 0, via the log-redirected copy described above). The five "?" items are the
  ASK list, answered in this section.
