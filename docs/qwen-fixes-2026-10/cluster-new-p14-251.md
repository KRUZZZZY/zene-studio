FIXED
new-p14-251: FIXED
new-p16-190: FIXED
new-p14-250: FIXED

# Cluster new-p14-251 — RemotePluginBase receive path (3 bugs)

All three findings are REAL on branch `bugs/qwen-new-p14-251` (base `b61e5f965`). The fixes are in
`src/common/RemotePluginBase.cpp`, `include/RemotePluginBase.h` and (for the caller only) `src/core/RemotePlugin.cpp`.
The regression tests are in `tests/src/core/RemotePluginAudioPortsTest.cpp`.

Logs: the supplied log folder is outside this session's writable sandbox. Writing to it was attempted and
refused (`touch
/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p14-251.r1/logs/probe.txt` → EXIT=1,
`Permission denied`; the `write` tool returned `EACCES`). Every log was therefore written to
`build/qwen-logs/new-p14-251.r1/` inside the worktree, with the names quoted below. The gate script was also
run as an identical copy whose only change is that its log directory is writable
(`build/qwen-logs/feature_check_local.py`); the official run and its permission error are recorded too.

## new-p14-251 — decoding client messages allocated on the audio thread

**Verdict: FIXED (REAL).** `RemotePlugin::collectProcessingReplies()` called
`receiveFrameBefore()` (src/core/RemotePlugin.cpp:631) on the audio thread, and that path built a
`std::vector<std::string>` in `receiveMessage()` / `readFrameLocked()` before the handlers deferred their
work (`IdDebugMessage`, `IdChangeInputOutputCount`). With a 1000-byte debug text the period made 6 allocations.

**Fix:** `receiveFrameBefore(deadline, out, bounded=true)` now reads the complete wire frame into a
once-allocated `m_frameScratch` (sized in the constructor, src/common/RemotePluginBase.cpp:67-73) and
`parseBoundedFrame()` (src/common/RemotePluginBase.cpp:350) makes `message::argumentView()` a
`std::string_view` into it (include/RemotePluginBase.h:478-500); the existing `stringArgument()` keeps its
owning `const std::string &` contract. `getInt()` parses the view with
`std::from_chars`, and `size()` reports the wire count. Socket
(`receiveBoundedSocketFrame`, src/common/RemotePluginBase.cpp:451) and shared-memory
(`readFrameLockedBounded`, src/common/RemotePluginBase.cpp:547) both use the same parser. The caller asks for
the bounded form at src/core/RemotePlugin.cpp:631. The main-thread receive path is unchanged (owning messages).

**RED (runtime, bounded dispatch reverted):**
`git diff -- src/common/RemotePluginBase.cpp > build/qwen-logs/new-p14-251.r1/revert-251-bounded-dispatch-final.patch`
EXIT=0; `git apply -R …/revert-251-bounded-dispatch-final.patch` EXIT=0;
`cmake --build build --target RemotePluginAudioPortsTest -j4` EXIT=0;
`./RemotePluginAudioPortsTest completeDebugFrameDecodesWithoutAllocating` EXIT=1 — `Actual (allocations): 6`,
`Expected: 0` (`test-251-direct-red-final.log`).
The complete 251 product patch was also reversed earlier (`fix-251-final.patch`, product files of commit
651d8b41e) EXIT=0; the peer path then failed at run time with allocations 6 (`test-251-red-final.log`).
Reversing the whole API removes the bounded parameter, so after that API-wide reverse the direct test no longer
compiles; the dispatch revert above is the final runtime proof that the assertion catches the allocation.

**GREEN:**
`git apply -R …/revert-251-bounded-dispatch-final.patch` EXIT=0; build EXIT=0;
`./RemotePluginAudioPortsTest completeDebugFrameDecodesWithoutAllocating` EXIT=0
(`test-251-direct-green-final.log`); the peer path also passes in the full-family run.

**Tests:** `completeDebugFrameDecodesWithoutAllocating()` (socketpair, complete 1000-byte debug frame
pre-written, allocation probe, no peer/clock) and `debugMessageDuringPeriodDoesNotAllocate()` (fake client
sends debug text + `IdProcessingDone` in one write; period must allocate nothing). Full family:
`cd build/tests && ctest -R '^RemotePluginAudioPortsTest$' --output-on-failure` EXIT=0,
1/1 executable, 23 QtTest functions (`ctest-family-final9.log`, `test-functions-final.log`).

## new-p16-190 — a 100 ms query could block for two seconds

**Verdict: FIXED (REAL).** `receiveStartedFrame()` started its clock at
`now + StartedFrameTimeout` (2 s) and ignored the query deadline passed to `receiveWithin()`
(src/common/RemotePluginBase.cpp:405-406 before the fix). A partial frame therefore stretched a 100 ms query.

**Fix:** `receiveStartedFrame()` now takes the caller's `_deadline` and uses
`std::min(now + StartedFrameTimeout, _deadline)` (src/common/RemotePluginBase.cpp:405-409); `receiveWithin()`
passes the query deadline (src/common/RemotePluginBase.cpp:504). `waitForMessageReadable()` is virtual so the
regression can observe the requested wait without waiting (include/RemotePluginBase.h:741-748).

**RED:** reverted only the deadline cap (`fix-190-revert-cap.patch`) with
`git apply -R …/fix-190-revert-cap.patch` EXIT=0; build EXIT=0;
`./RemotePluginAudioPortsTest startedFrameWaitIsCappedByTheQueryDeadline` EXIT=1 —
`'receiver.lastWaitMs < 1000' returned FALSE (a started frame ignored the query deadline and asked to wait up
to 2 s)` (`test-190-red.log`).

**GREEN:** `git apply …/fix-190-revert-cap.patch` EXIT=0; build EXIT=0; same test EXIT=0
(`test-190-green-restored.log`).

**Test:** `startedFrameWaitIsCappedByTheQueryDeadline()` injects the per-piece wait result through
`waitForMessageReadable()`: it returns true for the zero poll, records the first positive timeout and reports
that the rest never arrives. A 200 ms deadline makes the old code record ~2000 ms and the fixed code <1000 ms.
No sleep, no wall-clock assertion.

## new-p14-250 — a started frame could allocate gigabytes before timing out

**Verdict: FIXED (REAL).** `receiveStartedFrame()` checked `length < 0` but not the remaining frame capacity
before `std::string arg(length)` (src/common/RemotePluginBase.cpp:392-410 before the fix). The FIFO
`readString()` also resized before checking that the bytes existed (include/RemotePluginBase.h:226-237 before
the fix).

**Fix:** `receiveStartedFrame()` tracks the bytes already consumed and rejects an argument that cannot fit in
the remaining peek-window frame before allocating (src/common/RemotePluginBase.cpp:424-438), and
`shmFifo::readString()` checks the signalled FIFO's remaining bytes under its data lock before resizing
(include/RemotePluginBase.h:226-252). Lengths that fit are decoded exactly as before. The owning socket
`readString()` (the main thread's whole-message wait for settings, presets and VST chunks) is deliberately not
capped by the audio thread's peek window, so a legitimate large chunk still loads.

**RED:** `git diff -- src/common/RemotePluginBase.cpp > build/qwen-logs/new-p14-251.r1/revert-250-budget-final.patch`
(after temporarily disabling only the remaining-capacity check); `git apply -R …` EXIT=0; build EXIT=0;
`./RemotePluginAudioPortsTest oversizedStartedFrameLengthIsRefusedBeforeAllocating` EXIT=1 —
`Actual (allocations): 1`, `Expected: 0` (`test-250-red-final2.log`).

**GREEN:** `git apply -R …/revert-250-budget-final.patch` EXIT=0; build EXIT=0; same test EXIT=0
(`test-250-green-final2.log`); the pre-existing count-limit regression also passes
(`oversizedArgumentCountInvalidatesRemoteMessage`, `ctest-family-final9.log`).

**Test:** `oversizedStartedFrameLengthIsRefusedBeforeAllocating()` injects the 8 MiB argument length only after
the peek has seen the 8-byte header, so the old path really reaches the allocation; progress is 0 and the
payload never arrives (no sleep). The allocation probe must see 0.

## Gates and evidence table

| Step | Command (from the worktree) | EXIT | Log |
|---|---|---|---|
| build (190 fix) | `cmake --build build --target RemotePluginAudioPortsTest -j4` | 0 | `build-190-green.log` |
| 190 RED | `git apply -R …/fix-190-revert-cap.patch; build; ./RemotePluginAudioPortsTest startedFrameWaitIsCappedByTheQueryDeadline` | 0;0;1 | `test-190-red.log` |
| 190 GREEN | `git apply …/fix-190-revert-cap.patch; build; same test` | 0;0;0 | `test-190-green-restored.log` |
| 250 RED | `git apply -R …/revert-250-budget-final.patch; build; ./RemotePluginAudioPortsTest oversizedStartedFrameLengthIsRefusedBeforeAllocating` | 0;0;1 | `test-250-red-final2.log` |
| 250 GREEN | `git apply -R …/revert-250-budget-final.patch; build; same test` | 0;0;0 | `test-250-green-final2.log` |
| 251 RED | `git apply -R …/revert-251-bounded-dispatch-final.patch; build; ./RemotePluginAudioPortsTest completeDebugFrameDecodesWithoutAllocating` | 0;0;1 | `test-251-direct-red-final.log` |
| 251 GREEN | `git apply -R …/revert-251-bounded-dispatch-final.patch; build; same test` | 0;0;0 | `test-251-direct-green-final.log` |
| family | `cd build/tests && ctest -R '^RemotePluginAudioPortsTest$' --output-on-failure` | 0 | `ctest-family-final6.log` (1/1, 23 functions) |
| gates (official) | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | 1 (sandbox: cannot create `<worktree>-feature-check`; `Permission denied`) | `feature-check-official-final2.log`, `feature-check-official.log` |
| gates (identical copy, writable log dir) | `python3 build/qwen-logs/feature_check_local.py <worktree>` | 0 — all 8 rows PASS | `feature-check-local-final2.log` |

`feature-check-local-final2.log` rows: fork-sources PASS, all-sources-reproduce PASS, no-upstream-regression PASS,
check-namespace PASS, no-tautology PASS, doc-figures PASS, complexity PASS (no new item in your files),
file-length PASS (no new item in your files).

## Files committed

- `fffb61fe8` new-p16-190: `include/RemotePluginBase.h`, `src/common/RemotePluginBase.cpp`,
  `tests/src/core/RemotePluginAudioPortsTest.cpp`, `tests/upstream-modifications.txt`
- `014079653` new-p14-250: `include/RemotePluginBase.h`, `src/common/RemotePluginBase.cpp`,
  `tests/src/core/RemotePluginAudioPortsTest.cpp`, `tests/upstream-modifications.txt`
- `651d8b41e` new-p14-251: `include/RemotePluginBase.h`, `src/common/RemotePluginBase.cpp`,
  `src/core/RemotePlugin.cpp`, `tests/src/core/RemotePluginAudioPortsTest.cpp`,
  `tests/upstream-modifications.txt`
- `02757b4f8` new-p14-251 guard labels: `include/RemotePluginBase.h`, `src/common/RemotePluginBase.cpp`,
  `tests/upstream-modifications.txt`
- `e6de14ae6` new-p14-251 direct regression: `tests/src/core/RemotePluginAudioPortsTest.cpp`,
  `tests/upstream-modifications.txt`
- new-p14-250 follow-up: `include/RemotePluginBase.h` (the owning socket `readString()` is not capped
  by the audio peek window), report, `tests/upstream-modifications.txt`
- `451db1c44` new-p14-251 API: `include/RemotePluginBase.h`, `src/core/RemotePlugin.cpp`, report,
  `tests/upstream-modifications.txt`
- `02d242368` new-p14-251 direct test uses `argumentView`: `tests/src/core/RemotePluginAudioPortsTest.cpp`
- this report: `docs/qwen-fixes-2026-10/cluster-new-p14-251.md`

## Self-check

**1. Regression test FAILS with the product change reverted and PASSES with it (exit codes).**
- new-p16-190: RED EXIT=1 (`test-190-red.log`), GREEN EXIT=0 (`test-190-green-restored.log`).
- new-p14-250: RED EXIT=1 (`test-250-red-final2.log`, allocations 1), GREEN EXIT=0
  (`test-250-green-final2.log`).
- new-p14-251: RED EXIT=1 (`test-251-direct-red-final.log`, allocations 6), GREEN EXIT=0
  (`test-251-direct-green-final.log`); the full-frame peer variant is also RED EXIT=1
  (`test-251-red-final.log`) and GREEN EXIT=0 (`test-251-green-final.log`).

**2. The test is deterministic: no wall-clock timing, sleeps or races (name the seam or injected values).**
- 190: `TestRemotePluginBase::waitForMessageReadable()` seam records the requested timeout; the socket carries
  only the 8-byte header, the hook reports that the rest never arrives. The assertion is on the requested
  timeout, not elapsed time.
- 250: the same seam supplies the 8 MiB length only after the peek saw the header, and progress is 0. The probe
  counts allocations only; no clock.
- 251: the primary regression pre-writes a complete frame on a socketpair, calls the same
  `receiveFrameBefore(..., bounded=true)` the period uses, and counts allocations. No peer, no sleep, no
  clock. The peer-based period test is an additional end-to-end check and makes no timing assertion; the
  deterministic socketpair test is the regression.

**3. The fix is at the cause and complete: every caller and sibling checked.**
- `receiveFrameBefore` callers: `RemotePlugin::collectProcessingReplies()` (audio, now bounded) and
  `RemotePlugin::pumpBeforeBuffer()` (main thread, owning path unchanged).
- `receiveStartedFrame` caller: only `receiveWithin()`; it now passes the deadline.
- `readString()` siblings: `shmFifo::readString()` bounds the length before resizing; the socket owning
  `readString()` is the main-thread whole-message path and is intentionally left uncapped by the audio peek
  window, so large VST chunks still load. The socket started-frame parser (the finding's socket half) bounds
  before allocating.
- Bounded decode siblings: socket `receiveBoundedSocketFrame()` and shared-memory `readFrameLockedBounded()`
  both funnel through `parseBoundedFrame()`; `receiveMessage()`/`readFrameLocked()` remain the owning path for
  the main thread.
- Message accessors: `getString`/`getQString` still return owned values; `argumentView` returns a view for
  the bounded path while `stringArgument` keeps its owning-reference contract; `getInt`/`getFloat`/`size`
  handle both modes. The only view consumer in product code
  (`RemotePlugin.cpp:1111`) consumes the view immediately by copying into the fixed pending-debug slot.

**4. No collateral damage: old projects and saved data load as before; no limit hits other callers.**
- The wire format is unchanged. The owning socket/FIFO decode still accepts every frame it accepted before;
  the new length checks only refuse a length that cannot fit in the frame/FIFO the transport already bounds.
- Bounded mode is used only by the audio-thread period receive; all main-thread queries, settings, preset and
  chunk messages keep the owning `std::string` path. A bounded message's views live until the next receive and
  are consumed immediately (debug text is copied into the existing 256-byte slot; counts are parsed as ints).
- No new commands, no undo changes, no save-format changes.

**5. Nothing added runs on the audio thread and allocates, frees, locks, waits, does I/O or emits a signal.**
- The bounded receive allocates nothing: the frame buffer is allocated once in the constructor; parsing walks
  length prefixes in place; `getInt` uses `std::from_chars`; `argumentView` is a view.
- The socket bounded receive takes the existing `m_receiveMutex` (already taken by `receiveMessage()` on this
  path) and reads only a frame the peek has already found complete, so it does not wait for new bytes. The FIFO
  bounded receive runs under the existing FIFO data lock taken by `receiveFrameBefore()` and reads a signalled
  whole message. `processMessage()` already deferred logging and resizing work; this change removes the decode
  allocation that preceded it.

**6. Official gate note.** The official `feature_check.py` cannot create its sibling log directory in this
session's sandbox (`feature-check-official-final2.log` records the `PermissionError`). The identical script copy with
only the log path changed runs the same eight gates in the same worktree and returns EXIT=0, all PASS
(`feature-check-local-final2.log`). The official `submit_check.py` run is recorded in `submit-2.log`; its only gate
FAIL is the same sandbox path issue, and the local gate run above is the evidence for that row.
