FIXED

# Cluster new-p23-128: non-stream iostream state saver diagnostic

## Decision and code evidence

**REAL.** `plugins/SfzPlayer/sfizz/external/abseil-cpp/absl/random/internal/iostream_state_saver.h:30` documents that `null_state_saver` does nothing. Before the fix, the non-stream `make_ostream_state_saver` overload at line 98 wrote `null_state_saver` to `std::cerr` before returning that no-op saver. The regression failed on the captured output. The fix removes only that write; the normal stream overload remains unchanged.

The sibling non-stream `make_istream_state_saver` overload remains silent. The stream saver still applies its requested flags/fill and restores flags, fill, and precision. A source search found no call to these helpers from SFZ runtime code outside vendored Abseil formatting code.

## Regression matrix

| Path | Coverage and result |
| --- | --- |
| Non-stream ostream overload | `SfzPlayerTest::nonStreamStateSaversAreSilent` captures `std::cerr` around a call with an `int`. It failed with the production change reversed and passed with it restored. |
| Sibling non-stream istream overload | Called in the same capture scope; it remains quiet. |
| Normal ostream overload | `SfzPlayerTest::ostreamStateSaverRestoresFormatting` checks requested flags/fill and restoration of flags, fill, and precision. |
| Failure, exhaustion, reconfiguration | Not applicable: the helper creates a stack RAII saver and owns no resource, capacity, or configurable state. |
| Audio-thread constraints | No new production work is added. The test runs in the stopped-engine `SfzPlayerTest` setup; the product change removes synchronous stderr I/O. No SFZ runtime call site was found. |
| Supported platforms | The regression uses standard iostreams and the vendored Abseil header without OS-specific APIs. It built with this worktree's configured C++20 toolchain on Linux; Windows and macOS builds were not run in this session. |

## Build and test evidence

Commands ran from the worktree unless the working directory is stated otherwise. Build output is in the numbered log files under `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p23-128.r1/logs`.

| Phase | Exact command | Exit | Evidence |
| --- | --- | ---: | --- |
| Original-code build | `cmake --build build --target zene` | 0 | `build-3.log` |
| Original-code test build | `cmake --build build --target SfzPlayerTest` | 0 | `build-4.log` |
| RED on original code | `ctest -R SfzPlayerTest --output-on-failure` from `build/tests` | 8 | `test-1.log`; 1/1 CTest case failed at `nonStreamStateSaversAreSilent`, with captured stderr non-empty. |
| Fixed build | `cmake --build build --target zene`; `cmake --build build --target SfzPlayerTest` | 0; 0 | `build-5.log`, `build-6.log` |
| GREEN with fix | `ctest -R SfzPlayerTest --output-on-failure` from `build/tests` | 0 | `test-2.log`; 1/1 CTest case passed. |
| Negative-control removal | `git diff -- plugins/SfzPlayer/sfizz/external/abseil-cpp/absl/random/internal/iostream_state_saver.h > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p23-128.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p23-128.r1/logs/fix.patch` | 0 | Reversed only the production hunk; test source remained unchanged. |
| RED with fix reversed | `cmake --build build --target zene`; `cmake --build build --target SfzPlayerTest`; `ctest -R SfzPlayerTest --output-on-failure` from `build/tests` | 0; 0; 8 | `build-7.log`, `build-8.log`, `test-3.log`; same 1/1 case failed at the same assertion. |
| Restore and GREEN | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p23-128.r1/logs/fix.patch`; `cmake --build build --target zene`; `cmake --build build --target SfzPlayerTest`; `ctest -R SfzPlayerTest --output-on-failure` from `build/tests` | 0; 0; 0; 0 | `build-9.log`, `build-10.log`, `test-4.log`; 1/1 CTest case passed. |

The first attempted separate test target was not present in the configured target table: `cmake --build build --target zene IostreamStateSaverTest` exited 2. Direct reconfiguration, `cmake -S . -B build`, exited 97 because the lane's CMake shim disallows reconfiguration. The regression therefore uses the existing `SfzPlayerTest` target without changing CMake registration.

## Gates

The first `feature_check.py` run exited 1 because `tests/all-sources.txt` contained vendored SFZ/Abseil source paths that its own documented recipe excludes. The list was regenerated from that recipe and verified; no gate or baseline was changed.

| Command | Exit | Result |
| --- | ---: | --- |
| `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p23-128` | 0 | `gate-3.log` and `gate-4.log`: both exit 0; all eight rows PASS, with no new complexity/file-length item. `gate-4.log` is after report registration. |
| `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/submit_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p23-128 new-p23-128` | 0 | `submit-1.log`: PASS, with all five ASK items answered in this report's Self-check section. |

## Self-check

- The test fails with the production hunk reversed and passes restored: `test-1.log` and the negative-control `test-3.log` both show the same failed assertion and CTest exit 8; `test-2.log` and `test-4.log` pass with exit 0. Each run executed 1/1 registered CTest case.
- The test is deterministic: it redirects `std::cerr` synchronously and checks a fixed `int`; there are no clocks, sleeps, or test-created threads. The normal stream test compares explicit flags, fill, and precision values.
- The changed behavior is confined to the non-stream ostream overload. The normal ostream overload and sibling non-stream istream overload were checked and covered; the only uses found outside the test are vendored Abseil stream-formatting code.
- No project serialization or saved-data code, limits, commands, undo behavior, or audio processing code changed. Removing the diagnostic leaves old project loading and rendering paths untouched.
- This is a real defect, not an already-fixed case: the `null_state_saver` contract says it does nothing, the original overload contradicted that contract by writing to stderr, and the pre-fix regression demonstrated the write.

## Files in the fix commit

- `plugins/SfzPlayer/sfizz/external/abseil-cpp/absl/random/internal/iostream_state_saver.h`
- `tests/src/plugins/SfzPlayerTest.cpp`
- `tests/upstream-modifications.txt`
- `tests/all-sources.txt` (regenerated to match its checked-in recipe)
- `tests/fork-sources.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p23-128.md`
