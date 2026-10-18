# Cluster new-p10-342: embedded sample rate validation

**Verdict: BLOCKED.** The bug fix is implemented and its regression is red without the fix and green with it. The required complexity and file-length check gates both fail on unrelated existing branch findings, so the repository gate requirement remains unmet. Neither gate baseline was changed.

## Finding and evidence

- `src/core/SampleClip.cpp:826` parses the embedded `sample_rate` with `toInt()` and passes it to `SampleBuffer::fromBase64()` at line 829.
- `src/core/SampleBuffer.cpp:111` now rejects non-positive rates and returns the empty buffer before decoding embedded frames.
- `tests/src/core/ClipSerialisationTest.cpp:305` exercises `SampleClip::loadSettings()` with embedded PCM and rates `0` and `-44100`; it asserts the invalid-rate samples are empty and the resulting buffer rate is positive.

## Evidence table

All build and CTest logs are under `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-342.r1/logs`. Each build rebuilt both `zene` and `ClipSerialisationTest`. The CTest family matched one registered test; red runs report one test failed, and green runs report one test passed.

| Stage | Exact command | Exit |
|---|---|---:|
| Original build | `cmake --build build --target zene ClipSerialisationTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-342.r1/logs/build-1.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 0 |
| Original RED | `ctest -R '^ClipSerialisationTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-342.r1/logs/test-1.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 8 |
| Fixed build | `cmake --build build --target zene ClipSerialisationTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-342.r1/logs/build-2.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 0 |
| Initial GREEN | `ctest -R '^ClipSerialisationTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-342.r1/logs/test-2.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 0 |
| Save and reverse production patch | `git diff -- src/core/SampleBuffer.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-342.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-342.r1/logs/fix.patch; result=$?; echo EXIT=$result; exit $result` | 0 |
| Reversal build | `cmake --build build --target zene ClipSerialisationTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-342.r1/logs/build-3.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 0 |
| Reversal RED | `ctest -R '^ClipSerialisationTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-342.r1/logs/test-3.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 8 |
| Reapply production patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-342.r1/logs/fix.patch; result=$?; echo EXIT=$result; exit $result` | 0 |
| Restored build | `cmake --build build --target zene ClipSerialisationTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-342.r1/logs/build-4.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 0 |
| Restored GREEN | `ctest -R '^ClipSerialisationTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-342.r1/logs/test-4.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 0 |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-342.r1/logs/gate-1.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 1 |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-342.r1/logs/gate-2.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 1 |

The original RED and reversal RED both fail at `sample_rate=0 retained embedded frames`. Both GREEN runs pass. The complexity gate reports six over-target functions in `XyPadPairTest.cpp`, `DawProjectReadTracks.cpp`, `ControlCommandsMixerTools.cpp`, `ControlCommandsMixerStrip.cpp`, `ProjectRevisions.cpp`, and `ControlCommandsShortcuts.cpp`. The file-length gate reports fourteen unbaselined oversized files, including `SmfInterchangeReader.cpp`, `TakeLane.cpp`, and twelve unrelated test files. Neither gate report names `SampleBuffer.cpp` or `ClipSerialisationTest.cpp`. No gate or baseline was modified.

The `fff grep` tool was not exposed in this environment (`command -v fff` returned no path and no matching tool was available), so the logs were searched with `rg` for `error`, `FAILED`, and `Assertion`.

## Files committed

- `src/core/SampleBuffer.cpp`
- `tests/src/core/ClipSerialisationTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p10-342.md`

`ClipSerialisationTest.cpp` was already registered in `tests/fork-sources.txt`; no source registry change was needed.
