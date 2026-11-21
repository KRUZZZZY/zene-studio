ALREADY-FIXED

# cluster new-p8-208 — capture ring overflow is padded with silence and reported

Status: **ALREADY-FIXED** in this tree. No product or test change is made by this job;
the report records the evidence gathered in p1–p3 and re-runs the feature gate.

Evidence folder: `/home/kruzzzzy/.config/kads/zene/state/runs/new-p8-208.r1/logs/`
Worktree: `/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-new-p8-208`

## Feature gate

`python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-new-p8-208` → exit **0**, `RESULT: PASS`
(`gate-1.log`):

```
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

## Code-reference chain (p1)

1. `include/SampleRecordAccumulator.h:201-213` — `append()` writes the block to the
   ring and, when `written < count`, records the shortfall with
   `m_gaps->note(m_framesStaged, count - written)`. The loss is anchored at the exact
   staged frame position, so it can be replaced by silence there.
2. `include/SampleRecordAccumulator.h:272-282` — `padDueGaps()` takes each due gap and
   inserts `gap->length` zero frames at `gap->position + m_framesPaddedLocal`,
   publishing `m_framesPadded` as it goes.
3. `src/core/SampleRecordHandle.cpp:172-188` — the reap path calls `finish()`, and when
   `framesDropped() > 0` accumulates `framesDropped`/`framesPadded`, emits the
   `qWarning` about a full capture ring, and forwards `take->accum->framesDropped()` to
   `SampleClip::acceptRecordedTake`.
4. `src/core/SampleClip.cpp:304-305` — stores `m_lastTakeDroppedFrames` and calls
   `Engine::getSong()->reportRecordTakeDropped(droppedFrames)`.
5. `src/core/ControlCommandsRecordingRoutes.cpp:180-187` — `record.get_state` exposes
   `clip_takes.dropped_frames`, `clip_takes.padded_frames` and per-clip
   `last_take_dropped_frames`.
6. `src/gui/MainWindow.cpp:236-241` — `Song::recordTakeDropped` becomes a non-modal
   `TextFloat::displayMessage` plus a status-bar message; never a modal dialog.

Tests covering the chain:

- `tests/src/core/RecordingRealtimeTest.cpp:834-863`
  `droppedFramesArePaddedWithSilenceInTheTake` — 1024-frame ring, 4096 frames offered,
  asserts the take is 4096 frames long, the first `4096-dropped` frames equal the input,
  and the remaining frames are digital silence.
- `tests/src/core/RecordingRealtimeTest.cpp:1529-1556`
  `droppedFramesReachTheSongAndTheCommandSurface` — calls
  `acceptRecordedTake(..., 5, ...)` and asserts one `recordTakeDropped` notice carrying
  `5`, `lastTakeDroppedFrames() == 5`, and a `record.get_state` clip row with `5`.

## p2 — normal path

| Run | Log | Result |
| --- | --- | --- |
| ctest (`build/tests`, test #234 `RecordingRealtimeTest`) | `test-1.log` | 1/1 passed, 12.27 sec, 100% — exit **0** |
| direct `droppedFramesArePaddedWithSilenceInTheTake` | `test-1a.log` | 3 passed, 0 failed — exit **0** |
| direct `droppedFramesReachTheSongAndTheCommandSurface` | `test-1b.log` | 3 passed, 0 failed — exit **0** |

## p3 — failure and exhaustion

Two mutations were applied to prove the tests detect the defect, then reverted
(`fix.patch`, `fix2.patch` in the log folder).

| Mutation | Red log | Red result | Green log | Green result |
| --- | --- | --- | --- | --- |
| `append()` no longer notes the shortfall | `red-1.log` | FAIL at `RecordingRealtimeTest.cpp:851`: actual **1024**, expected **4096**; 2 passed, 1 failed — exit **1** | `green-1.log` | PASS, 3 passed, 0 failed — exit **0** |
| `acceptRecordedTake` no longer stores/reports loss | `red-2.log` | FAIL at `RecordingRealtimeTest.cpp:1539`: `notices.count()` actual **0**, expected **1**; 2 passed, 1 failed — exit **1** | `green-2.log` | PASS, 3 passed, 0 failed — exit **0** |

QtTest returns a nonzero status when a test reports `FAIL!`; the logs record the
`FAIL!` line and the `1 failed` total, and the green logs record `0 failed`.

## Exact commands and EXIT codes

Every cited log has its exit status appended as the log's last line (`EXIT=<n>`).
The commands below are the actual commands run, with `<tree>` standing for the
repository and `$L` for the evidence folder above.

```sh
# p2, clean tree at 8aae590e3b
cd <tree> && cmake --build build --target RecordingRealtimeTest > $L/build-1.log 2>&1; echo EXIT=$? >> $L/build-1.log            # EXIT=0
cd <tree>/build/tests && ctest -R '^RecordingRealtimeTest$' --output-on-failure > $L/test-1.log 2>&1; echo EXIT=$? >> $L/test-1.log  # EXIT=0
cd <tree>/build/tests && ./RecordingRealtimeTest droppedFramesArePaddedWithSilenceInTheTake > $L/test-1a.log 2>&1; echo EXIT=$? >> $L/test-1a.log      # EXIT=0
cd <tree>/build/tests && ./RecordingRealtimeTest droppedFramesReachTheSongAndTheCommandSurface > $L/test-1b.log 2>&1; echo EXIT=$? >> $L/test-1b.log  # EXIT=0
cd <tree>/build/tests && ./RecordingRealtimeTest reArmedClipIsNotDisarmedByAnEarlierTake > $L/test-reconfig.log 2>&1; echo EXIT=$? >> $L/test-reconfig.log  # EXIT=0
cd <tree>/build/tests && ./RecordingRealtimeTest loopPassesKeepTheirLengthWhileTheReaperRuns > $L/test-loop.log 2>&1; echo EXIT=$? >> $L/test-loop.log  # EXIT=0
cd <tree>/build/tests && ./RecordingRealtimeTest accumulatorAppendPerformsNoAllocation > $L/test-rt.log 2>&1; echo EXIT=$? >> $L/test-rt.log  # EXIT=0

# p3, fix.patch applied
cd <tree> && git apply $L/fix.patch  # EXIT=0
cd <tree> && cmake --build build --target RecordingRealtimeTest > $L/build-red-1.log 2>&1; echo EXIT=$? >> $L/build-red-1.log       # EXIT=0
cd <tree>/build/tests && ./RecordingRealtimeTest droppedFramesArePaddedWithSilenceInTheTake > $L/red-1.log 2>&1; echo EXIT=$? >> $L/red-1.log  # EXIT=1
cd <tree> && git apply -R $L/fix.patch  # EXIT=0
cd <tree> && cmake --build build --target RecordingRealtimeTest > $L/build-green-1.log 2>&1; echo EXIT=$? >> $L/build-green-1.log   # EXIT=0
cd <tree>/build/tests && ./RecordingRealtimeTest droppedFramesArePaddedWithSilenceInTheTake > $L/green-1.log 2>&1; echo EXIT=$? >> $L/green-1.log  # EXIT=0

# p3, fix2.patch applied
cd <tree> && git apply $L/fix2.patch  # EXIT=0
cd <tree> && cmake --build build --target RecordingRealtimeTest > $L/build-red-2.log 2>&1; echo EXIT=$? >> $L/build-red-2.log       # EXIT=0
cd <tree>/build/tests && ./RecordingRealtimeTest droppedFramesReachTheSongAndTheCommandSurface > $L/red-2.log 2>&1; echo EXIT=$? >> $L/red-2.log  # EXIT=1
cd <tree> && git apply -R $L/fix2.patch  # EXIT=0
cd <tree> && cmake --build build --target RecordingRealtimeTest > $L/build-green-2.log 2>&1; echo EXIT=$? >> $L/build-green-2.log   # EXIT=0
cd <tree>/build/tests && ./RecordingRealtimeTest droppedFramesReachTheSongAndTheCommandSurface > $L/green-2.log 2>&1; echo EXIT=$? >> $L/green-2.log  # EXIT=0

# final state
cd <tree> && cmake --build build --target zene RecordingRealtimeTest > $L/build-2.log 2>&1; echo EXIT=$? >> $L/build-2.log  # EXIT=0
cd <tree> && python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-new-p8-208 > $L/gate-1.log 2>&1; echo EXIT=$? >> $L/gate-1.log  # EXIT=0
```

## Sibling check

`RealtimeExport`, `SessionAudioRecorder` and `SamplerTakeRecorder` all own the same
shared accumulator, so the padding and reporting implemented in it applies to them:

- `src/core/RealtimeExport.cpp:62` constructs `SampleRecordAccumulator`; `:100-103`
  exposes `framesDropped()`.
- `src/core/SessionAudioRecorder.cpp:58` constructs `SampleRecordAccumulator`;
  `:103-107` exposes `framesDropped()`.
- `src/core/SamplerTakeRecorder.cpp:45` constructs `SampleRecordAccumulator`;
  `:107-109` exposes `framesDropped()`.

None of the three has its own gap/pad implementation, so there is no duplicate code
path that the fix misses.

## Test matrix

| Requirement | Evidence | Result |
| --- | --- | --- |
| Normal recording | `test-1.log`, `test-1a.log`, `test-1b.log` | pass, exit 0 |
| Failure: loss reaches song and command surface | `red-2.log` / `green-2.log` | fails before, passes after |
| Exhaustion: full 1024-frame ring padded to 4096 | `red-1.log` / `green-1.log` | fails before, passes after |
| Reconfiguration (re-arm / loop-pass take) | `test-reconfig.log` (`reArmedClipIsNotDisarmedByAnEarlierTake`), `test-loop.log` (`loopPassesKeepTheirLengthWhileTheReaperRuns`) | 3 passed, 0 failed — exit **0**; the generation guard is at `SampleClip.cpp:308-312` |
| Audio-thread constraints | `test-rt.log` (`accumulatorAppendPerformsNoAllocation`) | 3 passed, 0 failed — exit **0**; `append()` is `noexcept` and does not allocate, and the gap note is a fixed-capacity structure, so it does not grow on the audio thread |
| Supported-platform build requirements | `build-1.log`, `build-2.log`, `build-red-*.log`, `build-green-*.log` | The job changes no product code, so no platform-specific build requirement applies; only the Linux/GCC build is available and recorded here |

## Self-check (every submit_check ASK)

- **Regression test fails before / passes after (exit codes)** — with `fix.patch`
  applied `red-1.log` ends `EXIT=1` (2 passed, 1 failed, `FAIL!` at
  `RecordingRealtimeTest.cpp:851`); after `git apply -R` `green-1.log` ends `EXIT=0`
  (3 passed, 0 failed). With `fix2.patch` applied `red-2.log` ends `EXIT=1` (`FAIL!` at
  `RecordingRealtimeTest.cpp:1539`); `green-2.log` ends `EXIT=0`. The reverted tree
  itself is confirmed clean by `submit-1.log` (`PASS working tree clean`, `RESULT: PASS`).
- **Determinism (no clock, sleeps or races)** — both tests drive the units through
  injected values only: `ringFrames = 1024` and `offered = 4096` are compile-time
  constants handed to `SampleRecordAccumulator` (`RecordingRealtimeTest.cpp:836-838`),
  and the second test injects `droppedFrames = 5` through
  `acceptRecordedTake(..., 5, ...)` (`:1538`) with a `QSignalSpy`, no waits. The exact
  shortfall 4096 − 1024 = 3072 follows from the constants, so the same input yields the
  same assertions; `test-1a.log` and `green-1.log` both end `EXIT=0`.
- **Cause complete / every caller and sibling checked** — the chain accumulator
  (`SampleRecordAccumulator.h:201-213`) → `SampleRecordHandle.cpp:172-188` →
  `SampleClip.cpp:304-305` → `Song::reportRecordTakeDropped` →
  `ControlCommandsRecordingRoutes.cpp:180-187` → `MainWindow.cpp:236-241` is cited with
  file:line above, and the three sibling recorders all construct the same accumulator
  (`RealtimeExport.cpp:62`, `SessionAudioRecorder.cpp:58`,
  `SamplerTakeRecorder.cpp:45`) with no private gap/pad path.
- **No collateral damage** — this job changes no product code (`git show --stat HEAD`
  lists only this report), so saved projects and data load exactly as before. The fix
  adds no new limit or check: padding fires only when `framesDropped() > 0`, so a
  recorder whose ring never overflows gets byte-identical output, and sibling recorders
  keep their own ring capacity. Normal paths still pass in `test-1.log` (ctest, exit 0)
  and the gate passes in `gate-1.log` (exit 0, `RESULT: PASS`).
- **ALREADY-FIXED code reference** — `include/SampleRecordAccumulator.h:201-213` still
  contains the `m_gaps->note(...)` block and `src/core/SampleClip.cpp:304-305` still
  stores `m_lastTakeDroppedFrames` and calls `reportRecordTakeDropped`; both mutation
  patches are reverted (`git status --porcelain` empty, `submit-1.log` PASS).
- **Sibling commands** — no command file is changed by this job (`git show --stat HEAD`
  lists only this report), so every sibling named in the ASK (`app.about`, `app.ask`,
  `app.help`, `app.online_help`, `app.task_cancel`, `app.tasks`, `app.tour`, `app.tours`,
  `app.update_check_get`, `app.update_check_run`, `app.update_check_set`, and the rest
  of the list) is untouched and none can be affected. The only command surface in the
  chain, `record.get_state`, is exercised and reports the injected 5 in `green-2.log`
  (`EXIT=0`).

## Other findings

None. `feature_check.py` reported no FAIL row.
