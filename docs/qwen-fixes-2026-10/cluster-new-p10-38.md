# Cluster new-p10-38: send-enable undo can target a different route after channel reordering

**Verdict: FIXED**

`mixer.send_enable` recorded its undo/redo step with the endpoints' mixer **positions**
(`mix_ch_t` indices), then found the route again with the same stale positions when the
step ran. Reordering the mixer channels between the call and its undo made the step act on
whatever route now occupied those positions - toggling an unrelated send - instead of the
route it recorded. The step now keeps the endpoints' **persistent channel ids** and resolves
them to live positions when it runs, the same stable-id pattern the routing verbs already
use (`recordMixerRouteStep` / `liveRouteEnds` in `src/core/ControlMixerSupport.cpp`).

The unrelated-route toggle was masked in the simple "no route at the old positions" case
because `AutomatableModel::setValue` also journals the `enabledModel` object itself
(`src/core/AutomatableModel.cpp:397`), and `ControlRegistry::runHandler` merges that object
checkpoint into the command's action step (`mergeCheckpointsSince`). The undo then restores
the recorded model directly, so only a *different* route sitting at the captured positions
shows the bug. The regression drives exactly that case.

## Code evidence

- Before: `src/core/ControlCommandsMixerTools.cpp:104` `findMixerRoute(from, to)` keyed on
  positions, and `:124` `addUndoStep([from, to, before]...)` captured the positions.
- After: `src/core/ControlCommandsMixerTools.cpp:106` `setSendEnabled(int fromId, int toId, bool on)`
  resolves each endpoint through `channelWithId()` and calls `findMixerRoute()` with the live
  `index()` values; `:137` captures `ends.from->id()` / `ends.to->id()` and `:139` records
  those persistent ids in the undo/redo closures.
- Regression: `tests/src/core/MixerToolsTest.cpp:166`
  `aSendEnableUndoFollowsItsChannelsThroughAReorder`.

## Evidence table

| Check | Exact command and result |
| --- | --- |
| RED before fix | `cd build/tests && ./MixerToolsTest aSendEnableUndoFollowsItsChannelsThroughAReorder > .../logs/red-4.log 2>&1; echo EXIT=$?` -> **EXIT=1**. `FAIL! : ... '!other->enabled()' returned FALSE. (the send-enable undo must never toggle the route now at the recorded positions) Loc: [../../tests/src/core/MixerToolsTest.cpp(195)]` |
| GREEN after fix | Same binary run `> .../logs/green-1.log 2>&1; echo EXIT=$?` -> **EXIT=0**, 3 passed / 0 failed. |
| Build (final) | `cmake --build build --target zene MixerToolsTest > .../logs/build-5.log 2>&1; echo EXIT=$?` -> **EXIT=0** |
| Reversed-fix proof | `git apply -R .../logs/fix.patch; echo REVERT=$?` -> **EXIT=0**; `cmake --build build --target zene MixerToolsTest > .../logs/build-7.log 2>&1` -> **EXIT=0**; same binary `> .../logs/red-5.log 2>&1; echo TEST=$?` -> **EXIT=1** |
| Restored-fix proof | `git apply .../logs/fix.patch; echo APPLY=$?` -> **EXIT=0**; rebuild `> .../logs/build-8.log 2>&1` -> **EXIT=0**; same binary `> .../logs/green-2.log 2>&1; echo TEST=$?` -> **EXIT=0** |
| Affected family | `cd build/tests && ctest -R '^(MixerToolsTest\|MixingRoutingTest\|MixerModesTest\|MixerSendsOnFadersTest\|ControlVcaCommandsTest\|ProjectStateTest)$' --output-on-failure > .../logs/test-3.log 2>&1; echo EXIT=$?` -> **EXIT=0**, `100% tests passed out of 6` (6 CTest targets executed). |
| Complexity gate | `bash tests/complexity-gate.sh --check > .../logs/gate-complexity.log 2>&1; echo EXIT=$?` -> **EXIT=1** (pre-existing; see gate note) |
| File-length gate | `bash tests/file-length-gate.sh --check > .../logs/gate-filelength.log 2>&1; echo EXIT=$?` -> **EXIT=1** (pre-existing; see gate note) |

## Gate note (pre-existing baseline drift, not caused by this change)

Both ratchet gates are RED on this worktree **before** any edit and remain byte-for-byte the
same after it. They fail on functions/files this change never touches:

- `bash tests/complexity-gate.sh --check` with the product fix reverted
  (`.../logs/gate-complexity-pristine.log`) -> **EXIT=1**, the same six
  `REGRESSION: new function over target` lines, including
  `lmms::adjust@215-249@src/core/ControlCommandsMixerTools.cpp (CCN 12)` - `adjust`'s body is
  unchanged by this fix (its CCN is 12 in both trees). The functions this fix adds/changes are
  under the target: `lmms::setSendEnabled` CCN 4, `lmms::sendEnable` CCN 5 (`python3 -m lizard`).
- `bash tests/file-length-gate.sh --check` lists 19 exceedances, all in files untouched here
  (`src/core/TakeLane.cpp`, `tests/src/core/DeviceDspTest.cpp`, ...); neither changed file
  appears (`ControlCommandsMixerTools.cpp`, `MixerToolsTest.cpp`).
- `git status --short` at gate time showed only the two files this change edits.

Per the lane rules the baselines were **not** re-anchored. The fix introduces no new
over-target function and pushes no file over 500 code lines.

## Commands run

All builds ran from the worktree root; ctest ran from `build/tests`.

```text
cmake --build build --target zene MixerToolsTest > .../logs/build-5.log 2>&1; echo BUILD=$?          # EXIT=0
./MixerToolsTest aSendEnableUndoFollowsItsChannelsThroughAReorder > .../logs/green-1.log 2>&1; echo TEST=$?  # EXIT=0
ctest -R '^MixerToolsTest$' --output-on-failure > .../logs/test-1.log 2>&1; echo EXIT=$?             # EXIT=0
ctest -R '^(MixerToolsTest|MixingRoutingTest|MixerModesTest|MixerSendsOnFadersTest|ControlVcaCommandsTest|ProjectStateTest)$' --output-on-failure > .../logs/test-3.log 2>&1; echo EXIT=$?  # EXIT=0
bash tests/complexity-gate.sh --check > .../logs/gate-complexity.log 2>&1; echo EXIT=$?              # EXIT=1 (pre-existing)
bash tests/file-length-gate.sh --check > .../logs/gate-filelength.log 2>&1; echo EXIT=$?             # EXIT=1 (pre-existing)
```

Log folder: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-38.r1/logs`

## Files committed

- `src/core/ControlCommandsMixerTools.cpp`
- `tests/src/core/MixerToolsTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p10-38.md`

Both product/test files are already listed in `tests/fork-sources.txt` (lines 1404 and 2317),
so no manifest edit is needed.
