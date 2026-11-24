FIXED

# cluster new-p16-122: the MIDI handoff queue must require lock-free atomics

## Finding

`include/MidiHandoffQueue.h` promises a lock-free realtime handoff, but its queue and overflow bookkeeping used
`std::atomic<std::size_t>`, `std::atomic<std::uint64_t>` and `std::atomic<std::uint32_t>` without checking that
those atomics are always lock-free. C++ permits a conforming implementation to put a lock inside an atomic, which
would put that lock on the render path.

## Verdict: REAL

- The queue contract says "nothing allocates, nothing locks" and `pop()` is wait-free (`include/MidiHandoffQueue.h:67-68`
  and `:155-170`). The overflow class says "All operations are lock-free" (`include/MidiHandoffQueue.h:226-236`).
- The declared audio-thread paths include `MidiHandoffQueue::push`, `pop`, `consumed`, `MidiChannelOverflow::record`,
  `stamp`, `due`, `clear`, `raise`, `pushOrRecord` and `drainMidi` (`tests/rt-safety-scope.txt:142-151`).
- Before the fix those paths used `std::atomic<std::size_t>` for cell/enqueue/dequeue positions
  (`include/MidiHandoffQueue.h:208,213-214`), `std::atomic<std::uint64_t>` for dropped counts and overflow stamps
  (`:215,595-597,601-603,607`) and `std::atomic<std::uint32_t>` for occupancy and deferred flags (`:600,608`), but
  the header had no `is_always_lock_free` requirement.

## Fix

`include/MidiHandoffQueue.h:53-61` now requires `is_always_lock_free` at build time for every atomic type on the
realtime path: `std::size_t`, `std::uint64_t` and `std::uint32_t`. A target where any of those atomics would use an
internal lock now fails to compile instead of taking a lock on the audio thread. The consumer-private
`m_deferredStamp` remains a plain `std::uint64_t` because only the single consumer touches it.

## Regression test

`tests/src/plugins/VestigeMidiHandoffTest.cpp:128` adds `realtimeAtomicsMustBeAlwaysLockFree`. It checks the three
compile-time traits on this target and that `include/MidiHandoffQueue.h` contains all three static assertions. The
test is deterministic: it uses compile-time traits and source text only, with no timing, sleeps, threads or races.
`tests/CMakeLists.txt:2334-2336` supplies the header path to that existing MIDI handoff test.

## Evidence commands and exit codes

All logs are in `/home/kruzzzzy/.config/kads/zene/state/runs/new-p16-122.r1/logs`.

```text
cmake --build build --target MidiHandoffQueueTest -j4 > .../build-1.log 2>&1; echo EXIT=$?
EXIT=0

ctest -R '^MidiHandoffQueueTest$' --output-on-failure > .../test-1.log 2>&1; echo EXIT=$?
EXIT=0

cmake --build build --target VestigeMidiHandoffTest -j4 > .../build-13-vestige-green.log 2>&1; echo EXIT=$?
EXIT=0

ctest -R '^VestigeMidiHandoffTest$' --output-on-failure > .../test-8-vestige-green.log 2>&1; echo EXIT=$?
EXIT=0

git diff -- include/MidiHandoffQueue.h > .../fix.patch && git apply -R .../fix.patch; echo APPLY_REVERSE_EXIT=$?
APPLY_REVERSE_EXIT=0

cmake --build build --target VestigeMidiHandoffTest -j4 > .../build-14-vestige-red-proof.log 2>&1; echo BUILD_EXIT=$?
BUILD_EXIT=0

ctest -R '^VestigeMidiHandoffTest$' --output-on-failure > .../test-9-vestige-red-proof.log 2>&1; echo TEST_EXIT=$?
TEST_EXIT=8

git apply .../fix.patch; echo APPLY_FORWARD_EXIT=$?
APPLY_FORWARD_EXIT=0

cmake --build build --target VestigeMidiHandoffTest -j4 > .../build-15-vestige-green-restored.log 2>&1; echo BUILD_EXIT=$?
BUILD_EXIT=0

ctest -R '^VestigeMidiHandoffTest$' --output-on-failure > .../test-10-vestige-green-restored.log 2>&1; echo TEST_EXIT=$?
TEST_EXIT=0

cmake --build build --target zene -j4 > .../build-16-zene-green-restored.log 2>&1; echo BUILD_EXIT=$?
BUILD_EXIT=0

cmake --build build --target MidiHandoffQueueTest -j4 > .../build-17-queue-final.log 2>&1; echo EXIT=$?
EXIT=0

ctest -R 'MidiHandoff|ZynAddSubFxInstrumentTest' --output-on-failure > .../test-12-family-final.log 2>&1; echo EXIT=$?
EXIT=0
```

The RED run's failure is the new test itself:

```text
FAIL!  : VestigeMidiHandoffTest::realtimeAtomicsMustBeAlwaysLockFree() 'text.find(assertion) != std::string::npos' returned FALSE. (static_assert(std::atomic<std::size_t>::is_always_lock_free)
   Loc: [../../tests/src/plugins/VestigeMidiHandoffTest.cpp(147)]
Totals: 6 passed, 1 failed, 0 skipped, 0 blacklisted, 502ms
```

## Gates

```text
PATH=.../logs/venv/bin:$PATH python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-new-p16-122 > .../feature-check-3.log 2>&1; echo EXIT=$?
EXIT=0

feature-check kads-new-p16-122 (base fa4a5f2e7, 654 changed files; logs in .../build/feature-check)
  PASS  fork-sources            exit 0
  PASS  all-sources-reproduce   exit 0
  PASS  no-upstream-regression  exit 0
  PASS  check-namespace         exit 0
  PASS  no-tautology            exit 0
  PASS  doc-figures             exit 0
  PASS  complexity              no new item in your files
  PASS  file-length             no new item in your files
RESULT: PASS
```

The complexity gate's `lizard` dependency was installed in a scratch virtual environment under the log folder; the
`PATH` prefix makes the gate use it. `feature-check-1.log` records the earlier FAIL from placing the test in
`MidiHandoffQueueTest.cpp` (it took that file to 521 code lines); the final test placement above passes both
complexity and file-length.

## Self-check

1. **Regression fails without the product change and passes with it.** Reversing only `include/MidiHandoffQueue.h`
   and rebuilding gave `TEST_EXIT=8` at `realtimeAtomicsMustBeAlwaysLockFree`; restoring the patch gave
   `TEST_EXIT=0`. See `test-9-vestige-red-proof.log` and `test-10-vestige-green-restored.log`.
2. **The test is deterministic.** It has no wall-clock timing, sleeps, threads or race window. It checks compile-time
   `is_always_lock_free` traits and the exact static assertions in the header.
3. **The fix is at the cause and complete.** It covers every atomic type the realtime queue and overflow paths use:
   queue positions/sequences (`size_t`), overflow stamps/counters (`uint64_t`) and occupancy/deferred flags
   (`uint32_t`). The callers and siblings all go through this one header: `push`, `pop`, `consumed`, `record`,
   `stamp`, `due`, `clear`, `raise`, `pushOrRecord` and `drainMidi`. `zene` and all four dependent test targets were
   rebuilt with the header: `MidiHandoffQueueTest`, `CarlaMidiHandoffTest`, `Lv2MidiHandoffTest`,
   `VestigeMidiHandoffTest` and `ZynAddSubFxInstrumentTest`.
4. **No collateral damage.** The change is compile-time only on this target; it adds no runtime check, allocation,
   lock, limit or save/load change. Projects and saved data are untouched.
5. **ALREADY-FIXED/FALSE-POSITIVE is not the verdict.** The pre-fix header had no lock-free requirement; the fix and
   the RED proof above are against the real missing guard. No control command, undo step or realtime operation was
   added.
6. **Sibling commands.** This commit touches only `include/MidiHandoffQueue.h`, `tests/CMakeLists.txt`,
   `tests/src/plugins/VestigeMidiHandoffTest.cpp` and this report. No `ControlCommands`, `ControlReversibilityTable`
   or `ControlRegistry` file is changed, so there is no sibling command behaviour to adjust; the app/arrangement
   commands listed by the completion check come from the branch's pre-existing history, not this commit.

## Other findings

None.
