Verdict: ALREADY-FIXED

new-p12-215 (integration): the finding was REAL on the accepted patch's base, and the pinned target already carries the same null guard from an independently accepted patch (`new-p10-345`). This integration therefore changed no product code: the conflict in `include/SampleFrameRingBuffer.h` was resolved by taking the target line's guard, which is a superset of the accepted hunk, and the accepted regression test and this report are retained.

## What the accepted patch is

Source `bugs/qwen-new-p12-215` @ `fb598e19636f7472b691af2a1f13464f48b7bf7b` (base `639e1d23e4b84a751f6ed90bd626d35d24727cc4`); target pinned at `050/m0-train-22` @ `4ee5321ffc929edc8260b4764ffe3fec5ed0ff54`. The accepted product hunk is one guard in `SampleFrameRingBuffer::read()`, plus a regression slot and its report (see `accepted.patch` in the run folder):

```
     //! Consumer: read up to \a frames frames into \a dst. Returns the number
-    //! of frames read (0 when empty).
+    //! of frames read (0 when empty, or when \a dst is null - nothing is consumed).
     std::size_t read(SampleFrame* dst, std::size_t frames) noexcept
     {
+        if (dst == nullptr) { return 0; }
```

`new-p10-345` (commit `021bf8e310`, 2026-10-27 10:28:44 +0000) landed on the train before the accepted patch was cut (`7ad353b0e`, 2026-10-27 10:58:49 +0000) and is an ancestor of the target:

```
$ git merge-base --is-ancestor 021bf8e310 HEAD ; echo $?
0
```

## Decision and code evidence

- Reconciled `include/SampleFrameRingBuffer.h` is byte-identical to the target HEAD (`git show HEAD:include/SampleFrameRingBuffer.h` vs the worktree file: equal). I took `theirs` — no product line changed.
- The target guard at `include/SampleFrameRingBuffer.h:105` is `if (dst == nullptr || frames == 0) { return 0; }`. It is a strict superset of the accepted `if (dst == nullptr) { return 0; }`:
  - `dst == nullptr`, any `frames`: both return 0 before touching `m_readPos`; the staged frames stay queued.
  - `dst != nullptr`, `frames == 0`: the target returns 0 immediately; the accepted code would fall through, load the indices, compute `toRead = min(0, available) = 0`, execute no copy, store `m_readPos + 0`, and return 0. Same return value, same indices, no memory access through `dst`. No observable difference.
- The doc comment differs in wording only ("nothing is consumed then, so the frames stay queued" vs "nothing is consumed"); both state the same contract.
- So there is **no semantic difference** between the accepted patch and the target line, and no demonstrated regression to fix.

### Why the finding is still a real defect (not a false positive)

With the guard removed from the target header (all else unchanged), the retained accepted regression dereferences null and dies:

```
$ ./RecordingRealtimeTest ringBufferReadRejectsNullDestination     # guard removed
EXIT=139   (SIGSEGV, "accessing address 0x0000000000000000", inside ringBufferReadRejectsNullDestination)
```

and the whole binary fails as well (`ctest` EXIT=8). That matches the round-1 evidence (r1 `test-reverted.log`: SIGSEGV in the same code path). The defect was real where the finding was filed; it is the guard, not the comment, that closes it.

## Affected failure paths and supported-platform hazards

- Consumers of this ring (`SampleFrameRingBuffer` has exactly two production users):
  - `src/core/AudioEngine.cpp:872` — render thread, destination `m_inputBuffer[m_inputBufferRead]`, allocated for both slots at `src/core/AudioEngine.cpp:512`. Non-null, so the guard is never taken; the render path is unchanged.
  - `include/SampleRecordAccumulator.h:292` — drain thread, destination `m_scratch.data()` from a vector sized `DrainBatchFrames` (`:183`, 4096). Non-null.
  - `src/core/audio/TrackRecorder.cpp:379` uses `RecordRingBuffer` (`include/TrackRecorder.h:173`), a different class with its own guard; not affected.
- Sibling contracts stay consistent: `writeBlock()` refuses a null source in the same file (`:83`); `InputChannelRing::read()` (`include/InputChannelRing.h:143`) and `RetroAudioRing::copyOut()` (`include/RetroAudioRing.h:159`) already refuse a null destination.
- Realtime: the guard is one pointer/integer compare on paths that already run per period; no allocation, free, lock, wait, I/O or log is added. Nothing new runs on the audio thread.
- Platforms: header-only inline body change, no new include, no Qt API, no `windows.h` names, no `std::filesystem`. The regression uses `SampleFrame`, `std::vector` and QTest comparisons only. No platform-specific hazard; MSVC/macOS/Qt 5.15 builds see the same standard C++.
- Save data / projects / commands / undo: untouched. No wire command, no project format, no undo transaction is involved.

## Commands and exit codes (all run in this worktree)

Every log was written to a new numbered file. The brief's LOG FOLDER (`.../runs/new-p12-215.r2/logs`) is **not writable in this sandbox** — `touch` there returns `Permission denied` even as its owner (the sandbox denies writes outside the worktree and `$TMPDIR`), so logs went to `$TMPDIR/new-p12-215.r2-logs/` (disk, not `/tmp`). I also had to bypass the bunny build-guard shim, whose lease file `.../bunny/build-pool.lock` is likewise outside the sandbox; `ZENE_BUILD_GUARDED=1` runs the same `/usr/bin/cmake` without that lease. Both deviations are environment limits, not results.

| Step | Command | EXIT |
| --- | --- | --- |
| Conflict resolved | `git checkout HEAD -- include/SampleFrameRingBuffer.h` (take the target's superset guard) | 0 |
| Accepted test hunk applied on top | `git apply --include='tests/src/core/RecordingRealtimeTest.cpp' .../r2/accepted.patch` | 0 |
| Build, integrated tree | `ZENE_BUILD_GUARDED=1 CCACHE_DISABLE=1 /usr/bin/cmake --build build --target zene RecordingRealtimeTest -j4 > build-3-green.log 2>&1` | **0** (0 `error:` lines) |
| Focused test, GREEN | `cd build/tests && ctest -R '^RecordingRealtimeTest$' --output-on-failure > test-2-green.log 2>&1` | **0**, 1/1 passed |
| Whole binary (all slots) | `./RecordingRealtimeTest > test-2b-green-binary.log 2>&1` | **0**, `Totals: 56 passed, 0 failed, 0 skipped` — both `ringBufferReadRefusesNullDestination` (accepted) and `ringBufferReadRejectsNullDestination` (target) are `PASS` |
| RED step 4b: guard removed | `python3 -` deletes `if (dst == nullptr \|\| frames == 0) { return 0; }` from `include/SampleFrameRingBuffer.h`; `git diff -- include/SampleFrameRingBuffer.h > reversal-remove-guard.patch` (572 bytes) | 0 |
| RED build | `ZENE_BUILD_GUARDED=1 CCACHE_DISABLE=1 /usr/bin/cmake --build build --target RecordingRealtimeTest -j4 > build-4-red.log 2>&1` | **0** (reverted code compiles, so the failure below is the test's) |
| RED, focused | `cd build/tests && ctest -R '^RecordingRealtimeTest$' --output-on-failure > test-3-red.log 2>&1` | **8** (SIGSEGV, `0% tests passed`) |
| RED, accepted slot alone | `cd build/tests && ./RecordingRealtimeTest ringBufferReadRejectsNullDestination > test-4-red-slot.log 2>&1` | **139** (SIGSEGV at address 0x0 in that slot) |
| GREEN step 4b: guard restored | `git checkout HEAD -- include/SampleFrameRingBuffer.h` (back to the target line) | 0 |
| GREEN build | `ZENE_BUILD_GUARDED=1 CCACHE_DISABLE=1 /usr/bin/cmake --build build --target RecordingRealtimeTest -j4 > build-6-green-4b.log 2>&1` | **0** |
| GREEN, accepted slot alone | `cd build/tests && ./RecordingRealtimeTest ringBufferReadRejectsNullDestination > test-5-green-slot.log 2>&1` | **0**, `Totals: 3 passed` |
| GREEN, focused (4b) | `cd build/tests && ctest -R '^RecordingRealtimeTest$' --output-on-failure > test-6-green-4b.log 2>&1` | **0**, 1/1 passed |
| Family: build the other four | `ZENE_BUILD_GUARDED=1 CCACHE_DISABLE=1 /usr/bin/cmake --build build --target RecordingInputPathTest RecordingCrashLossTest RecordingPassTest RecordingLatencyTest -j4 > build-7-family.log 2>&1` | **0** |
| Family: run | `cd build/tests && ctest -R '^(RecordingInputPathTest\|RecordingPassTest\|RecordingLatencyTest\|RecordingCrashLossTest\|RecordingRealtimeTest)$' --output-on-failure > test-8-family.log 2>&1` | **0**, 5/5 passed (InputPath, CrashLoss, Realtime, Pass, Latency) |

The first family attempt (`test-7-family.log`) reported 4 `Not Run` because only `RecordingRealtimeTest` existed in this build tree; the four targets were then built (`build-7-family.log`, EXIT=0) and the identical ctest command was rerun. The table above records the final, real run.

### Gates

| Gate | Command | EXIT |
| --- | --- | --- |
| canonical feature_check | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > gate-1-original.log 2>&1` | **1** — `PermissionError` creating `<worktree>-feature-check` in the read-only parent, before any gate runs (same failure as r1; no gate was evaluated) |
| full gate table (local copy) | `python3 $TMPDIR/feature_check_local.py <worktree> > gate-2-local.log 2>&1` | **0** — `RESULT: PASS`: fork-sources PASS, all-sources-reproduce PASS, no-upstream-regression PASS, check-namespace PASS, no-tautology PASS, doc-figures PASS, complexity PASS (no new item in your files), file-length PASS (no new item in your files) |

The local copy is the same script with two lines changed: `ROOT` points at the shared qwen directory (so `FIX-BASE` and the cached baseline for `4ee5321ffc` are read as-is) and the log folder is redirected into `$TMPDIR`. No gate command, limit, baseline or ratchet was altered. The base is `4ee5321ffc` (this integration's target), and the two changed files are `tests/src/core/RecordingRealtimeTest.cpp` and `tests/upstream-modifications.txt`.

Registration: `include/SampleFrameRingBuffer.h` is already in `tests/fork-sources.txt`; the inherited test file is declared in `tests/upstream-modifications.txt` with the `new-p12-215` reason appended by this integration (a ledger entry is required for any inherited file changed, even though `tests/**` is allowed by Gate 6).

## Files committed

- `tests/src/core/RecordingRealtimeTest.cpp` — the accepted regression `ringBufferReadRefusesNullDestination` (line 258), retained on top of the target's own `ringBufferReadRejectsNullDestination` (line 327).
- `tests/upstream-modifications.txt` — reason line for the inherited test file.
- `docs/qwen-fixes-2026-10/cluster-new-p12-215.md` — this report.

`include/SampleFrameRingBuffer.h` is deliberately **not** in the commit: the conflict resolution is the target's existing line, so the product diff against the target is empty. This is the honest integration result — the accepted patch is already present semantically.

## Self-check

1. **Regression FAILS with the product change reverted and PASSES restored.** Yes, with exit codes. Guard removed: focused `ctest` EXIT=8 (SIGSEGV), accepted slot alone EXIT=139. Guard restored: focused `ctest` EXIT=0 (1/1), slot alone EXIT=0 (`Totals: 3 passed`), full binary EXIT=0 (`Totals: 56 passed, 0 failed`). Logs: `test-3-red.log`, `test-4-red-slot.log`, `test-6-green-4b.log`, `test-5-green-slot.log`, `test-2b-green-binary.log`.
2. **Deterministic.** Yes. The retained slot stages one fixed `SampleFrame` (`frameAt(7)`) in a 16-frame ring on one thread, then checks `read(nullptr, 0)`, `read(nullptr, 1)`, `available() == 1`, the frame's intact values after a real read, and `read(nullptr, 1)` on an empty ring. No timing, sleeps, threads or injected clocks; no seam is needed.
3. **Fix at the cause and complete.** The guard sits in `read()` itself, before the copy loop and before `m_readPos` moves. Every caller checked: `AudioEngine.cpp:872` (non-null input buffers allocated at `:512`), `SampleRecordAccumulator.h:292` (non-null scratch sized at `:183`); `TrackRecorder.cpp:379` uses the unrelated `RecordRingBuffer`. Siblings checked: `writeBlock()` null-source guard (`:83`), `InputChannelRing::read()` (`:143`), `RetroAudioRing::copyOut()` (`:159`). `available()`, `writeBlock()`, `discardStaged()` and `reset()` never read `dst`.
4. **No collateral damage.** A non-null destination takes exactly the old path; `frames == 0` still returns 0; a null destination returns 0 without consuming, so queued frames stay available. No limit was added, no saved data, project format, command or undo path is touched, and this integration changes no product line at all.
5. **Verdict is ALREADY-FIXED, and here is why that is not an unproven claim.** The pinned target's guard (file:line above, `include/SampleFrameRingBuffer.h:105`) comes from accepted commit `021bf8e310` (`new-p10-345`), which is an ancestor of the target (`git merge-base --is-ancestor 021bf8e310 HEAD` → 0). It is a superset of the accepted hunk, with identical observable behaviour for null and zero-length reads. The defect itself is real, not a false positive: removing the guard segfaults the retained regression (EXIT=139).
6. **Realtime, undo, commands.** The only product code involved is one compare already in the target; nothing new allocates, frees, locks, waits, does I/O or logs on the audio thread, there is no new control command, and no undo step is introduced or changed.
7. **Build coverage.** `zene` and all five recording test targets were rebuilt after the header resolution; the whole `RecordingRealtimeTest` binary (56 slots) passes, as does the 5-test recording family.
