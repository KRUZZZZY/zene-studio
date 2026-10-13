# Cluster new-p12-127: sndio MIDI null handle

**Verdict: FIXED**

`MidiSndio` construction returns when `mio_open()` fails, leaving `m_hdl` null. `sendByte()` now returns before calling `mio_write()` when that happens ([MidiSndio.cpp:85](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p12-127/src/core/midi/MidiSndio.cpp:85)).

The regression test replaces `mio_open()` with a null result and counts calls to `mio_write()` ([MidiSndioTest.cpp:16](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p12-127/tests/src/core/MidiSndioTest.cpp:16), [MidiSndioTest.cpp:21](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p12-127/tests/src/core/MidiSndioTest.cpp:21)). The assertion at line 43 observed one write on the original code and zero with the guard. CMake registers the test only when the optional sndio backend is enabled ([tests/CMakeLists.txt:935](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p12-127/tests/CMakeLists.txt:935)).

The affected ctest family is `MidiSndioTest`: **1/1 CTest tests executed**. The original run and the reverse-patch proof both failed as expected; the fixed runs passed. Logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-127.r1/logs/`.

| Step | Command | EXIT | Log |
|---|---|---:|---|
| Initial target lookup | `cmake --build build --target MidiSndioTest -j2` | 2 | `build-1.log` (target absent before generated build files refreshed) |
| Refresh generated targets and build zene | `cmake --build build --target zene -j2` | 0 | `build-2.log` |
| Build regression target before fix | `cmake --build build --target MidiSndioTest -j2` | 0 | `build-3.log` |
| RED on original code | `ctest -R MidiSndioTest --output-on-failure` (cwd `build/tests`) | 8 | `test-1.log`; actual write count 1, expected 0 |
| Build fix | `cmake --build build --target zene MidiSndioTest -j2` | 0 | `build-4.log` |
| GREEN after fix | `ctest -R MidiSndioTest --output-on-failure` (cwd `build/tests`) | 0 | `test-2.log`; 1/1 passed |
| Remove product patch for proof | `git diff -- src/core/midi/MidiSndio.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-127.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-127.r1/logs/fix.patch` | 0 | — |
| Rebuild without fix | `cmake --build build --target zene MidiSndioTest -j2` | 0 | `build-5.log` |
| RED without fix | `ctest -R MidiSndioTest --output-on-failure` (cwd `build/tests`) | 8 | `test-3.log`; actual write count 1, expected 0 |
| Restore product patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-127.r1/logs/fix.patch` | 0 | — |
| Rebuild restored fix | `cmake --build build --target zene MidiSndioTest -j2` | 0 | `build-6.log` |
| GREEN after restore | `ctest -R MidiSndioTest --output-on-failure` (cwd `build/tests`) | 0 | `test-4.log`; 1/1 passed |
| Complexity ratchet | `bash tests/complexity-gate.sh --check` | 0 | `gate-1.log` |
| File-length ratchet | `bash tests/file-length-gate.sh --check` | 0 | `gate-2.log` |

The reverse-patch proof confirms the regression test catches the original bug. The commit contains:

- `src/core/midi/MidiSndio.cpp`
- `tests/src/core/MidiSndioTest.cpp`
- `tests/CMakeLists.txt`
- `tests/fork-sources.txt`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p12-127.md`
