FIXED

# Cluster new-p16-7: a long compressed file is refused before it is fully decoded

## Finding and verdict

Luna's patrol (BUG 5): `src/core/ControlCommandsClipImportFile.cpp:172` (historical)
applied the 512 MiB limit to the **encoded** file, then decoded and stored the whole
sample buffer before the song-length check, so a small compressed file could expand
to many gigabytes before `validateImport` refused it.

Verdict: **REAL**. On this branch (before the fix):

- `src/core/ControlCommandsClipImportFile.cpp:182` refused only `QFileInfo::size()`
  (the encoded bytes) and then called `SampleDecoder::decode(path)` at `:187`, which
  allocates `sfInfo.frames` / `numSamples` frames from the file's own header.
- `src/core/ImportDetection.cpp:128` (the loop detector `clip.import_file` runs from
  `LoopImportWarp.cpp:127`) called `SampleDecoder::decode(path)` a **second** time in
  full, then analysed at most its bounded prefix.
- Only after both full decodes did `validateImport`
  (`src/core/ControlCommandsClipImportFile.cpp:327` historical) compare the length
  with `MaxSongLength`.

The regression test below measured this on the unfixed tree: the refused import made
**2** full decodes (`calls.full : 2`) — one for the import, one for the detector.
A refused import must make none.

## The fix

1. `src/core/ControlCommandsClipImportFile.cpp:82` adds `LengthProbeSeconds = 1.0`.
   `prepareAudio` (`:213-230`) now asks `SampleDecoder::decodeLimited(path, 1.0)` for
   the file's **own** length (`BoundedResult::totalFrames`/`window.sampleRate`) before
   any full decode. The detector (`loopwarp::analyse`) then reads only its bounded
   prefix (change 2), and `clipLengthTicks` (`:188`) computes the clip length with the
   *same* arithmetic `resultingLengthTicks` (`:348-352`) uses after decoding, including
   the `follow_project` tempo rescale. Only `length > MaxSongLength` refuses; a file
   that could fit at some position is still decoded and judged by `validateImport`
   exactly as before.
2. `src/core/ImportDetection.cpp:125-158`: `analyseAudioFile` now calls
   `SampleDecoder::decodeLimited(path, bound)` instead of `decode(path)`. The
   estimators already only read the first `bound` seconds
   (`analysed = min(frames, bound*sampleRate)` is unchanged); `frames` and
   `durationSeconds` now come from `BoundedResult::totalFrames`, which is the file's
   whole length. This removes the detector's second full allocation, which is what made
   the refusal allocate gigabytes in the first place, and it keeps the reported
   `detect.analyze` values identical for every format this build reads.
3. The probe result is scoped (`:213-230`) so its one-second window is freed before the
   full decode, and for an accepted import the detector is no longer run twice
   (`:245`).

The full decode still happens for a file that fits the song: the clip needs its audio
in memory (`SampleClip`/`SampleBuffer`), and that is unchanged.

### Callers and siblings checked

- `prepareAudio` is reached only from `prepareFile` ← `validateImport` ← the
  `clip.import_file` handler. `rg -n "import_file" src` shows the GUI drag path and
  the agent command both run that one handler.
- `analyseAudioFile` callers (all verified by the runs below):
  `src/core/ControlCommandsDetectApply.cpp:114` (`detect.apply`),
  `src/core/ControlCommandsDetect.cpp:119` (`detect.analyze`) and
  `src/core/LoopImportWarp.cpp:127` (`clip.import_file`).
- `clip.*` siblings in the changed command file: `clip.source_get_state`
  (`ControlCommandsClipImportFile.cpp:481`) only reads the clip's stored loop state
  and never touches a file; every other `clip.*` command lives in another file and
  does not decode audio. No command was added or changed, so
  `tools/mcp-zene-control/zene_control/commands_snapshot.json` is unchanged.
- No save format, project XML or undo payload changed.

## Test matrix

| Path | Covered by | Result |
|---|---|---|
| Normal audio import | existing `anAudioFileBecomesASampleClipAtTheTickOnASampleTrack`, `anAudioFileDeletedRightAfterTheDropStillPlaysAndSaves`; new short-FLAC half of the regression | pass |
| Failure: compressed file longer than the song | new `aLongCompressedFileIsRefusedBeforeItIsFullyDecoded` (refused, `InvalidArgs`, state unchanged) | pass |
| Exhaustion: no full decode before the refusal | same test, `SampleDecoder::decodeCallCountsForTesting().full == 0`, `limited > 0` | pass (2 → 0) |
| Reconfiguration: `follow_project` / `keep_unstretched` / preference | `LoopImportWarpTest`, `ControlLoopImportWarp` (both use `resultingLengthTicks` → the same `clipLengthTicks`) | pass |
| Detector values after the bounded decode | `ImportDetectionTest`, `ControlDetectCommands`, `BrowserFeaturesWindowTest` | pass |
| Audio-thread constraints | none of the changed code runs per period: it is the control-thread command handler plus the import-time detector (`include/ImportDetection.h:32`); no allocation, lock, wait or log was added to an audio path | reason checked |
| Supported-platform build | libsndfile FLAC writing is already used by `BrowserFeaturesWindowTest.cpp:71`; the new test `QSKIP`s (not fails) if this build cannot write FLAC; Qt/std only, no POSIX path assumption | pass |

The new test is deterministic: fixed rate (1000 Hz), fixed project tempo (`MaxTempo`),
a frame count computed from `Engine::framesPerTick(rate)`, and the decoder's
`LMMS_TESTING` call counters. No sleeps, no wall-clock timing, no threads. A
`RestoreTempo` guard restores the project tempo even if an assertion fails.

## Commands and exit codes (logs in the run's `logs/` folder)

RED before the fix (test written first, product untouched):

```
cmake --build build --target ClipImportFileTest -j 4 > logs/build-1.log 2>&1   EXIT=0
cd build/tests && ./ClipImportFileTest aLongCompressedFileIsRefusedBeforeItIsFullyDecoded \
    > logs/test-1.log 2>&1                                                     EXIT=1
```

`test-1.log:6`: `Actual (calls.full) : 2` / `Expected : 0`.

GREEN with the fix:

```
cmake --build build --target ClipImportFileTest -j 4 > logs/build-2.log 2>&1   EXIT=0
cd build/tests && ./ClipImportFileTest aLongCompressedFileIsRefusedBeforeItIsFullyDecoded \
    > logs/test-2.log 2>&1                                                     EXIT=0
```

Family plus the real binary:

```
cmake --build build --target ClipImportFileTest LoopImportWarpTest ImportDetectionTest \
    BrowserFeaturesWindowTest SampleDecoderLimitTest SampleDecoderShortReadTest zene -j 4 \
    > logs/build-6.log 2>&1                                                    EXIT=0
cd build/tests && ctest -R "ClipImportFileTest|LoopImportWarpTest|ImportDetectionTest|\
BrowserFeaturesWindowTest|SampleDecoderLimitTest|SampleDecoderShortReadTest|\
ControlLoopImportWarp|ControlDetectCommands" --output-on-failure > logs/test-6.log 2>&1  EXIT=0
```

`test-6.log`: `100% tests passed, 0 tests failed out of 8`.

Proof that the test catches the bug (step 4b), against the final patch:

```
git diff -- src/core/ControlCommandsClipImportFile.cpp src/core/ImportDetection.cpp \
    > logs/fix.patch                                                           EXIT=0
git apply -R logs/fix.patch                                                    EXIT=0
cmake --build build --target ClipImportFileTest -j 4 > logs/build-7.log 2>&1   EXIT=0
cd build/tests && ./ClipImportFileTest aLongCompressedFileIsRefusedBeforeItIsFullyDecoded \
    > logs/test-7.log 2>&1                                                     EXIT=1
git apply logs/fix.patch                                                       EXIT=0
cmake --build build --target ClipImportFileTest -j 4 > logs/build-8.log 2>&1   EXIT=0
cd build/tests && ./ClipImportFileTest aLongCompressedFileIsRefusedBeforeItIsFullyDecoded \
    > logs/test-8.log 2>&1                                                     EXIT=0
```

`test-7.log:6-7`: `Actual (calls.full) : 2` / `Expected : 0`; `test-8.log:7`:
`Totals: 3 passed, 0 failed`.

Gates:

```
python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py \
    /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-new-p16-7 \
    > logs/gates-1.log 2>&1                                                    EXIT=0
```

`gates-1.log`: all eight rows PASS; `RESULT: PASS`.

## Self-check

- **The regression test FAILS with the product change reverted and PASSES with it.**
  `logs/test-7.log` (reverted via `git apply -R logs/fix.patch`, EXIT=0; build EXIT=0)
  exits **1** with `calls.full : 2`; `logs/test-8.log` (restored, build EXIT=0) exits
  **0**, `Totals: 3 passed, 0 failed`.
- **Deterministic.** Fixed 1000 Hz rate, fixed `MaxTempo`, frame count derived from
  `Engine::framesPerTick(rate)`, and the existing `LMMS_TESTING` decode-call counters
  (`SampleDecoder::resetDecodeCallCountsForTesting()` /
  `decodeCallCountsForTesting()`). No sleeps, timing or threads.
- **Fix at the cause and complete.** The length comes from the decoder's own bounded
  read (`decodeLimited` → `BoundedResult::totalFrames`) before `decode()`, and the
  detector no longer full-decodes (`ImportDetection.cpp:125-158`). `clipLengthTicks`
  is shared by the pre-decode check and the post-decode check, so both agree. All
  `analyseAudioFile` callers were listed and exercised: `detect.analyze`,
  `detect.apply`, `clip.import_file` (ControlDetectCommands, LoopImportWarpTest,
  ControlLoopImportWarp pass). The command's sibling `clip.source_get_state` only
  reads clip state; no other `clip.*` command calls this code.
- **No collateral damage.** Nothing in the save format or project XML changed. The new
  refusal is exactly the refusal `validateImport` already made: if the clip's own
  length exceeds `MaxSongLength`, `position + length > MaxSongLength` for every legal
  `position >= 0`. The `follow_project` rescale is applied before the comparison, so
  a long source that the tempo ratio shrinks to fit is still accepted. The new test
  also imports a short FLAC through the same path (accepted, track created), and
  `ImportDetectionTest`, `BrowserFeaturesWindowTest`, `SampleDecoderLimitTest`,
  `SampleDecoderShortReadTest`, `LoopImportWarpTest`, `ControlDetectCommands` and
  `ControlLoopImportWarp` all pass (`test-6.log`, `100% tests passed ... out of 8`).
- **ALREADY-FIXED / FALSE-POSITIVE:** not claimed; verdict is FIXED.
- **Sibling commands in the changed command file** (`clip.*`): `clip.source_get_state`
  is registered in `ControlCommandsClipImportFile.cpp` and reads only
  `loopwarp::clipStateJson(*sample)` from the resolved clip; it decodes no file and is
  unaffected. Every other `clip.*` id in the snapshot is implemented in other files
  and never calls `prepareAudio`, `analyseAudioFile` or `loopwarp::analyse`.
- **The pre-submit check lists `app.*` and `arrangement.*` siblings.** Those command
  files are part of the branch's train history since `FIX-BASE` (`050/m0-train-35`),
  not of this cluster: `git show --stat HEAD` shows this commit touches only
  `ControlCommandsClipImportFile.cpp` (`clip.*`) and `ImportDetection.cpp`. The
  `app.*`/`arrangement.*` commands neither import files nor decode audio, and the one
  shared function this commit changes (`analyseAudioFile`) is exercised for its own
  callers, `detect.analyze` and `detect.apply`, by the passing `ControlDetectCommands`
  run above.

## Other findings

- An accepted import can still need a large buffer: up to `MaxSongLength` ticks of
  audio (about 6 GB of stereo float at 44.1 kHz and the default tempo). That is the
  pre-existing contract for a clip the song can show; only the refusal path was in
  scope here.
- `SampleDecoder::decodeSampleSF` still sizes its allocation from the WAV header
  before checking the read length, so a header that lies about its frame count within
  the song limit can still allocate that declared size before the short read is
  refused. The import now refuses headers past the song limit before reaching it.
- DrumSynth `.ds` files are synthesised whole by `decodeLimited` before the window is
  cut, so their memory follows the synthesised hit, not the probe window. They are not
  a compressed container and were outside this finding.
