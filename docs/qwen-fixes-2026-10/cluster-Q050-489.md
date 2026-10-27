IMPLEMENTED

# cluster Q050-489 - Make session and clip state transitions preserve shared invariants

Unit: P1, size L, area L02 (HS-004). Branch `feat/050-Q050-489-oc`, base `050/m0-train-18` (`7641e9493`).
Feature record (transition map, file:line pointers, limits): `docs/050-features/Q050-489.md`.
Logs: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/Q050-489.r1/logs` (one numbered file per build/test).

## What was found and fixed

Four transitions were reproduced on the base and fixed. Each one now has a test that fails on the base:
- **T1:** clearing a playing cell, shrinking the grid under it, or `session.clear` left it playing. Its track stayed
  taken over and the launcher still said `playing`. Undo and redo had the same problem.
- **T2:** giving a playing cell a new pattern or file kept the old sound until some unrelated launch. Undoing that edit
  did not restore the old sound.
- **T3:** deleting or moving a pattern track renumbered the patterns, so a cell ended up naming a different pattern.
- **T4:** a command queued before a reset (a project change or stop_all) was applied after it. The closing project's
  content came back. My change exposed this through `SessionPlaybackRenderTest`.

There is one shared boundary, `syncSessionLauncher` (`src/core/SessionTransitions.cpp`). Every session block edit, undo
and redo goes through it, and so do capture_to_slot, a landed take and pattern renumbering. The GUI and agent paths share
it because both go through the same registered commands. T4 is fixed inside the scheduler: each queued command is tagged
with the reset generation. Four items are listed as unresolved in the feature record and not counted as fixes. One of
them is that grid columns are track positions.

## Acceptance

| # | Criterion | Test (command) | Exit with change | Exit, product reverted |
|---|---|---|---|---|
| 1 | Transition map + reproduced failures | `ctest -R ^SessionTransitionsTest$` (T1-T4 cases), map in `docs/050-features/Q050-489.md` | 0 (8/8, test-14.log, test-18.log) | 8: all product hunks reverted, 5 of 8 cases fail (test-11-reverted.log). Only the scheduler hunk reverted: the T4 case fails, `patternId` 7 vs -1 (test-15-reverted-sched.log) |
| 2 | Deterministic launch/stop/edit/undo/redo/save/load sequence, authored identities kept, transient fields excluded | `SessionTransitionsTest::launchStopEditUndoRedoSaveLoadKeepsTheAuthoredState` | 0 | 8 (fails, test-11-reverted.log) |
| 3 | Socket MIDI + audio workflows with record-to-arrangement, readback, rendered/audio events | `ctest -R ^ControlSessionTransitions$` (`tests/control-session-transitions.py`) | 0 (test-10.log) | 8: 4 checks fail (test-7-reverted.log) |
| 4 | Quantisation, follow-action, legato, linked-content and scene behaviour unchanged | 45 related tests (list in regression-targets.txt + the session socket tests) | 44/45 pass (test-19-final-regression.log); the one failure is environmental, see below | n/a |
| 5 | Common API used by UI and agent; no RT allocation/locking | `SessionGridViewTest::clearingAPlayingCellFromItsMenuStopsIt` (triggers the grid's Clear Cell action), the socket test, `SessionSchedulerTest` allocation probe | 0 (13/13, test-9.log) | 8: the GUI case fails (test-6-reverted.log) |

Failures that are not mine:
- `ControlUndoStructuralTranscript` fails with `plugin.load returned no instance id`. It fails identically on the base
  product (test-16-base-structural.log, exit 8). The cause is that this tree builds only the tripleoscillator module.
- `ControlMcpGroupCoverage` fails because the bridge's Python `mcp` package raises
  `TypeError: Server.__init__() got an unexpected keyword argument 'on_list_tools'`. This is environmental and has no
  connection to the session code.

## Self-check

- **Fails reverted, passes restored:** for each test I reversed the product hunks (`git diff 7641e9493 481183161a --
  include src | git apply -R`), rebuilt (build-8 and build-10, exit 0), ran, then restored with `git checkout HEAD --
  include src` and rebuilt (exit 0). Results with the fix reverted:
  - core test: exit 8
  - GUI test: exit 8
  - socket test: exit 8
  - the T4 case, reverting only the scheduler hunk: exit 8

  After restoring, all three tests exit 0.
- **Each criterion proven by a test I ran:** see the table. Exits are 0 with the change in place: SessionTransitionsTest,
  SessionGridViewTest and ControlSessionTransitions each 0, and the regression set is 44/45 with only the environmental
  failure above.
- **One undo step per edit, honest classes:** no command was added and no reversibility class changed. Each session edit
  is still one journal step. The undo or redo closure now also re-syncs the launcher, which adds no extra step.
  `SessionTransitionsTest` undoes one edit with a single `control.undo`. `ReversibilityContractTest` and
  `ControlCommandsSnapshot` pass.
- **Realtime:** the boundary runs on the model thread and reaches the engine only through lock-free POD queue pushes
  (`SessionScheduler::publishSlotContent` and `requestStop`).
  - The only audio-thread change is `SessionScheduler::drainCommands`: one atomic load and an integer compare per
    command. It does not allocate, lock, wait, do I/O or emit a signal.
  - `SessionScheduler::enqueue` is model thread; it copies the command and adds its tag there.
  - `SessionSchedulerTest` (including its allocation probe) passes.
  - TSan does not run on this box. The race reasoning is in the feature record: the generation is an atomic written by
    reset(), and the tag is fixed before the queue's release store.
- **Old projects load and render as before; round trip:** the saved format is unchanged.
  - The sequence test saves, reopens and compares authored state, and confirms that no launch state is written.
  - The socket test saves and reopens and gets identical slots with nothing launched.
  - `SessionCheckpointTest`, `ScenesUpconvertTest`, `SessionModelTest` and `LegacyPatternUpgradeTest` pass.
  - `SessionSchedulerRenderTest` passes; it covers byte-identical renders with nothing launched.
- **Portability:**
  - No Qt-6-only API is used.
  - The new code uses `std::make_tuple`, a lambda and a function template; nothing needs C++20 beyond what the tree
    already uses.
  - There is no `std::filesystem`, and no `min`/`max`/`small`/`near`/`far`/`interface` identifiers.
  - The test includes `<algorithm>` explicitly.
- **Gates:** `feature_check.py` writes its logs into `<worktree>/../…-feature-check`, which is outside my write jail,
  and it exited with PermissionError. I ran an identical copy whose only change is the log directory
  (`runs/Q050-489.r1/scratch/feature_check_local.py`, same base cache). All 8 gates PASS (feature-check-3.log, exit 0).
- **Snapshot and figures:** the snapshot was regenerated live (867 ids / 76 groups). `doc-figures-gate.py` exits 0.
  - Only four command descriptions changed: set_slot, clear_slot, set_grid and clear.
- **Environment notes:**
  - ccache's configured temp dir is outside the jail, so builds used `CCACHE_TEMPDIR=/tmp/ccache-tmp-q489`.
  - Adding the new test required CMake to regenerate inside the existing build tree. This happened automatically during
    the build; I did not run a configure step myself.
