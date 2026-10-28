Verdict: FIXED

new-p18-24: DeltaTap marks a zero delta as non-silent

Evidence: both `DeltaTap::process` overloads unconditionally marked the block non-silent after subtracting the dry copy. The bus overload cleared quiet bits 0 and 1 at the original `src/core/DeltaTap.cpp:63-64`; the buffer overload called `assumeNonSilent(0)` and `assumeNonSilent(1)` at the original `src/core/DeltaTap.cpp:82-83`. A bypassed device therefore produced an exact zero delta, and the mixer was still told it held audio. Both overloads are live: `EffectChain.cpp:357` (AudioBuffer) and `EffectChain.cpp:391` (AudioBus) call `deltaTap().process`, and `RoutingChainNodes.cpp:98` calls the AudioBuffer overload from the routing graph.

The fix measures the delta instead of assuming it, using the same calls the effect already runs on its own output:

- Bus overload, `src/core/DeltaTap.cpp:62-65`: after the subtraction, `bus.update(*apm)` when the effect has an `AudioPortsModel`, else `bus.updateAll()`. This mirrors `Effect.cpp:351` (`apm ? inOut.update(*apm) : inOut.updateAll()`).
- Buffer overload, `src/core/DeltaTap.cpp:83-84`: `buffer.updateSilenceFlags(0b11)`. This mirrors `Effect.cpp:283` (`inOut.updateSilenceFlags(0b11)`). With silence tracking off, it still clears the flags, so the non-silent assumption is unchanged there (`AudioBuffer.cpp:326-335`).

Behaviour: a bypassed device with silent input now yields a quiet block, so the mixer skips it, which is the intent. A silent output's delta is the inverted input and stays non-silent, because the delta is measured, not the output. Tap off: unchanged, since the tap returns before any of this.

Regression: `tests/src/core/PatcherCommandsTest.cpp` slot `theDeltaTapMeasuresTheSilenceOfItsDelta()`. It covers: bus, passthrough, silent block is quiet (RED site); bus, doubling, signal stays non-silent; buffer with tracking on, passthrough silent, flags set (RED site); buffer with tracking on, doubling, flags clear; buffer with tracking off, passthrough, flags stay clear. Both the "quiet" and the "non-silent" sides are checked, so a version that always marks quiet also fails. The test has no timing, sleeps or races.

Realtime: the fix calls only `AudioBus::update`/`updateAll` and `AudioBuffer::updateSilenceFlags`, which the effect already calls once per block on the audio thread. No new allocation, locking or logging. `PatcherCommandsTest` still contains the existing zero-allocation check for the patched graph, which passes.

Scope: `DeltaTap.cpp` and `PatcherCommandsTest.cpp` are the only changed files. No header changed, so no dependent rebuild was needed. No command, no registration (both files already registered in `tests/fork-sources.txt`).

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

Gate note: a direct run of `feature_check.py` (`logs/gates-1.log`) exits 1 with `PermissionError`. It tries to create `<worktree>-feature-check/` next to the worktree, outside the write-jail. The gate's logic was not changed. The table above comes from running the same script with its log folder pointed into the run folder.
