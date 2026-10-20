FIXED

# Cluster new-p10-476-port: an XY pad opened without macros stays unusable after a macro is added

The finding is real. `XyPadDialog` filled its axis combo boxes only in its constructor and set the pad's enabled state once, so a pad opened on a channel with no macros stayed disabled with empty axis lists even after a macro was added. Round 2 rebuilt the lists in `fillMacros()` but only when `refresh()` was called explicitly, and no rack macro add/remove path calls it - the user-visible bug survived.

## What this round changed (each point Luna asked for)

1. **Wired open pads to the actual rack macro add/remove changes.** Every mutation of a rack's macro list - `RackMacros::addMacro` / `removeMacro` / `insertMacro` / `clear` / `loadSettings` - now notifies listeners registered on that list ([include/RackMacros.h:133-148](include/RackMacros.h), [src/core/RackMacros.cpp:150-188](src/core/RackMacros.cpp) notify calls at 212, 221, 229, 312, 394). `Rack > Add macro...` (RackMenu.cpp:240-243), an agent's `rack.macro_add` / `rack.macro_remove`, and `control.undo` (whose journal steps call the same mutators) all go through those methods, so an open pad rebuilds in place. The dialog registers a listener on its channel's rack in its constructor ([src/gui/XyPad.cpp:246-252](src/gui/XyPad.cpp)) and removes it by id in the destructor ([src/gui/XyPad.cpp:254-257](src/gui/XyPad.cpp)) - the store is process-lifetime and id-keyed (mirroring `ControlRegistry::addShutdownHook`), so a channel removed under an open pad can never leave the destructor touching freed memory; a dying `RackMacros` drops its own listeners ([src/core/RackMacros.cpp:150-157](src/core/RackMacros.cpp)). No notification on value/target edits (`setValue`/`addTarget`): a drag stays one clean path, nothing new runs per pad move.
2. **The regression adds a macro through that path, with no manual `refresh()`.** `thePadBecomesUsableWhenAMacroIsAddedAfterOpening` ([tests/src/gui/XyPadPairTest.cpp:343](tests/src/gui/XyPadPairTest.cpp)) opens the pad on an empty channel, runs `rack.macro_add` (exactly what Rack > Add macro... runs) and asserts the pad became usable - the `dialog.refresh()` call round 2 had is gone.
3. **Selection identity survives macro-N index shifts.** `fillMacros()` now preserves the chosen macros by NAME, not by `macro-N` id ([src/gui/XyPad.cpp:269-300](src/gui/XyPad.cpp)): removing an earlier macro erases its vector entry and shifts every later id ([src/core/RackMacros.cpp:225-230](src/core/RackMacros.cpp)), so the old id would silently name a different macro. Covered by `thePadKeepsItsChosenMacrosWhenAnEarlierOneIsRemoved` ([tests/src/gui/XyPadPairTest.cpp:366](tests/src/gui/XyPadPairTest.cpp)): X on Gamma (macro-2), Y on Beta (macro-1), remove macro-0 - X stays Gamma (now macro-1), Y stays Beta (now macro-0).
4. **`changed()` fires when the final macro is removed.** `syncPoint()`'s empty branch emits `changed()` before returning ([src/gui/XyPad.cpp:310-314](src/gui/XyPad.cpp)), so an open axis list reloads its now-empty rows. Covered by `thePadSaysChangedWhenTheLastMacroIsRemoved` ([tests/src/gui/XyPadPairTest.cpp:391](tests/src/gui/XyPadPairTest.cpp)) with a `QSignalSpy` (no timing).
5. **Adapted the one existing test whose tail encoded the old non-following behaviour.** `aDragIsOneStepAndARefusalIsShown` ([tests/src/gui/XyPadPairTest.cpp:211](tests/src/gui/XyPadPairTest.cpp)) previously asserted that removing the Y macro while the pad was open leaves a stale id that the next move refuses. That staleness is exactly the bug being fixed, so the tail now asserts the follow (Y re-selects the surviving macro by name; the move drives it) and keeps a genuine refusal for the last-macro-removed case ("Not set: ..." on the status line). No assertion was dropped to make a failure pass - the changed assertions are the behaviour the fix intentionally changes; the drag-is-one-undo-step core of the test is untouched.
6. **Report format fixed:** verdict is line 1; exit codes below are the actual unpiped ones (the inverse `ctest` run returns **8**, as Luna measured).

## Evidence

Logs: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-476-port.r3/logs`. CTest ran from `build/tests`.

| Check | Exact command | Exit | Result |
|---|---|---:|---|
| RED on original product code | `ctest -R '^XyPadPairTest$' --output-on-failure` | **8** | 4 failed / 8 passed: the three new tests plus the adapted tail (`test-1-red.log`) |
| GREEN with fix | `ctest -R '^XyPadPairTest$' --output-on-failure` | **0** | 1/1 test passed (`test-2-green.log`) |
| Affected family with fix | `ctest -R '^(XyPadPairTest\|ItemListButtonsTest\|CanvasKeyboardTest\|RackMacrosTest\|RackTest\|RackZonesTest\|UndoHonestyTest)$' --output-on-failure` | **0** | 7/7 tests passed (`test-3-family.log`, `test-6-family-restored.log`) |
| Inverse proof: revert product fix | `git diff -- include/RackMacros.h src/core/RackMacros.cpp include/XyPad.h src/gui/XyPad.cpp > .../logs/fix.patch && git apply -R .../logs/fix.patch` | **0** | product hunks reverted, tests kept |
| Inverse build | `cmake --build build --target zene XyPadPairTest ItemListButtonsTest CanvasKeyboardTest -j12` | **0** | `build-5-inverse.log` |
| Inverse RED | `ctest -R '^XyPadPairTest$' --output-on-failure` | **8** | same 4 failed / 8 passed (`test-4-inverse-red.log`) - the test catches the bug |
| Restore fix | `git apply .../logs/fix.patch` | **0** | product hunks back |
| Restored build (all touched-header targets) | `cmake --build build --target zene XyPadPairTest ItemListButtonsTest CanvasKeyboardTest RackMacrosTest RackTest RackZonesTest UndoHonestyTest -j12` | **0** | `build-6-restored.log` |
| Restored GREEN | `ctest -R '^XyPadPairTest$' --output-on-failure` | **0** | 1/1 (`test-5-green-restored.log`) |
| Feature gate | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-476-port` | **0** | PASS table, see below (`gate-1.log`) |

One build error was hit and fixed on the way: `resolveRack` lives in `lmms`, not `lmms::control` (`build-3.log`, exit 2; corrected, `build-4.log`, exit 0).

Feature-check table (`gate-1.log`):

| Gate | Result |
|---|---|
| fork-sources | PASS (exit 0) |
| all-sources-reproduce | PASS (exit 0) |
| no-upstream-regression | PASS (exit 0) |
| check-namespace | PASS (exit 0) |
| no-tautology | PASS (exit 0) |
| doc-figures | PASS (exit 0) |
| complexity | PASS - no new item in your files |
| file-length | PASS - no new item in your files |
| **RESULT** | **PASS (exit 0)** |

No gate baseline was touched; the known pre-existing direct-gate failures on the train are untouched.

## Files committed

- `include/RackMacros.h`
- `src/core/RackMacros.cpp`
- `include/XyPad.h`
- `src/gui/XyPad.cpp`
- `tests/src/gui/XyPadPairTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p10-476-port.md`

(All four product files are already registered as fork sources in `tests/fork-sources.txt`; no registration change needed.)

## Self-check

**Q: The regression test FAILS with your product change reverted and PASSES with it (exit codes).**
Yes, proven twice with exit codes. Pre-fix on the original code: `ctest -R '^XyPadPairTest$' --output-on-failure` -> **exit 8**, 4 failed / 8 passed (`test-1-red.log`: `thePadBecomesUsableWhenAMacroIsAddedAfterOpening`, `thePadKeepsItsChosenMacrosWhenAnEarlierOneIsRemoved`, `thePadSaysChangedWhenTheLastMacroIsRemoved`, and the adapted tail of `aDragIsOneStepAndARefusalIsShown`). Inverse-patch proof (tests kept, only product files reverted via `git apply -R fix.patch`): build **exit 0** (`build-5-inverse.log`), same ctest -> **exit 8**, same 4 failures (`test-4-inverse-red.log`). Restored (`git apply fix.patch`): build **exit 0** (`build-6-restored.log`), ctest -> **exit 0** 1/1 (`test-5-green-restored.log`); family 7/7 **exit 0** (`test-6-family-restored.log`).

**Q: The test is deterministic: no wall-clock timing, sleeps or races (name the seam or injected values it uses).**
Deterministic. The seam is `RackMacros::addChangeListener` notification, which fires synchronously *inside* the mutating `rack.macro_add`/`rack.macro_remove` handler on the same (UI) thread - the test only invokes the command and asserts the already-rebuilt pad; there is nothing to wait for. No timers, sleeps, `qWait`, or polling anywhere in the new or changed assertions. The `changed()` signal coverage uses `QSignalSpy` on the direct `emit changed()` (`XyPadPairTest.cpp:391`), also synchronous. The drag test's existing `control.set_undo_coalescing window_ms` is a pre-existing injected value, not timing.

**Q: The fix is at the cause and complete: every caller and sibling of what you changed was checked and stays consistent.**
Cause: the pad read the macro list only in its constructor. The single funnel every list mutation goes through is the `RackMacros` mutators, so the notification lives there - `addMacro`, `removeMacro`, `insertMacro` (commands, journal undo/redo steps, the `__transaction` inverse replays all call these exact methods), `clear` (channel teardown paths), `loadSettings` (project load, one notification via the reset-directly change). Siblings checked and deliberately NOT notified: `setValue`, `addTarget`, `insertTarget`, `removeTarget`, `apply`, `reindexChains` (the list of macros is unchanged; notifying there would rebuild the pad on every drag step). Callers of `refresh()` (`XySource::setValue` after a `rack.macro_set`) keep working. Every add/remove client was checked: RackMenu.cpp:240-255 (menu path), ItemListSurfaceSources.cpp:94 (item-list `rack.macro_set` path, refresh retained), control socket/agent invokes, `control.undo`/`redo` journal steps (`ControlCommandsRackMacros.cpp:141-218`), and the transaction-inverse replay (`ControlReversibilityTableAction.cpp:285-292`). Targets rebuilt after the `RackMacros.h` change: zene plus XyPadPairTest, ItemListButtonsTest, CanvasKeyboardTest, RackMacrosTest, RackTest, RackZonesTest, UndoHonestyTest - all pass.

**Q: No collateral damage: old projects and saved data still load as before, and any limit or check you added does not hit other callers that legitimately exceed it.**
The `<macro>`/`<target>` XML written by `saveSettings` and read by `loadSettings` is byte-for-byte unchanged; no schema, undo record, command or journal format changed. `loadSettings` only swaps `clear()` for a direct `m_macros.clear()` plus one guarded notification - same resulting list, same behaviour for every non-listening caller. No numeric limits or checks were added, so no caller can trip one; `clear()`'s new empty-guard is a no-op on an already-empty list. Pads that resolve to no rack register no listener and behave exactly as before. Family tests covering rack macros, undo honesty and the item lists: 7/7 pass.

**Q: If your verdict is ALREADY-FIXED or FALSE-POSITIVE: the code (file:line) or the intended contract that shows it.**
Not applicable - the verdict is FIXED (the finding was real; evidence lines above).

**Q: Realtime, undo and commands.**
Listeners fire only on the control thread - the audio thread never touches macros (class comment in `RackMacros.h`). The notification runs no new code on any audio-thread path. No new control commands were added (the `ControlCommandsSnapshot` canary is unmoved). Undo stays one honest step per recorded step; the notification neither creates nor alters journal steps.

**Q: Registration and gates.**
Feature gate `python3 .../feature_check.py <worktree>` -> **exit 0**, every row PASS (complexity and file-length: "no new item in your files"; no baseline touched). All four product files and the test file are already registered fork sources (`tests/fork-sources.txt` lines for `include/RackMacros.h`, `src/core/RackMacros.cpp`, `include/XyPad.h`, `src/gui/XyPad.cpp`, `tests/src/gui/XyPadPairTest.cpp`), so no registration file changed. Test counts: XyPadPairTest 1/1; affected family 7/7 - zero tests executed would have been an error, none was.
