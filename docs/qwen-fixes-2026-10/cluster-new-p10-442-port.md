FIXED

# cluster new-p10-442-port — Invalid VST3 parameter values enter the audio snapshot

Verdict: FIXED

## Finding and fix

The last plug-in-sourced ingress not covered by the earlier cluster 3625 fix was
`HostedPlugin::load()` in `plugins/Vst3Effect/Vst3Host.cpp`: it copied the plug-in's
`getParamNormalized()` result straight into `Vst3ParamDescriptor::defaultNormalized` with a
`static_cast<float>()` and stored that into the `paramValues` array the audio thread reads.
`std::clamp()` does not sanitize NaN (both comparisons are false for it) and a `double` too
large for `float` overflows to infinity on the cast, so either reached the snapshot the moment
the plug-in was loaded. The fix routes that value through the same `sanitizedNormalized()`
normalizer used by `setParamNormalized()`, `notifyController()`, `loadState()` and
`Vst3ComponentHandler::performEdit()`.

## Regression

`tests/src/plugins/Vst3ChunkProbeTest.cpp::testInvalidParameterDefaultsNeverEnterTheSnapshot()`
loads the fixture's `Zene VST3 Invalid Defaults Probe` class (added to
`tests/data/vst3-chunk-probe/vst3-chunk-probe.cpp`) whose two parameters report a default of
`1.0e300` and `NaN`, then asserts both `paramNormalized()` values are finite and in `[0, 1]`
(`1.0f` and `0.0f`), that every descriptor default is finite and in range, and that
`setParamNormalized()` / `notifyController()` still refuse non-finite values.

## Evidence

Build commands ran from the worktree root; CTest commands from `build/tests`.

| Purpose | Exact command | Exit |
|---|---|---:|
| Build fixture and test target (fix in place) | `cmake --build build --target vst3-chunk-probe Vst3ChunkProbeTest -j8 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-442-port.r2/logs/build-1.log 2>&1; echo EXIT=$?` | 0 |
| Initial GREEN (`'^Vst3ChunkProbeTest$'`, 1 test selected) | `ctest -R '^Vst3ChunkProbeTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-442-port.r2/logs/test-1.log 2>&1; echo EXIT=$?` | 0 |
| Extract product fix to patch | `git diff -- plugins/Vst3Effect/Vst3Host.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-442-port.r2/logs/fix.patch` | 0 |
| Reverse product fix | `git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-442-port.r2/logs/fix.patch` | 0 |
| Rebuild with fix reversed | `cmake --build build --target Vst3ChunkProbeTest -j8 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-442-port.r2/logs/build-2.log 2>&1; echo EXIT=$?` | 0 |
| RED (`'^Vst3ChunkProbeTest$'`, 1 test selected, 1 failed) | `ctest -R '^Vst3ChunkProbeTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-442-port.r2/logs/test-2.log 2>&1; echo EXIT=$?` | 8 |
| Restore product fix | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-442-port.r2/logs/fix.patch` | 0 |
| Rebuild with fix restored | `cmake --build build --target Vst3ChunkProbeTest -j8 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-442-port.r2/logs/build-3.log 2>&1; echo EXIT=$?` | 0 |
| GREEN again (`'^Vst3ChunkProbeTest$'`, 1 test selected) | `ctest -R '^Vst3ChunkProbeTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-442-port.r2/logs/test-3.log 2>&1; echo EXIT=$?` | 0 |
| Build Vst3HostTest | `cmake --build build --target Vst3HostTest -j8 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-442-port.r2/logs/build-4.log 2>&1; echo EXIT=$?` | 0 |
| Vst3HostTest family (`'^Vst3HostTest$'`, 1 test selected) | `ctest -R '^Vst3HostTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-442-port.r2/logs/test-6.log 2>&1; echo EXIT=$?` | 0 |
| Rebuild zene | `cmake --build build --target zene -j8 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-442-port.r2/logs/build-5.log 2>&1; echo EXIT=$?` | 0 |
| Raw complexity ratchet | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-442-port.r2/logs/gate-complexity.log 2>&1; echo EXIT=$?` | 1 |
| Raw file-length ratchet | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-442-port.r2/logs/gate-filelength.log 2>&1; echo EXIT=$?` | 1 |
| Feature check | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-442-port > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-442-port.r2/logs/gate-1.log 2>&1; echo EXIT=$?` | 0 |

The raw complexity and file-length checks exit 1 because of pre-existing baseline entries in the
train; the feature check reports no new ratchet items in the changed files. The RED run failed at
`std::isfinite(probe.paramNormalized(HugeNormDefaultParamId))` (a default of `1.0e300` reached
the parameter array as infinity). The GREEN runs executed their selected suites with 0 failures.

## Files in this commit

- `plugins/Vst3Effect/Vst3Host.cpp` — sanitize the plug-in-sourced default at parameter ingress.
- `tests/data/vst3-chunk-probe/vst3-chunk-probe.cpp` — `InvalidDefaultsProbe` fixture class.
- `tests/src/plugins/Vst3ChunkProbeTest.cpp` — regression test.
- `docs/qwen-fixes-2026-10/cluster-new-p10-442-port.md` — this evidence report.

## Self-check

- Regression RED/GREEN: with the product change reverted, `ctest -R '^Vst3ChunkProbeTest$'` exited 8
  (failure at `std::isfinite(probe.paramNormalized(HugeNormDefaultParamId))`); restored, it exited 0.
  Commands and codes are in the evidence table above.
- Determinism: the test uses no sleeps, clocks or races. It drives injected plug-in-reported
  defaults (`1.0e300`, `NaN`) through the fixture and reads the host's atomic parameter array
  synchronously via `paramNormalized()`.
- Cause and completeness: the only unsanitized plug-in-sourced default ingress was
  `HostedPlugin::load()`; the other four ingresses (`setParamNormalized`, `notifyController`,
  `loadState`, `Vst3ComponentHandler::performEdit`) already used `sanitizedNormalized()`.
  `Vst3HostTest::testNonFiniteParameterValuesAreRefused` still passes.
- Collateral damage: only the default value description changes; finite in-range defaults are
  unchanged because `sanitizedNormalized()` clamps finite values into `[0, 1]` and refuses only
  non-finite ones. No saved-data format or control command changed.
- Verdict is FIXED, not ALREADY-FIXED/FALSE-POSITIVE; the unsanitized code was at
  `plugins/Vst3Effect/Vst3Host.cpp:286` before this change.

FIXED
