FIXED

Cluster ci-61ad26e4f8b135f9 (CI regression: `build / macos-x86_64`, train 050/m0-train-31 at
`c20ef591b571e3c3dc7b108107d166497f1b34be`, job 132496796528, log
`run-43611324689-job-132496796528.log` line 1731).

## Verdict and evidence

REAL, product and test side. The hosted `macos-x86_64` job builds for a deployment target below
macOS 11 (this project's supported floor is 10.13: `CMakeLists.txt:264-278`,
`README.md:22-38`), and Apple's libc++ marks the C++20 synchronisation primitives unavailable
there - `_LIBCPP_AVAILABILITY_SYNC`, which is `availability(macos, strict, introduced = 11.0)`
on `<barrier>`, `<latch>`, `<semaphore>` and on `std::atomic`'s `wait` / `notify_one` /
`notify_all`. The job stopped on the fork-new regression test:

    tests/src/core/RemotePluginShmFifoTest.cpp:109:16: error: 'barrier' is unavailable: introduced in macOS 11.0 unknown
    tests/src/core/RemotePluginShmFifoTest.cpp:117:10: error: 'arrive_and_wait' is unavailable: introduced in macOS 11.0 unknown

Site list, all confirmed by reading the tree and by the new check's RED run
(`logs/19-step4b-full-revert-check.log`, 26 failing lines):

| Site | Gated construct |
| --- | --- |
| `tests/src/core/RemotePluginShmFifoTest.cpp:21,109-111,117-131` | `<barrier>`, `std::barrier` x3 |
| `tests/src/core/NotePlaybackTest.cpp:47,234,243` | `<barrier>`, `std::barrier` |
| `tests/src/core/TempoMapTest.cpp:57,563-564,571-572,581,601` | `<latch>`, `std::latch` x2 |
| `tests/src/core/ModulationAudioViewTest.cpp:50,228-229,277-278,431,526` | `<semaphore>`, `std::binary_semaphore` x6 |
| `tests/src/plugins/PluginPortsMigrationTest.cpp:48,4426,4431,4441` | `<barrier>`, `std::barrier` |
| `tests/src/wasm/WasmWorkerPoolTest.cpp:59,65,415,468,530` | `<barrier>`, `<semaphore>` |
| `src/core/AudioEngine.cpp:672` | `std::atomic<int>::notify_all` (test seam, product TU) |
| `tests/src/core/ScriptDawModelRegistrationTest.cpp:124,134,144` | `std::atomic<int>::wait` / `notify_all` (the seam's one consumer) |
| `src/wasm/WasmWorkerPool.cpp:84,153,234` | `std::atomic<uint64_t>::notify_all` / `notify_one` / `wait` (product TU, `WANT_WASM`) |

The product already answered this class once: `src/core/AudioWorkerWake.cpp:37-165` and
`include/AudioWorkerWake.h:61-115` replace the audio worker's wake with `__ulock_wait` /
`__ulock_wake` (macOS 10.12) and a Linux futex, with a compile-time assertion that an Apple
build takes that path. `src/wasm/WasmWorkerPool` was the one remaining product wake still
built on `std::atomic::wait`/`notify_*`, and `AudioEngine`'s test seam still called
`notify_all` even though its only consumer is a control-thread test.

Why this is not a test-infrastructure artefact: two earlier trains
(`710d06ff8f`, `203b137e33`) hid the same failure by raising the *test targets'* deployment
floor to 11.0. Those commits are not ancestors of this branch (`git merge-base --is-ancestor
203b137e33 HEAD` exits 1), and re-applying the trick would compile the tests for an OS the
product does not support while leaving the same primitives in product code. Their own round-7
follow-up (`24fbd10f1f`) already dropped `AudioEngine`'s `notify_all` for a poll, which is
independent confirmation that the product half is real rather than a macOS runner quirk.

## Fix

At the cause, in this order:

1. `tests/src/core/PortableSync.h` (new): `lmms::testsupport::Barrier`, `Latch` and
   `BinarySemaphore`, implemented with `std::mutex` + `std::condition_variable` (available long
   before macOS 10.13), with the same contracts the tests used - fixed-count rendezvous,
   one-shot gate, and `try_acquire_for` for the timed "did the writer reach the model" checks.
   Predicated waits, so a spurious wake cannot release a thread early.
2. The six test TUs above use it. No assertion, count, timeout or ordering changed; the
   replacements are mechanical (`std::barrier start{3}` -> `testsupport::Barrier start{3}`).
3. `src/core/AudioEngine.cpp:666-674`: the seam's `compare_exchange_strong` is kept and the
   `notify_all` is gone, with the reason in a comment. The store was always the signal; its one
   consumer (`tests/src/core/ScriptDawModelRegistrationTest.cpp:121-131`, `:134`, `:143`) now
   re-reads the flag with `std::this_thread::yield()` in the loop instead of `wait`, the same
   shape the round-7 train used. Nothing new runs on the audio thread: this seam is
   `LMMS_TESTING`-only and is consulted on the control thread.
4. `src/wasm/WasmWorkerPool.{h,cpp}`: the pool parks and posts through
   `lmms::workerwake::notify` / `notifyOne` / `wait` (the audio worker's own primitive) instead of
   `m_generation.notify_*` / `wait`, and the generation is `std::atomic<uint32_t>` because the
   trap reads a 32-bit word. `workerwake::notify` wakes every waiter, which is what
   `notify_all` meant in `shutdown()`; `notifyWork()` uses `workerwake::notifyOne`, preserving
   the single-waiter shape `std::atomic::notify_one` had (see "Round 2 rework" below), and the
   existing `m_idleLanes` check keeps it to the parked case, so the audio thread still takes no
   lock and allocates nothing. `tests/CMakeLists.txt` compiles
   `src/core/AudioWorkerWake.cpp` into `WasmWorkerPoolTest`, which builds the pool's sources
   directly instead of linking `lmmsobjs`.

## Regression test

`tests/macos-sync-availability-check.py`, registered as ctest `MacosSyncAvailabilityCheck`
(`tests/CMakeLists.txt:6202-6216`) and in `tests/fork-sources.txt` (both recipe copies).

It has two halves, and both run on the Linux development box, because a macOS-only check would
be untestable here:

1. COMPILE MODEL. A shadow `<barrier>`, `<latch>`, `<semaphore>` directory ahead of the system
   headers refuses a *direct* include exactly as libc++ refuses the declarations. Seven real
   translation units (plus the two wasm ones when `WANT_WASM` is on) are recompiled from the
   build's own `compile_commands.json` with `-fsyntax-only`. Red and green probes run first, so
   a model that cannot fail fails the check.
2. SOURCE SCAN. `std::atomic`'s `wait`/`notify_*` are class members of the real `<atomic>` and
   cannot be shadowed by a header, so the check scans every first-party source for a gated type
   or header and for a declared `std::atomic<...>` object that is the receiver of `.wait(`,
   `.notify_one(` or `.notify_all(`. Comments are blanked first (newlines preserved, so line
   numbers are exact) and the one audited exception is
   `src/core/AudioWorkerWake.cpp:148-158`: its `generation.wait(...)` sits behind
   `#ifndef LMMS_WORKER_WAKE_WAIT_ON_ADDRESS`, which Apple and Linux both define, so no Apple
   build compiles it.

There is no thread, no sleep and no clock in the check: a fixed set of compiler invocations and
a fixed scan over fixed inputs.

RED -> GREEN, all with `QT_QPA_PLATFORM=offscreen`:

    python3 tests/macos-sync-availability-check.py --root . --build-dir build   # before fix
    -> exit 1; 26 FAIL lines, e.g. tests/src/core/RemotePluginShmFifoTest.cpp:109
       "std::barrier is macOS 11 only"; src/core/AudioEngine.cpp:672

    ctest -R MacosSyncAvailabilityCheck --output-on-failure   # from build/tests, before fix
    -> exit 8, test 789 ***Failed

    python3 tests/macos-sync-availability-check.py --root . --build-dir build   # after fix
    -> exit 0: "PASS: 7 translation units compile ..." and the scan PASS

    ctest -R MacosSyncAvailabilityCheck --output-on-failure
    -> exit 0, test 789 Passed (21-24 s)

The full family rebuilt and green (log `40-ctest-final-recheck.log`):

    cmake --build build --target zene RemotePluginShmFifoTest ScriptDawModelRegistrationTest \
        NotePlaybackTest TempoMapTest ModulationAudioViewTest PluginPortsMigrationTest \
        AudioWorkerWakeTest AudioEngineTeardownTest ThreadPoolTest   -> exit 0
    cd build/tests && ctest -R "MacosSyncAvailabilityCheck|RemotePluginShmFifoTest|\
        ScriptDawModelRegistrationTest|NotePlaybackTest|TempoMapTest|ModulationAudioViewTest|\
        PluginPortsMigrationTest|AudioWorkerWakeTest|AudioEngineTeardownTest|ThreadPoolTest"
    -> exit 0, 10/10 passed

The optional wasm half was proved by hand, because this tree is configured with
`WANT_WASM=OFF` (a configure is the parent lane's job, so no second build tree was made):
`g++ -std=c++20 -fsyntax-only` over `src/wasm/WasmWorkerPool.cpp` and
`tests/src/wasm/WasmWorkerPoolTest.cpp` (with Qt 6 and wasmtime 48.0.1 headers) both exit 0
(logs `04-wasm-pool-syntax.log`, `05-wasm-test-syntax.log`), and a link probe that instantiates
the pool and calls `stats()` links against `libwasmtime.a` and exits 0 (`23-wasm-link-probe.log`).

### Step 4b - revert / reapply

Product hunks (`src/core/AudioEngine.cpp`, `src/wasm/WasmWorkerPool.{h,cpp}`, captured in
`logs/fix.patch`):

    git apply -R logs/fix.patch                        -> exit 0
    cmake --build build --target zene ScriptDawModelRegistrationTest   -> exit 0
    ctest -R MacosSyncAvailabilityCheck                -> exit 8 (test 789 Failed)
      FAIL: src/core/AudioEngine.cpp:672 std::atomic 'attemptState' is waited/notified
    git apply logs/fix.patch                           -> exit 0
    cmake --build build --target zene ScriptDawModelRegistrationTest   -> exit 0
    ctest -R MacosSyncAvailabilityCheck                -> exit 0 (test 789 Passed)

Test hunks (`logs/fix-tests.patch`), separately, because the product half alone must also be
caught:

    git apply -R logs/fix-tests.patch                  -> exit 0
    cmake --build build --target RemotePluginShmFifoTest ScriptDawModelRegistrationTest -> exit 0
    python3 tests/macos-sync-availability-check.py ... -> exit 1; 26 FAIL lines
    ctest -R MacosSyncAvailabilityCheck                -> exit 8 (test 789 Failed)
    git apply logs/fix-tests.patch                     -> exit 0
    cmake --build build --target ... six test targets  -> exit 0
    ctest -R MacosSyncAvailabilityCheck                -> exit 0 (test 789 Passed)

Both halves together reverted also exit 1 / 8 (`19-...`, `20-...`).

The six runtime tests named in the round-1 review
(`ModulationAudioViewTest`, `NotePlaybackTest`, `PluginPortsMigrationTest`,
`RemotePluginShmFifoTest`, `ScriptDawModelRegistrationTest`, `TempoMapTest`) are
behaviour-preservation checks: they drive the same workflows through the replacement
`PortableSync` primitives, so they pass when the round-1 product change is reverted. They are not
the macOS proof; `MacosSyncAvailabilityCheck`'s compile model is, and it fails on the reverted
product source (`src/core/AudioEngine.cpp`'s `notify_all`). The round-2 regression proof is the
new single-waiter source case described next.

## Round 2 rework - single-waiter WASM work post

The round-1 tip `721e386ba32d04bc391ae08446d848fe1fef3bf9` sent every
`WasmWorkerPool::notifyWork()` through `lmms::workerwake::notify`, whose platform half woke all
waiters (`__ulock_wake` with `ULF_WAKE_ALL`, `FUTEX_WAKE_PRIVATE` with `INT_MAX`). Before the
macOS fix the same call was `m_generation.notify_one()`, so one queued block woke every parked
lane and each one scanned the worker list - a behaviour regression on the audio-driven path, not
the original build failure.

The rework restores the old shape at the platform helper:

| Site | Behaviour |
| --- | --- |
| `include/AudioWorkerWake.h:100-104` | `notifyOne()` declared beside `notify()`: same contract, wake at most one waiter |
| `src/core/AudioWorkerWake.cpp:79-123` | one shared `wake(generation, all)` post; Apple omits `ULF_WAKE_ALL`, Linux passes `1` instead of `INT_MAX`, the fallback calls `notify_one()`; `notify()` keeps the wake-all values |
| `src/wasm/WasmWorkerPool.cpp:143-158` | `notifyWork()` calls `workerwake::notifyOne` |
| `src/wasm/WasmWorkerPool.cpp:77-89` | `shutdown()` keeps `workerwake::notify`, because every parked lane must see the stop flag |
| `src/wasm/WasmWorkerPool.h:50-62` | the threading contract says "wakes ONE lane", with shutdown named as the exception |

The other caller of `notify()` is `AudioEngineWorkerThread::wakeWorkers()`
(`src/core/AudioEngineWorkerThread.cpp:251`), which still wants every audio worker woken; it is
unchanged.

Regression test: `AudioWorkerWakeTest::theWasmPoolWakesOneLaneForWorkAndAllOnShutdown`
(`tests/src/core/AudioWorkerWakeTest.cpp`). It reads the shipping `notifyWork()` and `shutdown()`
bodies from `ZENE_SOURCE_DIR` with the existing comment/literal-stripping reader, asserts
`notifyWork()` names `workerwake::notifyOne(` and not `workerwake::notify(`, and asserts
`shutdown()` still names `workerwake::notify(`. It is deterministic (fixed source input, no
threads, sleeps or clock) and runs in the default configure; `WasmWorkerPoolTest` is only built
with `WANT_WASM=ON`.

Evidence (`EXIT` copied from the commands, logs in
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/ci-61ad26e4f8b135f9.r2/logs/`):

| Step | Command | Result |
| --- | --- | --- |
| fix capture | `git diff -- include/AudioWorkerWake.h src/core/AudioWorkerWake.cpp src/wasm/WasmWorkerPool.cpp src/wasm/WasmWorkerPool.h > .../fix.patch` | `EXIT=0`, 143 lines (`logs/fix.patch`) |
| reverse | `git apply -R .../fix.patch` | `EXIT=0` |
| RED build | `cmake --build build --target AudioWorkerWakeTest -j$(nproc)` (`logs/03-build-reverted.log`) | `EXIT=0` |
| RED test | `cd build/tests && ctest -R '^AudioWorkerWakeTest$' --output-on-failure` (`logs/04-ctest-red.log`) | `EXIT=8`; `theWasmPoolWakesOneLaneForWorkAndAllOnShutdown` failed: `notifyWork.find("workerwake::notifyOne(") ... returned FALSE` |
| reapply | `git apply .../fix.patch` | `EXIT=0` |
| GREEN build | `cmake --build build --target zene AudioWorkerWakeTest -j$(nproc)` (`logs/05-build-reapplied.log`) | `EXIT=0` |
| GREEN test | same ctest (`logs/06-ctest-green-reapplied.log`) | `EXIT=0`, 1/1 passed |
| family build | `cmake --build build --target zene AudioWorkerWakeTest ModulationAudioViewTest NotePlaybackTest PluginPortsMigrationTest RemotePluginShmFifoTest ScriptDawModelRegistrationTest TempoMapTest -j$(nproc)` (`logs/10-build-family.log`) | `EXIT=0` |
| availability + focused family | `ctest -R '^(MacosSyncAvailabilityCheck\|AudioWorkerWakeTest\|ModulationAudioViewTest\|NotePlaybackTest\|PluginPortsMigrationTest\|RemotePluginShmFifoTest\|ScriptDawModelRegistrationTest\|TempoMapTest)$' --output-on-failure` (`logs/11-ctest-family.log`) | `EXIT=0`, 8/8 passed |
| optional wasm TU | `g++ -std=c++20 -fsyntax-only ... src/wasm/WasmWorkerPool.cpp` (`logs/07-wasm-pool-syntax.log`) | `EXIT=0` |
| optional wasm test TU | Qt6 moc + `g++ -std=c++20 -fsyntax-only ... tests/src/wasm/WasmWorkerPoolTest.cpp` (`logs/13-moc.log`, `logs/14-wasm-test-syntax.log`) | `EXIT=0` |

## Gates and registration

    python3 .../feature_check.py <worktree>     -> exit 0, all 8 rows PASS
      (fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace,
       no-tautology, doc-figures, complexity, file-length)
    python3 .../submit_check.py <worktree> ci-61ad26e4f8b135f9 -> see commit message / final run

Registration, same commit: `tests/src/core/PortableSync.h` and
`tests/macos-sync-availability-check.py` are fork-NEW, so they entered
`tests/fork-sources.txt` (with a pathspec line in *both* copies of its recipe, so the manifest
still reproduces) and `tests/all-sources.txt` (sorted position; the manifest reproduces
byte-exactly). The inherited product file changed, `src/core/AudioEngine.cpp`, is declared in
`tests/upstream-modifications.txt` with its reason. `src/wasm/WasmWorkerPool.{h,cpp}` were
already fork-NEW entries. `tests/src/core/PortableSync.h` needs no `tests/CMakeLists.txt`
registration (it is a header compiled through the seven TUs), and the new CTest is registered.

**Log folder.** All round-2 build and test logs are in
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/ci-61ad26e4f8b135f9.r2/logs/` (writable in
this session; numbered `01-` .. `14-`, plus `fix.patch`). The round-1 logs cited in the earlier
sections are the ones recorded in `$TMPDIR/zene-qwen-ci-61ad26e4f8b135f9/logs/`, because the r1
folder was not writable then.

## Self-check

**1. The regression test FAILS with your product change reverted and PASSES with it (exit
codes).** Yes. Product hunks reversed: `git apply -R logs/fix.patch` exit 0, rebuild exit 0,
`ctest -R MacosSyncAvailabilityCheck` **exit 8**, failing exactly on
`src/core/AudioEngine.cpp:672 std::atomic 'attemptState' is waited/notified`. Reapplied:
`git apply` exit 0, rebuild exit 0, same ctest **exit 0**. Test hunks reversed: `git apply -R
logs/fix-tests.patch` exit 0, rebuild exit 0, script **exit 1** with 26 FAIL lines (first:
`tests/src/core/RemotePluginShmFifoTest.cpp:109 std::barrier is macOS 11 only`), ctest **exit
8**; reapplied, rebuild exit 0, ctest **exit 0**. Logs `16-`..`20-`, `28-`..`36-`.

Round 2, for the single-waiter rework: `logs/fix.patch` reversed (`git apply -R`, exit 0),
`AudioWorkerWakeTest` rebuilt (exit 0), the test **failed with exit 8** on exactly
`theWasmPoolWakesOneLaneForWorkAndAllOnShutdown`: `notifyWork.find("workerwake::notifyOne(")
... returned FALSE`. Reapplied (`git apply`, exit 0), rebuilt (exit 0), the same test **passed
with exit 0** (1/1). Logs `03-`..`06-`.

**2. The test is deterministic: no wall-clock timing, sleeps or races (name the seam or
injected values it uses).** The check has no thread, sleep or clock: it runs a fixed list of
compiler invocations over fixed inputs (a shadow header set and the seven TUs recorded in
`compile_commands.json`) and one regex scan over `git ls-files`. Its only variability is which
optional wasm TUs exist, which it reports explicitly. The round-2 source case is deterministic
in the same way: it reads one fixed file (`ZENE_SOURCE_DIR/src/wasm/WasmWorkerPool.cpp`) with the
existing comment/literal-stripping reader and compares strings; there is no thread, sleep or
clock in it. The runtime tests it protects already
used event seams rather than clock deadlines before this change -
`shmFifo::setCallerIdHashForTest` (an injected caller-id hash),
`ModulationLayerPublisher`'s token generation, `AudioEngine::setAutomatableModelRegistrationAttemptStateForTest`
(the injected flag), `TempoMapPublisher::setSnapshotProbeForTest`,
`WasmWorker::setAudioEndpointEntryTestHook` - and the replacement primitives are predicated
`condition_variable` waits, not timed polls. The one `try_acquire_for` in the semaphore keeps its
existing meaning ("the writer reached the model within this budget") and is unchanged.

**3. The fix is at the cause and complete: every caller and sibling of what you changed was
checked and stays consistent.** The population was read out of the whole tree, not guessed:
`rg -n '#include <(barrier|latch|semaphore)>|std::(barrier|latch|binary_semaphore|counting_semaphore)'`
plus `rg -n 'notify_all\(|notify_one\(|\.wait\([^)]*memory_order'` over `src/`, `include/`,
`plugins/` and `tests/`. That produced exactly the nine sites tabled above; all nine are fixed
or (for `AudioWorkerWake`) audited and exempted with a reason. The other `notify_*` hits
(`ThreadPool`, `RemotePluginClient`, `VideoFramePump`, `LinkAudioStream`, `MidiClockCapture`,
`RubberBandStretch`) are `std::condition_variable` methods, which Apple does not gate. The
`AudioEngine` seam keeps its `LMMS_TESTING` guard and its release/acquire ordering; its one
consumer and its sibling test are updated together (`ScriptDawModelRegistrationTest`), and the
`AudioWorkerWakeTest` that already guards the product wake still passes. The wasm pool's public
`Stats` API is unchanged; only the private generation's width changed, consistently in the
`.h` and `.cpp`, which were edited together and whose only other includer
(`src/wasm/WasmWorker.cpp:66,98,468`) only calls `addWorker`/`removeWorker`/`notifyWork`.
The round-2 population was re-read with `rg -n 'workerwake::notify|workerwake::wait'`: the only
`notify()` callers are `AudioEngineWorkerThread::wakeWorkers()` (all workers, unchanged) and
`WasmWorkerPool::shutdown()` (all parked lanes, kept); the only `notifyOne()` caller is
`WasmWorkerPool::notifyWork()` (one lane, restored). All three use the same generation-word
protocol and the same non-blocking post.

**4. No collateral damage: old projects and saved data still load as before, and any limit or
check you added does not hit other callers that legitimately exceed it.** No serialised format,
project schema, model default, command id or control surface changed - the diff touches a
test-seam notify, a private wake primitive, test synchronisation and build registration only.
Old projects therefore load and render through exactly the same code paths
(`registerAutomatableModel` still returns the same values and still takes `m_changeMutex` after
the seam store; the wasm generation is still "the value is the signal", just spelled through
`workerwake`). The only new check is a source/compile scan, and its single exemption is written
down in the script (the `AudioWorkerWake.cpp` fallback behind `#ifndef
LMMS_WORKER_WAKE_WAIT_ON_ADDRESS`) rather than silencing a whole file or directory. The ten-test
family, including the tests that load projects and render
(`PluginPortsMigrationTest`, `NotePlaybackTest`, `TempoMapTest`, `AudioEngineTeardownTest`,
`ThreadPoolTest`), was rebuilt and passes: exit 0, 10/10 (`logs/15-build-extra.log`,
`logs/16-ctest-ten.log`). The wake-one restore changes only how many parked WASM lanes a work
post releases; lane draining, ordering, the SPSC queues and every project format are untouched.

**5. If your verdict is ALREADY-FIXED or FALSE-POSITIVE: the code (file:line) or the intended
contract that shows it.** Not claimed: the verdict is FIXED, and the RED runs above show the
defect present at `c20ef591b5` (`RemotePluginShmFifoTest.cpp:109`,
`src/core/AudioEngine.cpp:672`). The nearest "already fixed" evidence was checked and
deliberately not reused: `fix/ci-train21` carries a test-target floor workaround
(`tests/CMakeLists.txt:2468-2476` there) and a poll-based `AudioEngine` seam, but
`git merge-base --is-ancestor 203b137e33 HEAD` exits 1, so none of it is on this branch, and a
test-only deployment floor is not a product fix.
