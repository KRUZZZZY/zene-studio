FIXED

Cluster new-p12-223, "Muting a track leaves the sample cursor behind" (`src/core/SamplePlayHandle.cpp`, reported at `:~570`). This is round 1, on top of a round 0 that ended with no diff; nothing of round 0 was on the branch (`git log --grep=223` showed no commit, `git status` clean at start), so this report and its commit carry the whole fix.

## Verdict: REAL

`SamplePlayHandle::play()` renders the clip only when the track is unmuted:

- `src/core/SamplePlayHandle.cpp:573` - `if( !( m_track && m_track->isMuted() && !m_ignoresTrackMute ) && !(m_patternTrack && m_patternTrack->isMuted()))` guards the whole render (the audible branch, `:574-632`).
- `src/core/SamplePlayHandle.cpp:645` - `m_frame += frames;` runs whether or not the render did.

The handle's timeline position (`m_frame`, exported as `framesDone()`) therefore advances on a muted pass, while the SOURCE cursor - `m_state`, advanced only by the reads in the audible branch (`Sample::play`, `src/core/Sample.cpp:100`; the stretcher's analysis cursor in `renderPreservingPitch`, `src/core/SamplePlayHandle.cpp:748-751`) - stays on the frame the mute began on. The two drift apart for as long as the mute lasts: unmuting inside the clip replays audio that has already sounded, and the tail runs out early. The muted pass writes nothing into the caller's buffer, which `PlayHandle::doProcessing` has already zeroed (`src/core/PlayHandle.cpp:58`), so the drift is silent until the unmute.

Reproduced before the fix, with the tests written for this cluster (output copied from `logs/test-9.log`):

```
FAIL!  : SampleClipMuteCursorTest::mutingATrackKeepsTheSampleCursorMoving() (frame 0 after the mute is -0.370618, the unmuted reading of the same clip is 0.108828 - the muted pass left the source cursor behind)
FAIL!  : SampleClipMuteCursorTest::mutingATrackKeepsTheStretchedCursorMoving() (the stretched read after the mute averaged 0.2 off the unmuted reading of the same clip (worst 0.2): the muted pass left the analysis cursor behind)
FAIL!  : SampleClipMuteCursorTest::aMutedPassAdvancesWithoutAllocating() (frame 0 after the muted allocation pass is 0.466667, the unmuted reading is 0.666667)
Totals: 2 passed, 3 failed
```

## The fix

One helper and one member, called from the muted branch only.

1. `include/SamplePlayHandle.h:254` - `void advanceSourceSilently(f_cnt_t frames);`, private, documented at `:238-253`.
2. `include/SamplePlayHandle.h:257` - `std::array<SampleFrame, DEFAULT_BUFFER_SIZE> m_silentScratch{};` - 2 KB of per-handle, preallocated discard storage, so the audio thread pays no allocation (I8).
3. `src/core/SamplePlayHandle.cpp:636-643` - an `else` after the audible branch calls `advanceSourceSilently(frames)`. `m_frame += frames` (`:645`) is unchanged.
4. `src/core/SamplePlayHandle.cpp:781-821` - the helper: loops in `DEFAULT_BUFFER_SIZE` chunks, the pattern `CompSlipReaders::render` uses (`src/core/CompSlip.cpp:89-104`), and drives exactly what the audible branch drives, into the scratch:
   - `m_preservePitch` -> `renderPreservingPitch(chunk, count)` (the WSOLA / Rubber Band analysis cursor);
   - otherwise `m_sample->play(chunk, &m_state, count, m_loopMode, ratio)` with the same `ratio = warpRatio() / speedHere()` the audible branch computes, taken once for the period as the audible branch takes it once per `Sample::play` call;
   - `m_compSlip->render(*m_sample, chunk, count, m_envelopeStart + m_frame + done, ratio)` when `m_compSlip` is set, the clip frame advanced per chunk;
   - `m_repeatsTransients` -> `renderTransientRepeat(chunk, count)`, which keeps its own source cursor (`src/core/TransientRepeat.cpp:361-362`) and is the third mode of the audible branch. Without it, a Repeat/Gate clip would keep that cursor behind instead.

   It deliberately does NOT run the fade/gain envelope, the active-take gate, the comp gate or `processClipEffects`: a muted clip does not sound, so only the read position has to keep step. It never writes to the caller's buffer.

Siblings and callers checked:

- The mute check covers both `m_track` and `m_patternTrack`; the `else` covers both.
- `m_ignoresTrackMute` (set by `SamplePlayHandle::setIgnoresTrackMute` for `clip.consolidate`, and by `armPooledVoice` for frozen take voices, `src/core/SamplePlayHandle.cpp:494`) keeps taking the audible branch: an offline render of a muted track still bakes audio, unchanged.
- `frames` here is the post-offset count the audible branch would render, so the first-period offset path (`:551-566`) is untouched.
- No serialisation, no control command, no undo path touched: a saved project loads as before (the change adds an in-memory member and a render-time branch only).

## Regression test

New file `tests/src/tracks/SampleClipMuteCursorTest.cpp`, target `SampleClipMuteCursorTest`, registered in `tests/CMakeLists.txt` (comment at the list entry), `tests/fork-sources.txt` (new fork source) and `tests/upstream-modifications.txt` (why `tests/CMakeLists.txt` changed), same commit.

Why not a slot in `tests/src/tracks/SampleClipWindowTest.cpp`, as the checkpoint asked: Gate 7 (`tests/file-length-gate.sh`) measures CODE lines and that file sits at 494 of its 500-line limit at base `fa4a5f2e7` (measured with the gate's own awk on `git show fa4a5f2e7:tests/src/tracks/SampleClipWindowTest.cpp`). Three slots plus their fixtures do not fit in six lines: with them in that file the gate reported `FAIL file-length  NEW: new file over 500 lines: tests/src/tracks/SampleClipWindowTest.cpp (671)` (`logs/gates-1.log`). Split into its own target, both gates pass (`logs/gates-2.log`).

The test matrix:

| path | slot |
|---|---|
| normal path: resample mode, mute inside the clip, unmute | `mutingATrackKeepsTheSampleCursorMoving` - two handles on one clip, the reference reads with `setIgnoresTrackMute(true)`, the other spends 10 periods muted, then the first audible period must match the reference frame for frame (exact: a source at the engine rate reads 1:1, `src/core/Sample.cpp:132`) |
| reconfiguration path: pitch-preserving mode | `mutingATrackKeepsTheStretchedCursorMoving` - same shape on a 2x-warped `WarpStretchMode::PreservePitch` clip over a position ramp; the stretcher smears at a block edge, so the middle half of the period is compared with a tolerance (mean < 0.05) far below the 0.2 a stale analysis cursor produces |
| audio-thread constraint (I8) | `aMutedPassAdvancesWithoutAllocating` - 24 muted periods under `test::tlCountAllocations`, zero allocations, and the cursor still caught up |
| muted pass renders nothing | inside slot 1: every frame of the caller's buffer is still zero after each muted `play()` (the buffer is zeroed first, as `PlayHandle::doProcessing` hands it over) |
| exhaustion | slot 1 tail: a mute that runs past the clip's window leaves no audio behind the cursor - the next audible period is silence |
| platform | no platform-specific code; the target builds from the existing test infrastructure (no new dependency) |

## Commands and exit codes

All output is in `/home/kruzzzzy/.config/kads/zene/state/runs/new-p12-223.r1/logs`.

RED first (tests only, before the product change):

```
cmake --build build --target SampleClipWindowTest -j4 > logs/build-1.log 2>&1   EXIT=0
cd build/tests && ctest -R '^SampleClipWindowTest$' --output-on-failure > logs/test-1.log 2>&1   EXIT=8
  -> 3 new slots failed, 18 existing slots passed (Totals: 18 passed, 3 failed)
```

GREEN (after the product change):

```
cmake --build build --target SampleClipWindowTest -j4 > logs/build-2.log 2>&1   EXIT=0
cd build/tests && ctest -R '^SampleClipWindowTest$' --output-on-failure > logs/test-2.log 2>&1   EXIT=0  (100% passed)
```

Header rebuilt into every target that includes it:

```
cmake --build build -j4 --target zene SampleClipWindowTest TakeLaneCompPlaybackTest SampleClipWarpTest \
  SampleClipWarpModesTest SampleClipTransientRepeatTest AudioEditingTest AudioClipLoopTest \
  SampleClipTransientRepeatSafetyTest SampleClipStretchTest SampleClipRubberBandTest ClipFadesRenderTest \
  ClipEffectsTest AudioResamplerRatioTest W3EngineTest SampleClipWarpEditTest AutomationRenderAllocationTest \
  ClipPolarityTest ClipOffsetFadeTest ClipOffsetFadePairTest FrozenTakeRoundTwoTest FrozenTakeHandoffTest \
  FreezeRegionRepairTest LoopImportWarpTest MixerToolsTest RecordToolsTest > logs/build-3.log 2>&1   EXIT=0
cd build/tests && ctest -R '<those 25 tests>' -j2 --output-on-failure > logs/test-3.log 2>&1   EXIT=8
  -> 24/25 passed; RecordToolsTest::theRemainingRecordTime, see Other findings
```

After the split into its own target:

```
cmake --build build --target SampleClipMuteCursorTest -j4 > logs/build-8.log 2>&1   EXIT=0
cd build/tests && ctest -R '^SampleClipMuteCursorTest$' --output-on-failure > logs/test-7.log 2>&1   EXIT=0
./SampleClipMuteCursorTest > logs/test-8.log 2>&1   EXIT=0   (Totals: 5 passed, 0 failed)
```

## Step 4b - the test catches the bug

Product files only (`include/SamplePlayHandle.h`, `src/core/SamplePlayHandle.cpp`):

```
git diff -- include/SamplePlayHandle.h src/core/SamplePlayHandle.cpp > logs/fix.patch   DIFF_EXIT=0
git apply -R logs/fix.patch                                                             REVERSE_EXIT=0
cmake --build build --target SampleClipMuteCursorTest -j4 > logs/build-9.log 2>&1        BUILD_EXIT=0
cd build/tests && ctest -R '^SampleClipMuteCursorTest$' --output-on-failure > logs/test-9.log 2>&1   TEST_EXIT=8
  -> Totals: 2 passed, 3 failed  (the three slots above)
git apply logs/fix.patch                                                                REAPPLY_EXIT=0
cmake --build build --target SampleClipMuteCursorTest -j4 > logs/build-10.log 2>&1      BUILD_EXIT=0
cd build/tests && ctest -R '^SampleClipMuteCursorTest$' --output-on-failure > logs/test-10.log 2>&1  TEST_EXIT=0
./SampleClipMuteCursorTest > logs/test-11.log 2>&1                                      DIRECT_EXIT=0  (Totals: 5 passed, 0 failed)
```

The same reversal was run earlier with the slots still inside `SampleClipWindowTest.cpp` (`logs/build-4.log`, `logs/test-4.log` EXIT=8 with 18 passed / 3 failed; `logs/build-5.log`, `logs/test-5.log` EXIT=0), so the proof does not depend on where the slots live.

## Gates

```
python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > logs/gates-2.log 2>&1
EXIT=0
  PASS fork-sources / all-sources-reproduce / no-upstream-regression / check-namespace / no-tautology / doc-figures
  PASS complexity    no new item in your files
  PASS file-length   no new item in your files
RESULT: PASS
```

An earlier run (`logs/gates-1.log`, EXIT=1) had `FAIL file-length` while the slots lived in `SampleClipWindowTest.cpp`, and `logs/gates-3.log` (EXIT=1) had `FAIL fork-sources` / `FAIL all-sources-reproduce` because the new test source was not in the whole-tree manifest. Both are fixed: the slots are their own target, and `tests/src/tracks/SampleClipMuteCursorTest.cpp` is in `tests/all-sources.txt` at the position the manifest's own recipe derives (`logs/diff-all2.txt` is empty, i.e. the manifest reproduces exactly: DIFF_EXIT=0), and in `tests/fork-sources.txt` next to this report, like the previous cluster's entries.

## Self-check

`python3 .../submit_check.py <worktree> new-p12-223` (`logs/submit-1.log`, EXIT=1) asked:

1. *The regression test FAILS with your product change reverted and PASSES with it (exit codes).* Yes, step 4b above: fixed test with the product change reversed -> `TEST_EXIT=8` (`logs/test-9.log`, `Totals: 2 passed, 3 failed`); with the change applied -> `TEST_EXIT=0` (`logs/test-10.log`, `logs/test-11.log`, `Totals: 5 passed, 0 failed`). `logs/fix.patch` holds only the two product files.
2. *The test is deterministic: no wall-clock timing, sleeps or races (name the seam or injected values).* No clock, no sleep, no thread. It drives `SamplePlayHandle::play()` directly on injected values: a deterministic fixture source (`makeTone`, a 440 Hz tone; `makeRamp`, a block-constant position ramp), fixed period `kPeriod = 1024`, a fixed number of periods, and `track.setMuted()`. The unmuted reading is an injected seam too: `setIgnoresTrackMute(true)` on a second handle over the same clip (`include/SamplePlayHandle.h:100`). Output is pure arithmetic on those inputs; `Engine::init(true)` is the same fixture the neighbouring clip tests use. (No assertion depends on elapsed time; the allocation slot reads `test::tlAllocationCount`, a thread-local counter, not a timer.)
3. *The fix is at the cause and complete: every caller and sibling checked and consistent.* Cause: the muted branch of `play()` advanced the timeline but not the source. The helper it now calls drives the same three render modes the audible branch drives (resample + comp-slip, pitch-preserving stretcher, transient repeaters) with the same `warpRatio()/speedHere()` ratio and the same per-chunk clip frame. Callers/siblings checked: `SampleTrack::play` and `PatternTrack` both reach the same mute check (`src/core/SamplePlayHandle.cpp:573`); the offline `clip.consolidate` path sets `setIgnoresTrackMute` and keeps the audible branch; frozen take voices set the same flag at `src/core/SamplePlayHandle.cpp:494` (intended, unchanged); the first-period offset path (`:551-566`) passes the same post-offset `frames`; `m_frame += frames` (`:645`) is untouched.
4. *No collateral damage: old projects and saved data load as before; no new limit or check.* No serialisation, project XML, control command, undo or command vocabulary is touched: the change is one private member (`include/SamplePlayHandle.h:257`) and one private helper plus the `else` that calls it. A saved project loads and renders exactly as before, and a muted clip still renders silence, so nothing that used to sound changes.
5. *Verdict ALREADY-FIXED or FALSE-POSITIVE evidence.* Not applicable: the verdict is FIXED, with the RED output quoted above.
6. *Sibling commands in the command files you changed.* None: no `*.commands` file, command file, or control command was changed (`git show --stat HEAD` lists only the header, the play-handle source, the test, the two manifests, `tests/CMakeLists.txt` and this report). `app.*`, `arrangement.*` and every other command behave as before; the mute check they drive is untouched.

Also answered for the reviewer, not asked by the check: why the test is its own file instead of a slot in `SampleClipWindowTest.cpp` is Gate 7 headroom (base 494/500 code lines), measured and demonstrated above.

## Brief plumbing (from the checkpoint): BLOCKED, read-only harness

The checkpoint also asked for three changes to the brief generator and the runner. They live
outside this worktree, in the harness that drives the lanes, and that tree is mounted
read-only, so they cannot be made from this session:

```
$ touch /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/.write-probe-p12223
touch: cannot touch '.write-probe-p12223': Read-only file system            (EXIT=1)
$ python3 -c "open('/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/.write-probe-p12223','w').write('x')"
OSError: [Errno 30] Read-only file system
$ mount | grep zene-bunny-fix
/dev/nvme0n1p2 on /home/kruzzzzy/.local/share/zene-bunny-fix type btrfs (ro,nosuid,nodev,noatime,...)
```

Where each of the three would go, for whoever owns that tree:

1. *strip the previous diagnostic's read-only blockers before an implementation brief* -
   `workflow_runtime.py:196-199` appends the diagnostic's JSON plan (its `blockers` key) to
   `job['notes']`; `qwenfix.py:1401-1404` (and `feature_brief`) prints those notes verbatim
   under "FIRST: ... asked for changes". The strip belongs where the notes are rendered: parse
   the fenced plan, drop `blockers`, re-render.
2. *correct the "earlier commits are already on this branch" line* - `qwenfix.py:1402`, which
   asserts commits that a round ending with no diff never made (round 0 of this very unit).
3. *make the runner reject a round that ends with no diff instead of logging finished rc=0* -
   `qwenfix.py:1908` records `rc` as the session's alone and `:1994-2013` only queues a retry
   afterwards; a no-diff round should carry a non-zero `rc` (or its own outcome) from `:1908`
   so it is never recorded as a finished, clean round.

Nothing in this unit depends on them: the fix, its proof and the gates are complete above.

## Other findings

- `RecordToolsTest::theRemainingRecordTime` fails in this worktree with `cannot measure the volume holding '/home/kruzzzzy/Documents/Zene Studio'` (`logs/test-3.log`). That directory does not exist on this box (`ls -d` -> exit 2); the slot calls `record.remaining`, which measures the default project directory. Unrelated to this change (no play handle, no mute) and pre-existing in the lane.
- Five GUI/take-lane targets are not built in this lane's build tree, so ctest reports them `Not Run` (`TakeLaneCompTest`, `TakeLaneTest`, `ClipEffectsDialogTest`, `SampleClipWarpModeMenuTest`, `TakeLanesPanelTest`, `logs/test-12.log`). None of their sources includes `SamplePlayHandle.h`, so the header change does not reach them.
- Design note, not a defect: a frozen take's pooled voice sets `m_ignoresTrackMute` (`src/core/SamplePlayHandle.cpp:494`), so muting a track after a frozen pass starts does not silence that already-running sample. That is the documented frozen-take behaviour and is untouched here.
