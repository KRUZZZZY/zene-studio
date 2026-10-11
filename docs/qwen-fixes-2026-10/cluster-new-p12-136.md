# Cluster new-p12-136

## Verdict: FIXED

`writeToByteSeq()` now rejects key-pressure keys outside 0–127 before narrowing the key to a MIDI byte. The invalid event returns size 0. The test also checks that the valid maximum key still serializes correctly.

Evidence:

- [MidiEventToByteSeq.cpp](../../src/core/midi/MidiEventToByteSeq.cpp#L76): the key-pressure case checks the same range as note-on/off before writing the key.
- [MidiEventToByteSeqTest.cpp](../../tests/src/core/MidiEventToByteSeqTest.cpp#L14): key 127 serializes as expected; key 256 is rejected.
- [CMake registration](../../tests/CMakeLists.txt#L345) builds and registers the regression target.

The baseline test failed at `outOfRangeKeyPressureIsRejected()`. After removing the production patch and rebuilding, the same assertion failed again. With the patch restored, the test passed. The selected ctest family contains 1 test, and it ran (1/1).

## Command evidence

All builds and tests wrote output to new files under `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-136.r1/logs`. Exit markers were appended to each log. The `fff` grep tool was not available in this session; logs were searched with `rg` for failure markers.

| Evidence | Exact command | Exit |
|---|---|---:|
| Initial target lookup (before generated test target refresh) | `cmake --build build --target MidiEventToByteSeqTest > "/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-136.r1/logs/build-1.log" 2>&1` | 2 |
| Normal build refreshed generated targets | `cmake --build build --target zene > "/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-136.r1/logs/build-2.log" 2>&1` | 0 |
| Regression target build | `cmake --build build --target MidiEventToByteSeqTest > "/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-136.r1/logs/build-3.log" 2>&1` | 0 |
| RED on original code | From `build/tests`: `ctest -R '^MidiEventToByteSeqTest$' --output-on-failure > "/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-136.r1/logs/test-1.log" 2>&1` | 8 |
| Fixed `zene` and regression target build | `cmake --build build --target zene MidiEventToByteSeqTest > "/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-136.r1/logs/build-4.log" 2>&1` | 0 |
| GREEN with fix | From `build/tests`: `ctest -R '^MidiEventToByteSeqTest$' --output-on-failure > "/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-136.r1/logs/test-2.log" 2>&1` | 0 |
| Save and reverse production patch | `git diff -- src/core/midi/MidiEventToByteSeq.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-136.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-136.r1/logs/fix.patch` | 0 |
| Rebuild with fix removed | `cmake --build build --target zene MidiEventToByteSeqTest > "/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-136.r1/logs/build-5.log" 2>&1` | 0 |
| RED with fix removed | From `build/tests`: `ctest -R '^MidiEventToByteSeqTest$' --output-on-failure > "/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-136.r1/logs/test-3.log" 2>&1` | 8 |
| Restore production patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-136.r1/logs/fix.patch` | 0 |
| Final `zene` and regression target build | `cmake --build build --target zene MidiEventToByteSeqTest > "/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-136.r1/logs/build-6.log" 2>&1` | 0 |
| Final GREEN, 1/1 test | From `build/tests`: `ctest -R '^MidiEventToByteSeqTest$' --output-on-failure > "/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-136.r1/logs/test-4.log" 2>&1` | 0 |
| Complexity gate | `bash tests/complexity-gate.sh --check > "/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-136.r1/logs/gate-1.log" 2>&1` | 0 |
| File-length gate | `bash tests/file-length-gate.sh --check > "/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-136.r1/logs/gate-2.log" 2>&1` | 0 |

## Files in the cluster commit

- `src/core/midi/MidiEventToByteSeq.cpp`
- `tests/src/core/MidiEventToByteSeqTest.cpp`
- `tests/CMakeLists.txt`
- `tests/fork-sources.txt`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p12-136.md`
