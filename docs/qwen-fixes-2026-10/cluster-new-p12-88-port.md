# Cluster new-p12-88-port — BUG 14, the GIG player's no-progress resampler fallback

Verdict: **FIXED**

The finding is real: the GIG player's no-progress fallback cleared the *shared*
output buffer of the period, and that buffer holds the audio of every other
sample of every other note. The fallback now leaves the frames it does not fill
alone, so a sample that cannot make progress contributes nothing and erases
nothing.

## The defect

`GigInstrument::playImpl()` mixes **every sample of every note** into **one**
buffer per period - the nested loops over `m_notes` and `note.samples` all
`+=` into the same `out` (`plugins/GigPlayer/GigPlayer.cpp:447` and `:455`,
mixing at `:507`). The buffer is zeroed once per period
(`plugins/GigPlayer/GigPlayer.cpp:369`, `zeroSampleFrames`), so the frames a
sample does not fill are already silent.

The old fallback (upstream commit `44a68b8b0`, "Fix audio resampling
functionality (#7858)") did this when the resampler reported no progress:

```cpp
if (inputFramesUsed == 0 && outputFramesGenerated == 0)
{
        std::fill_n(&out[framesMixed], frames - framesMixed, SampleFrame{});   // <- the bug
        break;
}
```

`out` is the shared buffer, so that `fill_n` did not silence *this* sample -
it overwrote the audio the other samples had already mixed into the rest of
the period. One sample without progress silenced the whole instrument for that
period.

## The fix

`plugins/GigPlayer/GigPlayer.cpp:104-111` - the no-progress case now returns
without touching the buffer:

```cpp
if (inputFramesUsed == 0 && outputFramesGenerated == 0)
{
        // This sample has nothing to contribute to the rest of the block.
        // The output buffer is SHARED with the other samples and notes, so
        // the frames this sample does not fill are left alone: clearing
        // them would erase the audio they have already mixed in.
        return 0;
}
```

The mixing step itself is now the named function
`gigplayerMixResampledBlock()` (`plugins/GigPlayer/GigPlayer.cpp:100-120`),
which the render path calls (`plugins/GigPlayer/GigPlayer.cpp:507`). It only
ADDS to the shared buffer (`out[outOffset + i] += mix[i]`), never overwrites
it. The function is exported (`extern "C"`, `PLUGIN_EXPORT`) so the regression
test can drive the no-progress path with an injected resampler result; it is
the very function the render path uses, not a copy of it.

## Reachability (why the test needs a seam)

With libsamplerate 0.2.2 (the library this build links - `CMakeCache.txt`:
`SampleRate_DIR:PATH=/usr/lib/cmake/SampleRate`) the branch cannot be reached
through the public render path today, because
`AudioResampler::process()` throws whenever libsamplerate reports an error
(`src/core/AudioResampler.cpp:95-98`) and libsamplerate only reports "no
progress" *together with* an error (`SRC_ERR_BAD_SRC_RATIO` for a ratio
outside 0.0001 .. 256).

Measured, not assumed: `logs/probe-src-no-progress.cpp` replays exactly the
`playImpl()` call pattern (a 256-frame input view that shrinks by
`input_frames_used` and is refilled when empty, a 256-frame output view that
shrinks by the frames actually mixed) over the whole legal ratio range for all
four converters:

```
$ g++ -O2 -o probe-src-no-progress probe-src-no-progress.cpp -lsamplerate   # EXIT=0
$ ./probe-src-no-progress                                                   # EXIT=0
no-progress results: 19872, of them without an error: 0
```

So the defect is a latent one today, and the fix removes it at the cause. The
regression test therefore drives the exported mixing step with an injected
`(0, 0)` resampler result - a deterministic seam, no timing and no race.

## Evidence

The affected CTest family is `PluginPortsMigrationTest` (this tree has no
`^GigPlayer$` target; the GIG player cases live in this binary: `initTestCase`
loads the migrated module, `gigPlayerAcceptsMidiNoteZero`,
`gigplayerBidirectionalLoopReflectsAtExclusiveEnd`,
`gigplayerSingleFrameLoopRepeatsFrame`, `gigPatchDialogPreviewsSelectedProgram`
and the new `gigplayerNoProgressFallbackKeepsEarlierSamples`). The family
executed **48 cases** in the green run: 45 passed, 2 skipped, 1 failed.

The one failure is **pre-existing and unrelated**: `nonRealtimeLadspaEffectsAreInvalid`
needs the SWH Decimator LADSPA plugin installed on the machine
("SWH Decimator test fixture was not loaded"); it fails identically on the
untouched tree (`logs/test-1.log`, baseline before any change: 44 passed,
1 failed, 2 skipped). It is why the family exit code is 8 in every run.

| Check | Exact command (from `build/tests`) | Exit |
| --- | --- | ---: |
| RED - the new case, original code | `./PluginPortsMigrationTest gigplayerNoProgressFallbackKeepsEarlierSamples > LOGS/test-3.log 2>&1` | 1 |
| GREEN - the new case, with the fix | `./PluginPortsMigrationTest gigplayerNoProgressFallbackKeepsEarlierSamples > LOGS/test-4.log 2>&1` | 0 |
| GREEN - the whole family | `ctest -R '^PluginPortsMigrationTest$' --output-on-failure > LOGS/test-5.log 2>&1` | 8 |
| Reverse the product patch | `git apply -R LOGS/fix.patch` | 0 |
| RED - the new case, fix reverted | `./PluginPortsMigrationTest gigplayerNoProgressFallbackKeepsEarlierSamples > LOGS/test-6.log 2>&1` | 1 |
| RED - the whole family, fix reverted | `ctest -R '^PluginPortsMigrationTest$' --output-on-failure > LOGS/test-7.log 2>&1` | 8 |
| Restore the product patch | `git apply LOGS/fix.patch` | 0 |
| GREEN - the new case, fix restored | `./PluginPortsMigrationTest gigplayerNoProgressFallbackKeepsEarlierSamples > LOGS/test-8.log 2>&1` | 0 |
| GREEN - the whole family, fix restored | `ctest -R '^PluginPortsMigrationTest$' --output-on-failure > LOGS/test-9.log 2>&1` | 8 |
| Realtime safety family | `ctest -R '^RtSafety' --output-on-failure > LOGS/test-10.log 2>&1` | 0 |
| Gates (all of them) | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > LOGS/gate-1.log 2>&1` | 0 |

The RED assertion, verbatim from `LOGS/test-6.log` (the product patch reverted):

```
FAIL!  : PluginPortsMigrationTest::gigplayerNoProgressFallbackKeepsEarlierSamples()
          'shared[0].left() == 0.5f && shared[0].right() == -0.25f' returned FALSE.
          (the no-progress fallback erased the audio of the earlier samples: 0, 0)
   Loc: [../../tests/src/plugins/PluginPortsMigrationTest.cpp(1793)]
```

The product patch used for the reverse experiment is `LOGS/fix.patch`
(`git diff -- plugins/GigPlayer/GigPlayer.cpp`, 27 lines) - it contains the
behaviour change only, because the seam it drives was committed separately
(commit `84f96fcce`, behaviour preserving: the family result was identical
before and after it, `LOGS/test-1.log` vs `LOGS/test-2.log`).

Builds, all before the corresponding test runs, all with exit 0:

| Log | Exact command |
| --- | --- |
| `build-1.log` | `cmake --build build --target zene PluginPortsMigrationTest` |
| `build-2.log` | `cmake --build build --target gigplayer` |
| `build-3.log` | `cmake --build build --target gigplayer PluginPortsMigrationTest` |
| `build-4.log` | `cmake --build build --target gigplayer` |
| `build-5.log` | `cmake --build build --target PluginPortsMigrationTest` |
| `build-6.log` | `cmake --build build --target gigplayer PluginPortsMigrationTest` |
| `build-7.log` | `cmake --build build --target gigplayer PluginPortsMigrationTest` (fix reverted) |
| `build-8.log` | `cmake --build build --target gigplayer PluginPortsMigrationTest` (fix restored) |

All build, test, gate and probe logs are under
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-88-port.r2/logs`.

## Siblings and callers

* `gigplayerMixResampledBlock()` has exactly one caller - the render loop at
  `plugins/GigPlayer/GigPlayer.cpp:507`. The other path into the same
  function, the export itself, is the seam the test uses; both run the same
  code.
* The same "no progress" shape exists in `src/core/Sample.cpp:140-144` and
  `plugins/Sf2Player/Sf2Player.cpp:908-912`, and both are **correct** there:
  their destination is the single voice's own buffer (`Sample::play()` writes
  into the per-voice working buffer that the caller then mixes -
  `src/core/SamplePlayHandle.cpp:512`, `plugins/Patman/Patman.cpp:209`,
  `plugins/AudioFileProcessor/AudioFileProcessor.cpp:158`; the SF2 player
  overwrites its own instrument buffer with `fluid_synth_write_float` at
  `plugins/Sf2Player/Sf2Player.cpp:891`). Only the GIG player shares one
  buffer between all samples of all notes, so only there the clear was wrong.
  `src/core/SampleBufferResample.cpp:49` already breaks without clearing.
* The type that flows through the seam is `f_cnt_t` (`std::uint64_t`,
  `include/LmmsTypes.h:43`) in and out, the same type the render loop uses for
  `frames` and `framesMixed`; `outFrames - outOffset` cannot underflow
  because the loop condition keeps `outOffset < outFrames`.

## Files committed

* `plugins/GigPlayer/GigPlayer.cpp` - the fix (and the named mixing step)
* `tests/src/plugins/PluginPortsMigrationTest.cpp` - the regression
* `tests/upstream-modifications.txt` - the ledger entries for both files
* `docs/qwen-fixes-2026-10/cluster-new-p12-88-port.md` - this report

Commits: `84f96fcce` (behaviour-preserving extraction of the mixing step, so
that the reverse experiment above reverses the *behaviour* only) and the fix
commit that carries this report.

## Self-check

1. **The regression test FAILS with the product change reverted and PASSES
   with it.** Reverted: `./PluginPortsMigrationTest gigplayerNoProgressFallbackKeepsEarlierSamples`
   -> EXIT 1 (`LOGS/test-6.log`, the assertion quoted above). Restored: the
   same command -> EXIT 0 (`LOGS/test-8.log`). The family run
   `ctest -R '^PluginPortsMigrationTest$'` goes from 2 failures with the patch
   reverted (`LOGS/test-7.log`) to 1 - the pre-existing LADSPA fixture
   failure - with it restored (`LOGS/test-9.log`).
2. **The test is deterministic.** It injects the resampler result
   (`inputFramesUsed = 0`, `outputFramesGenerated = 0`) into the exported
   mixing step; there is no wall-clock timing, no sleep, no thread and no
   race, and no filesystem or audio-device dependency. The values are exact
   binary fractions (0.5, 0.25, 0.125), so the comparisons are exact.
3. **The fix is at the cause and complete.** The cause is the shared-buffer
   clear in the no-progress fallback; it is gone
   (`plugins/GigPlayer/GigPlayer.cpp:104-111`), and the mixing step can only
   add to the shared buffer (`:113-117`). The single caller was updated
   (`:507`), the sibling implementations with the same shape were checked and
   are correct as they are (see above), and the frame count type is unchanged.
4. **No collateral damage.** The change is confined to the no-progress branch:
   a progressing resampler result takes exactly the same path as before (the
   same `framesToMix` is computed and added at the same offsets), so the
   rendered audio is bit-identical - the Part C behaviour comparison
   (`migratedPluginsPreserveBehaviour`) and the three GIG render regressions
   pass unchanged. No project, save file or setting is touched: the fix adds
   no parameter, no limit and no persisted value. The frames a sample does not
   fill stay silent because the buffer is zeroed once per period
   (`plugins/GigPlayer/GigPlayer.cpp:369`).
5. **Realtime, undo and commands.** The new function runs on the audio
   thread: it only adds floats into the caller's buffer - no allocation, no
   free, no lock, no wait, no logging, no Qt call (the `RtSafetySweep` and
   `RtSafetySelfTest` tests pass, `LOGS/test-10.log`). The function is a
   plain call, not a virtual dispatch, and it is `inline`-able. No command
   was added, so the control-command snapshot is untouched; no undo step
   exists for a render-path change and none was added.
6. **Verdict evidence (asked for ALREADY-FIXED / FALSE-POSITIVE only).** Not
   applicable - the verdict is FIXED. The contract the fix restores is the
   one documented on the function itself
   (`plugins/GigPlayer/GigPlayer.cpp:83-99`): the period buffer is shared by
   every sample of every note, so a sample may only add to it.
