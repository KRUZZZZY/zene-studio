# cmds1 batch-12 — ControlCommands[A-M] findings

Lane `cmds1`, worktree `zene-bunny-cmds1`, branch `bugs/bunny-cmds1-2026-10-05`.
Six reported findings: **4 FIXED**, **2 FALSE-POSITIVE**, 0 SKIPPED-SCOPE, 0 DUPLICATE.

### Cluster 2819 — Failed handoff can leave an incomplete bundle
Verdict: FIXED
Evidence: `src/core/ControlCommandsHandoff.cpp:162` wrote the session copy before
`collectProjectMediaInto` (was line 170) and before `writeManifest`, and both failure
paths returned early leaving the copy behind. Added `discardPartialBundle()` and called
it on both failure returns, so the folder is all of a bundle or none of it.
Commit: faa175bfa
Test: LibraryProjectsTest::aFailedHandOffLeavesNoBundleBehind
Red/green: `cmake --build build --target zene LibraryProjectsTest` EXIT=0; RED
`cd build/tests && ctest -R '^LibraryProjectsTest$' --output-on-failure` EXIT=8
(FAIL! aFailedHandOffLeavesNoBundleBehind — "the session copy survived a failed hand off:
project.mmpz"; 11 passed, 1 failed); GREEN same command EXIT=0 (12 passed). Gates:
complexity-gate.sh --check EXIT=0, file-length-gate.sh --check EXIT=0.

### Cluster 5671 — Stable effect count omits mixer rack and post-insert effects
Verdict: FIXED
Evidence: `src/core/ControlCommandsIdContract.cpp:105` counted only `target.chain`, and
`ControlDeviceSupport.cpp:157` points a channel's target at `m_fxChain` alone — while the
rack's parallel chains and the post-fader inserts hand out `fx-` ids too
(`ControlDeviceSupport.cpp:288-300`, the same three kinds `ControlCommandsHandoff.cpp:68-74`
already counted). Extracted `channelEffectCount()`, which adds the rack chains and the
post inserts; the split also kept the handler inside the CCN budget.
Commit: 1d5893829
Test: RackMacrosTest::theIdContractCountsEveryChainThatHandsOutFxIds
Red/green: `cmake --build build --target zene RackMacrosTest` EXIT=0; RED
`ctest -R '^RackMacrosTest$' --output-on-failure` EXIT=8 (FAIL! the comparison; 9 passed,
1 failed); GREEN EXIT=0 (10 passed). Gates: complexity-gate.sh --check EXIT=0 (the first
attempt regressed CCN 11 -> 12 and was fixed by the helper split), file-length-gate.sh
--check EXIT=0.

### Cluster 5672 — Undo does not cancel an in-flight hardware insert ping
Verdict: FIXED
Evidence: `src/core/ControlCommandsHardwareInsert.cpp:125-136` restored settings in its undo
step but never touched the ping state (`HardwareInsert::requestPing` only stores
`PingState::Requested`, `src/core/HardwareInsert.cpp:100-103`; `setSettings` does not reset
it), so the click could be answered later and write a measured latency into the project the
undo had just restored. Added `HardwareInsert::cancelPing()` (header and source together)
and called it from the ping's undo step — one atomic store on the interface thread, nothing
on the audio path.
Commit: 2bc56f391
Test: HardwareIoTest::anUndoCancelsThePingItRequested
Red/green: `cmake --build build --target zene HardwareIoTest` EXIT=0; RED
`ctest -R '^HardwareIoTest$' --output-on-failure` EXIT=8 (FAIL! pingState() != Idle; 12
passed, 1 failed); GREEN EXIT=0 (13 passed). Gates: complexity-gate.sh --check EXIT=0,
file-length-gate.sh --check EXIT=0.

### Cluster 5673 — I/O setup can report no restart needed while the device is still mismatched
Verdict: FIXED
Evidence: `src/core/ControlCommandsHardwareIo.cpp:142` computed
`reopen = channels != countBefore && channels != deviceChannels().outputs`, so a names-only
call while the counts still differ reported `restart_required:false`. Split the two ideas:
`restart_required` is now `setup.outputs.channels != deviceChannels().outputs`, while
`applied` keeps the old "count changed on this call" meaning so existing replies are
unchanged.
Commit: e75a36c9b
Test: HardwareIoTest::aStillMismatchedOutputCountAsksForARestart
Red/green: `cmake --build build --target zene HardwareIoTest` EXIT=0; RED
`ctest -R '^HardwareIoTest$' --output-on-failure` EXIT=8 (FAIL! restart_required returned
FALSE; 11 passed, 1 failed); GREEN EXIT=0 (12 passed). SurroundTest, the other consumer of
this file, EXIT=0. Gates: complexity-gate.sh --check EXIT=0, file-length-gate.sh --check
EXIT=0.

### Cluster 1085 — `history_jump` can bypass its step limit and hit `abs(INT_MIN)`
Verdict: FALSE-POSITIVE
Evidence: the step cap is enforced by the schema before the handler ever runs.
`src/core/ControlCommandsHistoryJump.cpp:84` declares
`integerProperty(-MaxJump, MaxJump)` (`MaxJump` = 1000, line 44), and
`ControlRegistry::invoke` calls `validateArgs(cmd->argsSchema, args)` at
`src/core/ControlRegistry.cpp:251` before dispatching, which reaches `checkRange`
(`src/core/ControlSchema.cpp:84-99`, applied at line 155). A wire value of -2147483648 or
1001 is rejected with InvalidArgs there, so `std::abs(steps)` at
`ControlCommandsHistoryJump.cpp:67` never sees an out-of-range int. Adding a redundant
in-handler check would defend against nothing.
Commit: none
Test: none
Red/green: none

### Cluster 3877 — `history_jump` is classified as nonmutating
Verdict: FALSE-POSITIVE
Evidence: the classification is deliberate and enforced. `control.history_jump` has an
explicit contract row at `src/core/ControlReversibilityTableW3Custom.cpp:88-89` —
`RC::NotMutating`, with the reason that it only runs `control.undo`/`control.redo` in turn
and "records nothing of its own" — and `ControlSurfaceReferenceTest.cpp:189` asserts that a
command classed NotMutating is not marked mutating. Marking it mutating would break that
existing test and contradict the row's stated reason: it changes no state of its own, it
drives the two commands that do.
Commit: none
Test: none
Red/green: none

Leftovers: none — all four real defects are fixed, committed and green; no finding was left
half-done, and no command or schema was added, removed or renamed.