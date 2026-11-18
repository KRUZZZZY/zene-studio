# Cluster new-p8-266 — FIXED

The output-event drop counter was a plain `std::uint64_t`: the audio thread incremented it in `HostedPlugin::Impl::collectOutputEvents()`, while `HostedPlugin::droppedOutputEvents()` could read it from a diagnostics thread. The counter is now atomic, with relaxed `store`, `fetch_add`, and `load` operations.

## File evidence

| Location | Evidence |
| --- | --- |
| `plugins/Vst3Effect/Vst3HostInternals.h:104` | `droppedOutput` is `std::atomic<std::uint64_t>`. |
| `plugins/Vst3Effect/Vst3Host.cpp:518` | `prepare()` resets it with relaxed `store`. |
| `plugins/Vst3Effect/Vst3Host.cpp:774` | `collectOutputEvents()` counts overflow with relaxed `fetch_add`. |
| `plugins/Vst3Effect/Vst3HostReports.cpp:57-59` | `droppedOutputEvents()` reads it with relaxed `load`. |
| `tests/src/plugins/Vst3HostTest.cpp:435-453` | Regression checks the declaration and reset, increment, and read operations. |

The regression checks the private implementation source directly. A concurrent plain-integer read has undefined behavior and would not provide a deterministic failure without a race detector; this check fails deterministically on the original declaration and verifies every access is atomic.

## Build and test evidence

The affected CTest family contains two tests: `Vst3HostTest` and `Vst3EffectIntegrationTest`. The final family run executed both and passed both. Reversing the product patch made the host test fail at the dropped-counter assertion while the integration test passed.

| Stage | Exact command | Exit |
| --- | --- | ---: |
| Original RED build | `cmake --build build --target Vst3HostTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-266.r1/logs/red-build-1.log 2>&1` | 0 |
| Original RED test | `ctest -R Vst3HostTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-266.r1/logs/red-test-1.log 2>&1` | 8 |
| Interim test check (assertion prefix mismatch) | `ctest -R 'Vst3HostTest\|Vst3EffectIntegrationTest' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-266.r1/logs/green-test-1.log 2>&1` | 8 |
| Corrected regression rerun | `ctest -R 'Vst3HostTest\|Vst3EffectIntegrationTest' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-266.r1/logs/green-test-2.log 2>&1` | 0 |
| Reverse-patch build | `cmake --build build --target zene Vst3HostTest vst3effect Vst3EffectIntegrationTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-266.r1/logs/proof-red-build-1.log 2>&1` | 0 |
| Reverse-patch RED test | `ctest -R 'Vst3HostTest\|Vst3EffectIntegrationTest' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-266.r1/logs/proof-red-test-1.log 2>&1` | 8 |
| Restored-patch build | `cmake --build build --target zene Vst3HostTest vst3effect Vst3EffectIntegrationTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-266.r1/logs/proof-green-build-1.log 2>&1` | 0 |
| Restored-patch GREEN test | `ctest -R 'Vst3HostTest\|Vst3EffectIntegrationTest' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-266.r1/logs/proof-green-test-1.log 2>&1` | 0 |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-266.r1/logs/gate-complexity-2.log 2>&1` | 0 |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-266.r1/logs/gate-file-length-2.log 2>&1` | 0 |

The reverse-patch proof used these exact commands; each exited 0:

```sh
git diff -- plugins/Vst3Effect/Vst3HostInternals.h plugins/Vst3Effect/Vst3Host.cpp plugins/Vst3Effect/Vst3HostReports.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-266.r1/logs/fix.patch
git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-266.r1/logs/fix.patch
git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-266.r1/logs/fix.patch
```

An intermediate green attempt exited 8 because the new test expected a `d.` prefix on the increment inside `Impl`; the check was corrected to match the member access. That attempt is in `green-test-1.log`. The same regression then passed in `green-test-2.log` and again in the restored-patch proof above.

## Files committed

- `plugins/Vst3Effect/Vst3HostInternals.h`
- `plugins/Vst3Effect/Vst3Host.cpp`
- `plugins/Vst3Effect/Vst3HostReports.cpp`
- `tests/src/plugins/Vst3HostTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p8-266.md`
