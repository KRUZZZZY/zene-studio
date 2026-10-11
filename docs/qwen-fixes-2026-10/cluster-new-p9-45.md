# Cluster new-p9-45 — FIXED

The `mixer.surround_pan` reply could claim that a surround-format source had no surround sends, even when it had one. The note is now emitted only for a stereo source with no surround sends.

## Evidence

- `src/core/ControlCommandsMixerSurround.cpp:211` guards the note with `!channel->m_surround.isSurround() && surroundSends(*channel).isEmpty()`.
- `tests/src/core/SurroundTest.cpp:233` adds a control-surface regression. It verifies that a 5.1 source routed to a 5.1 bus reports its send without the note, while a stereo source without surround sends retains the note.

## Build and test record

All ctest commands below ran from `build/tests`. Each run executed the one registered `SurroundTest` target (1/1); the green runs passed the complete QtTest binary.

| Stage | Command | Exit |
| --- | --- | ---: |
| Initial regression build | `cmake --build build --target SurroundTest -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-45.r1/logs/build-1.log 2>&1; build_status=$?; echo EXIT=$build_status >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-45.r1/logs/build-1.log; exit "$build_status"` | 0 |
| Initial RED | `ctest -R '^SurroundTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-45.r1/logs/test-1.log 2>&1; test_status=$?; echo EXIT=$test_status >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-45.r1/logs/test-1.log; exit "$test_status"` | 8 |
| Final regression RED build | `cmake --build build --target zene SurroundTest -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-45.r1/logs/build-2.log 2>&1; build_status=$?; echo EXIT=$build_status >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-45.r1/logs/build-2.log; exit "$build_status"` | 0 |
| RED on original production code | `ctest -R '^SurroundTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-45.r1/logs/test-2.log 2>&1; test_status=$?; echo EXIT=$test_status >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-45.r1/logs/test-2.log; exit "$test_status"` | 8 |
| Fixed build | `cmake --build build --target zene SurroundTest -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-45.r1/logs/build-3.log 2>&1; build_status=$?; echo EXIT=$build_status >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-45.r1/logs/build-3.log; exit "$build_status"` | 0 |
| GREEN | `ctest -R '^SurroundTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-45.r1/logs/test-3.log 2>&1; test_status=$?; echo EXIT=$test_status >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-45.r1/logs/test-3.log; exit "$test_status"` | 0 |
| Reverse-fix patch command | `git diff -- src/core/ControlCommandsMixerSurround.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-45.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-45.r1/logs/fix.patch` | 0 |
| Reversed-fix build | `cmake --build build --target zene SurroundTest -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-45.r1/logs/build-4.log 2>&1; build_status=$?; echo EXIT=$build_status >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-45.r1/logs/build-4.log; exit "$build_status"` | 0 |
| RED with production fix reversed | `ctest -R '^SurroundTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-45.r1/logs/test-4.log 2>&1; test_status=$?; echo EXIT=$test_status >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-45.r1/logs/test-4.log; exit "$test_status"` | 8 |
| Restore-fix patch command | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-45.r1/logs/fix.patch` | 0 |
| Restored-fix build | `cmake --build build --target zene SurroundTest -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-45.r1/logs/build-5.log 2>&1; build_status=$?; echo EXIT=$build_status >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-45.r1/logs/build-5.log; exit "$build_status"` | 0 |
| GREEN after restore | `ctest -R '^SurroundTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-45.r1/logs/test-5.log 2>&1; test_status=$?; echo EXIT=$test_status >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-45.r1/logs/test-5.log; exit "$test_status"` | 0 |

The RED runs failed at `aSurroundSourceWithSurroundSendsHasNoMissingSendsNote`: the 5.1 source's reply included the “this channel has none” note. Both green runs reported `100% tests passed, 0 tests failed out of 1`.

## Required gates

| Gate | Command | Exit |
| --- | --- | ---: |
| Complexity | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-45.r1/logs/gate-1.log 2>&1; gate_status=$?; echo EXIT=$gate_status >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-45.r1/logs/gate-1.log; exit "$gate_status"` | 0 |
| File length | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-45.r1/logs/gate-2.log 2>&1; gate_status=$?; echo EXIT=$gate_status >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-45.r1/logs/gate-2.log; exit "$gate_status"` | 0 |

The `fff` grep tool was not exposed in this environment (`command -v fff` found no executable), so build and gate logs were searched with `rg`, one requested term per search (`error`, `FAILED`, `Assertion`). No matches were found.

## Files in commit

- `src/core/ControlCommandsMixerSurround.cpp`
- `tests/src/core/SurroundTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p9-45.md`
