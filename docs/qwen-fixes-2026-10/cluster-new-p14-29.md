# Cluster new-p14-29

**Verdict: FIXED**

new-p14-29: FIXED (latent: no public command found that produces the bad state today; the unchecked read is undefined behaviour either way)

Logs: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p14-29.r1/logs`

## new-p14-29 - Track ordering can index past the mixer channel list

**Verdict: FIXED**

### Evidence

- [ControlCommandsMixerTools.cpp:292-293](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p14-29/src/core/ControlCommandsMixerTools.cpp:292)
  `orderByTracks()` looked up a track's channel with `model->value() > 0` as its only check, then called
  `mixer()->mixerChannel(model->value())`. That accessor is `return m_mixerChannels[_ch];` with no check
  ([Mixer.h:621-623](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p14-29/include/Mixer.h:621)),
  so an index at or past `numChannels()` is an out-of-range read. The fix adds the upper bound
  `model->value() < mixer()->numChannels()`; a track whose index names no channel is then handled exactly as a track
  with a null model, which is the contract the existing `channel != nullptr` branch already defines.
- Every sibling lookup by track value bounds-checks against the live count, and `orderByTracks` was the only one that
  did not: [LevelBar.cpp:108](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p14-29/src/gui/LevelBar.cpp:108),
  [ControlCommandsLowLatency.cpp:84](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p14-29/src/core/ControlCommandsLowLatency.cpp:84),
  [ControlCommandsTrackLooks.cpp:88](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p14-29/src/core/ControlCommandsTrackLooks.cpp:88),
  [DawProjectSession.cpp:553](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p14-29/src/core/DawProjectSession.cpp:553),
  [TrackFolder.cpp:291](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p14-29/src/tracks/TrackFolder.cpp:291).
- Why an index past the end can exist: the track models' range is set from `numChannels()-1` only at construction and
  load ([InstrumentTrack.cpp:85](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p14-29/src/tracks/InstrumentTrack.cpp:85),
  [:1195](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p14-29/src/tracks/InstrumentTrack.cpp:1195),
  [SampleTrack.cpp:65](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p14-29/src/tracks/SampleTrack.cpp:65),
  [:356](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p14-29/src/tracks/SampleTrack.cpp:356)).
  Only the GUI narrows or widens them again, in
  [MixerView.cpp:419-443](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p14-29/src/gui/MixerView.cpp:419).
  [TrackFolder.cpp:295-304](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p14-29/src/tracks/TrackFolder.cpp:295)
  says so in its own comment: `Mixer::createChannel()` does not re-range anybody. A headless session therefore keeps a
  range wider than the live channel count, and `setValue()` accepts an index one past the end.
- Why it is latent and not a user-facing path today: [Mixer.cpp:1371-1401](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p14-29/src/core/Mixer.cpp:1371)
  (inside `Mixer::deleteChannel`, [Mixer.cpp:1336](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p14-29/src/core/Mixer.cpp:1336))
  rewrites each track's index (to 0 when it equals the deleted index, down by one when above), and the commands that
  set the value validate the channel first (`resolveMixerChannel`, `ControlCommandsTrackChannel.cpp:56`). I found no public
  command that leaves a value at or past the end, so the defect is an unchecked read whose invariant is held by
  convention, not by the model. The bound is the cause-level guard; it changes nothing that a valid project does.

### Fix

[ControlCommandsMixerTools.cpp:292-293](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p14-29/src/core/ControlCommandsMixerTools.cpp:292):
the lookup is now `model->value() > 0 && model->value() < mixer()->numChannels()`, computed into `onAChannel`. Nothing
else in the file changes. Index 0 (the master) is still left out, as before; valid indices behave as before; an index
past the end is skipped instead of read.

### Regression test

[MixerToolsTest.cpp:279-294](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p14-29/tests/src/core/MixerToolsTest.cpp:279)
`aTrackOnAMissingChannelIsLeftOutOfTheOrder()`. The test seam is the stale range a headless session keeps: the track's
model range is widened with `setRange`, then `Engine::mixer()->deleteChannel(1)` leaves the model's index one past the
new end. Without the fix the unguarded lookup reaches the channel whose pointer is still in the slot just past the new
end, and `order_by_tracks` reports `moved` = 1. With the fix the track is skipped, `moved` = false, and the track keeps
its index. No wall-clock time, sleeps, threads or races are involved. The test keeps at least five channels for the
rest of the class (see Notes).

### Verification record

Commands run from `build/tests` unless stated. `$TMPDIR` is `/home/kruzzzzy/.cache/zene-tmp`; every build sets
`CCACHE_TEMPDIR=$TMPDIR/ccache-tmp-new-p14-29` (see Notes). `<LOGS>` is the log folder above.

| Step | Exact command | EXIT | Log / result |
|---|---|---|---|
| RED: product fix reverted | `git apply -R <LOGS>/fix-r2.patch` | 0 | (diff of `src/core/ControlCommandsMixerTools.cpp` only) |
| RED: build | `CCACHE_TEMPDIR=... cmake --build build --target zene MixerToolsTest -j4` | 0 | `build-18-red.log` |
| RED: run | `ctest -R '^MixerToolsTest$' --output-on-failure` | **8** | `test-18-red.log`: `FAIL! : MixerToolsTest::aTrackOnAMissingChannelIsLeftOutOfTheOrder()`, `Actual ... moved: 1`, `Expected (false): 0`, `Loc: MixerToolsTest.cpp(292)` |
| GREEN: fix reapplied | `git apply <LOGS>/fix-r2.patch` | 0 | |
| GREEN: build | same build command | 0 | `build-19-green.log` |
| GREEN: run | `ctest -R '^MixerToolsTest$' --output-on-failure` | **0** | `test-19-green.log`: `100% tests passed, 0 failed out of 1` |
| Whole binary, direct | `./MixerToolsTest` | 0 | `direct-21-totals.log`: `Totals: 28 passed, 0 failed, 0 skipped` (includes the new slot) |
| Family: relink | `cmake --build build --target MixerAbRegressionTest MixerConcurrencyTest MixerRoutingBackwardCompatTest PdcMixerTest MixerChannelPanTest MixerListenTest MixerStripTest MixerToolsTest MixerSendsOnFadersTest MixerModesTest MixerMeteringTest MixerSelectionIdentityTest MixerWindowsTest MixerLatencyTextTest -j4` | 0 | `build-16-family.log`; targets in `family-r2-targets.txt` |
| Family: run | `ctest -R "Mixer\|PdcMixerTest" --output-on-failure` | **0** | `test-20-family.log`: `100% tests passed out of 14` |
| Gate (copy, see Notes) | `python3 <scratch>/feature_check_r2.py <worktree>` | **0** | `gate-3-feature-check.log`: 8 of 8 rows PASS, `RESULT: PASS` |
| Gate (original script) | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | **1** | `gate-4-original-script.log`: `PermissionError` creating `zene-qwen-new-p14-29-feature-check` (outside the write-jail) |

Affected family: `ctest -R "Mixer|PdcMixerTest"` runs 14 tests; all 14 pass. The new test is one of the 28 slots of
`MixerToolsTest`.

The 4b reverse-patch file is `fix-r2.patch` (the product `git diff` of `src/core/ControlCommandsMixerTools.cpp`), not
`fix.patch`: a `fix.patch` from an earlier attempt on this cluster was already in the log folder and was not overwritten.

### Notes

- Earlier attempts in this run (kept as logs, not evidence for the verdict): `test-10-red.log` (EXIT 0): the first
  version of the test used an index far past the vector's capacity; the test passed without the fix (I did not inspect
  what the unguarded read returned), so it was discarded as non-discriminating. `test-11-red.log` (EXIT 8): the second version, which deletes a channel,
  fails without the fix. `test-12-green.log` (EXIT 8): the test had reduced the shared channel count to four, and the
  later `sendMetersAndEqCompare` failed on `channel(4)`. The test now creates channels until six exist before the
  deletion, so five remain (`test-13-green.log`, EXIT 0).
- `build-1-red.log` (EXIT 2) is my first build attempt: ccache could not create its temp file under the write-jail
  path `zene-bunny-fix/dsh/home/ccache-tmp-r6`, so I set `CCACHE_TEMPDIR` to a writable path under `$TMPDIR`. The same
  redirect was used for every later build. This first attempt's redirect also overwrote a `build-1-red.log` that an
  earlier attempt had written in the log folder.
- The gate's own script writes its logs to `<worktree>/../<name>-feature-check/`, outside the write-jail, so it cannot
  run here. I ran a scratch copy in `$TMPDIR/gate-r2-script/feature_check_r2.py` that differs only in two lines: `ROOT`
  points at the real `qwen/` directory (for `FIX-BASE` and the cached `gate-baselines/cf3618e61....json`, which exists),
  and the log directory is `<LOGS>/feature-check-r2`. Every check, limit and baseline is the real one.

## Self-check

Result of `submit_check.py` before the commit (`submit-check-1.log`, EXIT 1): commit, clean tree, report, and
self-check items were pending by design; the `gates (feature_check.py)` row FAILs because the real script cannot create
its sibling log directory from inside the write-jail (`gate-4-original-script.log`, EXIT 1). The redirected copy is the
gate evidence (`gate-3-feature-check.log`, EXIT 0).

1. **RED and GREEN.** The test fails with the product change reverted (`test-18-red.log`, ctest EXIT 8, `moved` 1
   against expected 0) and passes with it (`test-19-green.log`, EXIT 0). The exit codes and commands are in the table above.
2. **Deterministic.** No timing, sleep, thread or race. The state is built with `setRange` and `Engine::mixer()->deleteChannel(1)`.
   Caveat: the discriminating observation depends on `std::vector::erase` leaving the old last pointer in the slot just past
   the new end (`Mixer.cpp:1439`). That slot is the out-of-size read the fix removes. The result is stable with this
   standard library, but a different one could change which pointer that slot holds.
3. **Cause and siblings.** `grep` for `mixerChannel(` with a track value found one unguarded caller, `orderByTracks`; the
   other callers that index by a value check the bound first (see Evidence). `arrange()` and `channelWithId()` look channels
   up by id, so they do not index by value. The fix changes only the bound in `orderByTracks`.
4. **Collateral.** Projects and saved data are untouched: `loadSettings` paths are not changed. The new bound is
   `numChannels()`, the first index that does not exist, so it refuses no valid index. Index 0 is still excluded, as before.
   The undo step is still added only when `after != before`.
5. **Sibling commands in the changed file.** `ControlCommandsMixerTools.cpp` also registers the `chain.*` and `meter.*`
   commands (`ControlCommandsMixerTools.cpp:548-576`). None of the ones the check lists (chain.apply, chain.bypass_all,
   chain.clean, chain.copy, chain.get_state, chain.list, chain.receive, chain.remove, chain.rename, chain.save,
   meter.arm, meter.get_state, meter.input, meter.measure_file, meter.set_reference) reads a track's mixer-channel value,
   so none shares the bad lookup and none needs a change. In this file the only lookup of a track's channel value is
   `orderByTracks` (lines 291-293, fixed). The other `mixerChannelModel()` use, `whatUsesAChannel` (line 430), compares
   the value with a channel's index and never indexes with it. The other `mixerChannel(` calls in the file take their index
   from a loop bound or from a channel the command has just resolved or created.
6. **Gates row.** `gates (feature_check.py)` stays red in `submit_check` for the environment reason given at the top of this
   section; the redirected copy passes all eight rows (`gate-3-feature-check.log`, EXIT 0).
7. **Verdict and contract.** FIXED. The contract is the one `orderByTracks` already states: a track on no channel is not
   followed (the `channel != nullptr` branch, and the master at index 0). No new control command was added, the
   `ControlCommandsSnapshot` canary is untouched, and no audio-thread code is involved.
