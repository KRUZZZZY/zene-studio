# Cluster new-p9-262: CoreMIDI notification name leak

**Verdict: FIXED**

`MidiApple::getFullName()` returns a `malloc`-allocated buffer ([`src/core/midi/MidiApple.cpp:640-644`](../../src/core/midi/MidiApple.cpp#L640)). The add and remove branches in `NotifyCallback()` now own that buffer with `std::unique_ptr` and `std::free` ([lines 571-572](../../src/core/midi/MidiApple.cpp#L571), [593-594](../../src/core/midi/MidiApple.cpp#L593)); the buffer is released when each notification branch exits. The regression in [`tests/src/core/MidiReconnectTest.cpp:409-423`](../../tests/src/core/MidiReconnectTest.cpp#L409) checks that both formatted names have an owner. It is a source-contract test because this worktree is Linux and the Apple backend is excluded from its build.

## Verification evidence

Commands below are the build or test invocation run; build commands ran from the worktree root, and `ctest` ran from `build/tests`. Build/test logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-262.r1/logs/`.

| Check | Exact command | Exit | Log |
| --- | --- | ---: | --- |
| RED, original callback | `ctest -R '^MidiReconnectTest$' --output-on-failure` | 8 | `test-1-red.log` |
| GREEN, fixed callback | `ctest -R '^MidiReconnectTest$' --output-on-failure` | 0 | `test-2-green.log` |
| RED, production patch reversed | `ctest -R '^MidiReconnectTest$' --output-on-failure` | 8 | `test-3-revert-red.log` |
| GREEN, production patch reapplied | `ctest -R '^MidiReconnectTest$' --output-on-failure` | 0 | `test-4-reapply-green.log` |
| Affected CTest family count | `ctest -R '^MidiReconnectTest$' --output-on-failure` | 1 test executed | `test-2-green.log` |
| Complexity ratchet | `bash tests/complexity-gate.sh --check` | 0 | `gate-1-complexity.log` |
| File-length ratchet | `bash tests/file-length-gate.sh --check` | 0 | `gate-2-file-length.log` |

The original and reversed-patch failures were the regression assertion: it found 0 owning declarations where 2 were expected. The passing CTest runs report 1/1 tests passed.

Build and patch proof commands:

| Exact command | Exit | Log |
| --- | ---: | --- |
| `cmake --build build --target MidiReconnectTest` | 0 | `build-1.log` |
| `cmake --build build --target zene MidiReconnectTest` (after fix) | 0 | `build-2.log` |
| `git diff -- src/core/midi/MidiApple.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-262.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-262.r1/logs/fix.patch` | 0 | `fix.patch` |
| `cmake --build build --target zene MidiReconnectTest` (patch reversed) | 0 | `build-3.log` |
| `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-262.r1/logs/fix.patch` | 0 | `fix.patch` |
| `cmake --build build --target zene MidiReconnectTest` (patch restored) | 0 | `build-4.log` |

Scope checks: `bash tests/fork-sources-gate.sh --verbose` exited 0 and `bash tests/no-upstream-regression-gate.sh` exited 0. The unsupported command `bash tests/fork-sources-gate.sh --check` exited 2 with its usage message; the supported `--verbose` invocation then passed. `git diff --check` exited 0.

The Linux build cannot compile `MidiApple.cpp`, which is guarded by `LMMS_BUILD_APPLE`; the macOS-specific compile remains unverified here.

## Files committed

- `src/core/midi/MidiApple.cpp`
- `tests/src/core/MidiReconnectTest.cpp`
- `tests/fork-sources.txt`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p9-262.md`
