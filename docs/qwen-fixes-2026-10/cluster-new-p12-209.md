ALREADY-FIXED

# cluster new-p12-209 - oversized shared-memory messages hang or corrupt the stream

## Verdict

ALREADY-FIXED on branch `bugs/qwen-new-p12-209`. The reported defect is the exact one fixed
earlier on this line by commit `b5ecf3f46a` ("fix(remote-plugin): reject oversized shared FIFO
frames (new-p15-170)"), on top of `94cf5d4456` ("fix(remote-plugin): unblock shared FIFO
writers"). No product change was made in this session.

## Code evidence

`src/common/RemotePluginBase.cpp:305-324` now computes the complete serialized frame size before
touching the FIFO and refuses the message before writing any bytes:

- line 305: `frameSize = 2 * sizeof(std::int32_t)` (the two header ints).
- line 306-313: the argument-count check `_m.data.size() > (SHM_FIFO_SIZE - frameSize) / 4`;
  on overflow the transport calls `invalidate()`, releases the peer's token with
  `m_out->messageSent()` and returns `0` - so the caller sees the refusal and neither end waits
  for a frame that can never fit.
- line 314-324: the running `frameSize` check for every argument
  (`argument.size() > SHM_FIFO_SIZE - frameSize`). This is the path that also catches a single
  argument larger than the 512 KiB capacity, so `shmFifo::write()`'s silent
  `_len > SHM_FIFO_SIZE` skip at `include/RemotePluginBase.h:436` is no longer reachable with a
  length that was already sent.
- `include/RemotePluginBase.h:453` (`releaseLockForSpaceWait()` inside `shmFifo::write`,
  defined at line 390) releases the data semaphore while a writer waits for space and reacquires
  it afterwards; `include/RemotePluginBase.h:441-461` restores the depth and re-checks
  `isInvalid()`. The reader can therefore consume while a legacy blocking send waits, so the
  outer-lock deadlock described in the finding cannot occur.
- `include/RemotePluginBase.h:474` keeps the invalidation flag atomic, which is how the refusal
  reaches the peer's next receive.

Sibling paths checked: the only writes to the outgoing FIFO in `src/` are
`sendFrameNonBlocking()` (`src/common/RemotePluginBase.cpp:162-183`, which preflights
`freeSpace() < _size` and returns `NoRoom`) and `sendMessage()` (`:302`). Both refuse before
writing; neither writes a partial frame. `writeRaw()`/`writeString()` only run after those
preflight checks.

## Regression test already on the branch

`tests/src/core/RemotePluginShmFifoTest.cpp:146`
`oversizedAggregateMessageFailsAndReleasesTheReceiver()` builds two 300 KiB arguments
(individually under capacity, together over it), sends them through a real `shmFifo` pair, and
asserts: no space wait happened, `sendMessage()` returned 0, the sender is invalid, and the peer
receives `ReceiveResult::Lost` with an `IdUndefined` message instead of waiting for a frame.
It is registered as ctest test `RemotePluginShmFifoTest` (`tests/CMakeLists.txt:3248-3259`).
The related writer-lock regression is `recursiveSpaceWaitReleasesTheDataSemaphore()`
(`tests/src/core/RemotePluginShmFifoTest.cpp:175`).

## Commands and results (all from this session)

Logs are in `/home/kruzzzzy/.config/kads/zene/state/runs/new-p12-209.r1/logs`.

1. Green, fix present as shipped:
   `cmake --build build --target RemotePluginShmFifoTest -j4` -> EXIT=0 (`build-1.log`)
   `cd build/tests && ctest -R 'RemotePluginShmFifoTest' --output-on-failure` -> EXIT=0
   (`test-1.log`, `100% tests passed out of 1`, 0.03 s)

2. Prove the test catches the bug by reversing only the product hunk:
   `git show b5ecf3f46a -- src/common/RemotePluginBase.cpp > logs/oversized-preflight.patch`
   -> EXIT=0; `git apply -R --check .../oversized-preflight.patch` -> EXIT=0
   `git apply -R .../oversized-preflight.patch` -> EXIT=0
   `cmake --build build --target RemotePluginShmFifoTest -j4` -> EXIT=0 (`build-2-red.log`)
   `cd build/tests && ctest -R 'RemotePluginShmFifoTest' --output-on-failure` -> EXIT=8
   (`test-2-red.log`):
   `FAIL!  : RemotePluginShmFifoTest::oversizedAggregateMessageFailsAndReleasesTheReceiver()`
   `'!s_spaceWaitObserved.load(...)' returned FALSE`; `Totals: 10 passed, 1 failed`.

3. Restore the fix and re-run:
   `git apply .../oversized-preflight.patch` -> EXIT=0 (`git status --short` empty)
   `cmake --build build --target RemotePluginShmFifoTest -j4` -> EXIT=0 (`build-3-green.log`)
   `cd build/tests && ctest -R 'RemotePluginShmFifoTest' --output-on-failure` -> EXIT=0
   (`test-3-green.log`, `100% tests passed out of 1`, 0.02 s)

4. Gates: `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>`
   -> EXIT=0 (`gates-1.log`), RESULT: PASS, all eight checks PASS; complexity and file-length
   report "no new item in your files".

5. Completion check: `python3 .../submit_check.py <worktree> new-p12-209` -> EXIT=1 before the
   report existed (`submit-check-1.log`), failing only "report ... missing" and "report has a
   'Self-check' section"; gates and tests items passed. With the report written but still
   untracked, `submit-check-2.log` -> EXIT=1, failing only "working tree clean". After committing
   the report, `submit-check-3.log` -> EXIT=0, `RESULT: PASS - submit`, all six checks PASS.

## Self-check

- Regression test FAILS with the product change reverted and PASSES with it. Reversing only the
  `sendMessage` preflight hunk, rebuilding and running ctest gave EXIT=8 with
  `oversizedAggregateMessageFailsAndReleasesTheReceiver` failing on the injected space-wait
  observation; restoring the hunk, rebuilding and rerunning gave EXIT=0. Commands and logs are
  listed above (steps 2 and 3). The product change reverted here is the pre-existing fix hunk,
  because this session's verdict is ALREADY-FIXED and it made no product edits.
- The test is deterministic. It uses the `LMMS_TESTING` seam
  `shmFifo::setSpaceWaitHookForTest()` (`include/RemotePluginBase.h:214-217`), which records
  that `shmFifo::write()` reached its space wait and invalidates the FIFO so the test cannot
  hang. No wall-clock timing, sleeps or thread races: the assertions are on the hook flag, the
  `sendMessage()` return value and the FIFO's invalid flags. Both red and green runs finished
  in milliseconds.
- The fix is at the cause and complete. The preflight is exact: 8 header bytes + 4 bytes per
  argument length + payload bytes, checked against `SHM_FIFO_SIZE` before any write; both the
  argument-count path and every-argument path are covered. The only other outgoing-FIFO writer,
  `sendFrameNonBlocking()`, separately preflights `freeSpace()` and returns `NoRoom`
  (`src/common/RemotePluginBase.cpp:162-183`); no other code writes to `m_out`. A single
  oversized argument takes the same per-argument refusal path as the second 300 KiB argument in
  the test, because the comparison is against the remaining capacity.
- No collateral damage from this session: no product code, saved data or load path changed. The
  512 KiB bound is the FIFO's own capacity (`include/RemotePluginBase.h:115`), and a frame of
  exactly `SHM_FIFO_SIZE` bytes still fits (`freeSpace()` at `include/RemotePluginBase.h:346`
  returns the full segment when empty), so callers below the limit are unaffected. The whole
  `RemotePluginShmFifoTest` family passes.
- ALREADY-FIXED evidence: `src/common/RemotePluginBase.cpp:305-324` (preflight, invalidate,
  `messageSent()`, return 0) and the shipped regression
  `tests/src/core/RemotePluginShmFifoTest.cpp:146`; the fix commits are `b5ecf3f46a` and
  `94cf5d4456`, both ancestors of `HEAD`.
- No undo/command, registration or realtime surface is touched: this session changes only this
  report document. The preflight runs on the sending thread before the FIFO lock, not on the
  audio thread, and `sendFrameNonBlocking()` (the audio-thread send) allocates, locks and waits
  no more than before.

## Other findings

- The same defect was reported as cluster new-p15-170 and its report was filed under
  `docs/qwen-fixes-2026-10/cluster-new-p14-259.md`; this cluster is a duplicate of that accepted
  work.
