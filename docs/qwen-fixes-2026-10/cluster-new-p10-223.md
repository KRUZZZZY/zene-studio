FIXED

# cluster new-p10-223: source WAV metadata validation

The mastering source loader used to trust whatever `SF_INFO` libsndfile returned:
a source with the wrong channel count, a negative frame count, a sample rate
outside the render path's window, or a frame count that overflowed the byte/frame
arithmetic could reach the meter and the candidate WAV headers. The fix validates
the header, bounds the sample count, and reads the source in fixed-size chunks so
memory is bounded by the chunk size rather than by a hostile frame count. It is a
job-thread change only.

## Evidence (final tree)

- `src/core/MasteringJob.cpp:135` `validateSourceInfo`: refuses a non-stereo source
  (`info.channels != DEFAULT_CHANNELS`), a negative `info.frames`, and a rate
  outside `MinSourceRate..MaxSourceRate`.
- `src/core/MasteringJob.cpp:345` `checkedSampleCount`: refuses
  `channels == 0`, `frames > maxSourceFrames`, and
  `frames > SIZE_MAX / channels` (multiplication overflow), so the product
  `frames * channels` cannot wrap.
- `src/core/MasteringJob.cpp:160` `readChunks`: reads `SourceChunkFrames` frames at
  a time through one fixed buffer, appends to the source vector, and fails on any
  short read (`read != want`) - mastering a prefix is not mastering the mix.
- `src/core/MasteringJob.cpp:360` `readSourceMix`: opens with `SndFileHandle`, then
  calls `validateSourceInfo` -> `checkedSampleCount` -> `readChunks`; the
  declarations are at `include/MasteringJob.h:152` and `:160`.
- The rate window is the render path's own: `src/core/main.cpp:1099`
  (`sr >= 44100 && sr <= 192000`) and the export-preset schema at
  `src/core/ControlCommandsExportPresets.cpp:231-241`
  (`integerProperty(44100, 192000)`). The loader now refuses the same window
  before a source reaches the meter or a candidate header.

## libsndfile frame clamping

`SF_INFO::frames` is libsndfile's value. For a WAV, a header cannot actually make
libsndfile allocate the promised frame count or hand back more than the data chunk
holds: `sf_open` derives the frame count from the data chunk and clamps it, and a
truncated file short-reads (covered by the short-read test). The huge-frame cases
(2^63, 2^32, cap+1) therefore cannot be produced through a real WAV header, and
are covered only through `checkedSampleCount` directly. That is deliberate: the
cap, the overflow check and the chunked read make the allocation bound
independent of what the library reports.

## Commands and exits (copied from logs)

| step | command | exit |
| --- | --- | --- |
| p2 RED | `cd build/tests && ctest -R MasteringTest` with the fix reverted (`logs/test-red-2.log`) | 8 - 27 passed, 7 failed |
| p2b RED | same, after the overflow row was added (`logs/test-unfixed.log`) | 8 - 30 passed, 7 failed |
| p3 GREEN | fix restored (`logs/test-green-2.log`) | 0 - 100% passed |
| p3b mutation | `MinSourceRate=44099` (`logs/test-mut-min-rate.log`) | 8 - red row 44099 |
| p3b mutation | `MaxSourceRate=192001` (`logs/test-mut-max-rate.log`) | 8 - red row 192001 |
| p3b mutation | drop `frames > maxSourceFrames` (`logs/test-mut-drop-cap.log`) | 8 - red row cap+1 |
| p4 mutation | drop the `SIZE_MAX/channels` clause, no new row (`logs/test-mut-drop-overflow.log`) | 0 - SURVIVED |
| p4 GREEN | add the "multiplication overflow" row (`logs/test-new-row.log`) | 0 - 100% passed |
| p4 mutation | drop the clause, with the new row (`logs/test-mut-drop-overflow-new-row.log`) | 8 - red row multiplication overflow |
| p4 unfixed | fix reverted (`logs/test-unfixed.log`) | 8 - 30 passed, 7 failed |
| p4 refixed | fix restored (`logs/test-refixed.log`) | 0 - 100% passed |
| final unfixed | `git apply logs/unfix.patch` then build (`logs/test-unfixed-final.log`) | apply 0, ctest 8 - 14 passed, 8 failed |
| final fixed | `git apply -R logs/unfix.patch`, `git diff HEAD` empty, rebuild, `ctest -R Mastering` (`logs/test-fixed-final.log`) | apply 0, diff 0, ctest 0 - 10/10 |

## Notes on method

- The brief's literal `git apply -R fix.patch` was replaced by a behaviour-only
  reversal (`logs/unfix.patch`, `src/core/MasteringJob.cpp` only). The fix
  introduces the `readSourceMix`/`checkedSampleCount` seams that the tests call;
  reversing the whole fix would delete those declarations and the tests would not
  compile, so the red run could not be produced.
- The short-read, chunk-boundary, valid, channel and zero-frame tests pin
  *preserved* behaviour and are expected to pass on the unfixed code. Only the
  rate-window rows and the huge-count rows go red; the final red log shows exactly
  those 8 rows.
- The first whole-family ctest run failed `MasteringSequenceTest` because
  `~/.cache` is read-only in this environment. That is environmental: rerunning
  with `XDG_CACHE_HOME` redirected to the log folder passes (the redirect is in
  the final run and was removed afterwards).
- Realtime: the loader runs on the mastering job thread; nothing it does touches
  the audio thread, and it issues no control commands. No concurrency change.
- TSan was not run locally: there is no concurrency change in this fix.
- Test organisation: the source-mix tests were split from `MasteringTest.cpp`
  into `MasteringTestSourceMix.cpp` to stay under the 500-code-line gate (the
  original file was already long and the new file would have been a new
  violation). The target is named under the `MasteringTest` prefix so
  `ctest -R MasteringTest` exercises both binaries, and both manifests and
  `tests/CMakeLists.txt` are updated.

## Other findings

- The rate window is stricter than the old loader: sources outside
  44100..192000 Hz are now refused. That window is the render path's own
  (`main.cpp:1099`), and such a source cannot come from this render path, but a
  project assembled by other means with an out-of-window source would now fail
  instead of being measured at the wrong rate.
- The overflow mutation initially survived because the cap row masked it; the
  explicit "multiplication overflow" row now kills it.

## Self-check

- **Regression test red/green.** With the product change reversed, the final run
  exits 8 (14 passed, 8 failed: the five out-of-window rates and the three
  huge/overflow counts, `logs/test-unfixed-final.log`); restored, `ctest -R
  Mastering` exits 0 (10/10, `logs/test-fixed-final.log`).
- **Deterministic.** No wall-clock timing or sleeps: fixtures are written to a
  temporary directory, out-of-window rates are injected by patching the WAV
  header, and huge counts are passed straight to `checkedSampleCount`. The same
  rows fail/pass on every run.
- **Cause and completeness.** All validation lives in the single loader path
  (`readSourceMix` -> `validateSourceInfo`/`checkedSampleCount`/`readChunks`),
  used by `loadSourceMix`; the other callers of the loader keep the same
  contract because the valid, chunk-boundary, channel and zero-frame rows pass.
- **No collateral damage.** Valid stereo, chunk-boundary, channel-count,
  zero-frame and short-read cases pass on both the unfixed and fixed code; saved
  projects are untouched (no format change). The only behaviour change is the
  rate window noted under Other findings.
- **Verdict.** FIXED, not ALREADY-FIXED or FALSE-POSITIVE, so the code/contract
  item does not apply.
- **Sibling commands.** This packet changes no command file and no command
  signature: the loader is internal to `MasteringJob`, reached by
  `mastering.run`. The command surface that passes a source rate keeps its
  contract (`ControlCommandsExportPresets` already bounds the same
  44100..192000 window at `src/core/ControlCommandsExportPresets.cpp:231-241`),
  and no `app.*`, `arrangement.*` or other sibling command reads the loader, so
  none is affected.
