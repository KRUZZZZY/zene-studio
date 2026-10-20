# Cluster new-p10-476-port: XY pad without macros stays unusable after a macro is added

**Verdict: FIXED**

The finding is real. `XyPadDialog` filled its axis combo boxes only in the constructor and set the pad's enabled state once there, so a pad opened on a channel with no macros stayed disabled with empty axis lists even after a macro was added. `refresh()` only called `syncPoint()`, which returned early on empty lists and never repopulated them. `fillMacros()` now clears and rebuilds the axis lists (preserving the selected macro ids where they still exist), restores sensible defaults otherwise, updates the enabled state, and then syncs the point; `refresh()` calls `fillMacros()` instead of `syncPoint()`.

Evidence: `src/gui/XyPad.cpp:252` (`fillMacros`), `src/gui/XyPad.cpp:236` (constructor call), `include/XyPad.h:92` (`refresh`). Regression: `tests/src/gui/XyPadPairTest.cpp` (`thePadBecomesUsableWhenAMacroIsAddedAfterOpening`).

## Verification evidence

CTest ran from `build/tests`; build/test logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-476-port.r2/logs`.

| Check | Exact command | Exit | Result |
|---|---|---:|---|
| RED on original code | `ctest -R '^XyPadPairTest$' --output-on-failure` | 1 | 1 test failed: `area->isEnabled()` returned FALSE (`test-2.log`). |
| GREEN with fix | `ctest -R '^XyPadPairTest$' --output-on-failure` | 0 | 10/10 tests passed (`test-3.log`). |
| Inverse-patch RED | `ctest -R '^XyPadPairTest$' --output-on-failure` | 1 | Rebuilding without the product fix reproduced the same failure (`test-2.log`). |
| Affected family | `ctest -R '^(XyPadPairTest\|ItemListButtonsTest\|CanvasKeyboardTest)$'` | 0 | 3/3 tests passed (`test-4.log`, `test-5.log`). |
| Feature gate | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py .` | 0 | PASS (`gate-1.log`). |

Inverse proof:

```sh
git diff -- src/gui/XyPad.cpp include/XyPad.h > .../logs/fix.patch && git apply -R .../logs/fix.patch   # EXIT=0
cmake --build build --target XyPadPairTest     # EXIT=0, build-3.log
ctest -R '^XyPadPairTest$' --output-on-failure # EXIT=1, test-2.log
git apply .../logs/fix.patch                   # EXIT=0
cmake --build build --target XyPadPairTest zene # EXIT=0, build-4.log
ctest -R '^XyPadPairTest$' --output-on-failure # EXIT=0, test-3.log
```

## Self-check

- The regression test FAILS with the product change reverted and PASSES with it: inverse-patch run exited 1 (`test-2.log`, `area->isEnabled()` returned FALSE), fixed run exited 0 (`test-3.log`), 10/10 XyPadPairTest passing.
- The test is deterministic: no sleeps, timers or races; it calls `XyPadDialog::refresh()` synchronously after `rack.macro_add` and asserts the resulting combo counts and enabled state.
- The fix is at the cause and complete: `fillMacros()` is the single place that populates the axis combos and sets `m_area->setEnabled`; `refresh()` (used by `ItemListSurfaceSources` value edits and the existing follow-macros test) now calls it, so lists and enabled state follow macro add/remove/rename. `syncPoint()` still runs at the end and keeps the dot/tooltip consistent. Callers (`ShellCommandMenus` opens the dialog; `ControlCommandsAccessibleEditing` uses `changed()`) are unaffected.
- No collateral damage: existing tests `aDragIsOneStepAndARefusalIsShown` and `thePadFollowsTheMacrosItIsOverAndSaysWhereItStands` still pass; combos are rebuilt only inside `fillMacros`/`refresh`, with signals blocked during rebuild and selection preserved by macro id, so a value-only edit does not reset the user's axis choice. Saved data and audio paths are untouched.
- Verdict is FIXED, not ALREADY-FIXED/FALSE-POSITIVE, so no code/contract citation for those cases is required.

## Files committed

- `src/gui/XyPad.cpp`
- `include/XyPad.h`
- `tests/src/gui/XyPadPairTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p10-476-port.md`
