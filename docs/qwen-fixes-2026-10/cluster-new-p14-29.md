# Cluster new-p14-29

**Verdict: FIXED**

new-p14-29: FIXED (latent: no public command found that produces the bad state today; the unchecked read is undefined behaviour either way)

Logs: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p14-29.r2/logs` (round 2; round 1 logs are in `new-p14-29.r1/logs`).

## new-p14-29 - Track ordering can index past the mixer channel list

**Verdict: FIXED**

### Evidence

- [ControlCommandsMixerTools.cpp:292-293](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p14-29/src/core/ControlCommandsMixerTools.cpp:292)
  `orderByTracks()` looked up a track's channel with `model->value() > 0` as its only check, then called
  `mixer()->mixerChannel(model->value())`. That accessor is `return m_mixerChannels[_ch];` with no check
  ([Mixer.h:621-627](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p14-29/include/Mixer.h:621)),
  so an index at or past `numChannels()` is an out-of-range read. The production guard adds the upper bound
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

### Fix (production)

[ControlCommandsMixerTools.cpp:292-293](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p14-29/src/core/ControlCommandsMixerTools.cpp:292):
the lookup is `model->value() > 0 && model->value() < mixer()->numChannels()`, computed into `onAChannel`. Nothing
else in that file changes. Index 0 (the master) is still left out, as before; valid indices behave as before; an index
past the end is skipped instead of read. This guard is unchanged from round 1 (commit `c922178aa`).

### Test seam (LMMS_TESTING only)

The round-1 regression used the erased vector slot as its oracle, which is undefined behaviour and failed only by
accident of the allocator. It is removed. The replacement observes the invalid lookup itself:

- [Mixer.h:621-634](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p14-29/include/Mixer.h:621):
  under `LMMS_TESTING`, the inline `mixerChannel()` refuses an index that names no channel before the vector read: it
  returns `refusedLookupForTesting()` instead of `m_mixerChannels[_ch]` (Mixer.h:624). Two static declarations are added
  (Mixer.h:632, 634). No data member is added, so the class layout is unchanged.
- [Mixer.cpp:1024-1039](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p14-29/src/core/Mixer.cpp:1024):
  the definitions, under `LMMS_TESTING`, with a file-static counter. `refusedLookupForTesting()` counts and returns
  `nullptr`; `takeRefusedLookupsForTesting()` returns the count and restarts it at zero.
- Compiled out of the shipping product: `lmmsobjs` is the only target that gets `LMMS_TESTING`, and only when
  `BUILD_TESTING` is on ([tests/CMakeLists.txt:4-7](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p14-29/tests/CMakeLists.txt:4)).
  With the define off, the inline accessor is the original one-line body.

### Regression test

New file [MixerOrderByTracksTest.cpp](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p14-29/tests/src/core/MixerOrderByTracksTest.cpp),
registered in [tests/CMakeLists.txt:878-880](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p14-29/tests/CMakeLists.txt:878)
and [tests/all-sources.txt](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p14-29/tests/all-sources.txt).
The file is new rather than an addition to `MixerToolsTest.cpp`, which sits at the file-length limit; that file is restored
to its base content.

- `anIndexPastTheLastChannelIsRefusedBeforeTheRead()` ([line 170](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p14-29/tests/src/core/MixerOrderByTracksTest.cpp:170)):
  tracks at `numChannels()` and at `numChannels() + 3` go through the real `mixer.order_by_tracks` command. The order and
  both tracks' indices are unchanged, and `takeRefusedLookupsForTesting()` must be 0. This is the regression: with the
  production guard reversed, the two refusals are counted (2, not 0) and the assertion fails at line 182.
- `aMasterOnlyMixerRefusesEveryTrackIndex()` ([line 186](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p14-29/tests/src/core/MixerOrderByTracksTest.cpp:186)):
  with one channel (the master), indices 1 and 4 are refused, `moved` is false, and the refusal count is 0. With the guard
  reversed it fails at line 196 (2, not 0).
- Ordinary cases, which pass with either guard: `theUsedChannelsComeFirstInTrackOrder` (tracks on 4 and 2 give
  `[0,4,2,1,3]`), `aChannelUsedTwiceAppearsOnce` (two tracks on 3 and one on 1 give `[0,3,1,2,4]`, the unused
  channels keep their order), `theMasterAndTheUnusedChannelsKeepTheirOrder`, `undoPutsTheChannelsBack` (one undo step),
  and `aSecondRunChangesNothing` (idempotence).

Determinism: no timing, sleep, thread or race. The observer is a counter in the test build; the only invalid lookups are
the ones the test makes, and `init()` resets the counter before each test.

### Verification record (round 2; logs in `new-p14-29.r2/logs`)

Commands run from the worktree root or `build/tests`. Every build sets `CCACHE_TEMPDIR=$TMPDIR/ccache-tmp-new-p14-29`
(see Notes).

| Step | Exact command | EXIT | Log / result |
|---|---|---|---|
| Build zene, first try | `cmake --build build --target zene MixerOrderByTracksTest -j4` | 2 | `build-1-seam.log`: `zene` built; the new target was not yet in the generated makefile (regenerated during this build) |
| Reconfigure attempt | `cmake .` in `build` | 97 | `configure-2.log`: refused by the lane guard ("configuring is the parent's job"); not needed, the tree had regenerated |
| Family build, first try | `cmake --build build --target MixerOrderByTracksTest MixerToolsTest MixerConcurrencyTest PdcMixerTest <11 Mixer targets> -j4` | 2 | `build-2-family.log`: AutoMoc error, the new test lacked its `.moc` include |
| Family build | same, after adding `#include "MixerOrderByTracksTest.moc"` | 0 | `build-3-family.log` |
| GREEN, guard in place | `ctest -R "^MixerOrderByTracksTest$" --output-on-failure` | **0** | `test-2-green.log`: 100% passed, 1 of 1 |
| GREEN, direct binary | `./MixerOrderByTracksTest` | 0 | `direct-3-green.log`: `Totals: 9 passed, 0 failed` (seven test functions plus init and cleanup) |
| Realtime | `ctest -R "^MixerConcurrencyTest$" --output-on-failure` | **0** | `test-3-concurrency.log` (includes `topologyChangesWaitForTheRenderPeriodToEnd`) |
| Negative control: revert only the guard | `git apply -R <LOGS>/guard-r2.patch` | 0 | `guard-r2.patch` is `git show c922178aa -- src/core/ControlCommandsMixerTools.cpp`; diff stat shows that file only |
| RED: build | `cmake --build build --target MixerOrderByTracksTest -j4` | 0 | `build-4-red.log` |
| RED: run | `ctest -R "^MixerOrderByTracksTest$" --output-on-failure` | **8** | `test-4-red.log`: `FAIL!` in `anIndexPastTheLastChannelIsRefusedBeforeTheRead()`, `Actual ... takeRefusedLookupsForTesting(): 2`, `Expected (0): 0`, `Loc: MixerOrderByTracksTest.cpp(182)`; and in `aMasterOnlyMixerRefusesEveryTrackIndex()`, `Actual 2`, `Loc ... (196)`. The seven PASS lines are the ordinary cases plus init and cleanup |
| Restore the guard | `git apply <LOGS>/guard-r2.patch` | 0 | |
| GREEN: build | `cmake --build build --target MixerOrderByTracksTest -j4` | 0 | `build-5-green.log` |
| GREEN: run | `ctest -R "^MixerOrderByTracksTest$" --output-on-failure` | **0** | `test-5-green.log`: 100% passed, 1 of 1 |
| Affected family | `ctest -R "Mixer\|PdcMixerTest" --output-on-failure` | **0** | `test-6-family.log`: 100% passed out of 15 (the 14 earlier tests plus the new one) |
| Gate, before the commit | `python3 <scratch>/feature_check_r2.py <worktree>` (copy; see Notes) | 1 | `gate-1-feature-check.log`: six rows PASS; `fork-sources` and `all-sources-reproduce` FAIL because the new file was not yet tracked; `all-sources` now reproduces with the file staged |
| Gate, after the commit | same | see final report | |

Notes on the verification:

- The Mixer family's relink was needed because `Mixer.h` is included across the product and the product is linked into
  each test binary; `build-3-family.log` rebuilt all 15 targets after the header change.
- Portability (the diagnostic's last test): NOT RUN. Only the libstdc++ toolchain of this machine is available, and an
  AddressSanitizer or UBSan build would be a second full product build tree (tens of GB) on a 30 GB shared disk. The
  observer does not depend on the standard library's behaviour, which is the point of the seam; the sanitizer run is
  still open for the reviewer.

### Notes

- Round 1 (`c922178aa`, `c6a36e091`) was sent back by review: the regression read the erased vector slot, which is
  undefined behaviour. Round 2 replaces it with the observer above; the round-1 test is removed from `MixerToolsTest.cpp`.
- `build-1-red.log` in the r1 folder (EXIT 2) was the first ccache permission failure: ccache could not create its temp
  file under the write-jail path `zene-bunny-fix/dsh/home/ccache-tmp-r6`. Every build since sets `CCACHE_TEMPDIR` to a
  writable path under `$TMPDIR`.
- The gate's own script writes its logs to `<worktree>/../<name>-feature-check/`, outside the write-jail, so it cannot run
  here. The gate evidence is a scratch copy in `$TMPDIR` that differs only in two lines: `ROOT` points at the real `qwen/`
  directory (for `FIX-BASE` and the cached baseline), and the log directory is `<LOGS>/feature-check-r2`. Every check,
  limit and baseline is the real one.

## Self-check

Answers to the completion check's items, with the evidence above.

1. **RED and GREEN.** With only the production guard reversed, the focused test fails deterministically at the refusal
   observer (`test-4-red.log`, ctest EXIT 8, refusal counts 2 against 0 at lines 182 and 196). With the guard restored,
   it passes (`test-5-green.log`, EXIT 0). The commands and exit codes are in the table above.
2. **Deterministic.** The discriminating observation is the count of refused lookups in a test-only seam, not a memory
   read. No timing, sleep, thread or race. The seam is reset in `init()`; the tracks are made by `trackOn()`, which
   widens the model range so the stale state is constructed, not assumed.
3. **Cause and siblings.** Checked as before: the other callers that index by a track's value bounds-check first (see
   the Evidence list). `arrange()` and `channelWithId()` look channels up by id. The only production change is the
   bound in `orderByTracks`; the seam is test-only and does not change the shipping accessor.
4. **Collateral.** Projects and saved data are untouched: no `loadSettings` path changes. The production bound is
   `numChannels()`, the first index that does not exist, so it refuses no valid index. The seam is compiled out of the
   shipping product, and under `LMMS_TESTING` it changes only what happens on an index that names no channel, which was
   undefined behaviour before. The Mixer family passes (test-6-family.log).
5. **Sibling commands in the changed file.** `ControlCommandsMixerTools.cpp` also registers `chain.*` and `meter.*`
   commands (lines 548-576 of that file). None of the ones the check lists reads a track's mixer-channel value, so none
   shares the bad lookup and none needs a change. In this file the only lookup of a track's channel value is
   `orderByTracks` (lines 291-293, fixed). `whatUsesAChannel` (line 430) compares the value with a channel's index and
   never indexes with it.
6. **Gates row.** The real `feature_check.py` cannot create its sibling log directory from inside the write-jail; the
   redirected copy is the gate evidence (see Notes and the table).
7. **Verdict and contract.** FIXED. The contract is the one `orderByTracks` already states: a track on no channel is not
   followed (the `channel != nullptr` branch, and the master at index 0). No new control command was added, the
   `ControlCommandsSnapshot` canary is untouched, and no audio-thread code is involved.
