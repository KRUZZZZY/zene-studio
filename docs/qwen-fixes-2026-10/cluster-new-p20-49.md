FIXED

# Cluster new-p20-49: a maximum `jump_to` overflows in the follow-action editor

## Verdict

**FIXED.** The finding is real. `addRow` added 1 to a signed `int` before clamping it to the spin box, and OK then wrote
the box's value back over the stored scene. Loading `jump_to` = 2147483647 wrote 0 (old line 187 overflowed to INT_MIN,
the box clamped to 1, OK wrote 0). Loading `jump_to` = 600 wrote 511.

## Evidence (file:line, current tree after the fix)

- The bug, before the fix (`git show HEAD:src/gui/FollowActionEditor.cpp`):
  - line 187: `jump->setValue(action.value(QStringLiteral("jump_to")).toInt(0) + 1);`. The `+ 1` on an `int` that
    holds INT_MAX is signed overflow (undefined behaviour).
  - line 186: `jump->setRange(1, 512);`, so the box clamps anything above 512 to 512.
  - line 244: `jump_to` is written as `spinbox->value() - 1` whatever the file held, so an untouched row is rewritten.
- The fix:
  - `src/gui/FollowActionEditor.cpp:51`: `kMaxScenes = 512`, the grid's cap (`src/core/SessionModelPrivate.h:157`,
    `MaxScenes = 512`). The box range is `1..kMaxScenes` (line 199).
  - `:200-204`: the stored value is read as a `double` (exact for every `int`), clamped to `[0, 511]` before the `+ 1` for
    display, and kept in the box's `loaded` property.
  - `:130-135` `jumpOf()`: returns the typed or stepped scene (`value() - 1`) if the box was edited, else the loaded scene
    unchanged.
  - `:205-209`: the box is marked `edited` on any value change (arrow or wheel step) and on typing (`textEdited`, which
    catches typing the number the box already shows, the same rule `numberBox` uses).
  - `:266`: `chain()` writes `jumpOf(...)`.
- The engine side (not changed): `src/core/ControlCommandsSessionFollow.cpp:131-149` (`intField`: whole number in the int
  range), `:198-203` (`jump_to` refused if negative, or if it is `>= sceneCount` for a Jump). For a non-Jump row the
  engine accepts any non-negative int, so a stored value past the grid is valid data for that row. The old editor
  rewrote it silently.
- Callers and siblings: `chain()` is called only from `apply()` (`FollowActionEditor.cpp`). `SessionGridMenus.cpp:134`
  only constructs the editor; it does not read `jump_to`. `numberBox`/`valueOf` (chance, bars) are unchanged.
  `grep -rn jump_to src` lists only the editor, `ControlCommandsSessionFollow.cpp`, `ControlCommandsSessionShared.h:296`
  (state output) and `SessionClip.cpp` (save/load), none of which changed.

## Behaviour change (for review)

Before the fix, OK on a row whose stored scene the box could not show silently rewrote it (to 511, or to 0 for the
overflow case). After the fix, OK writes the stored value unchanged unless the user edits that Jump box, and the engine
then decides:
- A Jump row past the grid (`>= sceneCount`) or below 0 is refused by the engine with its own error, and the dialog stays
  open until the user edits the box. The old behaviour retargeted it silently.
- A non-Jump row with a stored scene past 512 is kept as stored. The old behaviour rewrote it to 511.
- Limit: the box still shows the nearest scene it can (512 for INT_MAX). The stored value is not displayed. This is the
  one part of the finding's "display a different target" that the fix does not change.

## Regression test

`tests/src/gui/FollowActionEditorTest.cpp` (4 new test functions, same family, file already registered in
`tests/CMakeLists.txt:1270`):
- `aMaximumJumpSceneIsWrittenBackAsLoaded`: INT_MAX is written back, box shows 512. **RED before the fix.**
- `aJumpSceneTheBoxCannotShowIsKeptUntilTyped`: 600 is written back; typing 512 writes 511. **RED before the fix**
  (the typing half is a guard).
- `aStepOfAJumpBoxIsAnEdit`: a Down step from the shown 512 writes 510, not the loaded 600. Guards the `valueChanged`
  path (passes before and after, since the old code also wrote the shown value).
- `anInRangeJumpSceneIsWrittenAsLoaded`: scenes 0 and 511 round-trip. A guard that passes before and after.

The existing `anExplicitChanceEditIsWritten`, `aPassiveFocusLossKeepsTheLoadedChance` and
`aRowRemovedWhileItsControlsReceiveFocusLeavesTheEditorIntact` still pass.

## Commands and exit codes

Environment notes (not code failures):
- The first build (`build-1-red.log`) failed with `EXIT=2`: ccache could not create temp files under
  `/home/kruzzzzy/.local/share/zene-bunny-fix/dsh/home/ccache-tmp-r6` (Permission denied). Every later build used
  `CCACHE_TEMPDIR=/home/kruzzzzy/.cache/zene-tmp/ccache-tmp` (on disk, writable); the build was otherwise unchanged.
- `feature_check.py` run as given (`gates-1.log`) failed with `EXIT=1`: it creates
  `<worktree>/../zene-qwen-new-p20-49-feature-check`, which is outside the write-jail (PermissionError on `mkdir`). See
  the gates section.

| Step | Command (run from the worktree / `build/tests`) | EXIT | Log (LOG FOLDER) |
| --- | --- | --- | --- |
| RED build | `CCACHE_TEMPDIR=… cmake --build build --target FollowActionEditorTest -j4` | 0 | `build-2-red.log` |
| **RED test** | `cd build/tests && ctest -R '^FollowActionEditorTest$' --output-on-failure` | **8** | `test-1-red.log` |
| Mutation: save fix | `git diff -- src/gui/FollowActionEditor.cpp > fix.patch` | 0 | `fix.patch` |
| Mutation: revert fix | `git apply -R fix.patch` | 0 | |
| Mutation build | `cmake --build build --target FollowActionEditorTest -j4` | 0 | `build-4-unfixed.log` |
| **Mutation test (must FAIL)** | `ctest -R '^FollowActionEditorTest$' --output-on-failure` | **8** (both bug tests FAIL) | `test-4-unfixed.log` |
| Mutation: reapply fix | `git apply fix.patch` | 0 | |
| Re-fixed build | `cmake --build build --target FollowActionEditorTest -j4` | 0 | `build-5-refixed.log` |
| **GREEN test** | `ctest -R '^FollowActionEditorTest$' -V` | **0** (9 passed, 0 failed) | `test-5-refixed.log` |
| GREEN build (first) | `cmake --build build --target FollowActionEditorTest -j4` | 0 | `build-3-green.log` |
| GREEN test (first) | `ctest -R '^FollowActionEditorTest$' --output-on-failure` | 0 | `test-2-green.log` |

Affected family: `FollowActionEditorTest` is one ctest test (`#451`) with 9 cases in the verbose run: 7 test functions
plus init/cleanup. Count from `test-5-refixed.log`: `Totals: 9 passed, 0 failed, 0 skipped`.

RED failures (`test-1-red.log`):
- `aMaximumJumpSceneIsWrittenBackAsLoaded()` at `tests/src/gui/FollowActionEditorTest.cpp:136`: actual 0, expected 2147483647.
- `aJumpSceneTheBoxCannotShowIsKeptUntilTyped()` at `FollowActionEditorTest.cpp:148`: actual 511, expected 600.

Mutation proof: the RED failures reproduce after reverting only the product file (`test-4-unfixed.log`), and the GREEN
run passes after reapplying it (`test-5-refixed.log`).

### Gates

`python3 .../qwen/feature_check.py <worktree>` as given: `EXIT=1` (`gates-1.log`), PermissionError creating
`projects/zene-studio-program/zene-qwen-new-p20-49-feature-check`, outside the write-jail. Nothing was computed.

The same runner was run with two changes and nothing else (copy at
`/home/kruzzzzy/.cache/zene-tmp/fc-new-p20-49/feature_check_logdir.py`, diff = two lines): `ROOT` points at the real
`qwen/` directory (for `FIX-BASE` and the cached base listing for `37eaf07557`), and the log folder is the scratch
`gate-logs` (copied to `gate-logs/` in the LOG FOLDER). Command: `python3 -I …/feature_check_logdir.py <worktree>`,
`EXIT=0` (`gates-2.log`).

```
feature-check zene-qwen-new-p20-49 (base 37eaf0755, 2 changed files)
  PASS  fork-sources            exit 0
  PASS  all-sources-reproduce   exit 0
  PASS  no-upstream-regression  exit 0
  PASS  check-namespace         exit 0
  PASS  no-tautology            exit 0
  PASS  doc-figures             exit 0
  PASS  complexity              no new item in your files
  PASS  file-length             no new item in your files
RESULT: PASS
```

No new file, so no registration is needed in `tests/fork-sources.txt` or `tests/upstream-modifications.txt`. No header
changed. No command was added; the snapshot and canary are not affected.

## Self-check

1. **The regression test FAILS with the product change reverted and PASSES with it.** Yes: `test-4-unfixed.log` (EXIT 8,
   two FAIL lines), `test-5-refixed.log` (EXIT 0, 9 passed). Both bug tests are RED before the fix.
2. **The test is deterministic.** No timing, sleeps, or races. The edits are simulated with `QTest::keyClicks` and
   `keyClick` on the box's own line edit, after `editor.show()`, as the existing chance test does. The bug is driven by
   the stored JSON value, not by the environment.
3. **Fix at the cause, complete.** The cause is the `int` `+ 1` before clamping, and the write-back of the clamped
   display. Both are fixed in `addRow` and `chain()`. Siblings are checked above (callers, `numberBox`, engine range
   check). The engine already range-checks `jump_to` (`ControlCommandsSessionFollow.cpp:198-203`), so the editor does not
   need its own bound.
4. **No collateral damage.** Loading does not change: the box still shows `clamp(scene) + 1`, and an in-range scene is
   written exactly as before (guard test `anInRangeJumpSceneIsWrittenAsLoaded`). The changed behaviour is the
   write-back of out-of-range stored values, described above under "Behaviour change". It is a decision for review, not
   an accident.
5. **Realtime, undo, commands.** The editor is GUI-only. Nothing runs on the audio thread. No command was added, and
   apply still goes through `runCommand` (one undo step, as before).
6. **Registration and gates.** No new files. Gates pass (table above, via the logdir copy). The literal `feature_check.py`
   call is blocked by the write-jail, so `submit_check.py`'s gates item will report the same PermissionError. This is the
   environment, not the change.
7. **Portability (Qt 5.15 / MSVC / macOS).** `std::clamp` (C++17) and `QJsonValue` only; no Qt-6-only API; no
   platform-specific code. The new `#include <algorithm>` is standard.
8. **Alternatives considered.** Clamping only (the finding's literal suggestion) removes the UB and the display
   difference, but OK would still rewrite the stored value, which is the second half of the finding. Refusing OK for
   out-of-range rows (like an unknown action type) would block a non-Jump row the engine accepts; not chosen.
9. **Residual risk.** The box still shows the nearest scene (512) for a stored value it cannot show, so the display
   still differs from the stored value. A reviewer may want a "not in grid" hint on that row. Not done here (out of
   scope for a bug fix, and it would add UI).
10. **"If your verdict is ALREADY-FIXED or FALSE-POSITIVE"**: not applicable. The verdict is FIXED, and the code cited
    above is the evidence for the fix.
11. **`submit_check.py` (`submit-check-1.log`, EXIT=1).** Its only FAIL is the gates item. That item runs
    `feature_check.py`, which hits the same write-jail PermissionError described under "Gates", so it is an environment
    limit and not a code failure. Every other item passes: commits on the branch, working tree clean, verdict in the
    first lines, tests changed.
