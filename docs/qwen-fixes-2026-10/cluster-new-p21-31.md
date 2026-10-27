FIXED

# cluster new-p21-31 - vca.color_members can dereference an invalid member channel index

Finding (Luna patrol, BUG 9): `src/core/ControlCommandsMixerTools.cpp:451` - `mixer()->mixerChannel(member)` used without
checking that `member` is in range.

## Decision: FIXED (latent defect; the defensive guard is the fix)

Evidence that the dereference is unguarded:

- `include/Mixer.h:656-661`: `Mixer::mixerChannel()` returns `m_mixerChannels[_ch]` with **no range check** except under
  `#ifdef LMMS_TESTING` (where an out-of-range index returns `nullptr` through the refusal seam). In a release build a stale
  index is an out-of-bounds read. The `if (MixerChannel* channel = ...)` null test in `colorMembers` therefore protects only
  test builds.
- The mixer's own loops over group members guard the index before the lookup: `src/core/Mixer.cpp:1173-1175` and
  `src/core/Mixer.cpp:1345-1348` (`if (member < m_mixerChannels.size())`). `colorMembers` did not.

Reachability - stated honestly: I traced the production paths that could leave a stale member and found none that does today:

- `Mixer::deleteChannel` (`src/core/Mixer.cpp:1361-1373`) calls `group->channelDeleted()` for every group before the channel
  is erased (`VcaGroup::channelDeleted`, `src/core/VcaGroup.cpp:145-160`, drops the deleted member and shifts higher ones).
- `Mixer::clear()` (`src/core/Mixer.cpp:2566`) calls `clearVcaGroups()` (`src/core/Mixer.cpp:1275`) before it removes channels.
- Project load range-checks each saved member (`src/core/Mixer.cpp:3156-3160`).
- `VcaGroup::addMember` (`src/core/VcaGroup.cpp:105-118`) does not range-check by itself. I did not audit every caller of
  `addMember`; the regression seeds the stale state by calling `addMember(numChannels())` directly, a state I could not reach
  from the command surface.

So the defect is latent: the code is undefined behaviour in release if a stale member ever appears, and it is fixed by the
same guard the sibling loops already use. It is not a reproduced user-visible failure.

Note on the finding text: "no existing test includes this file's header" is not accurate. `tests/src/core/MixerToolsTest.cpp`
already drives `vca.color_members` (`aGroupColoursItsMembers`, `colouringWhatIsAlreadyColoured`). The regression is added there,
so no new file and no registration change are needed.

## The change

- `src/core/ControlCommandsMixerTools.cpp:450-455` (in `colorMembers`): a member at or past `mixer()->numChannels()` is skipped
  before `mixerChannel()` is called. Valid state is unchanged; the reply's `colored` count still counts only existing channels,
  and the undo step is unchanged.
- `tests/src/core/MixerToolsTest.cpp:494-511`: new `aStaleMemberIsSkippedNotRead()`. It counts out-of-range lookups through the
  existing LMMS_TESTING seam (`Mixer::takeRefusedLookupsForTesting`, `include/Mixer.h`), so it is deterministic: no timing,
  sleeps or races. It colours group `{4, stale}`, asserts that channel 4 is coloured, that the count is 1 and that the refused
  lookup count is 0, then undoes and cleans up. Channel 4 is used because channels 1 and 2 belong to the `Drums` group from the
  previous test.

## Commands and exit codes

Build cache: `ccache` could not create temp files in the directory its config names (permission denied, `build-1-red.log`).
I set `CCACHE_TEMPDIR=$TMPDIR/ccache-tmp-p21` for the later builds. That is an environment setting, not a code change.

| # | Command (cwd) | Log (LOG FOLDER) | EXIT |
|---|---|---|---|
| 1 | `cmake --build build --target MixerToolsTest -j4` (worktree) | build-1-red.log | 2 (ccache temp dir, not the test) |
| 2 | `CCACHE_TEMPDIR=$TMPDIR/ccache-tmp-p21 cmake --build build --target MixerToolsTest -j4` | build-2-red.log | 0 |
| 3 | `ctest -R MixerToolsTest --output-on-failure` (build/tests) | test-1-red.log | 8 - **wrong reason**: `addMember(1)` returned false because channel 1 is in the Drums group. Test moved to channel 4. |
| 4 | rebuild as #2 (MixerToolsTest.cpp edited) | build-3-red.log | 0 |
| 5 | `ctest -R MixerToolsTest --output-on-failure` (build/tests) - **RED, unmodified product code** | test-2-red.log | 8: `FAIL! MixerToolsTest::aStaleMemberIsSkippedNotRead()`, `Actual (takeRefusedLookupsForTesting()): 1`, `Expected (0): 0`, `Loc: MixerToolsTest.cpp(506)`; Totals 27 passed, 1 failed |
| 6 | `CCACHE_TEMPDIR=... cmake --build build --target zene MixerToolsTest -j4` (fix applied) | build-4-green.log | 0 |
| 7 | `ctest -R MixerToolsTest --output-on-failure` (build/tests) - **GREEN** | test-3-green.log | 0 (100% passed, 1/1 ctest test) |
| 8 | `git diff -- src/core/ControlCommandsMixerTools.cpp > logs/fix.patch` | - | 0 |
| 9 | `git apply -R logs/fix.patch` (revert the product fix only) | - | 0 |
| 10 | `CCACHE_TEMPDIR=... cmake --build build --target zene MixerToolsTest -j4` | build-5-revert.log | 0 |
| 11 | `ctest -R MixerToolsTest -V` (build/tests) - **RED, with the fix reverted** | test-4-revert-fail.log | 8: `FAIL! ... aStaleMemberIsSkippedNotRead()`; Totals 27 passed, 1 failed |
| 12 | `git apply logs/fix.patch` (re-apply) | - | 0 |
| 13 | `CCACHE_TEMPDIR=... cmake --build build --target zene MixerToolsTest -j4` | build-6-reapply.log | 0 |
| 14 | `ctest -R MixerToolsTest -V` (build/tests) - **GREEN, fix re-applied** | test-5-reapply-pass.log | 0: `PASS : MixerToolsTest::aStaleMemberIsSkippedNotRead()`; Totals 28 passed, 0 failed |

Affected family: `MixerToolsTest` is one ctest test with 28 QTest functions; all 28 pass with the fix. It is the only test in
the tree that drives `vca.color_members` (`grep -rn color_members tests`).

## Gates

- `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>`: **EXIT 1, not a gate result.** It stopped on
  `PermissionError` while creating `<worktree>-feature-check` next to the worktree (`gates-1-feature-check.log`), a path outside
  the write-jail. It also writes a `gate-baselines` cache and uses a `git worktree add` in `zene-cachyos`, which I did not run.
  I did not edit the script or its baselines. Instead I ran each of its gate commands directly in the worktree:

| Gate (command, run in worktree) | Log | EXIT |
|---|---|---|
| fork-sources (`bash tests/fork-sources-gate.sh`) | gate-fork-sources.log | 0 |
| all-sources-reproduce (`bash tests/all-sources-reproduce.sh`) | gate-all-sources-reproduce.log | 0 |
| no-upstream-regression (`bash tests/no-upstream-regression-gate.sh`) | gate-no-upstream-regression.log | 0 |
| check-namespace (`tests/scripted/check-namespace`) | gate-check-namespace.log | 0 |
| no-tautology (`bash tests/no-tautology-gate.sh`) | gate-no-tautology.log | 0 |
| doc-figures (`python3 tests/doc-figures-gate.py`) | gate-doc-figures.log | 0 |
| complexity (`bash tests/complexity-gate.sh --check`) | gate-complexity.log | 1 (ratchet, see below) |
| file-length (`bash tests/file-length-gate.sh --check`) | gate-file-length.log | 1 (ratchet, see below) |

Ratchet items, checked by hand against the two files this change touches:

- `ControlCommandsMixerTools.cpp`: the one listed item is `new function over target: lmms::adjust@...` (CCN 12, lines 230-264).
  I did not touch `adjust`: `git diff HEAD` has one hunk, `@@ -449`, inside `colorMembers`. The same key is already in the
  baseline cache `~/.local/share/zene-bunny-fix/qwen/gate-baselines/06c5dcf48260d3e8fe518759ee4104e8305fae6c.json`. I could
  not build the baseline for this branch's own base, `5c1c6715d9`, because that needs the `zene-cachyos` worktree, so the
  "new" classification is unverified. The function is pre-existing and unchanged by this commit. `colorMembers` is not listed.
- `MixerToolsTest.cpp`: no item in the file-length or complexity output names it.
- The other items in both logs are in files this change does not touch.

## Files committed

- `src/core/ControlCommandsMixerTools.cpp`
- `tests/src/core/MixerToolsTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p21-31.md`

No header changed, so no other target needs a rebuild. No new file, so `tests/fork-sources.txt` and
`tests/upstream-modifications.txt` are unchanged. No control command was added, so `ControlCommandsSnapshot` is unchanged.

## Sibling not changed (follow-up)

`src/core/ControlCommandsVcaShared.h:145-157` (the VCA state reader used by `vca.get_state`) has the same pattern: it calls
`Engine::mixer()->mixerChannel(member)` with no guard and then `channel->vcaGain()` at line 157. I did not change it. It is a
different command in a shared header and needs its own regression test. It is a candidate for a follow-up cluster.

## Self-check

1. **RED then GREEN.** RED with the product change reverted: `test-4-revert-fail.log`, ctest EXIT 8, `aStaleMemberIsSkippedNotRead` FAILS
   (`Actual 1`, `Expected 0`). GREEN with the change re-applied: `test-5-reapply-pass.log`, EXIT 0, 28 passed 0 failed. The
   earlier RED run without the revert (`test-2-red.log`, EXIT 8) shows the same assertion.
2. **Deterministic.** No timing, sleeps or threads. The test reads the refusal counter from the LMMS_TESTING seam
   (`Mixer::takeRefusedLookupsForTesting`, `include/Mixer.h`) and seeds the stale state with
   `VcaGroup::addMember(numChannels())`.
3. **At the cause, siblings checked.** `grep -n "members()"` in `ControlCommandsMixerTools.cpp` returns one loop, the one changed.
   The loops at `Mixer.cpp:1173-1175` and `Mixer.cpp:1345-1348` already guard the index. The loop at `Mixer.cpp:2803`
   (`saveSettings`) only writes the ids and never dereferences them. The unguarded reader at
   `ControlCommandsVcaShared.h:145-157` is the same pattern in a different command and is NOT changed here (follow-up above).
   Every production path that shrinks the channel list keeps members in range (`Mixer.cpp:1361-1373`, `Mixer.cpp:2566`,
   `Mixer.cpp:1275`), as shown in the decision section.
4. **No collateral.** The guard only skips a member `>= numChannels()`, which valid state never produces. The loader
   (`Mixer.cpp:3156-3160`) and the save path (`Mixer::saveSettings`, `Mixer.cpp:2803`) are unchanged, so old projects and saved data are
   unaffected. `colorMembers` runs on the control thread, not the audio thread; the undo step is still one `addUndoStep` call.
   `colouringWhatIsAlreadyColoured` and `aGroupColoursItsMembers` (same test file) still pass (28/28).
5. **Verdict.** FIXED (not ALREADY-FIXED or FALSE-POSITIVE), so no "already fixed" citation is needed. The reachability caveat is
   stated in the decision section.
6. **Sibling commands.** The changed file registers its own commands with `add(registry, ...)`
   (`ControlCommandsMixerTools.cpp:550-578`, including `meter.input` and `chain.bypass_all`/`chain.clean`). The product change is
   inside `colorMembers` only (`ControlCommandsMixerTools.cpp:442-468`). `grep -rn colorMembers src` finds two hits: the definition
   (`:442`) and its single registration (`:572`, `vca.color_members`). So no other command in this file calls it or reads VCA
   members. The other commands the check lists are registered in other files and do not read VCA members either:
   `chain.apply/copy/get_state/list/receive/remove/rename/save`
   (`ControlCommandsChain*.cpp`, `ControlCommandsDeviceCopy.cpp:573`, `ControlReversibilityTableW3Engine.cpp:45`),
   `meter.arm/get_state/inputs/measure_file` (`ControlCommandsMeter.cpp`, `ControlCommandsInputMeters.cpp:42`), and `meter.input`
   (`ControlCommandsMixerTools.cpp:553`, handler `inputLevel` at `:389`, which reads the input meter only). None is affected
   by the guard.
7. **Gates (feature_check.py) - not passed.** The script stopped on `PermissionError` creating a directory outside the write-jail
   (`gates-1-feature-check.log`). Its six simple gates and two ratchet commands were run directly (table above): six exit 0,
   complexity and file-length exit 1 with no item new to this change. The check therefore still shows this gate as FAIL. It needs
   to be run by someone with write access beside the worktree, or with the script's log path changed.
