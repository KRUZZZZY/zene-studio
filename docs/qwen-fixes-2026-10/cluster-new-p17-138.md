FIXED — cluster new-p17-138: startup recovery treated a failed `record.recovery_get_state` as "nothing to recover"

## The finding (Luna patrol, cluster new-p17-138)

`src/gui/RecordingRecoveryMenu.cpp`: `interruptedTakes()` returns an empty array both when no
recordings exist and when `record.recovery_get_state` fails, so the startup offer silently skipped
recovery in either case. The fix asked for: preserve the query error and show or log it instead of
treating it as an empty result.

## Verdict: REAL

Evidence in this tree (before the fix):

- `src/gui/RecordingRecoveryMenu.cpp:124-128` — `interruptedTakes()`:
  `return state.ok ? state.result.value("takes").toArray() : QJsonArray{};` — the typed failure
  (`ControlResult::errorKind` / `errorMessage`) was discarded, and the empty array was the only
  thing the caller could see.
- `src/gui/RecordingRecoveryMenu.cpp:130-133` — `offerRecordingRecovery()`:
  `const QJsonArray takes = interruptedTakes(); if (takes.isEmpty()) { return StartupRecoveryChoice::NothingToOffer; }`
  — a failed query and an empty recovery directory were indistinguishable, so no dialog and no
  warning appeared and the interrupted takes stayed invisible.
- The sibling path in the SAME file already did the honest thing: `rebuild()` (the
  File > Recover Recordings menu, `:88-92`) put `state.errorMessage` in the menu line when the
  query failed. The startup path was the only one that swallowed it — the inconsistency the
  finding names.

Why the query fails at startup is not hypothetical: `ControlRegistry::setReady(true)` is called
only for a control-socket instance (`src/core/main.cpp:1807-1809`), and `invoke()` refuses every
command that needs the engine with the typed `busy` refusal while the registry is not ready
(`src/core/ControlRegistry.cpp:283-291`). A normal desktop run therefore reaches the startup
offer with the query refused. Measured on this branch, running the built `zene` with a temporary
`HOME` and no `--control-socket`:

    NoteEditor: scale.get_state refused: the engine is not addressable yet [engine_starting]: ...

(`/tmp/zene-exp/run1.log`; the same refusal is what `record.recovery_get_state` answers, and it is
the `busy` refusal the regression test injects.)

## The fix

`src/gui/RecordingRecoveryMenu.cpp`, `include/RecordingRecoveryMenu.h`:

- `interruptedTakes()` is now `ControlResult interruptedTakes(QJsonArray* takes)`: it returns the
  typed reply of `record.recovery_get_state` and hands the takes out through the argument. A
  failed query leaves the list empty **and** carries the failure — an empty list from a failed
  query now means "the takes are unknown", never "there are none". The header comment said the
  opposite before ("empty when none, or when the command fails"), which is the bug's contract.
- `offerRecordingRecovery()` asks the command once and, when it FAILED, reports the failure
  through `reportQueryFailure()` and returns the new `StartupRecoveryChoice::QueryFailed`
  instead of `NothingToOffer`. It never asks to place takes it could not list.
- `reportQueryFailure()` always logs the failure (`qWarning`, the command id and the reason) and
  shows it to a human in a warning box when a human can answer — except for the `busy` refusal
  ("the model is not addressable yet"), which is a start-up state that clears itself and is shown
  by File > Recover Recordings when the user opens it. An unattended run (agent instance, no
  display) never gets a box, only the log line.
- `rebuild()` (the menu) now uses the same typed query, so the menu and the startup offer read the
  command through one function. Its behaviour is unchanged: it still shows the error message as
  the disabled menu line when the query fails.

Nothing else changed: no new control command, no change to a project/journal format, no change to
`record.recovery_get_state` itself.

## Regression test

`tests/src/gui/RecordingRecoveryMenuTest.cpp`, new slot `aFailedQueryIsNotNothingToRecover`:

- a real take is journalled (a WAV plus its `in_progress` journal) in the instance's recovery
  directory, so the interrupted take really exists;
- the model is made unaddressable with the existing seam `ControlRegistry::setReady(false)` — the
  registry then refuses the query with the typed `busy` refusal (`src/core/ControlRegistry.cpp:283`),
  which is the same refusal a start-up run gets. No sleep, no clock, no race: the refusal is a
  state, not a timing;
- the warnings Qt emitted during the offer are captured with `qInstallMessageHandler`;
- the test then asserts that the offer did **not** return `NothingToOffer`, that it never asked to
  place takes, that the failure was reported, and that the journal was left exactly as it was.

## Commands and exit codes

| # | Command | Exit |
|---|---|---|
| 1 | `cmake --build build --target zene RecordingRecoveryMenuTest -j 8` (after the fix) | 0 (`logs/build-2.log`) |
| 2 | `ctest -R '^RecordingRecoveryMenuTest$' --output-on-failure` from `build/tests` (after the fix) | 0 — 1/1 test, 9 slots (`logs/test-1.log`, `logs/test-3-green.log`) |
| 3 | `git diff -- include/RecordingRecoveryMenu.h src/gui/RecordingRecoveryMenu.cpp > logs/fix.patch && git apply -R logs/fix.patch` | 0 (reverted) |
| 4 | `cmake --build build --target zene RecordingRecoveryMenuTest -j 8` (reverted) | 0 (`logs/build-3-red.log`) |
| 5 | `ctest -R '^RecordingRecoveryMenuTest$' --output-on-failure` from `build/tests` (reverted) | **8** — RED: `FAIL! aFailedQueryIsNotNothingToRecover() 'choice != StartupRecoveryChoice::NothingToOffer' returned FALSE.` (`logs/test-2-red.log`) |
| 6 | `git apply logs/fix.patch` | 0 (restored) |
| 7 | `cmake --build build --target zene RecordingRecoveryMenuTest -j 8` (restored) | 0 (`logs/build-4-green.log`) |
| 8 | `ctest -R '^RecordingRecoveryMenuTest$' --output-on-failure` from `build/tests` (restored) | **0** — GREEN: 9 passed, 0 failed (`logs/test-3-green.log`) |
| 9 | `ctest -R 'Recording\|Recover\|Crash' --output-on-failure` from `build/tests` (the affected family) | **0** — 15/15 tests passed (`logs/test-6-family.log`) |
| 10 | `ctest -R '^MainWindowTest$' --output-on-failure` from `build/tests` (the GUI target that includes the changed header) | **0** (`logs/test-8-mainwindow.log`) |
| 11 | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | **0** — every gate PASS (see the gate table below) |
| 12 | `python3 ~/.local/share/zene-bunny-fix/qwen/submit_check.py <worktree> new-p17-138` | **0** — `RESULT: PASS - submit` (`logs/submit-check-1.log`) |

The RED run failed on a real assertion (`choice != NothingToOffer`), not on a build error: the
regression test uses only the API that exists before the fix (`offerRecordingRecovery`,
`StartupRecoveryChoice::NothingToOffer`, `ControlRegistry::setReady`), so it compiles against both
the fixed and the unfixed product code.

## Gate table

`python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` — exit 0:

```
  PASS  fork-sources
  PASS  all-sources-reproduce
  PASS  no-upstream-regression
  PASS  check-namespace
  PASS  no-tautology
  PASS  doc-figures
  PASS  complexity
  PASS  file-length
RESULT: PASS
```

## Files

- `include/RecordingRecoveryMenu.h`
- `src/gui/RecordingRecoveryMenu.cpp`
- `tests/src/gui/RecordingRecoveryMenuTest.cpp`
- `docs/050-features/Q050-046.md` (the feature record names the new API and the new outcome)
- `docs/qwen-fixes-2026-10/cluster-new-p17-138.md` (this report)

## Self-check

- **The regression test FAILS with the product change reverted and PASSES with it.** Reverted
  (`git apply -R logs/fix.patch`): `ctest -R '^RecordingRecoveryMenuTest$'` exit **8**, the slot
  `aFailedQueryIsNotNothingToRecover` fails on `choice != StartupRecoveryChoice::NothingToOffer`
  (`logs/test-2-red.log`). Restored (`git apply logs/fix.patch`): the same ctest exits **0**, 9
  slots passed (`logs/test-3-green.log`). The affected family (`Recording|Recover|Crash`, 15
  tests) exits **0** (`logs/test-6-family.log`).
- **The test is deterministic.** No wall-clock, no sleep, no polling: the failure is injected
  through the existing seam `ControlRegistry::setReady(false)`, which makes `invoke()` answer the
  typed `busy` refusal synchronously, and the report is observed through `qInstallMessageHandler`.
  The same test binary passes on every run.
- **The fix is at the cause and complete.** The cause was the discarded `ControlResult`: the query
  is now read once, through the typed `interruptedTakes(QJsonArray*)`, and the failure travels with
  it. Every caller was checked: `interruptedTakes()` had exactly two callers — the offer (fixed)
  and the test (updated to the typed form) — and `rebuild()`, the sibling path into the same
  command, now goes through the same function and keeps its behaviour (the error message in the
  menu line). `offerRecordingRecovery()` is called from `offerRecordingRecoveryAtStartup()`
  (`src/gui/RecordingRecoveryMenu.cpp:160`), which passes it through unchanged, and from
  `src/core/main.cpp:1793`, which ignores the result — the new `QueryFailed` value changes
  nothing there. The wider type the value flows into, `ControlResult`, carries the typed error
  kind and message, so the failure is not flattened into a string on the way.
- **No collateral damage.** No project or journal format changed, no command was added or
  changed, and no limit was introduced. The successful path is untouched: a query that succeeds
  still yields the same takes, the same "Place on New Tracks / Decide Later" box, the same
  `record.recovery_place` per take and the same unattended stderr line. The only new behaviour is
  on the failed-query path, which used to be silent. The `busy` refusal is logged but not boxed,
  so a start-up run is never parked on a dialog (a modal box in an unattended run would hang the
  instance - see the same rule in `src/core/main.cpp:397-406`).
- **Realtime, undo, commands.** Nothing on the audio thread is touched: the change is in the GUI
  file that builds the File menu and the start-up offer, it allocates no audio-thread state, takes
  no lock and logs nothing on the audio thread. Undo is untouched — no `record.recovery_place` runs
  on the failure path, so the failed query records no undo step, and the successful path still
  places each take with one `record.recovery_place` (one undo step) as before. No new control
  command was added, so the `ControlCommandsSnapshot` canary is unaffected.
- **Old projects and saved data.** Untouched: no serialisation, no file format, no config key
  changed.
- **Portability.** No Qt-6-only API, no platform-specific code: the change uses `QMessageBox`,
  `qWarning` and `ControlResult`, all already used in this file.
- **The ALREADY-FIXED / FALSE-POSITIVE item does not apply**: the verdict is FIXED. The code that
  shows the finding was real on this branch is quoted in "Verdict: REAL" above
  (`src/gui/RecordingRecoveryMenu.cpp:124-128` and `:130-133` before the fix, against the sibling
  `rebuild()` at `:88-92` that already showed the error), and the RED run above is the proof that
  the bug was in the tree and is gone now.
