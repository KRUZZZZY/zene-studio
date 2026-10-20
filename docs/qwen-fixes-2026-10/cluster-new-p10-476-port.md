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

1. **Decision with evidence:** REAL -> FIXED. Cause was the pad never re-reading the rack's macro list (`src/gui/XyPad.cpp` constructor called `fillMacros()` once; nothing notified it). Evidence lines above.
2. **Deterministic regression that fails without the fix:** `test-1-red.log` and `test-4-inverse-red.log` both show the four assertions failing with the product hunks absent (exit 8); `test-2-green.log` / `test-5-green-restored.log` show them passing with the fix (exit 0). No sleeps, no timers, no wall clock: the notification is synchronous inside the mutating call, and the signal coverage uses `QSignalSpy` on the direct emit.
3. **Fix at the cause, siblings consistent:** notification sits in the one place every add/remove path funnels through - the `RackMacros` mutators (`addMacro`, `insertMacro`, `removeMacro`, `clear`, `loadSettings`); value/target mutators deliberately do not notify (a pad move must not rebuild mid-drag). Command path, socket/agent path and undo journal steps all call these same methods, so they all behave the same. `loadSettings` resets directly instead of via `clear()` so a project load produces one notification, not one per the transient empty list.
4. **No collateral damage:** the persisted `<macro>` XML, save/load format, undo records and command schemas are untouched; `RackMacrosTest`, `RackTest`, `RackZonesTest` and `UndoHonestyTest` all still pass (7/7 family). Pads whose channel resolved to no rack register no listener and behave exactly as before. Old behaviour outside macro list changes (value edits, `refresh()`, refusals) is unchanged.
5. **Realtime, undo, commands:** listeners fire only on the control thread - the audio thread never touches macros (RackMacros.h class comment). No new control commands were added (no `ControlCommandsSnapshot` movement). Undo remains one honest step per recorded step; the notification does not create or alter journal steps.
6. **Registration and gates:** feature_check exit 0, all rows PASS. Test counts recorded: XyPadPairTest 1/1; affected family 7/7 (not zero).
