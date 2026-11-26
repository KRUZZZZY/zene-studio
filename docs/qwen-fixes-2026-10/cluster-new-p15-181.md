FIXED

# Cluster new-p15-181 — sample-play-handle cleanup frees heap state on the audio thread

**Verdict: FIXED. The finding was REAL.** The engine deletes a finished `SamplePlayHandle` on
the audio thread, and destroying the handle hands heap memory back to the allocator there: the
clip's comp-slip readers are a per-handle `new` (`src/core/CompSlip.cpp:44`), and the pre-fade and
loop `Sample` snapshots release a source buffer the handle can be the last owner of.

The finding's line (`include/SamplePlayHandle.h:~230`) has moved: the members are at
`include/SamplePlayHandle.h:321` (`std::unique_ptr<CompSlipReaders> m_compSlip`), `:329`
(`std::optional<Sample> m_preFadeSample`) and `:339` (`std::optional<Sample> m_loopSample`).

## Evidence that the free happened on the audio thread

- The engine's only delete of a play handle is `AudioEngine::disposePlayHandle`
  (`src/core/AudioEngine.cpp:453`): it returns early for a pooled voice (`:455-461`) and for a
  handle that hands itself over (`:463`), releases notes to `NotePlayHandleManager` (`:464-467`),
  and otherwise runs `delete handle` (`:468`).
- `disposePlayHandle` is called on the render path: `AudioEngine::renderStageEffects`
  (`src/core/AudioEngine.cpp:1151`) disposes every finished handle at `:1168-1172`, and that whole
  stage runs inside `renderNextPeriod`'s `const RealtimeScope realtime;`
  (`src/core/AudioEngine.cpp:1228`). `renderStageNoteSetup` (`:1037`, `:1043`) and the capacity-deferred
  sweep (`:1397`) dispose from the same scope.
- The handles that reach that delete are built on the render path by `SampleTrack::play`
  (`src/tracks/SampleTrack.cpp:222`, `new SamplePlayHandle(st, scheduled.window)`), and a comped
  clip's slipped span gives each one its `CompSlipReaders` in `snapshotComp`
  (`src/core/SamplePlayHandle.cpp:382`).
- The inherited converter is *not* what frees there: clip handles are built with
  `AudioResampler::RealtimeVoiceTag{}` (`src/core/SamplePlayHandle.cpp:147`), so their resampler
  state is a preallocated pool slot. The free is the fork's own heap-backed members, exactly as
  the finding says.
- The render-path contract already exists: `PlayHandle::handOverToOwner()` (`include/PlayHandle.h:91`,
  "the handle's owner frees it: the engine then drops its reference without deleting it"), and the
  codebase already retires heap state this way (`MpeStreamSnapshot::reclaimRetired`,
  `src/core/AudioEngine.cpp:569`; `SampleVoicePool`; `TestTonePlayHandle`). The sample handle simply
  did not answer it.

## The fix (at the cause, no new allocation on the render path)

- `include/SamplePlayHandle.h:73-95`: the handle overrides `handOverToOwner()` (`:81`), exposes
  `ownsHeapBackedState()` (comp-slip readers, a pre-fade snapshot or a loop snapshot) and
  `reclaimRetired()`; `:342-346` adds the private `retireSelf()` and the retire list's link.
- `src/core/SamplePlayHandle.cpp:510`: a handle that owns such state and is being disposed **inside
  a `RealtimeScope`** pushes itself on a lock-free retire list - one compare-and-swap, the link
  lives in the handle (`:523 retireSelf`), no allocation, no lock, no wait, no logging. Off the
  render path it returns false, so the caller's `delete` frees immediately, as before. A handle with
  no such state (every clip with no comp slip, no pre-fades and no audio loop; every preview and
  metronome handle) also returns false: the common path is untouched.
- `src/core/SamplePlayHandle.cpp:537 reclaimRetired()` deletes the retired handles and refuses to
  run inside a `RealtimeScope`.
- `src/core/AudioEngine.cpp:1638`: the control-thread service point
  (`serviceAudioBusHandlesOnControlThread`, already called by the engine's 100 ms timer and by
  tests) calls `reclaimRetired()`; `src/core/AudioEngine.cpp:635` drains it in `~AudioEngine` next
  to `MpeStreamSnapshot::reclaimRetired()`, so a teardown never leaks.

The retire list is a file-scope static pair, not static data members of the exported class (the
reason `AudioEngine.cpp`'s own `LMMS_TESTING` block gives: MSVC C2492 on `thread_local`/static data
with a DLL interface).

## The regression test

`tests/src/core/SamplePlayHandleRetireTest.cpp` (new, registered in `tests/CMakeLists.txt`, and in
`tests/all-sources.txt` + `tests/fork-sources.txt`),
`disposingASlippedCompHandleDoesNotFreeOnTheRenderPath`:

1. builds a real take track and a real `SampleClip` (`TakeLaneTestSupport.h`), paints its own lane
   over the clip's span with a two-tick slip (`TakeLaneModel::selectSegment`), and constructs the
   clip's `SamplePlayHandle`;
2. runs the engine's own disposal contract (`src/core/AudioEngine.cpp:463-468`) inside a
   `RealtimeScope` with the allocation probe armed
   (`tests/src/core/AllocationProbe.h`, `tlFreeCount`) and asserts **zero frees**;
3. runs the control-thread service point and asserts the frees happen there (so nothing leaked);
4. asserts an ordinary handle (no comp slip, no pre-fade, no loop) still hands itself *not* over,
   i.e. the engine still deletes it where it always did.

Determinism: no sleeps, no wall clock and no race. The seam is the product's own `RealtimeScope`
(the same flag `AudioEngine::disposePlayHandle(handle, RealtimeScope::isActive())` reads at
`src/core/AudioEngine.cpp:1792`) plus the probe's thread-local free counter; the fixture is a plain
model setup. The test deliberately uses **no API added by the fix**, so the RED tree still compiles,
and it is self-validating: with the fix in place step 2 can only pass if the handle really owned
heap-backed state, because step 4 shows the ordinary handle is still deleted there.

## Commands and exit codes (all logs in the run's `logs` folder)

| # | command | log | exit |
|---|---------|-----|------|
| 1 | `cmake --build build --target SamplePlayHandleRetireTest zene -j4` | `build-9-newtest-green.log` | 0 |
| 2 | `ctest -R SamplePlayHandleRetireTest --output-on-failure` (from `build/tests`) | `test-6-green.log` | 0 (1/1 passed) |
| 3 | `git diff -- include/SamplePlayHandle.h src/core/SamplePlayHandle.cpp src/core/AudioEngine.cpp > logs/fix.patch` | - | 0 (158 lines) |
| 4 | `git apply -R logs/fix.patch` | - | 0 |
| 5 | `cmake --build build --target SamplePlayHandleRetireTest -j4` (RED tree) | `build-10-red.log` | 0 |
| 6 | `ctest -R SamplePlayHandleRetireTest --output-on-failure` (RED tree) | `test-7-red.log` | **8** |
| 7 | `git apply logs/fix.patch` | - | 0 |
| 8 | `cmake --build build --target SamplePlayHandleRetireTest zene -j4` | `build-11-green-final.log` | 0 |
| 9 | `ctest -R SamplePlayHandleRetireTest --output-on-failure` | `test-8-green-final.log` | 0 (1/1 passed) |
| 10 | `ctest -R "SamplePlayHandleRetireTest\|AudioBusPlayHandleQueueTest\|TakeLaneCompTest\|FrozenTakeHandoffTest\|ClipFadesRenderTest\|AudioClipLoopTest" --output-on-failure` | `test-9-family.log` | 0 (6/6 passed) |
| 11 | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | `gates-3.log` (detail in `gate-detail/`) | 0, RESULT: PASS |

The RED failure, verbatim from `test-7-red.log`:

```
1/1 Test #358: SamplePlayHandleRetireTest .......***Failed    0.74 sec
FAIL!  : SamplePlayHandleRetireTest::disposingASlippedCompHandleDoesNotFreeOnTheRenderPath()
'freesOnRenderPath == 0' returned FALSE. (the engine's disposal handed the handle's heap state
back to the allocator on the audio thread)
```

Gate table from `gates-3.log`:

```
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

Notes for a reader of the log folder: `build-1.log` is an interrupted first attempt (the harness
stopped a backgrounded build; `build-2.log` restarts it). The test first lived in
`tests/src/core/AudioBusPlayHandleQueueTest.cpp`; the file-length ratchet then reported
`NEW: new file over 500 lines: tests/src/core/AudioBusPlayHandleQueueTest.cpp (548)`
(`gates-1.log`), so the test moved to its own registered file and that file was restored
(`tests/test-in-old-file.patch` is the revert). The RED/GREEN pair above is the one for the final
artifact.

## Self-check

1. **The regression test FAILS with the product change reverted and PASSES with it.** RED: product
   diff reversed (step 4), rebuild (step 5, exit 0), test step 6 exit **8** with the assertion above;
   GREEN: patch restored (step 7), rebuild (step 8, exit 0), test step 9 exit **0**.
2. **The test is deterministic.** No wall-clock timing, no sleeps, no races: it injects the
   product's own `RealtimeScope` state (the flag the engine itself reads when it disposes) and reads
   the probe's thread-local free counter; the fixture is model setup only. Both the render-path and
   the control-thread step are ordinary synchronous calls on the test's thread.
3. **The fix is at the cause and complete.** Every disposal of a play handle in the engine goes
   through `disposePlayHandle` (`src/core/AudioEngine.cpp:453`; the only delete of a play handle in
   `src/` - searched `delete handle`, `delete ph`, `delete smpHandle` - is its `:468`), and it asks
   `handOverToOwner()` before deleting (`:463`), so all eight of its call sites (`:1037`, `:1043`,
   `:1102`, `:1173`, `:1356`, `:1397`, `:1792`, and the engine's removal paths at `:1897`-`:1976`)
   are covered by the one override. Siblings checked and
   left alone: pooled voices (`recyclePooledSampleVoice`, returns before `handOverToOwner`),
   `NotePlayHandleManager::release` (`:464-467`), `TestTonePlayHandle` (its own override still
   wins, `src/core/ControlCommandsAudioTestTone.cpp:102`), and the preview/metronome constructors
   (`ownAudioBusHandle == true`, no clip state, so `ownsHeapBackedState()` is false). Header and
   definition changed together; `zene` and every test that includes the header were rebuilt.
4. **No collateral damage.** No save or load path changes (the new member is a transient pointer
   that no serializer sees), no new command, no undo step, no config key. A handle with no
   heap-backed state returns false, so every ordinary clip handle, preview and metronome handle is
   deleted exactly where it was - asserted by the test's last check. The people who exceed no new
   limit: there is no limit; the retire list is unbounded and drained every 100 ms.
5. **Realtime.** The new render-path work is one relaxed load, one compare-and-swap and one
   pointer store per retired handle, plus a `RealtimeScope::isActive()` read - no allocation, no
   free, no lock, no wait, no logging, no Qt call. `reclaimRetired()` refuses to free inside a
   `RealtimeScope`, so it cannot run on the audio thread even if it is called from one.
6. **Registration and gates.** `tests/upstream-modifications.txt` gained a reason for each of the
   three inherited product files; the new test file is registered in `tests/CMakeLists.txt` and in
   both scope manifests; `fork-sources`, `all-sources-reproduce`, `no-upstream-regression`,
   `check-namespace`, `no-tautology`, `doc-figures`, `complexity` and `file-length` all PASS
   (`gates-3.log`). No gate limit, baseline or ratchet was touched.
7. **Not an "already fixed" claim.** The verdict is FIXED, on the evidence above: before this
   change `SamplePlayHandle` had no `handOverToOwner` override (only `TestTonePlayHandle` did), and
   the RED run shows the free on the render path in this tree at this commit.

## Other findings (not fixed here)

- Inherited: a *non-pooled* `SamplePlayHandle` (the preview/`audio.test_tone` family) still has its
  `Sample::PlaybackState`'s resampler state freed when the engine deletes it on the render path;
  the fork's pooled voices avoid this by construction. Out of this finding's scope (the finding
  names the fork's own members), and fixing it means changing the inherited destruction path.
- The engine deleting play handles on the audio thread is the inherited design
  (`AudioEngine.cpp:468`); this fix removes the heap-backed part of that delete for the sample
  handle, not the delete itself.
