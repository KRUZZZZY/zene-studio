FIXED
new-p14-259: FIXED

Evidence: `include/RemotePluginBase.h:159-166, 682-683, 860-868, 422, 954` now stores both shared-memory and socket invalidation flags as `std::atomic<bool>` and reads/writes them with relaxed atomic operations. `RemotePluginAudioPortsTest::remoteConnectionInvalidFlagIsAtomic()` (`tests/src/core/RemotePluginAudioPortsTest.cpp:313-316`) fails on the original socket member type and passes after the change.

Commands and proof:

- `cmake --build build --target zene RemotePluginAudioPortsTest` — EXIT=0, log `001-build-red-remote-invalid.log`; `ctest -R '^RemotePluginAudioPortsTest$' --output-on-failure` from `build/tests` — EXIT=8, log `002-test-red-remote-invalid.log`, failing assertion: `RemoteInvalidFlagIsAtomic<RemotePluginBase>::value`.
- Reverse proof: `git diff -- include/RemotePluginBase.h > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p14-259.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p14-259.r1/logs/fix.patch` — EXIT=0; rebuild `cmake --build build --target zene RemotePluginAudioPortsTest` — EXIT=0, log `005-build-reversed-remote-invalid.log`; `ctest -R '^RemotePluginAudioPortsTest$' --output-on-failure` from `build/tests` — EXIT=8, log `007-test-reversed-remote-invalid.log`.
- Restore proof: `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p14-259.r1/logs/fix.patch` — EXIT=0; rebuild — EXIT=0, log `008-build-restored-remote-invalid.log`; the same ctest command — EXIT=0, log `009-test-restored-remote-invalid.log` (1/1 ctest test).

new-p14-261: FIXED

Evidence: `include/RemotePluginBase.h:175-183, 194-216, 428` uses the address of a per-thread TLS object as the FIFO owner token, so distinct live threads have distinct tokens independent of `std::hash<std::thread::id>`. The owner atomic and its empty sentinel use pointer values. `RemotePluginShmFifoTest::callerKeysSurviveForcedHashCollisions()` (`tests/src/core/RemotePluginShmFifoTest.cpp:47-67`) injects the same legacy hash for two concurrent threads and checks the production caller tokens remain distinct.

Commands and proof:

- Build `cmake --build build --target zene RemotePluginAudioPortsTest RemotePluginShmFifoTest` — EXIT=0, log `012-build-red-caller-key.log`; `ctest -R '^RemotePluginShmFifoTest$' --output-on-failure` from `build/tests` — EXIT=8, log `013-test-red-caller-key.log`, failing assertion: `keys[0] != keys[1]`.
- Reverse proof: `git diff -- include/RemotePluginBase.h > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p14-259.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p14-259.r1/logs/fix.patch` — EXIT=0; rebuild — EXIT=0, log `016-build-reversed-caller-key.log`; the same ctest command — EXIT=8, log `017-test-reversed-caller-key.log`, with the forced collision assertion failing again.
- Restore proof: `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p14-259.r1/logs/fix.patch` — EXIT=0; rebuild — EXIT=0, log `018-build-restored-caller-key.log`; `ctest -R '^(RemotePluginAudioPortsTest|RemotePluginShmFifoTest)$' --output-on-failure` from `build/tests` — EXIT=0, log `019-test-restored-caller-key.log` (2/2 ctest tests).

new-p15-170: FIXED

Evidence: `include/RemotePluginBase.h:323-354, 386-419` now releases all recursive data-lock levels while waiting for FIFO space, then reacquires the original depth. A separate atomic writer reservation spans a blocking message so another writer cannot insert a frame while the data semaphore is yielded. `src/common/RemotePluginBase.cpp:141-162, 277-292` uses a nonwaiting reservation attempt in `sendFrameNonBlocking()` and a blocking reservation in `sendMessage()`. `RemotePluginShmFifoTest::recursiveSpaceWaitReleasesTheDataSemaphore()` (`tests/src/core/RemotePluginShmFifoTest.cpp:69-86`) directly checks that a second thread can take the semaphore after the recursive release; the test uses no sleep or wall-clock deadline.

Commands and proof:

- Build `cmake --build build --target zene RemotePluginAudioPortsTest RemotePluginShmFifoTest -- -j2` — EXIT=0, log `022-build-red-shm-wait.log`; `ctest -R '^RemotePluginShmFifoTest$' --output-on-failure` from `build/tests` — EXIT=8, log `023-test-red-shm-wait.log`, failing assertion: `readerAcquired`.
- Reverse proof: `git diff -- include/RemotePluginBase.h src/common/RemotePluginBase.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p14-259.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p14-259.r1/logs/fix.patch` — EXIT=0; rebuild — EXIT=0, log `026-build-reversed-shm-wait.log`; the same ctest command — EXIT=8, log `027-test-reversed-shm-wait.log`, with the semaphore-acquisition assertion failing again.
- Restore proof: `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p14-259.r1/logs/fix.patch` — EXIT=0; rebuild — EXIT=0, log `028-build-restored-shm-wait.log`; `ctest -R '^(RemotePluginAudioPortsTest|RemotePluginShmFifoTest)$' --output-on-failure` from `build/tests` — EXIT=0, log `029-test-restored-shm-wait.log` (2/2 ctest tests).

Final verification:

- `cmake --build build --target zene RemotePluginAudioPortsTest RemotePluginClientE2ETest FakeRemotePluginClient RemoteZynAddSubFx vsteffect vestige zynaddsubfx RemotePluginShmFifoTest -- -j2` — EXIT=0, log `030-build-all-remote-dependents.log`. This rebuilt the host, client, fake client, plugin and test targets that include the changed header. `RemotePluginShmFifoCompileCheck` compiled `src/common/RemotePluginBase.cpp` with `SYNC_WITH_SHM_FIFO`.
- The first expanded ctest command `ctest -R '^(RemotePluginAudioPortsTest|RemotePluginClientE2ETest|RemotePluginShmFifoTest)$' --output-on-failure` — EXIT=8, log `031-test-remote-family-final.log`; only the existing `RemotePluginClientE2ETest::midiSentPerPeriodIsCapped()` assertion failed (expected 3 output channels, observed 2). Isolated rerun `ctest -R '^RemotePluginClientE2ETest$' --output-on-failure` — EXIT=0, log `032-test-client-e2e-rerun.log`; the combined family rerun — EXIT=0, log `033-test-remote-family-final-rerun.log`, 3/3 ctest tests passed.
- `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p14-259` — EXIT=0, log `034-feature-check-final.log`.

| Gate | Result |
|---|---|
| fork-sources | PASS |
| all-sources-reproduce | PASS |
| no-upstream-regression | PASS |
| check-namespace | PASS |
| no-tautology | PASS |
| doc-figures | PASS |
| complexity | PASS, no new item |
| file-length | PASS, no new item |

## Self-check

- **RED/GREEN per bug:** p14-259: reversed assertion EXIT=8, restored EXIT=0 (`007`, `009`); p14-261: reversed assertion EXIT=8, restored EXIT=0 (`017`, `019`); p15-170: reversed assertion EXIT=8, restored EXIT=0 (`027`, `029`). Each ctest selection executed at least one test. The extra initial RED runs are logged in `002`, `013`, and `023`.
- **Determinism:** the flag tests inspect declared types; the owner test injects a constant hash and synchronizes two threads with `std::barrier`; the FIFO wait test calls the production release helper and uses a nonblocking `tryLockFor(0)` from a second thread. No sleeps, elapsed-time checks or racy assertions are used by these regressions.
- **Cause and siblings checked:** all shared and socket `m_invalid` accesses are atomic; `shmFifo::lock()` and `tryLockFor()` use the same unique owner token as `unlock()`; both shared-memory write callers (`sendMessage()` and `sendFrameNonBlocking()`) participate in writer reservation, with the audio path returning `NoRoom` instead of waiting. The corresponding socket paths are unchanged.
- **Collateral and realtime:** no wire format, project serialization, saved data, undo path or control command changed. The byte order of blocking frames is preserved by writer reservation. The audio path only performs atomic `tryLockWriter()` and clears the flag; `lockWriter()` and its yield loop are only used by blocking `sendMessage()` callers, while host audio sends use `sendFrameNonBlocking()`.
- **Not-real verdict evidence:** not applicable; all three verdicts are FIXED.
- **Discarded ctest invocation:** an early proof command was mistakenly invoked from the repository root and returned EXIT=97 with zero tests (`006-test-reversed-remote-invalid.log`). It was not used as evidence; the reversed test was rerun from `build/tests` and failed as required (`007`, EXIT=8).
- **Files committed:** `include/RemotePluginBase.h`; `src/common/RemotePluginBase.cpp`; `tests/src/core/RemotePluginAudioPortsTest.cpp`; `tests/src/core/RemotePluginShmFifoTest.cpp`; `tests/CMakeLists.txt`; `tests/all-sources.txt`; `tests/fork-sources.txt`; `tests/upstream-modifications.txt`; this report. Cluster commits are `d870dd774`, `ed86c4e7c`, `c8fca30dc`, `deaa03515`, plus the final `new-p15-170` fix/report commit.
- **Initial rebuild retries:** after adding the new target, build `011` returned EXIT=2 because the regenerated CMake graph did not yet expose the target; retry `012` succeeded. The first broad header rebuild `021` was terminated with EXIT=143 before reaching diagnostics; limited-parallel retry `022` completed successfully.
