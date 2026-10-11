# Cluster new-p9-571

**Verdict: BLOCKED**

## Evidence

The finding describes a real portability and undefined-conversion risk. At [AudioFileProcessorWaveView.cpp:411](../../plugins/AudioFileProcessor/AudioFileProcessorWaveView.cpp#L411), the pixel-derived `double` is cast to `f_cnt_t` at line 415. `f_cnt_t` is `std::uint64_t` ([LmmsTypes.h:43](../../include/LmmsTypes.h#L43)), while `slideSamplePointByFrames` accepts signed `long` at line 419. A negative value whose truncated value is below zero cannot be represented by the unsigned destination, making the floating-to-integer conversion undefined; the later unsigned-to-signed conversion is also implementation-defined when out of range.

The supplied test paths are reference source snapshots, not tests. `tests/CMakeLists.txt:1540` registers the AudioFileProcessor sources in the reference-module list, and line 1669 builds `partc_ref_audiofileprocessor` as a module. The configured CTest tree has no `AudioFileProcessorWaveView` or `AudioFileProcessorView` test, and no `AudioFileProcessorWaveView` build target. I made no product-code change because the required regression cannot be run and proven red in this configured tree.

## Verification evidence

Commands ran from `build/tests` unless a different working directory is shown. CTest exits 0 when it finds no tests; per the task instructions, that is an error, not a pass.

| Check | Exact command | Exit | Result |
|---|---|---:|---|
| Test discovery | `ctest -N -R '^AudioFileProcessorWaveView$' > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-571.r1/logs/test-1.log 2>&1` | 0 | `Total Tests: 0` |
| Requested RED family | `ctest -R '^AudioFileProcessorWaveView$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-571.r1/logs/test-2.log 2>&1` | 0 | `No tests were found!!!`; zero tests, so not a RED result |
| Other named family | `ctest -R '^AudioFileProcessorView$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-571.r1/logs/test-3.log 2>&1` | 0 | `No tests were found!!!`; zero tests |
| Named build target | `cmake --build build --target AudioFileProcessorWaveView > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-571.r1/logs/build-1.log 2>&1` (from worktree root) | 2 | No rule to make target |
| RED assertion | Not run | — | No registered test target is available |
| GREEN assertion | Not run | — | No regression or product fix was applied |
| Affected-family test count | — | — | 0 |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-571.r1/logs/gate-3.log 2>&1` | 0 | Passed |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-571.r1/logs/gate-4.log 2>&1` | 0 | Passed |

The build/test output is in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-571.r1/logs/`: `test-1.log` through `test-3.log`, `build-1.log`, and `gate-3.log` / `gate-4.log`. The requested RED/green proof, affected-family test execution, and `zene` rebuild remain unperformed because there is no registered test family or target to build.

## Files committed

- `docs/qwen-fixes-2026-10/cluster-new-p9-571.md` (this blocked report only)
