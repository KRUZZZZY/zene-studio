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
| Build test target (fix in place) | `cmake --build build --target Vst3ChunkProbeTest -j8 > .../logs/build-1.log 2>&1; echo EXIT=$?` | 0 |
| GREEN | `ctest -R '^Vst3ChunkProbeTest$' --output-on-failure > .../logs/test-3.log 2>&1; echo EXIT=$?` | 0 |
| Reverse product fix | `git apply -R .../logs/fix.patch` | 0 |
| Rebuild reversed | `cmake --build build --target Vst3ChunkProbeTest -j8 > .../logs/build-2.log 2>&1; echo EXIT=$?` | 0 |
| RED | `ctest -R '^Vst3ChunkProbeTest$' --output-on-failure > .../logs/test-2.log 2>&1; echo EXIT=$?` | 8 |
| Restore product fix | `git apply .../logs/fix.patch` | 0 |
| Rebuild restored | `cmake --build build --target Vst3ChunkProbeTest -j8 > .../logs/build-3.log 2>&1; echo EXIT=$?` | 0 |
| GREEN again | `ctest -R '^Vst3ChunkProbeTest$' --output-on-failure > .../logs/test-3.log 2>&1; echo EXIT=$?` | 0 |
| Vst3HostTest family | `ctest -R '^Vst3HostTest$' --output-on-failure > .../logs/test-6.log 2>&1; echo EXIT=$?` | 0 |
| Rebuild zene | `cmake --build build --target zene -j8 > .../logs/build-5.log 2>&1; echo EXIT=$?` | 0 |
| Feature check | `python3 .../feature_check.py <worktree> > .../logs/gate-1.log 2>&1; echo EXIT=$?` | 0 |

RED failed at `std::isfinite(probe.paramNormalized(HugeNormDefaultParamId))` (a default of
`1.0e300` reached the parameter array as infinity). GREEN executed the full
`Vst3ChunkProbeTest` suite with 0 failures.

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