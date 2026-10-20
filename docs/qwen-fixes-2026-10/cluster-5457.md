ALREADY-FIXED
5457: ALREADY-FIXED

Evidence: the counter race is already fixed on this branch by commit `8e7ac2a15` (`fix: cluster new-p8-266 VST3 output counter race`). `plugins/Vst3Effect/Vst3HostInternals.h:104` declares `droppedOutput` as `std::atomic<std::uint64_t>`; `plugins/Vst3Effect/Vst3Host.cpp:553` resets it with relaxed `store`, line 822 increments it with relaxed `fetch_add`, and `plugins/Vst3Effect/Vst3HostReports.cpp:59` reads it with relaxed `load`. The existing regression is `Vst3HostTest::testDroppedOutputCounterUsesAtomicAccess` in `tests/src/plugins/Vst3HostTest.cpp:571-589`.

For a deterministic RED proof of that existing regression, I temporarily reconstructed the original plain-integer accesses, built the affected target, and ran only the regression. It failed at the atomic declaration assertion. I restored the atomic code and reran the same test successfully. No production changes for 5457 were needed.

| Stage | Exact command | Exit |
| --- | --- | ---: |
| RED build | `cmake --build build --target zene Vst3HostTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5457.r1/logs/build-4.log 2>&1` | 0 |
| RED test (from `build/tests`) | `./Vst3HostTest testDroppedOutputCounterUsesAtomicAccess > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5457.r1/logs/test-4.log 2>&1` | 1 |
| Restore atomic implementation | `git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5457.r1/logs/fix-5457.patch` | 0 |
| GREEN build | `cmake --build build --target zene Vst3HostTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5457.r1/logs/build-5.log 2>&1` | 0 |
| GREEN test (from `build/tests`) | `./Vst3HostTest testDroppedOutputCounterUsesAtomicAccess > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5457.r1/logs/test-5.log 2>&1` | 0 |

Files committed for 5457: `docs/qwen-fixes-2026-10/cluster-5457.md`.
