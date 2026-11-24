FIXED

# Cluster new-p16-7-port: a long compressed import is refused before it is fully decoded

Finding: "Port: Large compressed audio can exhaust memory before the import is refused".

## Verdict: REAL (on this branch, before the fix)

This is a port job. The fix was accepted as `bugs/qwen-new-p16-7` (commits `528ce8f86e`, `6086a4aa32`, `9065852f92`). I re-applied its product and test changes by hand with `git apply` of a diff of that branch against the merge base `21c2f3ee2e`. I did not merge, cherry-pick or rebase. The diff applied cleanly (`git apply --check` EXIT=0), so the code on this branch was still the pre-fix code.

Evidence that the bug was present here (pre-fix tree):
- `src/core/ControlCommandsClipImportFile.cpp:182` checked only `QFileInfo(path).size() > MaxAudioFileBytes` (512 MiB, line 77). That bounds the encoded file, not the audio it decodes to.
- `src/core/ControlCommandsClipImportFile.cpp:187` then called `SampleDecoder::decode(path)`, which allocates the whole decoded buffer (`src/core/SampleDecoder.cpp:144`, `:156`).
- `src/core/ImportDetection.cpp:128` (via `LoopImportWarp.cpp:127`) decoded the whole file a second time.
- The song-length refusal in `validateImport` came only after both decodes.
- Test seam proof: with the new tests and without the fix, `SampleDecoder::decodeCallCountsForTesting().full` was 2 (expected 0). See `test-red-1.log` and `test-revert-1.log`.

## The fix (product files)

- `src/core/ControlCommandsClipImportFile.cpp`: `prepareAudio` first calls `SampleDecoder::decodeLimited(path, 1.0)` (`LengthProbeSeconds`). That reports the file's own length without allocating it. A new shared helper `clipLengthTicks` turns the length into clip ticks (the same arithmetic `resultingLengthTicks` uses after decoding). `position + length > MaxSongLength` is refused with the same message `validateImport` uses, before any full decode. `prepareFile` now passes the position down. A file that fits is still fully decoded once. The detector runs once.
- `src/core/ImportDetection.cpp`, `include/ImportDetection.h`: `analyseAudioFile` takes `AnalysisDecode` (default `ValidateWholeFile`, same behaviour as before). `PrefixOnly` uses `decodeLimited(path, bound)` and reports the real total length. The default keeps `detect.analyze` (`ControlCommandsDetect*.cpp`) unchanged, so those callers still validate the whole file.
- `src/core/LoopImportWarp.cpp`: `loopwarp::analyse` (import preflight only) passes `PrefixOnly`.

No audio-thread code is touched. No control command is added. No file under `src/gui` or `src/core/Song*`, `*Record*`, `*Undo*`, `*Transport*`, `Take*`, `AudioEngine*` changed. All four product files were already in `tests/fork-sources.txt`; the registration gates pass.

## Regression tests (`tests/src/core/ClipImportFileTest.cpp`)

- `aLongCompressedFileIsRefusedBeforeItIsFullyDecoded`: a silent FLAC longer than the whole song (tiny on disk) is refused with InvalidArgs, state unchanged, `full` decode count 0 and `limited` > 0.
- `anAudioFilePastTheSongEndIsRefusedBeforeItIsFullyDecoded`: a short FLAC at `position = MaxSongLength` is refused before the full decode. The same file still imports where it fits (`full` count 1).
- `tests/src/core/SampleDecoderShortReadTest.cpp`: `analysisRefusesAFlacWithAudioButMissingDeclaredFrames` guards that the default whole-file analysis still refuses a FLAC with a missing tail (passes before and after).
No timing, sleeps or races: the decoder's own call counters are used.

## Commands and results (logs in /home/kruzzzzy/.config/kads/zene/state/runs/new-p16-7-port.r4/logs)

| Step | Command | EXIT |
| --- | --- | --- |
| apply check | `git apply --check orig-full.patch` | 0 |
| RED build (tests only) | `cmake --build build -j4 --target ClipImportFileTest SampleDecoderShortReadTest > build-red-1.log 2>&1` | 0 |
| RED run | `cd build/tests; ctest -R 'ClipImportFileTest\|SampleDecoderShortReadTest' --output-on-failure > test-red-1.log 2>&1` | 8 (ClipImportFileTest fails at ClipImportFileTest.cpp:456 and :481; SampleDecoderShortReadTest passes) |
| GREEN build | `cmake --build build -j4 --target zene ClipImportFileTest SampleDecoderShortReadTest ImportDetectionTest LoopImportWarpTest > build-green-1.log 2>&1` | 0 |
| GREEN run | `ctest -R 'ClipImportFile\|ImportDetection\|LoopImportWarp\|SampleDecoder\|Detect' --output-on-failure > test-green-1.log 2>&1` | 8, only because 5 matched tests were not built (ChordDetectTest, SfizzLeakDetectorTest, SampleDecoderOggChannelTest, SampleDecoderOggStateTest, SampleDecoderLimitTest: "Not Run"); 0 failures. Passed: ClipImportFileTest, LoopImportWarpTest, ImportDetectionTest, SampleDecoderShortReadTest, ControlDetectCommands, ControlLoopImportWarp |
| 4b revert | `git diff -- <4 product files> > fix.patch && git apply -R fix.patch` | 0 |
| 4b rebuild (reverted) | `cmake --build build -j4 --target zene ClipImportFileTest > build-revert-1.log 2>&1` | 0 |
| 4b run (reverted) | `ctest -R '^ClipImportFileTest$' --output-on-failure > test-revert-1.log 2>&1` | 8 (both new tests FAIL, `full` = 2, expected 0) |
| 4b reapply | `git apply fix.patch` | 0 |
| 4b rebuild (fix) | `cmake --build build -j4 --target zene ClipImportFileTest > build-reapply-1.log 2>&1` | 0 |
| 4b run (fix) | `ctest -R '^ClipImportFileTest$' --output-on-failure > test-reapply-1.log 2>&1` | 0 (100% passed) |
| gates | `python3 .../feature_check.py <worktree> > feature-check-1.log 2>&1` | 0, RESULT: PASS (all 8 rows PASS) |

The 5 "Not Run" siblings (the Ogg/limit decoder tests) were not built or run by me. I did not change `SampleDecoder.cpp`, so I judged them unaffected, but that is unverified.

## Limits of the evidence

- Memory use was not measured; the proof is the decode-call counter, which shows the full decode does not happen before the refusal.
- A file that fits within the song limit is still fully decoded (the clip needs the audio). The song limit, not a byte limit, bounds that.
- TSan does not run on this box; the change adds no threading, and the import runs on the command-handler path as before.

## Other findings

- `detect.analyze` (`ControlCommandsDetect.cpp`, `ControlCommandsDetectApply.cpp`) still does a whole-file validating decode by design (`ValidateWholeFile` default); a very long compressed file passed to those commands still allocates its full decode. Not changed here.

Delegation: Haiku ran the RED build and test, the GREEN build and test, and the 4b revert/rebuild/reapply sequence. I checked its exit codes and failure lines against the logs.
