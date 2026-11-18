# Cluster new-p11-96

Verdict: **FIXED**

The finding is real. `EnvelopeAndLfoParameters::getAmountModel()` supports values from −1 to 1, and `EnvelopeGraph::toggleAmountModel()` previously treated a negative value as a request to set the amount to 1. The graph now treats exactly zero as disabled; a click sets any nonzero amount to zero and enables zero as 1.

## File and line evidence

- [EnvelopeGraph.cpp:254](../../src/gui/instrument/EnvelopeGraph.cpp#L254) now tests for zero before enabling the amount.
- [EnvelopeGraphTest.cpp:49](../../tests/src/gui/EnvelopeGraphTest.cpp#L49) sets −0.5, sends a left click, and checks zero; a second click checks that zero still enables the amount.
- [tests/CMakeLists.txt:779](../../tests/CMakeLists.txt#L779) registers the regression as `EnvelopeGraphTest`.
- `tests/upstream-modifications.txt:1043` records the inherited production file and regression reason; `tests/all-sources.txt:2599` registers the test source.

The affected CTest family contains **1 test** (`EnvelopeGraphTest`). The original implementation failed that test with actual `1`, expected `0`; the fixed implementation passed it.

## Commands and exit codes

All build and CTest output went to new files in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-96.r1/logs`. The fff grep tool was not available in this session, so I searched those logs narrowly with `rg` for `error`, `FAILED`, and `Assertion`.

The first direct build found no generated `EnvelopeGraphTest` target. The generated build system was refreshed through its own check target; no standalone CMake configure command was run:

| Command | Exit |
|---|---:|
| `cmake --build build --target EnvelopeGraphTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-96.r1/logs/build-1.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"` | 2 (target not generated yet) |
| `cmake --build build --target help > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-96.r1/logs/build-2.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"` | 0 |
| `cmake --build build --target cmake_check_build_system > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-96.r1/logs/build-3.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"` | 0 |

### Test on original implementation

| Command | Exit |
|---|---:|
| `cmake --build build --target EnvelopeGraphTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-96.r1/logs/build-4.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"` | 0 |
| `cd build/tests && ctest -R '^EnvelopeGraphTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-96.r1/logs/test-1.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"` | 8 (1/1 failed as expected) |

### Test with fix

| Command | Exit |
|---|---:|
| `cmake --build build --target EnvelopeGraphTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-96.r1/logs/build-5.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"` | 0 |
| `cd build/tests && ctest -R '^EnvelopeGraphTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-96.r1/logs/test-2.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"` | 0 (1/1 passed) |

### Required proof reversal

The production-only patch was saved and reversed, then restored. First proof cycle:

| Command | Exit |
|---|---:|
| `git diff -- src/gui/instrument/EnvelopeGraph.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-96.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-96.r1/logs/fix.patch; rc=$?; printf 'EXIT=%s\n' "$rc"` | 0 |
| `cmake --build build --target EnvelopeGraphTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-96.r1/logs/build-6.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"` | 0 |
| `cd build/tests && ctest -R '^EnvelopeGraphTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-96.r1/logs/test-3.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"` | 8 (1/1 failed with actual 1, expected 0) |
| `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-96.r1/logs/fix.patch; rc=$?; printf 'EXIT=%s\n' "$rc"` | 0 |
| `cmake --build build --target EnvelopeGraphTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-96.r1/logs/build-7.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"` | 0 |
| `cd build/tests && ctest -R '^EnvelopeGraphTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-96.r1/logs/test-4.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"` | 0 (1/1 passed) |

The same proof was repeated with `zene` explicitly rebuilt in each state to rule out stale binaries:

| Command | Exit |
|---|---:|
| `git diff -- src/gui/instrument/EnvelopeGraph.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-96.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-96.r1/logs/fix.patch; rc=$?; printf 'EXIT=%s\n' "$rc"` | 0 |
| `cmake --build build --target zene > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-96.r1/logs/build-8.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"` | 0 |
| `cmake --build build --target EnvelopeGraphTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-96.r1/logs/build-9.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"` | 0 |
| `cd build/tests && ctest -R '^EnvelopeGraphTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-96.r1/logs/test-5.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"` | 8 (1/1 failed with actual 1, expected 0) |
| `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-96.r1/logs/fix.patch; rc=$?; printf 'EXIT=%s\n' "$rc"` | 0 |
| `cmake --build build --target zene > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-96.r1/logs/build-10.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"` | 0 |
| `cmake --build build --target EnvelopeGraphTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-96.r1/logs/build-11.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"` | 0 |
| `cd build/tests && ctest -R '^EnvelopeGraphTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-96.r1/logs/test-6.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"` | 0 (1/1 passed) |

### Ratchet gates

| Command | Exit |
|---|---:|
| `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-96.r1/logs/gate-3.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"` | 0 |
| `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-96.r1/logs/gate-4.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"` | 0 |

## Files committed

- `src/gui/instrument/EnvelopeGraph.cpp`
- `tests/src/gui/EnvelopeGraphTest.cpp`
- `tests/CMakeLists.txt`
- `tests/all-sources.txt`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p11-96.md`
