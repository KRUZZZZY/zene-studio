FIXED

new-p14-259: FIXED

Decision: REAL. `shmFifo::m_invalid` and the socket `RemotePluginBase::m_invalid` are concurrently read and written. The shared-memory flag now uses atomic loads/stores at `include/RemotePluginBase.h:159-166,441`; the socket flag uses atomic loads/stores at `include/RemotePluginBase.h:697-704,880-888,974`. The socket regression is `RemotePluginAudioPortsTest::remoteConnectionInvalidFlagIsAtomic()` at `tests/src/core/RemotePluginAudioPortsTest.cpp:311-316`; the shared-memory regression is `RemotePluginShmFifoTest::sharedMemoryInvalidFlagIsAtomic()` at `tests/src/core/RemotePluginShmFifoTest.cpp:44-52`.

RED/GREEN proof:

- Reversed only the atomic flag declarations/accesses with `git diff -- include/RemotePluginBase.h > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p14-259.r2/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p14-259.r2/logs/fix.patch` — EXIT=0. `cmake --build build --target RemotePluginShmFifoTest RemotePluginAudioPortsTest -j2` — EXIT=0, log `28-atomic-proof-red-build.log`. From `build/tests`, `ctest -R '^(RemotePluginShmFifoTest|RemotePluginAudioPortsTest)$' --output-on-failure` — EXIT=8, log `29-atomic-proof-red.log`; both atomic type assertions failed.
- Restored with `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p14-259.r2/logs/fix.patch` — EXIT=0. The same build command — EXIT=0, log `30-atomic-proof-green-build.log`; the same CTest command — EXIT=0, log `31-atomic-proof-green.log`, 2/2 CTest tests passed.

new-p14-261: FIXED

Decision: REAL. `std::hash<std::thread::id>` may collide, so it cannot be an ownership token. `shmFifo::callerKey()` now returns the address of a thread-local token, and `m_owner` stores that pointer at `include/RemotePluginBase.h:175-183,209-232,447`. `lock()`, `tryLockFor()` and `unlock()` use the same token. `callerTokensStayDistinctDespiteLegacyHashCollision()` at `tests/src/core/RemotePluginShmFifoTest.cpp:54-89` holds both threads live at comparison, verifies the injected legacy hashes collide, and verifies their actual FIFO tokens differ.

RED/GREEN proof:

- Reversed the caller token implementation with `git diff -- include/RemotePluginBase.h > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p14-259.r2/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p14-259.r2/logs/fix.patch` — EXIT=0. `cmake --build build --target RemotePluginShmFifoTest -j2` — EXIT=0, log `24-owner-proof-red-build.log`. From `build/tests`, `ctest -R '^RemotePluginShmFifoTest$' --output-on-failure` — EXIT=8, log `25-owner-proof-red.log`; the forced collision made the actual tokens equal.
- Restored with `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p14-259.r2/logs/fix.patch` — EXIT=0. The same build command — EXIT=0, log `26-owner-proof-green-build.log`; the same CTest command — EXIT=0, log `27-owner-proof-green.log`, 1/1 CTest test passed.

new-p15-170: FIXED

Decision: REAL. The recursive data-lock depth must be released while a blocking writer waits for space, and complete messages larger than `SHM_FIFO_SIZE` cannot be written under the token-after-completion protocol. `releaseLockForSpaceWait()` releases every recursive level and restores the previous depth at `include/RemotePluginBase.h:356-371,401-439`; writer reservation remains in place at `include/RemotePluginBase.h:338-353` and both send paths keep their ordering in `src/common/RemotePluginBase.cpp:141-162,277-312`. `sendMessage()` now checks the complete serialized size before writing any field at `src/common/RemotePluginBase.cpp:277-312`; an oversized frame returns 0, invalidates the local endpoints, and signals the outgoing FIFO. The test at `tests/src/core/RemotePluginShmFifoTest.cpp:91-118` sends two individually valid 300 KiB strings and makes a zero-budget receive verify the peer observes `Lost` rather than waiting for a token.

RED/GREEN proof — recursive lock release:

- Reversed full-depth release with `git diff -- include/RemotePluginBase.h > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p14-259.r2/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p14-259.r2/logs/fix.patch` — EXIT=0. `cmake --build build --target RemotePluginShmFifoTest -j2` — EXIT=0, log `32-lock-proof-red-build.log`. From `build/tests`, `ctest -R '^RemotePluginShmFifoTest$' --output-on-failure` — EXIT=8, log `33-lock-proof-red.log`; `readerAcquired` failed.
- Restored with `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p14-259.r2/logs/fix.patch` — EXIT=0. The same build command — EXIT=0, log `34-lock-proof-green-build.log`; the same CTest command — EXIT=0, log `35-lock-proof-green.log`, 1/1 CTest test passed.

RED/GREEN proof — oversized aggregate send path:

- `git diff -- include/RemotePluginBase.h src/common/RemotePluginBase.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p14-259.r2/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p14-259.r2/logs/fix.patch` — EXIT=0. `cmake --build build --target RemotePluginShmFifoTest -j2` — EXIT=0, log `38-aggregate-isolated-red-build.log`. From `build/tests`, `ctest -R '^RemotePluginShmFifoTest$' --output-on-failure` — EXIT=8, log `39-aggregate-isolated-red.log`; `oversizedAggregateMessageFailsAndReleasesTheReceiver()` observed the old space-wait path.
- Restored with `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p14-259.r2/logs/fix.patch` — EXIT=0. The same build command — EXIT=0, log `40-aggregate-isolated-green-build.log`; the same CTest command — EXIT=0, log `41-aggregate-isolated-green.log`, 1/1 CTest test passed.

Final evidence:

| Item | RED | GREEN | Affected-family count |
|---|---|---|---|
| new-p14-259 | `ctest -R '^(RemotePluginShmFifoTest|RemotePluginAudioPortsTest)$' --output-on-failure` — EXIT=8 | Same command — EXIT=0 | 2/2 CTest tests |
| new-p14-261 | `ctest -R '^RemotePluginShmFifoTest$' --output-on-failure` — EXIT=8 | Same command — EXIT=0 | 1/1 CTest test |
| new-p15-170 | `ctest -R '^RemotePluginShmFifoTest$' --output-on-failure` — EXIT=8 for each isolated regression | Same command — EXIT=0 for each isolated regression | 1/1 CTest test per regression |
| Final focused suite | — | `ctest -R '^(RemotePluginShmFifoTest|RemotePluginAudioPortsTest|RemotePluginClientE2ETest)$' --output-on-failure` — EXIT=0 | 3/3 CTest tests |
| Feature gate | — | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p14-259` — EXIT=0 | All 8 gates PASS |
| Completion gate | — | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/submit_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p14-259 new-p14-259` — EXIT=0, log `47-submit-check-final.log` | PASS |

Final build command: `cmake --build build --target zene RemotePluginShmFifoTest RemotePluginAudioPortsTest RemotePluginClientE2ETest -j2` — EXIT=0, log `42-final-build.log`. The final focused CTest command above — EXIT=0, log `43-final-tests.log` (3/3). The final feature gate table is in `48-feature-check-final.log`: fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures, complexity and file-length all PASS; complexity/file-length reported no new item.

## Self-check

- **Decisions:** each cluster is REAL and FIXED; evidence and failing assertions are given above.
- **Not-real verdict check:** not applicable; no cluster is marked ALREADY-FIXED or FALSE-POSITIVE. The REAL decisions are supported by the concurrent accesses, unspecified hash uniqueness, and whole-frame token protocol cited above.
- **Determinism:** the atomic tests inspect member types; the ownership test holds two live threads at barriers and forces equal legacy hashes; the lock test uses `tryLockFor(0)`; the oversized-send test uses a test hook and an immediate deadline (`time_point::min()`), with no test sleep or elapsed-time assertion.
- **Cause and sibling paths:** both shared-memory and socket invalidation flags use atomics; `lock()`, `tryLockFor()` and `unlock()` share the TLS token; the blocking writer and nonblocking audio sender retain writer ordering. Only the shared-memory blocking path rejects frames whose aggregate serialized bytes exceed the FIFO; the socket path and valid shared-memory frames retain their wire behavior.
- **Realtime and collateral:** the size preflight does not allocate, wait, or take a new lock. The audio send path remains nonblocking. No saved-data format, undo behavior, or control command changed.
- **Registration:** `RemotePluginShmFifoTest` is already registered in `tests/fork-sources.txt`; its target and shared-memory sources are in `tests/CMakeLists.txt`, with inherited changes declared in `tests/upstream-modifications.txt`.
- **Gates and family count:** final focused CTest was 3/3; `feature_check.py` exited 0 with the eight PASS rows above (log `48-feature-check-final.log`); the post-commit `submit_check.py` exited 0 (log `47-submit-check-final.log`). The earlier pre-commit submit check exited 1 only because the report was still modified.
- **Files committed in `b5ecf3f46`:** `include/RemotePluginBase.h`, `src/common/RemotePluginBase.cpp`, `tests/CMakeLists.txt`, `tests/src/core/RemotePluginShmFifoTest.cpp`, `tests/upstream-modifications.txt`, and this report.
