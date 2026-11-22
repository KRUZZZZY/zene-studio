Verdict: FIXED

new-p18-24: DeltaTap marks a zero delta as non-silent

Evidence: both `DeltaTap::process` overloads unconditionally marked the block non-silent after subtracting the dry copy. The bus overload cleared quiet bits 0 and 1 at the original `src/core/DeltaTap.cpp:63-64`; the buffer overload called `assumeNonSilent(0)` and `assumeNonSilent(1)` at the original `src/core/DeltaTap.cpp:82-83`. A bypassed device therefore produced an exact zero delta, and the mixer was still told it held audio. Both overloads are live: `EffectChain.cpp:357` (AudioBuffer) and `EffectChain.cpp:391` (AudioBus) call `deltaTap().process`, and `RoutingChainNodes.cpp:98` calls the AudioBuffer overload from the routing graph.

The fix measures the delta instead of assuming it, using the existing silence-scanning APIs:

- Bus overload, `src/core/DeltaTap.cpp:62-64`: after the subtraction, `bus.updateAll()` measures the resulting bus even if the effect's output-port map excludes pair 0, which DeltaTap still changes.
- Buffer overload, `src/core/DeltaTap.cpp:83-84`: `buffer.updateSilenceFlags(0b11)`. This mirrors `Effect.cpp:283` (`inOut.updateSilenceFlags(0b11)`). With silence tracking off, it still clears the flags, so the non-silent assumption is unchanged there (`AudioBuffer.cpp:326-335`).

Behaviour: a bypassed device with silent input now yields a quiet block, so the mixer skips it, which is the intent. A silent output's delta is the inverted input and stays non-silent, because the delta is measured, not the output. Tap off: unchanged, since the tap returns before any of this.

Regression: `tests/src/core/PatcherCommandsTest.cpp` slot `theDeltaTapMeasuresTheSilenceOfItsDelta()`. It covers: bus, passthrough, silent block is quiet (RED site); bus, doubling, signal stays non-silent; bus with an output-port map that excludes pair 0, passthrough delta is quiet (QA RED site); buffer with tracking on, passthrough silent, flags set (RED site); buffer with tracking on, doubling, flags clear; buffer with tracking off, passthrough, flags stay clear. Both the "quiet" and the "non-silent" sides are checked, so a version that always marks quiet also fails. The test has no timing, sleeps or races.

Realtime: the fix calls only `AudioBus::updateAll` and `AudioBuffer::updateSilenceFlags`, which scan the samples without allocation, locking or logging. `PatcherCommandsTest` still contains the existing zero-allocation check for the patched graph, which passes.

Scope: the product change is in `DeltaTap.cpp`, with regression coverage in `PatcherCommandsTest.cpp`; the report and `tests/upstream-modifications.txt` record the lane evidence and inherited-test registration. No header or control command changed. Both code paths were already listed in `tests/fork-sources.txt`.

Logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p18-24.r1/logs/`.

| Step | Command (run from the worktree or `build/tests` as noted) | EXIT | Result |
| --- | --- | --- | --- |
| Build attempt 1 (no fix) | `cmake --build build --target PatcherCommandsTest -j4 > logs/build-1-red.log 2>&1` | 2 | ccache could not create temp file under the configured temp dir (sandbox permission); not a compile error. Retried with `CCACHE_TEMPDIR=/home/kruzzzzy/.cache/ccache-bunny/tmp-p18` (environment override only). |
| Build (no fix) | same command, `logs/build-2-red.log` | 0 | Target built |
| RED, no fix | `ctest -R '^PatcherCommandsTest$' --output-on-failure > logs/test-1-red.log 2>&1` (from `build/tests`) | 8 | `FAIL! : PatcherCommandsTest::theDeltaTapMeasuresTheSilenceOfItsDelta() 'bus.quietChannels()[0]' returned FALSE`; 14 passed, 1 failed |
| Fix applied, build | `cmake --build build --target zene PatcherCommandsTest -j4 > logs/build-3-green.log 2>&1` | 0 | Built |
| GREEN | `ctest -R '^PatcherCommandsTest$' --output-on-failure > logs/test-2-green.log 2>&1` (from `build/tests`) | 0 | 1/1 test passed |
| Mutation: reverse fix (`git diff -- src/core/DeltaTap.cpp > logs/fix.patch; git apply -R logs/fix.patch`) | build as above, `logs/build-4b-reverted.log` | 0 | Built |
| Mutation RED | ctest as above, `logs/test-3-4b-reverted.log` | 8 | Same assertion as RED: `'bus.quietChannels()[0]' returned FALSE` |
| Mutation restored (`git apply logs/fix.patch`) | build as above, `logs/build-5-reapplied.log` | 0 | Built |
| GREEN after restore | `ctest -R '^PatcherCommandsTest$' -V --output-on-failure > logs/test-4-reapplied.log 2>&1` | 0 | 15 QTest functions PASS (14 existing plus the new one); `theDeltaTapMeasuresTheSilenceOfItsDelta()` PASS |
| Related families | `cmake --build build --target EffectDeviceMenuTest DevicePanelTest -j4 > logs/build-6-related.log 2>&1`; `ctest -R '^(EffectDeviceMenuTest\|DevicePanelTest\|PatcherCommandsTest)$' -V > logs/test-5-family.log 2>&1` | 0 / 0 | 3/3 ctest tests pass |
| Feature gates | `python3 -I <run folder>/run_gate_logdir.py <worktree>` (the unmodified `feature_check.py`, with only its log folder redirected to the run folder) | 0 | 8/8 rows PASS: fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures, complexity, file-length. Output in `logs/gates-table-1.log` |

Affected-family test count: `PatcherCommandsTest` 15 of 15 QTest functions pass, `DevicePanelTest` and `EffectDeviceMenuTest` 1 ctest each, all passing. Zero tests were not run.

Original gate note: the fixer's `submit_check` attempt logged a `PermissionError` when it tried to create `<worktree>-feature-check/` next to the worktree (`logs/gates-1.log`). The gate's logic was not changed. The independent QA run in the current writable workspace passed the direct command (see the QA supplement).

## Self-check

- **Regression fails with the change reverted and passes with it.** Reverted: `logs/test-3-4b-reverted.log`, `ctest` EXIT=8, `FAIL! : ...theDeltaTapMeasuresTheSilenceOfItsDelta() 'bus.quietChannels()[0]' returned FALSE`. Restored: `logs/test-4-reapplied.log`, EXIT=0, 15/15 PASS. The unfixed first run is `logs/test-1-red.log`, EXIT=8, the same assertion. The patch used for the reversal is `logs/fix.patch` (a diff of `src/core/DeltaTap.cpp` only).
- **Deterministic.** The test has no clock, sleep, thread or race. Inputs are fixed constants: a gain of 1.0 gives an exact zero delta, a gain of 2.0 gives a delta equal to the input (`kBypassedGain`). Silence tracking is set explicitly on the buffer (`enableSilenceTracking(true/false)`), and the tap is switched with `setEnabled`. The "quiet" and "non-silent" sides are both asserted, so an always-quiet version also fails.
- **At the cause, all callers consistent.** The two overloads are the only places the tap writes silence flags. Their callers are `src/core/EffectChain.cpp:357` (AudioBuffer), `src/core/EffectChain.cpp:391` (AudioBus fallback) and `src/core/RoutingChainNodes.cpp:98` (routing graph, planar). All three reach the changed code through `deltaTap().process`. The AudioBus path scans the actual resulting bus, so the output-port map cannot leave pair 0's pre-delta flags stale; the AudioBuffer path uses `updateSilenceFlags(0b11)`, as in `src/core/Effect.cpp:283`. The downstream readers of these flags are the mixer's AND-merges (`src/core/Mixer.cpp:863`, `:2070`, `:2077`, `:2101`), which stay correct because the flags are now true measurements.
- **No collateral damage.** The tap-off path returns before any changed line (`DeltaTap.cpp:57` and `:71`). Quiet flags are runtime state only: no save or load code reads them (`rg quietChannels` over `src` finds no serialisation). No limit or check was added. A silent output's delta still stays non-silent, because the delta is measured, not assumed. Audible output does not change: the newly quiet block is all zeros, which the mixer skips.

**Verdict note:** FIXED, not ALREADY-FIXED or FALSE-POSITIVE, so no contract citation is needed beyond the code above.

**Original `submit_check` gate note:** its attempt failed before running any check because its default log folder was outside the fixer's write jail (`logs/gates-1.log`, `logs/submit-check-1.log`). The same unmodified script with its log folder redirected into the run folder passed 8/8 rows (`logs/gates-table-2.log`). This reviewer independently ran the direct gate in the writable workspace and it passed 8/8 rows.

## QA reviewer supplement

The independent review found that `AudioBus::update(*apm)` only scans output-mapped channels, while DeltaTap subtracts bus pair 0 regardless of that map. In a valid configuration where the map excludes pair 0, the unchanged input minus its dry copy is zero but the quiet bits stayed non-silent. The bus overload now uses `updateAll()`, and `theDeltaTapMeasuresTheSilenceOfItsDelta()` includes that mapped-output case.

| QA step | Command | EXIT | Result |
| --- | --- | --- | --- |
| Build after correction | `cmake --build build --target zene EffectDeviceMenuTest PatcherCommandsTest -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p18-24.r1/logs/qa-build-fixed.log 2>&1` | 0 | All requested targets built |
| Focused tests after correction | `ctest -R '^(EffectDeviceMenuTest|PatcherCommandsTest)$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p18-24.r1/logs/qa-tests-fixed.log 2>&1` (from `build/tests`) | 0 | 2/2 ctest tests passed |
| Reverse only the QA product hunk; rebuild | `git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p18-24.r1/logs/qa-fix-product.patch` (EXIT 0); `cmake --build build --target zene EffectDeviceMenuTest PatcherCommandsTest -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p18-24.r1/logs/qa-build-reversed.log 2>&1` (EXIT 0) | 0 | Rebuilt with the submitted APM-based implementation |
| Regression with QA hunk reversed | `ctest -R '^PatcherCommandsTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p18-24.r1/logs/qa-test-reversed.log 2>&1` (from `build/tests`) | 8 | New mapped-output assertion failed: `bus.quietChannels()[0]` was false at line 720 |
| Restore product hunk | `git checkout -- src/core/DeltaTap.cpp` (EXIT 0); `cmake --build build --target zene EffectDeviceMenuTest PatcherCommandsTest -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p18-24.r1/logs/qa-build-restored.log 2>&1` (EXIT 0) | 0 | Restored the QA correction from the index |
| Focused tests after restore | `ctest -R '^(EffectDeviceMenuTest|PatcherCommandsTest)$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p18-24.r1/logs/qa-tests-restored.log 2>&1` (from `build/tests`) | 0 | 2/2 ctest tests passed |
| Feature gates after correction | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p18-24 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p18-24.r1/logs/qa-gates-final.log 2>&1` | 0 | 8/8 rows PASS; complexity and file-length report no new items |
