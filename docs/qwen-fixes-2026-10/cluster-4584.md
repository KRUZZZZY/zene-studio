4584: FIXED
**Verdict: FIXED** — cluster 4584 and new-p15-252 (two ALSA bugs in `src/core/audio/AudioAlsa.cpp`)

## cluster 4584 — ALSA capture error recovery logs from the capture thread

### Finding

`src/core/audio/AudioAlsa.cpp` — the capture thread's error recovery printed
the failure with `printf()` when an overrun or a suspend could not be
recovered from. A stdio call can block, and the loop that calls it forbids
logging by its own rule.

### Evidence (file:line)

- `src/core/audio/AudioAlsa.cpp:606-608` (unchanged by this fix): the capture
  loop's own contract — *"Realtime rules, kept: no allocation, no locks, no
  QString and no logging in this loop"*.
- `src/core/audio/AudioAlsa.cpp:621` and `:650`: `handleCaptureError()` is
  called ONLY from `captureLoop()` — the capture thread. There is no other
  caller (grep: `handleCaptureError` appears at `:529` (definition), `:621`
  and `:650` (the two call sites), `include/AudioAlsa.h:124` (declaration)).
- Original `src/core/audio/AudioAlsa.cpp:524-525` (overrun) and `:547-548`
  (suspend): both `printf("Can't recover capture from ..., prepare failed:
  %s\n", snd_strerror(err))` calls were on that thread.
- The playback twin `handleError()` (`src/core/audio/AudioAlsa.cpp:225-256`)
  prints too, but it runs on the *playback* thread, which is the render
  thread — that is upstream LMMS behaviour and outside this finding; it was
  left untouched.

### Regression test

`tests/src/core/AudioAlsaCaptureErrorTest.cpp` (new, registered in
`tests/CMakeLists.txt` inside the `if(LMMS_HAVE_ALSA)` block) — case
`captureThreadDoesNotLogTheFailedRecovery()`.

The seams (no timing, no sleeps):

- the test's fake `snd_pcm_readi()` injects ONE overrun (`-EPIPE`) and its
  fake `snd_pcm_prepare()` then fails (`-EINVAL`), so the recovery really
  fails;
- the fake `snd_pcm_readi()` posts a semaphore when the capture thread comes
  back for its NEXT period — the recovery has run by then — and parks the
  capture thread until the test lets it go on (the fake `snd_pcm_drop()`
  releases it, exactly as `closeCapture()`'s drop does);
- the test's own `printf()` records every printed line together with the
  thread that printed it, so the test can tell WHERE a message came from.

Assertions: the capture thread printed no `"Can't recover capture"` line, and
the failure WAS reported — by a thread that is not the capture thread.

### RED (original code)

```
$ cmake --build build --target AudioAlsaCaptureErrorTest -j 8        # build-capture-red-1.log
EXIT=0
$ (cd build/tests && ctest -R '^AudioAlsaCaptureErrorTest$' --output-on-failure)   # test-capture-red-1.log
EXIT=8
ALSA capture: null, 2 channels at 44100 Hz (float), period 256
Can't recover capture from overrun, prepare failed: Invalid argument
FAIL!  : AudioAlsaCaptureErrorTest::captureThreadDoesNotLogTheFailedRecovery() '!captureThreadLogged' returned FALSE. (the capture thread logged the failed recovery: Can't recover capture from overrun, prepare failed: Invalid argument)
   Loc: [../../tests/src/core/AudioAlsaCaptureErrorTest.cpp(223)]
Totals: 2 passed, 1 failed, 0 skipped, 0 blacklisted, 497ms
```

### Fix

- `include/AudioAlsa.h:157,160`: two parked errors,
  `std::atomic<int> m_captureOverrunError` / `m_captureSuspendError` (0: none
  parked), plus the declaration of `reportCaptureErrors()` (`:128`).
- `src/core/audio/AudioAlsa.cpp:541` and `:564`: the capture thread stores
  the ALSA error code instead of printing it — one relaxed atomic store, no
  allocation, no lock, no I/O, no logging:

```cpp
if( err < 0 )
{
    // THE CAPTURE THREAD MAY NOT LOG: its own rule says so, and a
    // stdio call can block it. The code is parked instead, and the
    // thread that closes the capture reports it (cluster 4584).
    m_captureOverrunError.store( err, std::memory_order_release );
}
```

- `src/core/audio/AudioAlsa.cpp:576-593` `reportCaptureErrors()`: drains both
  parked codes (`exchange(0)`) and prints the SAME two messages the capture
  thread used to print, once per failure.
- `src/core/audio/AudioAlsa.cpp:523`: `closeCapture()` calls it after the
  capture thread has been joined. `closeCapture()` runs on the thread that
  closes the capture — the main thread (`stopProcessingImpl()` and the
  destructor), never on the capture thread and never on the render thread.

Both call sites of `handleCaptureError()` (the two `printf()`s of the original
code) were changed; there is no other logging on the capture path
(`captureLoop()` itself only waits, reads and publishes).

### GREEN

```
$ cmake --build build --target AudioAlsaCaptureErrorTest -j 8        # build-capture-green-1.log
EXIT=0
$ (cd build/tests && ctest -R '^AudioAlsaCaptureErrorTest$' --output-on-failure)   # test-capture-green-1.log
EXIT=0
1/1 Test #485: AudioAlsaCaptureErrorTest ........   Passed    0.30 sec
100% tests passed out of 1
```

The message is still printed — once, now from the thread that closes the
capture (`test-capture-direct-green.log`, EXIT=0):

```
ALSA capture: null, 2 channels at 44100 Hz (float), period 256
Can't recover capture from overrun, prepare failed: Invalid argument
PASS   : AudioAlsaCaptureErrorTest::captureThreadDoesNotLogTheFailedRecovery()
```

### 4b proof (the test catches the bug)

```
$ git diff -- src/core/audio/AudioAlsa.cpp include/AudioAlsa.h > .../logs/fix-capture.patch
$ git apply -R .../logs/fix-capture.patch
$ cmake --build build --target AudioAlsaCaptureErrorTest -j 8        # build-capture-red-2.log
EXIT=0
$ (cd build/tests && ctest -R '^AudioAlsaCaptureErrorTest$' --output-on-failure)   # test-capture-red-2.log
EXIT=8
FAIL!  : AudioAlsaCaptureErrorTest::captureThreadDoesNotLogTheFailedRecovery() '!captureThreadLogged' returned FALSE. (the capture thread logged the failed recovery: ...)
$ git apply .../logs/fix-capture.patch
$ cmake --build build --target AudioAlsaCaptureErrorTest -j 8        # build-capture-green-2.log
EXIT=0
$ (cd build/tests && ctest -R '^AudioAlsaCaptureErrorTest$' --output-on-failure)   # test-capture-green-2.log
EXIT=0
1/1 Test #485: AudioAlsaCaptureErrorTest ........   Passed    0.61 sec
```

### Family + app build (the header changed, so `zene` was rebuilt too)

```
$ cmake --build build --target zene -j 8                             # build-zene-capture.log
EXIT=0
$ (cd build/tests && ctest -R 'AudioAlsa' --output-on-failure)        # test-alsa-family.log
EXIT=0
100% tests passed, 0 tests failed out of 3
   (AudioAlsaWriteTest, AudioAlsaPollDescriptorTest, AudioAlsaCaptureErrorTest)
```

## new-p15-252: FIXED

### Finding

`src/core/audio/AudioAlsa.cpp:112` (original) — the return value of
`snd_pcm_poll_descriptors()` was ignored, so a FAILED retrieval left the
`pollfd` array exactly as the fresh `new pollfd[count]` allocation had it
(uninitialised) and the CLOEXEC loop then handed those contents to `fcntl()`.

### Evidence (file:line)

- `src/core/audio/AudioAlsa.cpp:111-113` (original):
  `auto ufds = new pollfd[count]; snd_pcm_poll_descriptors( m_handle, ufds, count );`
  — the result is dropped.
- `src/core/audio/AudioAlsa.cpp:115-120` (original): the loop reads
  `ufds[i].fd` and passes it to `fcntl( fd, F_GETFD, 0 )` / `fcntl( fd, F_SETFD, ... )`
  — with an unfilled array that is whatever the allocator left behind, and a
  garbage descriptor number may belong to an unrelated open file.
- `tests/src/core/AudioAlsaPollDescriptorTest.cpp:41` (pre-existing): the
  sibling case proves `snd_pcm_poll_descriptors_count()` was already checked
  (`count > 0`) — the RETURN VALUE of the retrieval was the remaining hole.

### Regression test

`tests/src/core/AudioAlsaPollDescriptorTest.cpp` — new case
`failedDescriptorRetrievalSkipsCloexecSetup()`. The test file's fake
`snd_pcm_poll_descriptors()` now returns `-1` (the failure) and leaves a REAL,
open descriptor (a `tmpfile()` fd) in the array, so the garbage a trusting
caller reads is observable through that descriptor's `FD_CLOEXEC` flag. The
case asserts the flag is still clear after the device was constructed, i.e.
that a failed retrieval marked no descriptor at all.

### RED (original code)

```
$ cmake --build build --target AudioAlsaPollDescriptorTest -j 8        # build-poll-red-1.log
EXIT=0
$ (cd build/tests && ctest -R '^AudioAlsaPollDescriptorTest$' --output-on-failure)   # test-poll-red-1.log
EXIT=8
FAIL!  : AudioAlsaPollDescriptorTest::failedDescriptorRetrievalSkipsCloexecSetup() '(flags & FD_CLOEXEC) == 0' returned FALSE. (a failed snd_pcm_poll_descriptors must not mark an unrelated descriptor close-on-exec)
   Loc: [../../tests/src/core/AudioAlsaPollDescriptorTest.cpp(139)]
Totals: 3 passed, 1 failed, 0 skipped, 0 blacklisted, 185ms
```

### Fix

`src/core/audio/AudioAlsa.cpp:106-131` — the retrieval's result is checked and
a failed retrieval skips the CLOEXEC setup entirely (the array is freed and
never read):

```cpp
const int descriptorCount = snd_pcm_poll_descriptors( m_handle, ufds, count );
if (descriptorCount > 0)
{
    for (int i = 0; i < std::max(3, descriptorCount); ++i)
    {
        const int fd = ( i >= descriptorCount ) ? ufds[0].fd+i : ufds[i].fd;
        ...
    }
}
delete[] ufds;
```

The loop bound now uses the number of descriptors ALSA actually returned, so
it can never read past the filled entries either. Nothing else changes: a
successful retrieval marks exactly the same descriptors as before (the count
ALSA reports equals the count it was asked for).

### GREEN

```
$ cmake --build build --target AudioAlsaPollDescriptorTest -j 8        # build-poll-green-1.log
EXIT=0
$ (cd build/tests && ctest -R '^AudioAlsaPollDescriptorTest$' --output-on-failure)   # test-poll-green-1.log
EXIT=0
100% tests passed out of 1
```

### 4b proof (the test catches the bug)

```
$ git diff -- src/core/audio/AudioAlsa.cpp > .../logs/fix-poll.patch
$ git apply -R .../logs/fix-poll.patch
$ cmake --build build --target AudioAlsaPollDescriptorTest -j 8        # build-poll-red-2.log
EXIT=0
$ (cd build/tests && ctest -R '^AudioAlsaPollDescriptorTest$' --output-on-failure)   # test-poll-red-2.log
EXIT=8
FAIL!  : AudioAlsaPollDescriptorTest::failedDescriptorRetrievalSkipsCloexecSetup() '(flags & FD_CLOEXEC) == 0' returned FALSE.
$ git apply .../logs/fix-poll.patch
$ cmake --build build --target AudioAlsaPollDescriptorTest -j 8        # build-poll-green-2.log
EXIT=0
$ (cd build/tests && ctest -R '^AudioAlsaPollDescriptorTest$' --output-on-failure)   # test-poll-green-2.log
EXIT=0
100% tests passed out of 1
```

### Family + app build

```
$ cmake --build build --target zene -j 8                             # build-zene-poll.log
EXIT=0
$ (cd build/tests && ctest -R 'AudioAlsa' --output-on-failure)        # test-poll-family-2.log
EXIT=0
100% tests passed, 0 tests failed out of 2   (AudioAlsaWriteTest, AudioAlsaPollDescriptorTest)
```

## Gates

```
$ python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>      # feature-check-poll.log, feature-check-final.log
EXIT=0
  PASS  fork-sources / all-sources-reproduce / no-upstream-regression /
        check-namespace / no-tautology / doc-figures / complexity / file-length
  RESULT: PASS
```

## Self-check

- **The regression test FAILS with the product change reverted and PASSES with
  it (exit codes).** Cluster 4584: `git apply -R logs/fix-capture.patch` →
  build EXIT=0 → `ctest -R '^AudioAlsaCaptureErrorTest$'` EXIT=8 (the capture
  thread printed the failure) → `git apply logs/fix-capture.patch` → build
  EXIT=0 → ctest EXIT=0. new-p15-252: `git apply -R logs/fix-poll.patch` →
  build EXIT=0 → `ctest -R '^AudioAlsaPollDescriptorTest$'` EXIT=8 (the
  unrelated descriptor was marked close-on-exec) → `git apply logs/fix-poll.patch`
  → build EXIT=0 → ctest EXIT=0.
- **The test is deterministic.** No wall-clock timing, no sleeps, no races: the
  capture test injects the failure through the test's own `snd_pcm_readi()` /
  `snd_pcm_prepare()` fakes and hands over through two `QSemaphore`s (the
  second read is the hand-over point, the drop releases the parked read); the
  only timeout (`tryAcquire(1, 5000)`) is a bound that FAILS the test if the
  hand-over never happens, not a timing dependency. The poll test injects the
  failed retrieval through the test's own `snd_pcm_poll_descriptors()` fake and
  reads the `FD_CLOEXEC` flag of a descriptor it opened itself.
- **The fix is at the cause and complete.** Cluster 4584: BOTH `printf()`s of
  the capture error path were changed (overrun and suspend), `handleCaptureError()`
  has no other caller (`grep`: `src/core/audio/AudioAlsa.cpp:529` definition,
  `:621`/`:650` the two call sites, both in `captureLoop()`), and the parked
  error is drained by the one reporter (`reportCaptureErrors()`), which is
  called from the one place that closes the capture (`closeCapture()`,
  `src/core/audio/AudioAlsa.cpp:523` — itself called from
  `stopProcessingImpl()` and the destructor, both on the main thread).
  new-p15-252: the single call site of `snd_pcm_poll_descriptors()` was
  changed, and the sibling check on `snd_pcm_poll_descriptors_count()`
  (`count > 0`) is unchanged.
- **No collateral damage.** No project, save file or user data is touched: the
  changes are in the ALSA backend's error paths. The poll fix changes nothing
  on a successful retrieval (the same descriptors are marked, in the same
  order). The capture fix changes only WHO prints the message and WHEN: the
  text is unchanged, and the failure is still reported exactly once. The
  capture path's other behaviour (the overrun counter, the prepare/start
  recovery, the published frames) is untouched, and the playback path is
  byte-identical.
- **Realtime.** Nothing was added to an audio path: the capture thread now
  performs ONE relaxed atomic store instead of a `printf()` (no allocation,
  no lock, no I/O, no logging — strictly less than before), and the reporting
  `printf()` runs on the main thread when the capture is closed. The playback
  thread, the render path, the undo stack and the command registry are not
  touched; no control command was added (the command snapshot is unchanged).
- **Portability.** The change uses only C++20 (`std::atomic`) and POSIX
  `fcntl`/`FD_CLOEXEC` that the file already used; the new test uses
  `QSemaphore`, `QFile` and the ALSA headers the file already includes. No
  Qt-6-only API, no `windows.h` name, no `std::filesystem`.
- **ALREADY-FIXED / FALSE-POSITIVE.** Not applicable: both verdicts are FIXED.
  The code that shows both bugs were real is cited with file:line in the two
  "Evidence" sections above (cluster 4584: `src/core/audio/AudioAlsa.cpp:606-608`
  (the loop's own no-logging rule), `:621`/`:650` (the only two call sites of
  `handleCaptureError()`, both on the capture thread) and the original
  `:524-525`/`:547-548` `printf()`s; new-p15-252:
  `src/core/audio/AudioAlsa.cpp:111-113` (the dropped return value) and
  `:115-120` (the uninitialised `ufds[i].fd` handed to `fcntl()`)).

### Pre-existing failures in this tree (not caused by these fixes)

`ctest -R '^(ControlEngineWiring|ControlGoldenAudio)$'` fails in this tree
BOTH with the fixes reverted and with them applied (`test-control-baseline.log`
vs `test-audio-sweep.log`): the device catalogue is empty in this build tree
and a render ends in `std::bad_alloc`. The product changes were reverted
(`git apply -R` of both patches), `zene` was rebuilt and the same two tests
failed identically — so they are this tree's state, not a regression of these
fixes.

## Files committed

Commit 1 (`fix(alsa): new-p15-252 — check the poll-descriptor retrieval result`):

- `src/core/audio/AudioAlsa.cpp` (the fix)
- `tests/src/core/AudioAlsaPollDescriptorTest.cpp` (the regression test)
- `tests/upstream-modifications.txt` (the declared divergence of the inherited file)
- `docs/qwen-fixes-2026-10/cluster-4584.md` (this report, first revision)

Commit 2 (`fix(alsa): cluster 4584 — the capture thread no longer logs`):

- `src/core/audio/AudioAlsa.cpp` (the fix)
- `include/AudioAlsa.h` (the parked errors and the reporter)
- `tests/src/core/AudioAlsaCaptureErrorTest.cpp` (the regression test, new)
- `tests/CMakeLists.txt` (the registration of the new test)
- `tests/fork-sources.txt`, `tests/all-sources.txt` (the new test source registered)
- `tests/upstream-modifications.txt` (the declared divergences)
- `docs/qwen-fixes-2026-10/cluster-4584.md` (this report)
