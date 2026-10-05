# Batch 08 — Space Bunny lane `cmds1`

Six findings triaged against the current tree; all six were real and all six are
fixed, one commit each. No command was added, removed or renamed and no
command's params or result schema changed.

## Cluster 5637 — Setting MPE to its current value still creates an undo entry

Verdict: FIXED

Evidence: `src/core/ControlCommandsDevice.cpp` `mpeSet()` called
`control::addUndoStep(...)` unconditionally, after building the reply that could
say `changed: false`. A `device.mpe_set` naming the value the switch already held
therefore grew the history by a no-op entry, and the next `control.undo` spent
itself on it.

Commit: `52996ac40`

Test: `ControlNoteScaleVerbsTest::theMpeSwitchTakesNoUndoStepForAValueItAlreadyHas`
(tests/src/core/ControlNoteScaleVerbsTest.cpp) — real edit, then a same-value
call asserting `changed: false`, then ONE undo must restore the original flag.

Red/green:
- red: `cmake --build build --target ControlNoteScaleVerbsTest` EXIT=0; `cd build/tests && ctest -R '^ControlNoteScaleVerbsTest$' --output-on-failure` EXIT=8 (Actual 1, Expected 0 at line 720)
- green: `cmake --build build --target ControlNoteScaleVerbsTest zene` EXIT=0; `cd build/tests && ctest -R '^ControlNoteScaleVerbsTest$' --output-on-failure` EXIT=0
- gates: complexity EXIT=0, file-length EXIT=0

## Cluster 4755 — detect.apply omits its declared tempo map result

Verdict: FIXED

Evidence: `src/core/ControlCommandsDetectApply.cpp:84` — the result schema
declares `tempo_map`, but the handler built `path`, `tempo`, `key`, `method`,
`bounds` and `key_state` and never inserted `tempo_map`, so a client was told to
expect the map the call produced and received only the partial `tempo` summary.

Commit: `bff5d086c`

Test: `ControlDetectCommands` — `detect.apply publishes the tempo map its own
result schema declares` (tests/control-detect-commands.py), asserted against the
live `detect.get_state` tempo_map block.

Red/green:
- red: `cd build/tests && ctest -R '^ControlDetectCommands$' --output-on-failure` EXIT=8 (FAIL, `published={}`)
- green: `cmake --build build --target zene` EXIT=0; `cd build/tests && ctest -R '^ControlDetectCommands$' --output-on-failure` EXIT=0
- gates: complexity EXIT=0, file-length EXIT=0

The inserted value reuses `detect.get_state`'s own tempo_map block rather than
rebuilding it, so the two surfaces cannot drift.

## Cluster 4756 — detect.apply advertises itself as its own inverse

Verdict: FIXED

Evidence: `src/core/ControlCommandsDetectApply.cpp` built its transaction with
`transactionPayload(beforePayload, "detect.apply", {"path": …}, true, …)`.
`transactionPayload` (src/core/ControlEditSupport.cpp:481-493) emits
`inverse {op, args}` and nothing else, so the record published `detect.apply` as
the way back while calling itself reversible. But the restore is an ACTION
checkpoint (`recordDetectApplyRestore`, src/core/ControlDetectSupport.cpp:184):
re-issuing `detect.apply` runs the detection AGAIN, it does not write the
before-state back.

Commit: `5a821e62d`

Test: `ControlDetectCommands` — `detect.apply's recorded inverse restores rather
than re-applying` (tests/control-detect-commands.py), asserting the top
`detect.apply` record is reversible and its `inverse.op` is `control.undo`.

Red/green:
- red: `cd build/tests && ctest -R '^ControlDetectCommands$' --output-on-failure` EXIT=8 (`inverse={'args': {'path': '.../click128.wav'}, 'op': 'detect.apply'} reversible=True`)
- green: `cmake --build build --target zene` EXIT=0; `cd build/tests && ctest -R '^ControlDetectCommands$' --output-on-failure` EXIT=0 (1/1 passed)
- gates: complexity EXIT=0, file-length EXIT=0

The inverse now names `control.undo` with no arguments — the shape
`scale.from_detected_key` already records for the same non-replayable restore.

## Cluster 1054 — Acknowledging an already-offered report records a no-op as a mutation

Verdict: FIXED

Evidence: `handleAcknowledgeReport()` (src/core/ControlCommandsCrash.cpp:276-321)
checked only `report.isFile()`, then always called
`crashreporter::acknowledgePendingReport()` and recorded an irreversible
`__transaction`. It never checked the `offered` sentinel, which the same handler
later reads back at line 294.

Commit: `a60eec1ec`

Test: `ControlCrashReporter` — `a refused acknowledge must not record another
transaction` inside `check_acknowledge` (tests/control-crash-reporter.py): after
the sentinel is proven on disk, a second acknowledge must be a typed refusal and
must not add an A16 record.

Red/green:
- red: `cd build/tests && ctest -R '^ControlCrashReporter$' --output-on-failure` EXIT=8 (FAIL `a refused acknowledge must not record another transaction (1 -> 2)`; the second call replied `ok:true`)
- green: `cmake --build build --target zene` EXIT=0; `cd build/tests && ctest -R '^ControlCrashReporter$' --output-on-failure` EXIT=0 (1/1 passed)
- gates: complexity EXIT=0, file-length EXIT=0

The refusal happens BEFORE the write and before any transaction is recorded,
matching the group's own style (`crash.enable` refuses "already armed", a second
`crash.disable` refuses, a second discard is `not_found`).

## Cluster 5630 — Discard reports success even when files remain

Verdict: FIXED

Evidence: `handleDiscardReport()` (src/core/ControlCommandsCrash.cpp) called
`crashreporter::discardPendingReport()` and then set `discarded: true`
unconditionally, with `removed` built from the PRE-call state
(`hadReport`/`hadOffered`) and no read-back. The engine operation returns void
and `::unlink` fails on a directory or a read-only directory entry
(src/core/CrashReporter.cpp:402-407), so the handler acknowledged a report that
was still on disk — unlike `handleAcknowledgeReport`, which already read its own
sentinel back.

Commit: `99260fa5d`

Test: `ControlCrashReporter` — `check_discard_reads_back` (tests/control-crash-reporter.py)
plants a non-empty directory at the report path so `::unlink` cannot remove it,
then requires a typed failure rather than `discarded: true`.

Red/green:
- red: `cd build/tests && ctest -R '^ControlCrashReporter$' --output-on-failure` EXIT=8 (FAIL `a discard whose files remain must be a typed failure, not discarded:true: {}`)
- green: `cmake --build build --target zene` EXIT=0; `cd build/tests && ctest -R '^ControlCrashReporter$' --output-on-failure` EXIT=0 (1/1 passed)
- gates: complexity EXIT=0, file-length EXIT=0

The first gate run after the fix failed (complexity EXIT=1, `handleDiscardReport`
at CCN 13), so the read-back was extracted into `pathsLeftAfterDiscard` and both
gates were re-run to EXIT=0. No gate was loosened or re-anchored.

## Cluster 5631 — Undoing crash.enable can leave the reporter pointed at a different directory

Verdict: FIXED

Evidence: `install()` sets the remembered directory (`setReportDirectory`,
src/core/CrashReporter.cpp:222-238) while `uninstall()` (src/core/CrashReporter.cpp:280-299)
clears the handlers and KEEPS it. `handleEnable` recorded a bare `crash.disable`
as its inverse, and `crash.disable` takes no arguments, so undo could disarm the
reporter but never move it back: the reporter stayed pointed at the newly chosen
directory and `crash.list_reports` named paths the undo had quietly changed. The
before-state also recorded the REQUESTED directory, not the previous one.

Commit: `60cd559cb`

Test: `ControlCrashReporter` — `check_arm_cannot_move` inside `check_arm_pair`
(tests/control-crash-reporter.py): arming at a second existing directory must be
refused, must not arm, and must leave the remembered report directory where it
was.

Red/green:
- red: `cd build/tests && ctest -R '^ControlCrashReporter$' --output-on-failure` EXIT=8 (7 failed check(s), led by `the refused arm must leave the remembered report directory where it was: '/tmp/.../workspace/second-report-dir/crash-reports'` and `the refused arm must not have armed the reporter: {'armed': True}`)
- green: `cmake --build build --target zene` EXIT=0; `cd build/tests && ctest -R '^(ControlCrashReporter|CrashReportMenuTest|CrashReporterArmTest)$' --output-on-failure` EXIT=0 (3/3 passed)
- gates: complexity EXIT=0, file-length EXIT=0

`handleEnable` now refuses to arm at a directory other than the remembered one,
naming it in the refusal. This is the finding's second sanctioned option and
matches the file's stated preference for refusing over recording a lying inverse;
adding a `directory` param to `crash.disable` was rejected as a params-schema
change. The comparison is on cleaned paths, because main() hands the reporter a
trailing slash.

The first green attempt over-fired (7 failures): the remembered raw string keeps
its trailing slash while `armDirectory()` cleans both sides, so the already-correct
directory compared unequal. Comparing cleaned paths fixed it.

## Summary

- FIXED: 6 (5637, 4755, 4756, 1054, 5630, 5631)
- FALSE-POSITIVE / ALREADY-FIXED / DUPLICATE / REAL-UNTESTED / SKIPPED-SCOPE: 0

Every fix was captured by a test that failed first for the stated reason, and both
gates were run before each commit. Two fixes (5630, 5631) needed a helper split to
satisfy the complexity ratchet, and the tests were rebuilt against a current `zene`
before every result was trusted.

Leftovers: none — all six findings are fixed, committed and covered by regression tests.