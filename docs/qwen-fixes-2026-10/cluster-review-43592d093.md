# Cluster review-43592d093: advertised VCA assignment inverse omits shared membership flag

Verdict: **ALREADY-FIXED**

## Finding vs. current tree

The finding claims that the `vca.assign` inverse advertised by `vca.unassign` omits
`also:true`, so replaying it is refused instead of restoring a shared membership
(`also:true`, 0.5.0 L09) membership.

That defect and its regression both exist on this branch already:

- `src/core/ControlCommandsVcaMix.cpp:327` reads whether the member is held by a
  SECOND group BEFORE the removal (`sharedElsewhere`, `src/core/ControlCommandsVcaMix.cpp:75`),
  and the same value is captured in the undo step at
  `src/core/ControlCommandsVcaMix.cpp:329-331`.
- The advertised inverse carries it: `src/core/ControlCommandsVcaMix.cpp:346-351` builds
  `vca.assign` args `{group, channel, also}` from that captured value.
- The regression is `tests/src/core/ControlVcaCommandsTest.cpp:452-473`
  (`undoingUnassignRestoresASharedMembership`): it unassigns a channel shared by two
  groups, asserts the advertised inverse op is `vca.assign`, asserts
  `inverseArgs["also"] == true`, REPLAYS the inverse as written, and then checks both
  memberships (`member_count` 1 in the unassigned group AND 1 in the group that still
  holds it).

Landed by commit `53b86d0ff` ("fix(rework 43592d093): vca.unassign inverse omitted also,
so a shared channel could not be re-added") and still present at HEAD `de5a85a96` after the
later merge `88ffd4644`.

No product change was needed, so this report is the only commit.

## Proof the regression is not vacuous

The product half of `53b86d0ff` was reverse-applied from the working tree
(`git apply -R .../logs/fix.patch`), the test target rebuilt and the same suite rerun: it
fails exactly where the finding says it should - the advertised inverse loses `also`, and
`assignChannel`'s single-valued check (`src/core/ControlCommandsVcaMix.cpp:247-252`) plus
`VcaGroup::addMember`'s refusal of a channel another group holds would reject the replay.
Re-applying the patch restored GREEN.

## Evidence

All output is in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/review-43592d093.r1/logs`.
Commands run from the worktree root unless a different working directory is stated. The tree
was at HEAD `de5a85a96` (clean) for every run except the reverse-applied RED.

| Phase | Exact command | Exit | Log / evidence |
|---|---|---:|---|
| Baseline build | `cmake --build build --target ControlVcaCommandsTest -j"$(nproc)"` | 0 | `build-1.log` |
| Baseline GREEN at HEAD | From `build/tests`: `ctest -R '^ControlVcaCommandsTest$' --output-on-failure` | 0 | `test-1.log`; 1/1 CTest suite passed |
| Save product patch | `git show 53b86d0ff -- src/core/ControlCommandsVcaMix.cpp > .../logs/fix.patch` | 0 | Patch is the product half only |
| Reverse-apply product fix | `git apply -R .../logs/fix.patch` | 0 | `also` removed from the advertised inverse |
| Rebuild without the fix | `cmake --build build --target ControlVcaCommandsTest -j"$(nproc)"` | 0 | `build-2.log` |
| **RED** without the fix | From `build/tests`: `ctest -R '^ControlVcaCommandsTest$' --output-on-failure` | **8** | `test-2-red.log`; `FAIL! ...undoingUnassignRestoresASharedMembership() Actual (inverseArgs.value("also").toBool()): 0 / Expected (true): 1` at `tests/src/core/ControlVcaCommandsTest.cpp(463)`; Totals 11 passed, 1 failed |
| Restore the fix | `git apply .../logs/fix.patch` | 0 | Tree back to HEAD, `git status` clean |
| Rebuild with the fix | `cmake --build build --target ControlVcaCommandsTest -j"$(nproc)"` | 0 | `build-3.log` |
| **GREEN** with the fix | From `build/tests`: `ctest -R '^ControlVcaCommandsTest$' --output-on-failure` | **0** | `test-3-green.log`; 100% tests passed out of 1 |
| Qt case count | From `build/tests`: `ctest -R '^ControlVcaCommandsTest$' -V` | 0 | `test-6-verbose.log`; Totals 12 passed, 0 failed |
| Build the rest of the family | `cmake --build build -j"$(nproc)" --target VcaGroupTest VcaRefreshAtomicityTest ControlVcaEditGroupsTest ControlVcaLockedEditsTest VcaStripLifetimeTest VcaStripViewTest` | 0 | `build-4-family.log` |
| VCA family run | From `build/tests`: `ctest -R 'Vca' --output-on-failure` | 8 | `test-5-family.log`; 7 of 8 CTest suites passed, see note |
| Complexity gate | `bash tests/complexity-gate.sh --check` | 1 | `gate-complexity.log`; pre-existing at HEAD, see note |
| File length gate | `bash tests/file-length-gate.sh --check` | 1 | `gate-file-length.log`; pre-existing at HEAD, see note |

Affected-family test count: `ctest -R 'Vca'` runs 8 registered suites (`VcaGroupTest`,
`VcaRefreshAtomicityTest`, `ControlVcaCommandsTest`, `ControlVcaEditGroupsTest`,
`ControlVcaLockedEditsTest`, `VcaStripLifetimeTest`, `VcaStripViewTest`,
`ControlVcaCommands`); `ControlVcaCommandsTest` itself runs 12 Qt cases.

### Pre-existing failures observed at HEAD, NOT caused by this finding

No file changed in this task (`git status` is clean and identical to HEAD
`de5a85a96` before, during and after the runs), so these were already present at HEAD:

1. `VcaStripViewTest` (GUI) fails `aStripWhoseGroupIsRemovedRetiresAtOnce()` and then
   segfaults in `cleanupTestCase` under `QT_QPA_PLATFORM=offscreen`
   (`test-5-family.log`). Unrelated to the `vca.assign` / `vca.unassign` transaction
   payload; the sibling `VcaStripLifetimeTest` passes. Not investigated: out of scope for
   this finding, and the tree was not modified here.
2. Both ratchet gates fail at HEAD with `REGRESSION` entries in files that have nothing to
   do with VCA (complexity: `tests/src/gui/XyPadPairTest.cpp`,
   `src/core/DawProjectReadTracks.cpp`, `src/core/ControlCommandsMixerTools.cpp`,
   `src/core/ControlCommandsMixerStrip.cpp`, `src/core/ProjectRevisions.cpp`,
   `src/gui/ControlCommandsShortcuts.cpp`; file length: `src/core/TakeLane.cpp`,
   `src/core/SmfInterchangeReader.cpp` and 12 other files). `grep -ci vca` is 0 in both
   gate logs, and neither file of this finding appears in a failure list
   (`src/core/ControlCommandsVcaMix.cpp` 490 lines, no new over-target function). These
   entries arrived with the 2311 commits merged onto the branch after `53b86d0ff`. Gate
   baselines and limits were left untouched, as required; no fix was attempted for other
   lanes' regressions.

## Files committed

- `docs/qwen-fixes-2026-10/cluster-review-43592d093.md` (this report)
- `tests/fork-sources.txt` (registering this new report file)

No product or test source changed: the fix (`src/core/ControlCommandsVcaMix.cpp`) and its
regression (`tests/src/core/ControlVcaCommandsTest.cpp`) are already in HEAD.