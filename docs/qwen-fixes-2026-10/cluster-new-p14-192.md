FIXED - cluster new-p14-192: deleting the soloed channel left the other channels muted

Log folder: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p14-192.r1/logs`

## 1. Verdict: REAL, fixed

`Mixer::deleteChannel` (`src/core/Mixer.cpp:1175`) handled the solo bookkeeping like this before the fix:

```cpp
if (m_lastSoloed == index) { m_lastSoloed = -1; }
```

`activateSolo()` (`src/core/Mixer.cpp:934`) had saved each channel's pre-solo mute in
`m_muteBeforeSolo` and muted every other channel. Before the fix, `deactivateSolo()` (`:944`) was called only from
`toggledSolo()` (`:976`, `:1000`), `applyGroupSolo()` (`:1136`) and `applyAdditiveSolo()`
(`src/core/MixerStrip.cpp:423`, reached when `m_lastSoloed != -1`). Each restores the saved mutes. The
delete path reset `m_lastSoloed` without restoring them, so every channel that was audible before the
solo stayed muted with no solo left to restore it. The finding's description is accurate.

`MixerChannel::~MixerChannel()` (`src/core/Mixer.cpp:274`) only frees the buffer. It touches no solo
state, so nothing else restored the mutes.

The additive path (`applyAdditiveSolo`, `src/core/MixerStrip.cpp:411`) has the same blind spot. A
naive fix there would mute a channel that is still soloed, so the fix covers both modes (see section 2).

## 2. The fix

`src/core/Mixer.cpp`:

- `:1264-1273`: when `m_lastSoloed == index`, the solo goes with the channel. Call `deactivateSolo()`
  before the erase to put back each channel's pre-solo mute, then reset `m_lastSoloed`. The else-branch
  that shifts a later index down is unchanged.
- `:1312`: after the renumbering loop and before `doneChangeInModel()`, when the removed channel was
  the solo and the solo mode is additive, call `applyAdditiveSolo()`. The surviving soloed channels
  stay heard on the new layout. It runs after the renumbering because `applyAdditiveSolo()` reads
  `soloed.front()->index()`. I first put it before the loop, saw the stale index in the code, and
  moved it.

Callers checked:
- `clear()` (`src/core/Mixer.cpp:~2374`) calls `deleteChannel(1)` in a loop. `deactivateSolo()` there
  restores channels that are deleted in the same loop, so the result is the same.
- `TrackFolder.cpp:142,397`, `ControlCommandsBus.cpp:110,168`, `ScriptDawEdit.cpp:176`,
  `ControlCommandsDawProject.cpp:247`, `ControlCommandsDrumRack.cpp:133` all go through the same
  function, so the fix covers them.
- `applyAdditiveSolo()` (`src/core/MixerStrip.cpp:411`) already calls `deactivateSolo()` at `:423`
  when `m_lastSoloed != -1`, then re-derives the solo from the solo flags. The new `deleteChannel` path
  (`:1271`, then `:1312`) uses the same pair of steps.
- `m_soloingGroup` (VCA group solo) is not touched. A channel solo cannot run while a group solo owns
  the mutes, because `toggledSolo()` clears `m_soloingGroup` and `applyGroupSolo()` sets
  `m_lastSoloed = -1`.

## 3. Regression tests

`tests/src/core/MixerStripTest.cpp` (registered already in `tests/fork-sources.txt:2389`):

- `deletingTheSoloedChannelRestoresTheMutes()` (`:323`): channel 1 muted and channel 2 audible before
  the solo. Solo a new channel, then delete it. Channel 1 must stay muted and channel 2 must be
  audible. This is the RED test for the finding.
- `deletingAnAdditiveSoloKeepsTheOthers()` (`:341`): additive mode with two new soloed channels. Delete
  the lower one, which the engine names as its last solo. The survivor must stay soloed and audible,
  and channels 1 and 2 must stay muted. Once the survivor's solo is cleared, channel 1 must be unmuted
  again (its pre-solo state) and channel 2 must still be muted (its pre-solo state). Those last checks
  are what make this test fail when the product change is reversed (round 2, section 6). Without the
  fix, the state while the survivor is soloed passes, and the restoration checks fail.

No timing, no sleeps, no races. Both tests run on the main thread through the existing
`Engine::mixer()` fixture.

`tests/upstream-modifications.txt`: one line added for `src/core/Mixer.cpp`, naming the cluster and
the held tests.

## 4. Evidence

Build note: the first build failed with `ccache: failed to create temporary file for
/run/user/1000/ccache-tmp/...: Permission denied`. Every later build used
`CCACHE_TEMPDIR=/tmp/ccache-tmp-p14`. The failed build log is kept as `build-1-red.log`.

| Step | Command | EXIT | Result |
|---|---|---|---|
| RED build | `CCACHE_TEMPDIR=/tmp/ccache-tmp-p14 cmake --build build --target MixerStripTest -j4` | 0 | `build-2-red.log` |
| RED test | `cd build/tests && ctest -R '^MixerStripTest$' -V` | 8 | `test-4-unfixed-red.log`: `FAIL! deletingTheSoloedChannelRestoresTheMutes() '!channel(2)->m_muteModel.value()' returned FALSE`; 18 passed, 1 failed |
| GREEN build | `cmake --build build --target zene MixerStripTest -j4` | 0 | `build-3-green.log` |
| GREEN test | `cd build/tests && ctest -R '^MixerStripTest$' -V` | 0 | `test-3-green-verbose.log`: `PASS deletingTheSoloedChannelRestoresTheMutes`, `PASS deletingAnAdditiveSoloKeepsTheOthers`; Totals 19 passed, 0 failed |
| 4b revert | `git diff -- src/core/Mixer.cpp > logs/fix.patch && git apply -R logs/fix.patch` | 0 | (fix.patch saved) |
| 4b RED build | `cmake --build build --target zene MixerStripTest -j4` | 0 | `build-4-unfixed.log` |
| 4b RED test | `ctest -R '^MixerStripTest$' -V` | 8 | `test-4-unfixed-red.log`: same assertion fails |
| 4b reapply | `git apply logs/fix.patch` | 0 | |
| 4b GREEN build | `cmake --build build --target zene MixerStripTest -j4` | 0 | `build-5-refixed.log` |
| 4b GREEN test | `ctest -R '^MixerStripTest$' -V` | 0 | `test-5-refixed-green.log`: 19 passed, 0 failed |

Mixer-family build: `cmake --build build --target MixerAbRegressionTest MixerChannelPanTest
MixerListenTest MixerRoutingBackwardCompatTest MixingRoutingTest VcaGroupTest MixerConcurrencyTest
MixerToolsTest TrackFolderTest PdcMixerTest ScriptDawRemovalUndoTest DeviceMovesUndoTest -j4`: EXIT 0
(`build-6-family.log`, 122 targets built, no `Error`).

Family test run: `cd build/tests && ctest -R '^(MixerAbRegressionTest|MixerChannelPanTest|MixerListenTest|MixerStripTest|MixerRoutingBackwardCompatTest|MixingRoutingTest|VcaGroupTest|MixerConcurrencyTest|MixerToolsTest|TrackFolderTest|PdcMixerTest|ScriptDawRemovalUndoTest|DeviceMovesUndoTest)$' -V`: EXIT 8 (`test-6-family.log`).

| Test | Result |
|---|---|
| MixerAbRegressionTest | Passed, 3 cases |
| MixerChannelPanTest | Passed, 8 cases |
| MixerListenTest | Passed, 6 cases |
| MixerStripTest | Passed, 19 cases (both new tests pass) |
| MixerRoutingBackwardCompatTest | Passed, 16 cases |
| MixingRoutingTest | Passed, 14 cases |
| VcaGroupTest | Passed, 18 cases |
| MixerConcurrencyTest | Passed, 17 cases |
| MixerToolsTest | Passed, 26 cases |
| TrackFolderTest | Passed, 29 cases |
| PdcMixerTest | Passed, 13 cases |
| DeviceMovesUndoTest | **Failed**, 2 passed, 4 failed (pre-existing, see below) |
| ScriptDawRemovalUndoTest | **Failed**, 2 passed, 11 failed (pre-existing, see below) |

Affected-family total: 13 tests, 171 cases passed, 15 failed, 186 executed.

Pre-existing failures: `DeviceMovesUndoTest` and `ScriptDawRemovalUndoTest` fail with the same counts
on the original code. I reversed `fix.patch`, rebuilt `zene DeviceMovesUndoTest ScriptDawRemovalUndoTest`
(`build-7-base-check.log`, EXIT 0), and ran them (`test-7-base-check.log`, EXIT 8, "Totals: 2 passed,
4 failed" and "Totals: 2 passed, 11 failed"). I then reapplied the fix and rebuilt (`build-8-refixed.log`,
EXIT 0). The cause is in this build's environment: the plugin scan finds 0 plugins, the log says
`the LADSPA host plugin 'ladspaeffect' is not in this build`, and the amplifier fixture is missing
(`DeviceMovesUndoTest.cpp:119`). Neither test touches solo or mute code.

Gates (all run on the fixed tree; `feature_check.py` was not run, see section 5):

| Gate | Command | EXIT |
|---|---|---|
| fork-sources | `bash tests/fork-sources-gate.sh` | 0 |
| all-sources-reproduce | `bash tests/all-sources-reproduce.sh` | 0 |
| no-upstream-regression | `bash tests/no-upstream-regression-gate.sh` | 0 |
| check-namespace | `tests/scripted/check-namespace` | 0 |
| no-tautology | `bash tests/no-tautology-gate.sh` | 0 |
| doc-figures | `python3 tests/doc-figures-gate.py` | 0 |
| complexity (ratchet) | `bash tests/complexity-gate.sh --check` | 1 |
| file-length (ratchet) | `bash tests/file-length-gate.sh --check` | 1 |

The two ratchets exit 1 on the train, as they are meant to. Their REGRESSION lines are in
`gate-complexity-3.log` (5 lines) and `gate-file-length-3.log` (15 lines). None names
`src/core/Mixer.cpp`, `tests/src/core/MixerStripTest.cpp`, or `tests/upstream-modifications.txt`, the
files this change touches. Only the complexity lines for `ControlCommandsMixerTools.cpp` and
`ControlCommandsMixerStrip.cpp` name a file, and this change does not touch either. By the gate's own
rule (items NEW in files the branch changed), both ratchets pass.

## 5. Deviations

- **`feature_check.py` and `submit_check.py` were not run.** Both write outside the worktree: the
  checker writes its per-gate logs to `<worktree>/../<name>-feature-check/`, and it runs
  `git worktree add` inside `zene-cachyos` for the base listings. Both fall outside the write-jail. I
  ran each gate command directly with `cwd` set to the worktree and logged to the LOG FOLDER. The
  table in section 4 reproduces the checker's gate set, but not its baseline comparison for the
  ratchets. I did the comparison by hand (above).
- A foreground `for` loop over the gate commands passed the command as one unsplit word, which gave
  exit 127. I reran those gates with literal commands. The logs for the successful runs are the
  `gate-*-3.log` files.

## Self-check

Replicated by hand, because `submit_check.py` could not be run within the write-jail:

- **Verdict on line 1 of this file:** FIXED.
- **Regression test fails before the fix and passes after:** yes (section 4: RED exit 8, GREEN exit 0,
  4b revert and reapply).
- **Changed product files:** `src/core/Mixer.cpp` only. Its change is registered in
  `tests/upstream-modifications.txt`. `MixerStripTest.cpp` is already in `tests/fork-sources.txt`.
  No new file was added.
- **Realtime / audio thread:** the change runs only inside `deleteChannel`, which holds
  `requestChangeInModel()` for the whole call. It adds no allocation or lock to the audio path.
- **Undo:** unchanged. `deleteChannel` is still one step inside its command.
- **Commands:** none added. The ControlCommandsSnapshot canary is untouched.
- **Gates:** section 4. Every gate passes except the two ratchets, and they fail only on items outside
  the changed files.
- **Git status:** clean after the commit listed below. The commit adds these explicit paths only:
  `src/core/Mixer.cpp`, `tests/src/core/MixerStripTest.cpp`, `tests/upstream-modifications.txt`,
  `docs/qwen-fixes-2026-10/cluster-new-p14-192.md`.
- **Never pushed.**

## 6. Round 2: review rework (test-proves-nothing)

Luna's round-1 review (`qa/reviews/new-p14-192.r1.md`) sent the work back once. The blocking point:
`deletingAnAdditiveSoloKeepsTheOthers` checked only the state while the survivor was still soloed,
and that state passed with the product change reversed. It never checked the restoration after the
survivor's solo was cleared.

Change: `tests/src/core/MixerStripTest.cpp:358-362`. After `channel(first)->m_soloModel.setValue(false)`,
the test now asserts `!channel(1)->m_muteModel.value()` (channel 1 back to its pre-solo unmuted state)
and `channel(2)->m_muteModel.value()` (channel 2 keeps its pre-solo mute). No product code changed in
this round. The product fix is still commit `248fcac76`.

Round-2 evidence (log folder `.../new-p14-192.r2/logs`; the reversal used only the `src/core/Mixer.cpp`
hunks, saved as `fix-r2.patch` with `git diff b61e5f965 -- src/core/Mixer.cpp`):

| Step | Command | EXIT | Result |
|---|---|---|---|
| GREEN build | `CCACHE_TEMPDIR=/tmp/ccache-tmp-p14 cmake --build build --target zene MixerStripTest -j4` | 0 | `build-r2-1-green.log` |
| GREEN test | `cd build/tests && ctest -R '^MixerStripTest$' -V` | 0 | `test-r2-1-green.log`: 19 passed, 0 failed |
| Reverse | `git apply -R fix-r2.patch` | 0 | only `Mixer.cpp` changed back |
| RED build | same build command | 0 | `build-r2-2-reversed.log` |
| RED test | same ctest command | 8 | `test-r2-2-reversed.log`: `FAIL! deletingTheSoloedChannelRestoresTheMutes() '!channel(2)->m_muteModel.value()'`; `FAIL! deletingAnAdditiveSoloKeepsTheOthers() '!channel(1)->m_muteModel.value()' returned FALSE (clearing the last additive solo must restore channel 1's pre-solo unmuted state)`; 17 passed, 2 failed |
| Reapply | `git apply fix-r2.patch` | 0 | |
| Refixed build | same build command | 0 | `build-r2-3-refixed.log` |
| Refixed test | same ctest command | 0 | `test-r2-3-refixed-green.log`: 19 passed, 0 failed |

Mixer family, rerun on the final tree: `ctest -R` over the same 13 targets as section 4 gave EXIT 8,
with the same results: 11 passed, and `DeviceMovesUndoTest` and `ScriptDawRemovalUndoTest` failed as
in section 4 (`test-r2-4-family.log`). Those two are the pre-existing environment failures.

Gates on the final tree (`gate-r2-*.log`): fork-sources 0, all-sources-reproduce 0,
no-upstream-regression 0, check-namespace 0, no-tautology 0, doc-figures 0, complexity 1, file-length 1.
Neither ratchet lists a REGRESSION line for `src/core/Mixer.cpp`, `tests/src/core/MixerStripTest.cpp`,
or `tests/upstream-modifications.txt`.

Non-blocking note fixed: the report's `deactivateSolo()` caller list (section 1 and section 2) now
includes `applyAdditiveSolo()` (`src/core/MixerStrip.cpp:423`).
