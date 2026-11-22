FIXED

# Cluster new-p23-46: ControlCues::holds dereferences a possibly null layout item

Verdict: FIXED (latent: the null item is allowed by the `QLayout::itemAt` contract, and no in-tree layout was found returning null for an in-range index; the regression forces it).

## Finding and location

Luna's finding names `src/core/ControlCues.cpp:58`. That path does not exist on this branch: the file is
`src/gui/widgets/ControlCues.cpp` (the line numbers match). The function is in the anonymous namespace.

Before the fix, `src/gui/widgets/ControlCues.cpp:57-58` read:

```cpp
QLayoutItem* item = layout->itemAt(i);
if (item->widget() == widget || holds(item->layout(), widget)) { return true; }
```

`holds` runs from `applyTarget` (`src/gui/widgets/ControlCues.cpp:180`), which is reached on every Polish event of a
shared control in accessible mode. Qt's contract for `QLayout::itemAt` is that a layout must return `nullptr` when there is
no item at the index (Qt 6 documentation, `doc.qt.io/qt-6/qlayout.html`, fetched 2026-10-26). `count()` is the
bound of the loop, so a layout that reports a null item below its count made `item->widget()` a null dereference.

## Evidence the path is reachable (and what is not proven)

- `src/gui/ControlLayout.cpp:164-168`: `ControlLayout::itemAt` returns `nullptr` when `pairAt` reaches the end.
- `src/gui/ControlLayout.cpp:139-141`: `ControlLayout::count()` returns `m_itemMap.size() - 1`, computed separately from `pairAt`.
  I read `pairAt` (`ControlLayout.cpp:146-160`) and found no in-range index that returns null, so no current in-tree
  trigger was found. I did not prove the count/itemAt relation for every map state, so this stays latent.
- Luna's "when" (a layout reports a null item at an index) is exactly what the contract permits, so the guard is at the cause.

## Fix

`src/gui/widgets/ControlCues.cpp:58` (one line added in `holds`):

```cpp
if (item == nullptr) { continue; }  // QLayout::itemAt may return nullptr for an index below count()
```

Skipping the index (rather than returning false) keeps the search going, so a real widget after a gap is still found.
No caller changed: `applyTarget` is the only caller of `holds`.

## Regression test

`tests/src/gui/KnobKeyboardTest.cpp`: new `GapLayout` (a `QLayout` that stores a null item at index 0) and new slot
`accessibleTargetSkipsNullLayoutItems`. The slot runs in a fresh accessible-mode child process, the same way
`accessibleTargetSurvivesKnobSizingReset` does, because accessible mode is fixed at startup. It places a null item at
index 0 and a `Knob` at index 1, calls `ensurePolished()`, and asserts the knob's minimum width and height are at least
`tokens::targetSize()`. It is deterministic: no timers, no waits, no event loop.

Registration: `tests/CMakeLists.txt` adds `KnobAccessibleNullItemTest` (accessible-mode env, same pattern as
`KnobAccessibleTargetSizingTest`).

## Commands and exit codes

Logs are in the LOG FOLDER `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p23-46.r1/logs`.

| Step | Command (run from the worktree / `build/tests` as stated) | EXIT | Log |
| --- | --- | --- | --- |
| build attempt 1 (failed on ccache temp-file write outside the write-jail, not on code) | `cmake --build build --target KnobKeyboardTest -j4` | 2 | `build-red-1.log` |
| build (ccache disabled; same flags) | `CCACHE_DISABLE=1 cmake --build build --target KnobKeyboardTest -j4` | 0 | `build-red-2.log` |
| **RED** (fix not yet applied) | `cd build/tests && ctest -R '^KnobAccessibleNullItemTest$' --output-on-failure` | **8** | `test-red-1.log` (SIGSEGV at address 0, frame `holds` ← `applyTarget`) |
| build with fix | `CCACHE_DISABLE=1 cmake --build build --target KnobKeyboardTest -j4` | 0 | `build-green-1.log` |
| **GREEN** | `cd build/tests && ctest -R '^KnobAccessibleNullItemTest$' --output-on-failure` | **0** (1/1 passed) | `test-green-1.log` |
| family | `cd build/tests && ctest -R 'Knob' --output-on-failure` | **0** (3/3 passed: KnobKeyboardTest, KnobAccessibleTargetSizingTest, KnobAccessibleNullItemTest) | `test-family-green-1.log` |

### 4b: the test catches the bug

| Step | Command | EXIT | Log |
| --- | --- | --- | --- |
| save product diff | `git diff -- src/gui/widgets/ControlCues.cpp > logs/fix.patch` | 0 | `fix.patch` |
| revert fix | `git apply -R logs/fix.patch` | 0 | |
| rebuild unfixed | `CCACHE_DISABLE=1 cmake --build build --target KnobKeyboardTest -j4` | 0 | `build-4b-unfixed.log` |
| **must FAIL** | `cd build/tests && ctest -R '^KnobAccessibleNullItemTest$' --output-on-failure` | **8** (SIGSEGV) | `test-4b-unfixed.log` |
| re-apply fix | `git apply logs/fix.patch` | 0 | |
| rebuild fixed | `CCACHE_DISABLE=1 cmake --build build --target KnobKeyboardTest -j4` | 0 | `build-4b-fixed.log` |
| **must PASS** | `cd build/tests && ctest -R '^KnobAccessibleNullItemTest$' --output-on-failure` | **0** | `test-4b-fixed.log` |

Also: `CCACHE_DISABLE=1 cmake --build build --target zene -j4` → EXIT 0 (`build-zene-1.log`).

## Gates

The `feature_check.py` command itself exits 1 before any gate runs: it tries to create
`zene-qwen-new-p23-46-feature-check/` beside the worktree, which the write-jail refuses (`PermissionError`, EACCES;
`feature-check-native-1.log`, EXIT 1). I did not edit the shared script. Instead I ran the script's own
functions and logic (`SIMPLE`, `RATCHET`, the cached base listing for `cf3618e61`) from a driver in the LOG FOLDER,
with per-gate logs written there (`gate_driver.py`, `gates-1/`):

| Gate | Result |
| --- | --- |
| fork-sources | PASS (exit 0) |
| all-sources-reproduce | PASS (exit 0) |
| no-upstream-regression | PASS (exit 0) |
| check-namespace | PASS (exit 0) |
| no-tautology | PASS (exit 0) |
| doc-figures | PASS (exit 0) |
| complexity (ratchet) | PASS: no new item in the changed files |
| file-length (ratchet) | PASS: no new item in the changed files |
| **driver RESULT** | **PASS**, EXIT 0 (`feature-check-1.log`) |

The changed-file set the gate used is 3 files (the three listed below).

## Files committed

- `src/gui/widgets/ControlCues.cpp` (guard)
- `tests/src/gui/KnobKeyboardTest.cpp` (GapLayout, regression slot, `QLayout` include)
- `tests/CMakeLists.txt` (`KnobAccessibleNullItemTest` registration)
- `docs/qwen-fixes-2026-10/cluster-new-p23-46.md` (this report)

## Notes for the reviewer

- The finding's `src/core/ControlCues.cpp` path is wrong for this branch; the file is `src/gui/widgets/ControlCues.cpp`.
- The `ccache` failure (`build-red-1.log`) is an environment issue: ccache wrote its temp file to a path outside the write-jail. `CCACHE_DISABLE=1` skips the cache and compiles with the same flags.
- Not checked: the `ControlLayout::count()` vs `pairAt()` relation (`size() - 1`) is an odd count and may under-report by one in some map states. I did not prove that, and it is outside this bug.

## Self-check

- **Regression fails with the product change reverted and passes with it (exit codes).** Yes. Reverted: `git apply -R`
  → rebuild → `ctest -R '^KnobAccessibleNullItemTest$'` EXIT 8, SIGSEGV in `holds` (`test-4b-unfixed.log`). Re-applied:
  rebuild → EXIT 0, 1/1 passed (`test-4b-fixed.log`). The RED run before the first edit also gave EXIT 8 (`test-red-1.log`).
- **Deterministic.** The test uses no timers, sleeps, waits or event loop. The only wait is the child-process
  `waitForFinished(120000)` in the whole-executable path, the same pattern as `accessibleTargetSurvivesKnobSizingReset`.
  Inputs are fixed: a `GapLayout` with a null item at index 0, a `Knob` at index 1, and one explicit `ensurePolished()`
  (the Polish event drives `applyTarget`). The layout is never shown or activated, so no layout pass runs.
- **Fix at the cause; callers and siblings checked.** `holds` has one caller: `applyTarget`
  (`src/gui/widgets/ControlCues.cpp:180`), and `holds` recurses only into `item->layout()`, which the guard covers
  (`ControlCues.cpp:57-59`). Siblings in the same file (`applyTarget`, `prepare`, `kindOf`) do not index layouts.
  Repo-wide `itemAt(` uses (`rg -n 'itemAt\(' src include`) are on other types: `ControlLayout` (`ControlLayout.cpp:164-168`,
  returns `nullptr` only past the end), `QLayout` helpers in `ShellChrome.cpp:316` and `DevicePanel.cpp` (which dereference
  `itemAt(i)->widget()` without a check). Those are real siblings, but outside this finding and outside `ControlCues`, so
  I did not change them. Reported here as a follow-up candidate, not fixed.
- **No collateral damage.** The change is a single `continue` on a null item that previously crashed. Non-null paths run
  exactly as before: `item->widget()` and the recursion are unchanged for every non-null item. No new limit or check
  was added, no saved data or project format is touched, no command was added, and no audio-thread code is involved (the
  path runs only on Polish events in accessible mode). Undo is not involved.
- **Verdict evidence.** The verdict is FIXED, not ALREADY-FIXED or FALSE-POSITIVE, so no not-real citation is needed.
  The contract cited is Qt's `QLayout::itemAt` documentation (nullptr when there is no item).

### Gate row: `feature_check.py` (submit_check item "gates")

`feature_check.py` exits 1 in this sandbox before running any gate, because it creates
`zene-qwen-new-p23-46-feature-check/` outside the write-jail (`PermissionError`, `feature-check-native-1.log`).
The gate itself was not run to completion by its own code. I ran its SIMPLE and RATCHET logic from a driver in the
LOG FOLDER (`gate_driver.py`, logs in `gates-1/`); the driver's RESULT was PASS, EXIT 0 (`feature-check-1.log`). The
gate's script and baselines were not edited. A re-run of the native `feature_check.py` from a shell that can write
beside the worktree is still needed to close the native row.
