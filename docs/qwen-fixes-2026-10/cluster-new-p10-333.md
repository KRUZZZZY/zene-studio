FIXED

# cluster new-p10-333 - realtime safety depended on 64-bit atomics being lock-free

Finding (Luna patrol, BUG 15): "The producer paths use `std::atomic<uint64_t>` for their head and
sequence counters without checking that those atomics are lock-free. On targets where they are not,
the implementation may use internal locks on the audio or MIDI thread." Where:
`include/RetroAudioRing.h:250-254; include/RetroMidiRing.h:258-262` (historical lines). Suggested
fix: "Enforce lock-free atomics on supported targets or use a counter representation that is
lock-free there."

## Verdict: REAL, and fixed by machine-checking the premise

The producer paths really did assume, without checking, that the atomics they touch are lock-free.
`RetroAudioRing::push()` (`include/RetroAudioRing.h:109-146`) runs on the audio thread
(`src/core/RetroAudioCapture.cpp:165`, called from the engine's input path) and touches five
`std::atomic<std::uint64_t>` (`include/RetroAudioRing.h:315-319`), two `std::atomic<std::uint32_t>`
and one `std::atomic<bool>` (`:323-325`). `RetroMidiRing::push()`
(`include/RetroMidiRing.h:140-165`) runs on the MIDI thread (`src/core/RetroMidiCapture.cpp:129`) and
touches seven 64-bit atomics of its own (`:279-280`, `:286-290`) plus the 32-bit/bool handshake
(`:294-296`). The class comments promise "no locks" and nothing in either file made that a checked
fact: on the parent revision (`git show HEAD:...`) neither header contains `is_always_lock_free` and
`RetroAudioRing.h` contains no `static_assert` at all (log `evidence-1-prefix-headers.log`; the one
`static_assert` in `RetroMidiRing.h` is the 16-byte slot-size assert at `:96`, not a lock-freedom
check). `docs/MIDI-RETRO-CAPTURE.md:53` had already recorded the same gap for the precedent ring,
`include/RecordRingBuffer.h`.

The premise holds on this machine and on every platform the project builds: all six CI job
definitions are 64-bit (`.github/workflows/build.yml`: `linux-x86_64`, `linux-arm64`, `macos`
(arm64 + x86_64), `mingw` x86_64, `msvc` x64, `msys2` clang-arm64), and `std::atomic<uint64_t>`,
`std::atomic<uint32_t>`, `std::atomic<bool>` are lock-free on all of them. So this was a silent
assumption, exactly as the finding says, and not a live defect on any shipped target - which is why
the fix enforces the assumption instead of replacing the counter representation: the counters are
64-bit monotonic frame/event indices, and a 32-bit head would wrap (2^32 frames is 24.8 h at
48 kHz) and break the `overwrittenCount()` arithmetic that is defined in frames (`:136-144`).

## The fix

The project already answers this exact question in two places, and the fix follows them:
`static_assert(std::atomic<T>::is_always_lock_free, "the audio thread reads it")` in
`include/DeviceOptions.h:46` and `include/DspLoadProbe.h:42`.

| File | Change |
| --- | --- |
| `include/RetroAudioRing.h:73-92` | public `atomicOfTypeIsLockFree<T>()` + `producerCountersAreLockFree()` (the single source of truth for the check: `uint64_t`, `uint32_t`, `bool`) |
| `include/RetroAudioRing.h:329-337` | `static_assert(RetroAudioRing::producerCountersAreLockFree(), ...)` after the class |
| `include/RetroMidiRing.h:104-123` | the same two helpers |
| `include/RetroMidiRing.h:300-308` | `static_assert(RetroMidiRing::producerCountersAreLockFree(), ...)` after the class |
| `include/RetroAudioCapture.h:91-108` | the same two helpers for the atomics the capture itself touches on the audio thread (`std::atomic<bool> m_armed` `:187`, `std::atomic<std::uint64_t> m_rateTag` `:196`) |
| `include/RetroAudioCapture.h:217-223` | `static_assert(RetroAudioCapture::producerCountersAreLockFree(), ...)` after the class |

Why after the class and not at class scope: GCC refuses a class-scope `static_assert` that calls a
member function - `error: 'static constexpr bool ...::producerCountersAreLockFree()' called in a
constant expression before its definition is complete` (log `probe-compile-1.log`, from a standalone
probe compiled with the project's compiler `/usr/bin/c++ (GCC) 16.2.1`). The check therefore sits
immediately after each class, inside `namespace lmms`.

Nothing runs differently on a supported target: the change is three constexpr predicates and three
compile-time assertions. On an unsupported target the build stops with a message naming the thread
that would have locked, instead of shipping an internal lock inside the audio or MIDI callback.
`RetroMidiCapture` needed no change: it holds only `std::atomic<bool> m_armed`
(`include/RetroMidiCapture.h:124`) and a 32-bit `std::atomic<tick_t> g_publishedTick`
(`src/core/RetroMidiCapture.cpp`), and the ring it owns is now checked.

## Test matrix

- **Normal path** - `RetroMidiRingTest::theProducerCountersAreLockFree`
  (`tests/src/core/RetroMidiRingTest.cpp:83-94`) and
  `RetroAudioCaptureTest::theProducerCountersAreLockFree`
  (`tests/src/core/RetroAudioCaptureTest.cpp:104-120`) assert the three predicates answer true for
  every atomic type the producer paths touch, for both rings and for the capture's own rate tag.
- **Failure path** - the test asks the same predicate about a type no target can serve lock-free
  (`struct NeedsALock { char bytes[64]; }`) and requires `false`. The predicate is therefore a
  check, not a constant: the answer the header refuses to build with is reachable and is what the
  test observes. As an independent control the same `static_assert` shape was compiled against that
  type on its own: `/usr/bin/c++ -std=c++20 -fsyntax-only tmp/probe_negative.cpp` -> **EXIT 1**,
  `error: static assertion failed: the producer path (audio thread) must not take an internal atomic
  lock` (log `probe-compile-negative-2.log`).
- **Exhaustion / reconfiguration** - unchanged and still covered by the same binaries, which the
  family run executes in full: `RetroMidiRingTest` covers capacity rounding, drop-oldest, the
  snapshot handshake and the `std::length_error` on an impossible capacity;
  `RetroAudioCaptureTest` covers the rate-transition discard and the quiesce-timeout path
  (cluster new-p12-200). 4/4 passed, `test-15-green-final-family.log`.
- **Audio-thread constraints** - no code was added to any producer path. The added predicates are
  `constexpr` and evaluated at compile time; they allocate nothing, lock nothing, wait for nothing
  and log nothing. `RetroMidiRingTest`'s allocation-probe case still reports 0 allocations.
- **Supported platforms** - the assertion holds on every CI platform (see above). It is a C++17
  `std::atomic<T>::is_always_lock_free` constant, available on Qt 5 and Qt 6 toolchains alike.

## Commands and EXIT codes (logs in `~/.config/kads/zene/state/runs/new-p10-333.r1/logs/`)

`W=/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-new-p10-333`

RED, first (the tests exist, the header fix does not):

| Command | EXIT | Log |
| --- | --- | --- |
| `cmake --build $W/build --target RetroMidiRingTest RetroAudioCaptureTest -- -j4` | **2** | `build-1-red.log` - `error: 'producerCountersAreLockFree' is not a member of 'lmms::RetroMidiRing'` |

GREEN, first:

| Command | EXIT | Log |
| --- | --- | --- |
| `cmake --build $W/build --target RetroMidiRingTest RetroAudioCaptureTest -- -j4` | 0 | `build-2-green.log` |
| `cd $W/build/tests && ./RetroMidiRingTest theProducerCountersAreLockFree` | 0 | `test-1-midi-green.log` (Totals: 3 passed, 0 failed) |
| `cd $W/build/tests && ./RetroAudioCaptureTest theProducerCountersAreLockFree` | 0 | `test-2-audio-green.log` (Totals: 3 passed, 0 failed) |
| `cmake --build $W/build --target zene MidiRetroCaptureTest RetroMidiCaptureCommandsTest -- -j4` | 0 | `build-3-includers.log`, `build-7-final-zene.log` (the two capture test binaries had never been built in this tree) |
| `cd $W/build/tests && ctest -R '^(RetroMidiRingTest\|RetroAudioCaptureTest\|RetroMidiCaptureCommandsTest\|MidiRetroCaptureTest)$' --output-on-failure` | 0 | `test-4-family-green.log` - 100% tests passed out of 4 |
| `cd $W/build/tests && ctest -R '^ControlRetroCapture$' --output-on-failure` | 0 | `test-5-control-retro.log` - skipped by design in this environment |

Step 4b, with the final product set (`include/RetroAudioRing.h`, `include/RetroMidiRing.h`,
`include/RetroAudioCapture.h`). Both commands and their exit codes are recorded in
`step4b-red.log` / `step4b-green.log`:

| Command | EXIT | Log |
| --- | --- | --- |
| `git diff -- include/RetroAudioRing.h include/RetroMidiRing.h include/RetroAudioCapture.h > $L/fix.patch` | 0 | `step4b-red.log` (`DIFF_EXIT=0`) |
| `git apply -R $L/fix.patch` | 0 | `step4b-red.log` (`APPLY_REVERSE_EXIT=0`) |
| `rm -f $W/build/tests/RetroMidiRingTest $W/build/tests/RetroAudioCaptureTest` (so ctest cannot pass on a stale binary) | 0 | `step4b-red.log` |
| `cmake --build $W/build --target RetroMidiRingTest -- -j4` | **2** | `build-9-red-final-midi.log` - `RetroMidiRingTest.cpp:85:40: error: 'producerCountersAreLockFree' is not a member of 'lmms::RetroMidiRing'` |
| `cmake --build $W/build --target RetroAudioCaptureTest -- -j4` | **2** | `build-10-red-final-audio.log` - `RetroAudioCaptureTest.cpp:106:41: error: 'producerCountersAreLockFree' is not a member of 'lmms::RetroAudioRing'` |
| `cd $W/build/tests && ctest -R '^(RetroMidiRingTest\|RetroAudioCaptureTest)$' --output-on-failure` | **8** | `test-12-red-final.log` - `193 - RetroAudioCaptureTest (Not Run)`, `228 - RetroMidiRingTest (Not Run)` |
| `git apply $L/fix.patch` | 0 | `step4b-green.log` (`APPLY_FORWARD_EXIT=0`) |
| `cmake --build $W/build --target RetroMidiRingTest -- -j4` | 0 | `build-11-green-final-midi.log` |
| `cmake --build $W/build --target RetroAudioCaptureTest -- -j4` | 0 | `build-12-green-final-audio.log` |
| `cmake --build $W/build --target zene -- -j4` | 0 | `build-13-green-final-zene.log` |
| `cd $W/build/tests && ./RetroMidiRingTest theProducerCountersAreLockFree` | 0 | `test-13-green-final-midi.log` (Totals: 3 passed, 0 failed) |
| `cd $W/build/tests && ./RetroAudioCaptureTest theProducerCountersAreLockFree` | 0 | `test-14-green-final-audio.log` (Totals: 3 passed, 0 failed) |
| `cd $W/build/tests && ctest -R '^(RetroMidiRingTest\|RetroAudioCaptureTest\|RetroMidiCaptureCommandsTest\|MidiRetroCaptureTest)$' --output-on-failure` | 0 | `test-15-green-final-family.log` - 100% tests passed out of 4 |

Gates:

| Command | EXIT | Log |
| --- | --- | --- |
| `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py $W` | 0 | `gates-1.log` |

```
feature-check kads-new-p10-333 (base 3436a808b, 5 changed files; logs in .../build/feature-check)
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

The same check after the report and its `tests/fork-sources.txt` entry were added is
`gates-2.log` (table in `## Self-check` below).

Registration: `include/RetroAudioRing.h`, `include/RetroMidiRing.h`,
`include/RetroAudioCapture.h`, `tests/src/core/RetroMidiRingTest.cpp` and
`tests/src/core/RetroAudioCaptureTest.cpp` are already listed in `tests/fork-sources.txt`
(fork-NEW). The only new file is this report, registered there in the same commit (the list ends
with a newline). No inherited upstream file was touched, so `tests/upstream-modifications.txt` is
unchanged.

## Self-check

**The regression test FAILS with your product change reverted and PASSES with it (exit codes).**
Reverted (`git apply -R`, EXIT 0): `cmake --build ... --target RetroMidiRingTest` **EXIT 2**,
`... --target RetroAudioCaptureTest` **EXIT 2**, `ctest -R '^(RetroMidiRingTest|RetroAudioCaptureTest)$'`
**EXIT 8** with both tests `(Not Run)`. Restored (`git apply`, EXIT 0): builds **0/0/0**
(`RetroMidiRingTest`, `RetroAudioCaptureTest`, `zene`), the two test binaries **0/0**
(`3 passed, 0 failed` each) and the family ctest **0** (`100% tests passed out of 4`). Logs:
`step4b-red.log`, `build-9-red-final-midi.log`, `build-10-red-final-audio.log`,
`test-12-red-final.log`, `step4b-green.log`, `build-11`, `build-12`, `build-13`,
`test-13`, `test-14`, `test-15`.

This is a compile-time guarantee, so the test's red form is the build refusing to produce the
binary: the reverted headers no longer declare `producerCountersAreLockFree()`, and the error names
it. To keep that from being the only evidence, the enforcement mechanism itself was driven to
failure on a type with no lock-free implementation: the same `static_assert` compiled alone exits
**1** with `static assertion failed: ...` (`probe-compile-negative-2.log`). I also checked that
ctest cannot report a pass on a stale binary in the red step - the two binaries were deleted first,
which is why the red ctest run reports `(Not Run)` and EXIT 8 instead of running the green build.

**The test is deterministic: no wall-clock timing, sleeps or races (name the seam or injected
values it uses).** The test has no clock, no thread, no sleep and no shared state: it reads
`constexpr` predicates (`std::atomic<T>::is_always_lock_free` is a compile-time constant) and
constructs nothing. The injected value is the type parameter: `NeedsALock` (64 bytes) is a type no
target serves lock-free, so `atomicOfTypeIsLockFree<NeedsALock>()` exercises the false branch of the
check without depending on the machine.

**The fix is at the cause and complete: every caller and sibling of what you changed was checked
and stays consistent.** The producer entry points are `RetroAudioCapture::push`
(`src/core/RetroAudioCapture.cpp:165`, audio thread), `RetroMidiCapture::push`
(`src/core/RetroMidiCapture.cpp:129`, MIDI thread) and the consumers
(`ControlCommandsRecordingRetro.cpp:104`, `RetroAudioCapture.cpp:196`, control thread, unchanged).
Every atomic those two producer paths touch is now covered: the audio ring's 64/32/bool members, the
MIDI ring's 64/32/bool members including its two slot words, and - the sibling the ring-only fix
would have missed - `RetroAudioCapture`'s own `m_armed` and 64-bit `m_rateTag`
(`include/RetroAudioCapture.h:187`, `:196`), which `push()` writes on the audio thread. The MIDI
capture needs no check of its own: `std::atomic<bool> m_armed` and a 32-bit
`std::atomic<tick_t> g_publishedTick` are lock-free on every supported target. All three predicates
have the same shape and the same name, so a later ring inherits the idiom by copying one block.

**No collateral damage: old projects and saved data still load as before, and any limit or check
you added does not hit other callers that legitimately exceed it.** No runtime behaviour changes:
the diff adds three `constexpr` predicates and three `static_assert`s, and touches no member
layout, no serialisation, no project state and no control command (`ControlCommandsSnapshot` is
unchanged, and no `ControlRegistry` handler was added). Nothing is rejected that used to be
accepted on a supported target; on an unsupported one the build stops, which is the point of the
finding. Capacity, ring policy, counters and the snapshot handshake are untouched, and their tests
(`RetroMidiRingTest`, `RetroAudioCaptureTest`, `MidiRetroCaptureTest`,
`RetroMidiCaptureCommandsTest`) all pass.

**Registration and gates pass (procedure step 5).**
`python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py $W` with the report and its
`tests/fork-sources.txt` line in place - **EXIT 0** (`gates-2.log`):

```
feature-check kads-new-p10-333 (base 3436a808b, 7 changed files; logs in .../build/feature-check)
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

**If your verdict is ALREADY-FIXED or FALSE-POSITIVE: the code (file:line) or the intended
contract that shows it.** Not applicable - the verdict is FIXED, and the opposite is what the
evidence shows: on the parent revision neither ring and neither capture header contains
`is_always_lock_free` (`evidence-1-prefix-headers.log`; the only `static_assert` in either ring is
`RetroMidiRing.h:96`'s 16-byte slot size), and the regression tests do not build against those
headers (`build-1-red.log`, `build-9-red-final-midi.log`, `build-10-red-final-audio.log`).

## Other findings (not fixed here)

- `include/RecordRingBuffer.h:172-174` (`m_writePos`, `m_readPos`, `m_overflow`) carries the same
  unguarded assumption, and `TrackRecorder` writes it from the audio thread
  (`src/core/audio/TrackRecorder.cpp:159-160`). It is inherited upstream code, outside this
  finding, and changing it would require `tests/upstream-modifications.txt`; reported rather than
  touched.
- The same 64-bit pattern exists across the fork's other audio-thread state, e.g.
  `include/AudioBusHandle.h:256-261`, `include/AudioDevice.h:235`, `include/AudioEngine.h:856-862,
  909, 955`. Out of scope for this finding; a future sweep could apply the same predicate to them.
- `docs/MIDI-RETRO-CAPTURE.md:53` describes the missing machine assertion as a property of
  `RecordRingBuffer`; the two retro rings and the capture now have it, the precedent still does not.
