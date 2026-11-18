FIXED
5457: ALREADY-FIXED

Evidence: the counter race is already fixed on this branch by commit `8e7ac2a15` (`fix: cluster new-p8-266 VST3 output counter race`). `plugins/Vst3Effect/Vst3HostInternals.h:104` declares `droppedOutput` as `std::atomic<std::uint64_t>`; `plugins/Vst3Effect/Vst3Host.cpp:553` resets it with relaxed `store`, line 822 increments it with relaxed `fetch_add`, and `plugins/Vst3Effect/Vst3HostReports.cpp:59` reads it with relaxed `load`. The existing regression is `Vst3HostTest::testDroppedOutputCounterUsesAtomicAccess` in `tests/src/plugins/Vst3HostTest.cpp:571-589`.

For the RED proof, I temporarily reconstructed the original plain-integer accesses, generated `fix-5457.patch`, and built the affected target. The existing regression failed at the atomic declaration assertion. Reversing that downgrade restored the atomic implementation and the same test passed. No production changes for 5457 were needed.

| Stage | Exact command | Exit |
| --- | --- | ---: |
| Capture plain-integer version | `git diff -- plugins/Vst3Effect/Vst3Host.cpp plugins/Vst3Effect/Vst3HostInternals.h plugins/Vst3Effect/Vst3HostReports.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5457.r1/logs/fix-5457.patch` | 0 |
| RED build | `cmake --build build --target zene Vst3HostTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5457.r1/logs/build-4.log 2>&1` | 0 |
| RED test (from `build/tests`) | `./Vst3HostTest testDroppedOutputCounterUsesAtomicAccess > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5457.r1/logs/test-4.log 2>&1` | 1 |
| Restore atomic implementation | `git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5457.r1/logs/fix-5457.patch` | 0 |
| GREEN build | `cmake --build build --target zene Vst3HostTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5457.r1/logs/build-5.log 2>&1` | 0 |
| GREEN test (from `build/tests`) | `./Vst3HostTest testDroppedOutputCounterUsesAtomicAccess > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5457.r1/logs/test-5.log 2>&1` | 0 |

new-p16-263: FIXED

Evidence: `HostedPlugin::activateOutputBuses(int)` previously calculated `last` at `plugins/Vst3Effect/Vst3HostReports.cpp:80` without rejecting a negative request. For `-1`, `last` became `-1`, the activation loop did not run, and the function returned `true`. It now returns `false` for `upTo < 0` at line 80 before calculating `last`. The deterministic regression `Vst3ChunkProbeTest::testNegativeOutputBusActivationDoesNotMutate` in `tests/src/plugins/Vst3ChunkProbeTest.cpp:192-205` checks the return value, bus activation flags, and layout. `Vst3Instrument::enableOutputBus()` is the only production caller; it forwards its `int bus` to this API at `plugins/Vst3Instrument/Vst3Instrument.cpp:248`. The guard is at the shared API boundary. Existing upper-bound and partial-activation cases also remain covered.

The original regression run failed because `activateOutputBuses(-1)` returned `true`. The reverse-patch proof below removed the guard, rebuilt `zene` and the test, and reproduced the same failure; restoring the patch made it pass.

| Stage | Exact command | Exit |
| --- | --- | ---: |
| Initial RED build | `cmake --build build --target Vst3ChunkProbeTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5457.r1/logs/build-1.log 2>&1` | 0 |
| Initial RED test (from `build/tests`) | `ctest -R '^Vst3ChunkProbeTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5457.r1/logs/test-1.log 2>&1` | 8 |
| Capture and reverse production patch | `git diff -- plugins/Vst3Effect/Vst3HostReports.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5457.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5457.r1/logs/fix.patch` | 0 |
| Reverse-patch build | `cmake --build build --target zene Vst3ChunkProbeTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5457.r1/logs/build-2.log 2>&1` | 0 |
| Reverse-patch RED test (from `build/tests`) | `ctest -R '^Vst3ChunkProbeTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5457.r1/logs/test-2.log 2>&1` | 8 |
| Restore production patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5457.r1/logs/fix.patch` | 0 |
| Restored-patch build | `cmake --build build --target zene Vst3ChunkProbeTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5457.r1/logs/build-3.log 2>&1` | 0 |
| Restored-patch GREEN test (from `build/tests`) | `ctest -R '^Vst3ChunkProbeTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5457.r1/logs/test-3.log 2>&1` | 0 |

## Final build and affected test families

| Stage | Exact command | Exit |
| --- | --- | ---: |
| Final build | `cmake --build build --target zene Vst3HostTest Vst3EffectIntegrationTest Vst3ChunkProbeTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5457.r1/logs/build-8.log 2>&1` | 0 |
| Final CTest family (from `build/tests`; 3 tests, all passed) | `ctest -R '^(Vst3HostTest|Vst3EffectIntegrationTest|Vst3ChunkProbeTest)$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5457.r1/logs/test-7.log 2>&1` | 0 |
| Feature gate | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-5457 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5457.r1/logs/gate-5.log 2>&1` | 0 |

Feature gate table: `fork-sources`, `all-sources-reproduce`, `no-upstream-regression`, `check-namespace`, `no-tautology`, `doc-figures`, `complexity` (no new item), and `file-length` (no new item) all PASS. Output: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5457.r1/logs/gate-5.log`.

Files committed: `docs/qwen-fixes-2026-10/cluster-5457.md` for 5457; `plugins/Vst3Effect/Vst3HostReports.cpp`, `tests/src/plugins/Vst3ChunkProbeTest.cpp`, and this report update for new-p16-263.

## Self-check

- RED/GREEN: `Vst3HostTest::testDroppedOutputCounterUsesAtomicAccess` failed with the plain-counter source (exit 1) and passed with the atomic source (exit 0); `Vst3ChunkProbeTest::testNegativeOutputBusActivationDoesNotMutate` failed without the guard (CTest exit 8) and passed with it (CTest exit 0). Final affected family: 3/3 CTest tests passed.
- Determinism: the counter test checks source-level type and every access, avoiding a nondeterministic data race; the bus test loads the in-tree fixture, injects `-1`, then checks return value and unchanged layout/state. No sleeps, clocks, or concurrent test threads were added.
- Completeness: all `activateOutputBuses` references were searched. The sole production caller is `Vst3Instrument::enableOutputBus`, which forwards `bus` unchanged; guarding the shared API rejects negative requests there too. The counter's declaration, reset, increment, and diagnostic read were all checked. No command or undo interface changed.
- Collateral: the guard only changes negative `upTo` requests; the existing valid partial-activation and upper-bound tests pass. The final VST3 host, effect integration, and chunk probe test targets passed. No project serialization or saved data changed.
- Already-fixed verdict: the atomic code and regression predate this batch in commit `8e7ac2a15`; see the exact file and line evidence under `5457: ALREADY-FIXED`.
