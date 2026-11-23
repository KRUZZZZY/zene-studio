FIXED

# Cluster new-p16-7: audio past the song's limit is refused before it is fully decoded

## Finding and verdict

Luna's patrol (BUG 5): `src/core/ControlCommandsClipImportFile.cpp:172` (historical)
applied the 512 MiB limit to the **encoded** file, then decoded and stored the whole
sample buffer before the song-length check, so a small compressed file could expand
to many gigabytes before `validateImport` refused it.

Verdict: **REAL**. Before this branch's fixes:

- `src/core/ControlCommandsClipImportFile.cpp:187` (historical) called
  `SampleDecoder::decode(path)` after checking only `QFileInfo::size()` at `:182`;
  the decode allocates the frame count the file's header reports.
- `src/core/ImportDetection.cpp:128` (historical), reached from
  `LoopImportWarp.cpp:127` for `clip.import_file`, called `SampleDecoder::decode(path)`
  a **second** time in full, then analysed at most its bounded prefix.
- `validateImport` (`ControlCommandsClipImportFile.cpp:327` historical) compared the
  clip's length with `MaxSongLength` only after both decodes.

## Round-2 send-back and its fix

The round-1 preflight compared only `length > MaxSongLength`. A short compressed
file placed at `position = MaxSongLength` therefore passed the preflight, was fully
decoded, and was refused by the post-decode position-plus-length check. The
reviewer's observation is reproduced below (`test-1.log`: `full : 1`).

Fix: the requested tick position now goes into the preflight.
`prepareFile` (`src/core/ControlCommandsClipImportFile.cpp:250`) forwards it to
`prepareAudio` (`:202`), whose check at `:223` is the exact expression
`validateImport` uses at `:367` after decoding:

```
static_cast<qint64>(position) + length > static_cast<qint64>(MaxSongLength)
```

with the same refusal message, and `length` is computed by `clipLengthTicks`
(`:188`), the same function the post-decode `resultingLengthTicks` (`:348-352`)
calls. Nothing else about the preflight changed.

## The full fix

1. `LengthProbeSeconds = 1.0` (`ControlCommandsClipImportFile.cpp:82`).
   `prepareAudio` asks `SampleDecoder::decodeLimited(path, 1.0)` for the file's own
   length (`BoundedResult::totalFrames`/`window.sampleRate`) before any full decode.
   The detector's bounded analysis runs, `clipLengthTicks` computes the clip length
   at the requested position, and a clip that would end past the song is refused
   before the sample buffer exists.
2. `src/core/ImportDetection.cpp:125-158`: `analyseAudioFile` now uses
   `SampleDecoder::decodeLimited(path, bound)` instead of `decode(path)`. The
   estimators already read only the first `bound` seconds; `frames` and
   `durationSeconds` come from `BoundedResult::totalFrames`, the file's whole
   length. This removes the detector's second full allocation — the other half of
   the memory blow-up — and keeps the reported `detect.analyze` values identical.
3. The probe result is scoped (`:213-232`) so its one-second window is freed before
   the full decode; for an accepted import the detector runs once (`:245`).

A file that fits the song is still fully decoded: the clip needs its audio in
memory, and that contract is unchanged.

### Callers and siblings checked

- `prepareAudio` ← `prepareFile` ← `validateImport` ← the `clip.import_file`
  handler; both the GUI drop and the agent command use that one handler.
- `analyseAudioFile` callers, all run below: `ControlCommandsDetectApply.cpp:114`
  (`detect.apply`), `ControlCommandsDetect.cpp:119` (`detect.analyze`),
  `LoopImportWarp.cpp:127` (`clip.import_file`).
- `clip.*` siblings in the changed command file: `clip.source_get_state` only reads
  `loopwarp::clipStateJson(*sample)` from the resolved clip. Every other `clip.*`
  command is in another file and does not decode audio. No command was added or
  changed, so the commands snapshot is unchanged.
- No save format, project XML or undo payload changed.

## Test matrix

| Path | Covered by | Result |
|---|---|---|
| Normal audio import | existing audio slots; the accepted half of `anAudioFilePastTheSongEndIsRefusedBeforeItIsFullyDecoded` imports the same compressed FLAC where it fits | pass |
| Failure: compressed file longer than the song | `aLongCompressedFileIsRefusedBeforeItIsFullyDecoded` (refused, `InvalidArgs`, state unchanged) | pass |
| Failure: short file at the song's end (round 2) | `anAudioFilePastTheSongEndIsRefusedBeforeItIsFullyDecoded` at `position == MaxSongLength` | pass (full decodes 1 → 0) |
| Exhaustion: no full decode before the refusal | both tests use `SampleDecoder::decodeCallCountsForTesting().full == 0`, `limited > 0` | pass (2 → 0) |
| Reconfiguration: `follow_project` / `keep_unstretched` / preference | `LoopImportWarpTest`, `ControlLoopImportWarp` (the same `clipLengthTicks`) | pass |
| Detector values after the bounded decode | `ImportDetectionTest`, `ControlDetectCommands`, `BrowserFeaturesWindowTest` | pass |
| Audio-thread constraints | changed code is the control-thread command handler plus the import-time detector (`include/ImportDetection.h:32`); no allocation, lock, wait or log added to an audio path | reason checked |
| Supported-platform build | libsndfile FLAC writing is already used by `BrowserFeaturesWindowTest.cpp:71`; a missing FLAC encoder makes the test `QSKIP`, not fail; Qt/std only, no POSIX path assumption | pass |

The tests are deterministic: fixed 1000 Hz rate, fixed tempo where needed
(`MaxTempo` plus an RAII restore), a frame count computed from
`Engine::framesPerTick(rate)`, and the decoder's `LMMS_TESTING` call counters. No
sleeps, no wall-clock timing, no threads.

The two compressed-file tests live in the existing registered file
`tests/src/core/ClipImportFileTest.cpp` (`writeSilentFlac` at `:163`, tests at
`:430` and `:463`). An attempt to give them a new test target was refused by the
build guard (`bunny-guard: configuring is the parent's job`, exit 97), and the
file-length ratchet then flagged the file at 503 code lines. The round-1 short-file
success control was folded into the round-2 test's accepted half (same coverage:
a short compressed FLAC still imports and creates its track), which brings the file
back under the 500-code-line ratchet; `tests/file-length-gate.sh --check` exits 0
(`file-length-1.log`).

## Commands and exit codes (logs in the run's `logs/` folder)

RED, round-2 regression, before the position fix:

```
cmake --build build --target ClipImportFileTest -j 4 > logs/build-1.log 2>&1   EXIT=0
cd build/tests && ./ClipImportFileTest anAudioFilePastTheSongEndIsRefusedBeforeItIsFullyDecoded \
    > logs/test-1.log 2>&1                                                     EXIT=1
```

`test-1.log:6-7`: `Actual (SampleDecoder::decodeCallCountsForTesting().full) : 1` /
`Expected : 0`.

GREEN, after the position fix:

```
cmake --build build --target ClipImportFileTest -j 4 > logs/build-2.log 2>&1   EXIT=0
cd build/tests && ./ClipImportFileTest anAudioFilePastTheSongEndIsRefusedBeforeItIsFullyDecoded \
    > logs/test-2.log 2>&1                                                     EXIT=0
```

Full test binary on the consolidated tree:

```
cmake --build build --target ClipImportFileTest -j 4 > logs/build-7.log 2>&1   EXIT=0
cd build/tests && ./ClipImportFileTest > logs/test-7.log 2>&1                  EXIT=0
```

`test-7.log`: `Totals: 15 passed, 0 failed`.

Step 4b, narrow (this round's product patch, `logs/fix.patch`, reverted):

```
git diff -- src/core/ControlCommandsClipImportFile.cpp src/core/ImportDetection.cpp \
    > logs/fix.patch                                                           EXIT=0
git apply -R logs/fix.patch                                                    EXIT=0
cmake --build build --target ClipImportFileTest -j 4 > logs/build-8.log 2>&1   EXIT=0
cd build/tests && ./ClipImportFileTest anAudioFilePastTheSongEndIsRefusedBeforeItIsFullyDecoded \
    > logs/test-8.log 2>&1                                                     EXIT=1
git apply logs/fix.patch                                                       EXIT=0
cmake --build build --target ClipImportFileTest -j 4 > logs/build-9.log 2>&1   EXIT=0
cd build/tests && ./ClipImportFileTest anAudioFilePastTheSongEndIsRefusedBeforeItIsFullyDecoded \
    > logs/test-9.log 2>&1                                                     EXIT=0
```

Step 4b, whole fix (both product files against the pre-fix train commit
`21c2f3ee2e`, `logs/fix-full.patch`, reverted):

```
git diff 21c2f3ee2e -- src/core/ControlCommandsClipImportFile.cpp src/core/ImportDetection.cpp \
    > logs/fix-full.patch                                                      EXIT=0
git apply -R logs/fix-full.patch                                               EXIT=0
cmake --build build --target ClipImportFileTest -j 4 > logs/build-11.log 2>&1  EXIT=0
cd build/tests && ./ClipImportFileTest > logs/test-11.log 2>&1                 EXIT=2
git apply logs/fix-full.patch                                                  EXIT=0
cmake --build build --target ClipImportFileTest -j 4 > logs/build-12.log 2>&1  EXIT=0
cd build/tests && ./ClipImportFileTest > logs/test-12.log 2>&1                 EXIT=0
```

`test-11.log`: both compressed-file tests fail, `calls.full : 2` for the long file
and `full : 2` for the file at the song's end; `test-12.log`:
`Totals: 15 passed, 0 failed`.

Family plus the real binary:

```
cmake --build build --target zene ClipImportFileTest LoopImportWarpTest ImportDetectionTest \
    BrowserFeaturesWindowTest SampleDecoderLimitTest SampleDecoderShortReadTest -j 4 \
    > logs/build-13.log 2>&1                                                   EXIT=0
cd build/tests && ctest -R "ClipImportFileTest|LoopImportWarpTest|ImportDetectionTest|\
BrowserFeaturesWindowTest|SampleDecoderLimitTest|SampleDecoderShortReadTest|\
ControlLoopImportWarp|ControlDetectCommands" --output-on-failure > logs/test-13.log 2>&1  EXIT=0
```

`test-13.log`: `100% tests passed, 0 tests failed out of 8`.

Gates:

```
python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py \
    /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-new-p16-7 \
    > logs/gates-3.log 2>&1                                                    EXIT=0
bash tests/file-length-gate.sh --check > logs/file-length-1.log 2>&1           EXIT=0
```

`gates-3.log`: all eight rows PASS; `RESULT: PASS`.

## Self-check

- **Round-2 send-back addressed.** `prepareAudio` takes the requested `tick_t
  position` (`ControlCommandsClipImportFile.cpp:202`), `prepareFile` forwards
  `validateImport`'s position (`:250`, `:366`), and the preflight check at `:223` is
  the same `position + length > MaxSongLength` expression as the post-decode check
  at `:367`. The regression at `tests/src/core/ClipImportFileTest.cpp:463` places a
  short compressed FLAC at `MaxSongLength`: without the change it is fully decoded
  (`logs/test-1.log`, `full : 1`); with it the import makes zero full decodes.
- **The regression test FAILS with the product change reverted and PASSES with it.**
  Narrow: `logs/test-8.log` exits 1 (`full : 1`), `logs/test-9.log` exits 0. Whole
  fix: `logs/test-11.log` exits 2 (both compressed-file tests fail, `full : 2`),
  `logs/test-12.log` exits 0 (`15 passed`). Builds for both reversions and both
  restores exited 0.
- **The tests are deterministic.** Fixed 1000 Hz rate, fixed `MaxTempo` (RAII
  restore), frame count derived from `Engine::framesPerTick(rate)`, and the
  `LMMS_TESTING` decode-call counters (`resetDecodeCallCountsForTesting` /
  `decodeCallCountsForTesting`). No sleeps, timing or threads.
- **The fix is at the cause and complete.** Length comes from the decoder's bounded
  read before `decode()`, the detector no longer full-decodes, and the preflight and
  post-decode checks share `clipLengthTicks`. All `analyseAudioFile` callers were
  listed and exercised (`detect.analyze`, `detect.apply`, `clip.import_file`:
  `ControlDetectCommands`, `LoopImportWarpTest`, `ControlLoopImportWarp` pass). The
  command's sibling `clip.source_get_state` only reads clip state; no other `clip.*`
  command calls this code.
- **No collateral damage.** No save format or XML changed. The preflight refuses
  exactly what `validateImport` refuses: the same position-plus-length expression,
  the same `clipLengthTicks` (including the `follow_project` tempo rescale), so a
  long source that the tempo ratio shrinks to fit is still accepted, and a file that
  fits where it is placed still imports (asserted by the accepted half of the
  round-2 test). `ImportDetectionTest`, `BrowserFeaturesWindowTest`,
  `SampleDecoderLimitTest`, `SampleDecoderShortReadTest`, `LoopImportWarpTest`,
  `ControlDetectCommands` and `ControlLoopImportWarp` all pass (`test-13.log`,
  `100% tests passed ... out of 8`).
- **ALREADY-FIXED / FALSE-POSITIVE:** not claimed; verdict is FIXED.
- **Sibling commands in the command files you changed** (`clip.*`):
  `clip.source_get_state` is registered in `ControlCommandsClipImportFile.cpp` and
  reads only `loopwarp::clipStateJson(*sample)` from the resolved clip; it decodes no
  file and is unaffected. Every other `clip.*` id in the snapshot is implemented in
  other files and never calls `prepareAudio`, `analyseAudioFile` or
  `loopwarp::analyse`.
- **The pre-submit check lists `app.*` and `arrangement.*` siblings.** Those command
  files are part of the branch's train history since `FIX-BASE` (`050/m0-train-35`),
  not of this cluster: this work touches only `ControlCommandsClipImportFile.cpp`
  (`clip.*`) and `ImportDetection.cpp`. The `app.*`/`arrangement.*` commands neither
  import files nor decode audio, and the one shared function changed
  (`analyseAudioFile`) is exercised for its own callers by the passing
  `ControlDetectCommands` run.
- **Gate note.** `feature_check.py` passes all eight rows (`gates-3.log`). The
  file-length ratchet flagged the test file at 503 code lines after the first
  round-2 attempt; it is 495 physical lines now with the duplicate short-file block
  folded into the round-2 success half, and
  `bash tests/file-length-gate.sh --check` exits 0 (`file-length-1.log`). No gate
  limit or baseline was changed.

## Other findings

- An accepted import can still need a large buffer: up to `MaxSongLength` ticks of
  audio (about 6 GB of stereo float at 44.1 kHz and the default tempo). That is the
  pre-existing contract for a clip the song can show; only the refusal path was in
  scope here.
- `SampleDecoder::decodeSampleSF` still sizes its allocation from the WAV header
  before checking the read length, so a header that lies about its frame count
  within the song limit can still allocate that declared size before the short read
  is refused. The import refuses headers past the song limit before reaching it.
- DrumSynth `.ds` files are synthesised whole by `decodeLimited` before the window is
  cut, so their memory follows the synthesised hit, not the probe window. They are
  not a compressed container and were outside this finding.
