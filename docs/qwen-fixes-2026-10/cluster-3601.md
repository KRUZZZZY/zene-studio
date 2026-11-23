ALREADY-FIXED

# Cluster 3601 — a missing frozen-audio file cannot leave a region silent

**Verdict: ALREADY-FIXED on branch `bugs/qwen-3601`.** The two behaviours the finding asks for — surface the
load failure, and restore the source clips the freeze muted — are present in `src/core/Track.cpp`, and an
existing regression test fails when those product hunks are reversed and passes with them. The fix landed
upstream in `b622b3fac4` (with the null-buffer guard in `a91d7317ce`), both ancestors of this HEAD; no
product change is needed in this job.

## Evidence

- **The failure is surfaced at load.** `Track::loadFrozenTake()` resets the buffer and records a load error
  when `SampleBuffer::fromFile()` yields nothing usable:
  [`src/core/Track.cpp:1490-1497`](../../src/core/Track.cpp) — `m_frozenBuffer.reset()` then
  `collectError(tr("Frozen take not found: %1 (project.relink ...; freeze.unfreeze ...)"))`.
- **The muted source clips are released once they exist.** The clips a region freeze muted are loaded muted
  from the file; after the clip loop `Track::loadTrack()` unmutes exactly the `frozenMuted` record when the
  take installed but its audio did not load:
  [`src/core/Track.cpp:982-989`](../../src/core/Track.cpp), via `unmuteFrozenClips()`
  ([`src/core/Track.cpp:253-260`](../../src/core/Track.cpp)).
- **The take cannot silence the source when it has no audio.** `Track::frozenTakeSilencesSource()` returns
  false when `m_frozenBuffer == nullptr`:
  [`src/core/Track.cpp:1519-1523`](../../src/core/Track.cpp). Both playback paths consult it *after* trying
  `playFrozenTake()` and fall through to their own clips when it returns false:
  [`src/tracks/SampleTrack.cpp:154-160`](../../src/tracks/SampleTrack.cpp) and
  [`src/tracks/InstrumentTrack.cpp:941-951`](../../src/tracks/InstrumentTrack.cpp). `playFrozenTake()` itself
  refuses to queue with a null buffer ([`src/core/Track.cpp:1564`](../../src/core/Track.cpp)), which is
  correct because the source is the thing that must play.
- **The state is kept so the user can repair it.** `frozenTakeFileMissing()` remains true and the recovery
  commands (`project.relink`, `freeze.unfreeze`) still see it:
  [`src/core/Track.cpp:1513-1516`](../../src/core/Track.cpp),
  [`src/core/ControlCommandsFreeze.cpp:96`](../../src/core/ControlCommandsFreeze.cpp),
  [`src/core/ControlCommandsTransport.cpp:96`](../../src/core/ControlCommandsTransport.cpp).
- **Upstream fix is on this branch.** `git merge-base --is-ancestor b622b3fac4 HEAD; echo ancestor=$?` →
  `ancestor=0`. `b622b3fac4` is the commit that added the `loadTrack()` recovery block and the
  `missingRegionTakeIsAudibleAfterReopen` regression; `a91d7317ce` added the null-buffer guard and the
  `Frozen take not found` report.
- **Existing regression test.** `FreezeRegionRepairTest::missingRegionTakeIsAudibleAfterReopen`
  ([`tests/src/core/FreezeRegionRepairTest.cpp:459-495`](../../tests/src/core/FreezeRegionRepairTest.cpp)):
  freeze a region, save, delete the WAV, `project.open`, require the load report to name `Frozen take not
  found`, require `!clip->isMuted()`, measure the source at ticks 70–112 (inside the take window) and require
  level > 0.01, then unfreeze and undo.

## Commands and exit codes

Run from the worktree root unless shown. `LOGS` = `/home/kruzzzzy/.config/kads/zene/state/runs/3601.r1/logs`.
Every log named below is in that folder.

| Step | Exact command | Exit | Log / result |
|---|---|---:|---|
| Baseline build | `cmake --build build --target FreezeRegionRepairTest -j4 > LOGS/build-1.log 2>&1; echo EXIT=$?` | 0 | build-1.log |
| Baseline test | `cd build/tests && ctest -R '^FreezeRegionRepairTest$' --output-on-failure > LOGS/test-1.log 2>&1; echo EXIT=$?` | **0** | `100% tests passed out of 1` |
| First RED probe: remove the unmute recovery only | edit `Track.cpp:986` to disable the recovery, then `cmake --build build --target FreezeRegionRepairTest -j4 > LOGS/build-2-unfixed.log 2>&1; echo EXIT=$?` | 0 | build-2-unfixed.log |
| First RED probe run | `ctest -R '^FreezeRegionRepairTest$' --output-on-failure > LOGS/test-2-unfixed.log 2>&1; echo EXIT=$?` | **8** | `FAIL! ... missingRegionTakeIsAudibleAfterReopen() 'noClipMuted(reopened)' returned FALSE` |
| Second RED probe: restore the unmute, remove the null-buffer guard | edit `frozenTakeSilencesSource()` to drop `\|\| m_frozenBuffer == nullptr`, then build (`LOGS/build-3-silence-unfixed.log`) | 0 | build-3-silence-unfixed.log |
| Second RED probe run | `ctest -R '^FreezeRegionRepairTest$' --output-on-failure > LOGS/test-3-silence-unfixed.log 2>&1; echo EXIT=$?` | **8** | `FAIL! ... missingTakeRecoversThroughUnfreezeAndUndo() '!restored->frozenTakeSilencesSource(10)' returned FALSE`; `FAIL! ... missingRegionTakeIsAudibleAfterReopen() 'within > 0.01' returned FALSE (the source is silent inside the missing region take: 0)` |
| Restore both hunks, rebuild | `cmake --build build --target FreezeRegionRepairTest -j4 > LOGS/build-4-restored.log 2>&1; echo EXIT=$?` | 0 | build-4-restored.log |
| Restored test | `ctest -R '^FreezeRegionRepairTest$' --output-on-failure > LOGS/test-4-restored.log 2>&1; echo EXIT=$?` | **0** | 1/1 passed |
| Formal 4b: create the reversal patch | two product hunks reversed, then `git diff -- src/core/Track.cpp > LOGS/unfix-3601.patch; echo EXIT=$?` | 0 | unfix-3601.patch |
| 4b RED build | `cmake --build build --target FreezeRegionRepairTest -j4 > LOGS/build-6-red.log 2>&1; echo EXIT=$?` | 0 | build-6-red.log |
| 4b RED run (must FAIL) | `cd build/tests && ctest -R '^FreezeRegionRepairTest$' --output-on-failure > LOGS/test-7-red.log 2>&1; echo EXIT=$?` | **8** | `totals 11 passed, 2 failed`; both assertions quoted above |
| 4b restore fix | `git apply -R LOGS/unfix-3601.patch; echo EXIT=$?` | 0 | `git diff --stat` printed nothing |
| 4b GREEN build | `cmake --build build --target FreezeRegionRepairTest -j4 > LOGS/build-7-green.log 2>&1; echo EXIT=$?` | 0 | build-7-green.log |
| 4b GREEN run (must PASS) | `ctest -R '^FreezeRegionRepairTest$' --output-on-failure > LOGS/test-8-green.log 2>&1; echo EXIT=$?` | **0** | `100% tests passed out of 1` |
| Frozen-take family build | `cmake --build build --target FrozenTakeHandoffTest FrozenTakeRoundTwoTest MixerBusFreezeTest BatchFreezeTest -j4 > LOGS/build-5-family.log 2>&1; echo EXIT=$?` | 0 | build-5-family.log |
| Frozen-take family run | `cd build/tests && ctest -R '^(BatchFreezeTest\|FrozenTakeHandoffTest\|FrozenTakeRoundTwoTest\|FreezeRegionRepairTest\|MixerBusFreezeTest)$' --output-on-failure > LOGS/test-9-freeze-family-green.log 2>&1; echo EXIT=$?` | **0** | 5/5 passed |
| Gates | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-3601 > LOGS/gate-1.log 2>&1; echo EXIT=$?` | **0** | `RESULT: PASS`, all 8 rows PASS (`complexity`/`file-length`: no new item in your files) |

The whole freeze family (`-R 'Freeze|Frozen'`, `LOGS/test-6-freeze-family.log`) also ran; the four C++ tests
above passed, `ControlFreezeCommandsTranscript` failed, and that failure is pre-existing and environmental
(see Other findings). `MixerBusFreezeMenuTest` is not built in this build tree.

## Self-check

- **The regression test FAILS with the product change reverted and PASSES with it (exit codes).** Existing
  test `FreezeRegionRepairTest::missingRegionTakeIsAudibleAfterReopen`. With the two product hunks reversed
  by `LOGS/unfix-3601.patch`, build EXIT 0 and `ctest -R '^FreezeRegionRepairTest$'` EXIT **8**; after
  `git apply -R` restored them, build EXIT 0 and the same test EXIT **0** (logs `test-7-red.log`,
  `test-8-green.log`). This job added no test because the fix and its regression already landed upstream on
  this branch (`b622b3fac4`, ancestor check EXIT 0).
- **The test is deterministic.** `missingRegionTakeIsAudibleAfterReopen()` (`FreezeRegionRepairTest.cpp:459`)
  uses no sleeps or wall-clock checks: `QTemporaryDir`, a fixed tone WAV, `saveProjectFile`, `QFile::remove`,
  a synchronous `project.open`, fixed tick windows (60–120, measured 70–112), and `AudioEngine` render
  periods driven by the test. The audio assertion is an RMS threshold on rendered buffers, not a timer.
- **The fix is at the cause and complete.** The load path nulls the buffer and reports the failure
  (`Track.cpp:1490-1497`); the clip-mute recovery runs after clips are loaded, for exactly the take whose
  audio is absent (`Track.cpp:982-989`); both rendering paths check `frozenTakeSilencesSource()`
  (`SampleTrack.cpp:154-160`, `InstrumentTrack.cpp:941-951`), whose null-buffer guard is the second half
  (`Track.cpp:1519-1523`). `playFrozenTake()` also refuses a null buffer (`Track.cpp:1564`). The recovery
  commands that read `frozenTakeFileMissing()` (`ControlCommandsFreeze.cpp:96`,
  `ControlCommandsTransport.cpp:96`) still see the kept take state. No header changed, so no rebuild spread.
- **No collateral damage.** The unmute block fires only when `m_frozen.isFrozen() && m_frozenBuffer ==
  nullptr`, so a take whose file loads still owns its window and a track with no take never enters the
  branch. A region take whose file is present keeps silencing precisely its window (the
  `regionFlagRoundTripsAndWholeTrackTakeIsUnchanged` and `overlappingClipIsSilentOnlyInsideTheRegion` tests
  pass). Old projects serialise the same attributes (`frozenAudio/frozenStart/frozenEnd/frozenMuted/
  frozenRegion`); the missing-file case keeps the take so relink/unfreeze and one undo still work. The five
  frozen-take tests pass (EXIT 0).
- **ALREADY-FIXED code evidence.** `src/core/Track.cpp:982-989` (release the muted source clips when the
  take's audio is absent), `src/core/Track.cpp:1490-1497` (surface the failure), and
  `src/core/Track.cpp:1519-1523` (a missing take never silences the source). The upstream commit that added
  them, `b622b3fac4`, is an ancestor of HEAD.
- **Sibling commands in command files changed.** No command file changed in this job. The freeze/transport
  command families are untouched, so `app.about`, `app.ask`, `app.help`, `app.online_help`,
  `app.task_cancel`, `app.tasks`, `app.tour`, `app.tours`, `app.update_check_get`,
  `app.update_check_run`, `app.update_check_set`, `app.version`, `app.whats_new`,
  `arrangement.fit_tracks`, and `arrangement.get_state` are unaffected. The `ControlCommandsSnapshot`
  canary is unchanged because no command was added or edited.

## Other findings

- `ControlFreezeCommandsTranscript` (`tests/CMakeLists.txt:7169-7175`) returns 77 when the build has no
  audible instrument (`tests/control-freeze-commands-transcript.py:488-489`) but its registration sets no
  `SKIP_RETURN_CODE 77`, so ctest records the environment skip as Failed. It failed identically before and
  after the proof reversal (no product code differs); not changed here because this bug fix does not edit
  test registration.
- `MixerBusFreezeMenuTest` is not built in this configured build tree, so it could not run. It is a GUI
  target outside this finding.
