FIXED

# Cluster new-p16-30 — `record.recovery_place` placed audio beyond the guaranteed recovery count

Round 1 closed this as FALSE-POSITIVE on the argument that `commitTake()` writes the WAV header and the journal
from the same count. The reviewer is right that this overlooks the *window between them*: a crash after
`SFC_UPDATE_HEADER_NOW` and before `writeJournal()` leaves a WAV whose data chunk declares frames the journal never
recorded. `record.recovery_place` then placed the whole decodable file, so the clip played past
`frames_recoverable` — the number the command reports as the guaranteed count. The fix bounds the placed clip at
the cause, and the bound is applied again on redo.

## The defect, with line evidence

- `src/core/audio/TrackRecorder.cpp:262-272` — `commitTake()`: `sf_command(m_sf, SFC_UPDATE_HEADER_NOW, …)` (269),
  `sf_write_sync(m_sf)` (270), then `m_journalFrames = m_framesRecorded` and `writeJournal()` (271-272). A kill
  between 269 and 272 leaves the decodable WAV longer than the journal's last count.
- `src/core/RecordingJournal.cpp:310-319` and `include/RecordingJournal.h:122-125` — `frames_recoverable` is
  `min(journal.framesOnDisk, framesInFile())`, so in that crash it is the *journal* count, smaller than what the
  file decodes.
- `src/core/SampleDecoder.cpp:144-151` — the decoder reads exactly the declared frames, so the surplus is
  playable audio (`sfInfo.frames`), not padding.
- `src/core/ControlCommandsRecordingPlace.cpp:62-69` (round 1) — `landRecoveredTake()` called
  `clip->setSampleFile(audio)` and stopped there; `setSampleFile` → `resetWindowToFullBuffer()`
  (`src/core/SampleClip.cpp:493-506`) makes the clip's window the whole buffer, so the clip played every decodable
  frame.

## The fix (product code)

`src/core/ControlCommandsRecordingPlace.cpp`:

- `:104-112` — the guaranteed count is read once from `takeJson(journal)` (`frames_recoverable`) and used for both
  the "no recoverable audio" refusal and the clip's bound, so the two can never disagree.
- `:66` — `landRecoveredTake(Song*, const QString& audio, std::uint64_t framesGuaranteed)` takes the count.
- `:81-89` — after the file loads, the clip's play range is bounded: a `SampleWindow` of
  `min(framesGuaranteed, whole.length())` frames through `clip->setSampleWindow()`, which is the single authoring
  point for a window (`src/core/SampleClip.cpp:873-903`): it clamps into the buffer, refuses an empty window,
  moves the clip's timeline length with `changeLength(sampleLength())`, and clears auto-resize for a trimmed
  window. `limit == whole.length()` (a take whose journal already matches the file) changes nothing.
- `:131` and `:147` — both entry points pass the same count: the first placement and the redo lambda, which
  captures `guaranteed`. Redo therefore re-places the take with the same bound.
- `:31-34` — `<algorithm>`, `<cstdint>` for `std::min`/`std::uint64_t`.

Why a window and not a truncated copy of the file: the command promises `audio_untouched: true`
(`src/core/ControlCommandsRecordingPlace.cpp:165`) and "The take's WAV is never moved, rewritten or deleted"
(`:193-194`); a window is the clip's own authored state, it is saved with the project
(`src/core/SampleClip.cpp:1053-1054`) and restored on load (`:1129-1137`), and a clip that plays its whole source
still writes neither attribute, so no other project serialises differently.

Callers and siblings checked: `landRecoveredTake()` has exactly two call sites, both in this file (placement and
redo). `record.recovery_restore` resolves the offer and never places audio
(`src/core/ControlCommandsRecordingRecovery.cpp:124-131`). The GUI (`src/gui/RecordingRecoveryMenu.cpp:68`, the
startup offer at `:163-167`) only invokes the command, so the menu, the startup offer and the agent surface all
get the bound from the one implementation.

## Regression test

`tests/src/gui/RecordingRecoveryMenuTest.cpp:515-558`,
`aTakeWithMoreAudioThanTheJournalGuaranteesIsPlacedBounded`: a WAV of `2 * kFrames` (44100) frames journalled as
`kFrames` (22050) — the state the crash window leaves — placed through the real command. It asserts

- the reply reports `frames_in_file = 44100` and `frames_recoverable = 22050`;
- the clip's buffer really holds 44100 frames (`clip->sample().sampleSize()`), so the two numbers below are not
  equal by accident;
- `clip->sampleWindow()` is `[0, 22050)`, and `clip->sampleLength().getTicks() ==
  clip->windowTicksFor(SampleWindow::full(22050))` — the timeline length follows the guarantee, not the file;
- after `control.undo` + `control.redo` the re-placed clip carries the same window and the same timeline length.

Deterministic: the WAV and the journal count are injected; nothing reads the clock, sleeps, or starts a thread.
`control.undo`/`control.redo` are synchronous command calls.

## Test matrix

| Path | Result |
| --- | --- |
| Normal path: journal count == file frames (no surplus) | `anInterruptedTakeIsPlacedOrDismissed`, `aPlaceUndoRedoIsOneStep` — pass; `limit == whole.length()`, no window written |
| Failure path: no audio, undecodable file, redo that cannot place | `aTakeWithNoAudioCannotBePlaced`, `aTakeThatDoesNotDecodeIsNotPlaced`, `aRedoThatCannotPlaceKeepsTheOffer` — pass |
| Exhaustion / reconfiguration: journal larger than the file (truncated take) | `aTruncatedTakeIsOfferedWhatIsActuallyOnDisk` — pass; the bound is a no-op there, the clip is the file |
| The defect: file decodes more than the journal guarantees | the new test — fails without the fix, passes with it |
| Redo | the new test re-checks the bound after undo + redo |
| Audio thread | placement runs on the control thread; no new per-period code. `setSampleWindow` emits the same `sampleChanged()` a `setSampleFile` on this path already emitted |
| Undo | still one step: `addUndoStep` unchanged, `aPlaceUndoRedoIsOneStep` passes |
| Platforms | no Qt-6-only API, no `std::filesystem`, no platform ifdefs added; `SampleWindow`/`f_cnt_t`/`std::uint64_t` are already used across the tree |

## Commands and EXIT codes (logs in `~/.config/kads/zene/state/runs/new-p16-30.r2/logs`)

| Step | Command | EXIT |
| --- | --- | --- |
| RED build (test only, no product change) | `cmake --build build --target RecordingRecoveryMenuTest -j4 > build-red-1.log 2>&1` | 0 |
| RED test | `cd build/tests && ctest -R '^RecordingRecoveryMenuTest$' --output-on-failure > test-red-1.log 2>&1` | 8 (`FAIL!` at test line 540, `Totals: 14 passed, 1 failed`) |
| GREEN build (fix + `zene`) | `cmake --build build --target RecordingRecoveryMenuTest zene -j4 > build-green-1.log 2>&1` | 0 |
| GREEN test | `ctest -R '^RecordingRecoveryMenuTest$' … > test-green-1.log 2>&1` | 0 (15/15) |
| Family build | `cmake --build build -j4 --target ControlRecordingRoutesTest DataFileContainerRecoveryTest MultiTrackRecorderTest RecordingInputPathTest ProjectRecoveryTest RecordingCrashLossTest RecordingRealtimeTest RecordingPassTest RecordingLatencyTest TwoTrackRecordingHarness StepRecorderWidgetTest SessionAudioRecorderTest AudioAlsaResumeRecoveryTest > build-family-1.log 2>&1` | 0 |
| Family test | `ctest -R 'Recovery\|Recorder\|Recording' --output-on-failure > test-family-2.log 2>&1` | 0 (15/15 passed, incl. `ControlRecordingRecovery`) |
| 4b: write the product diff | `git diff -- src/core/ControlCommandsRecordingPlace.cpp > fix.patch` | 0 |
| 4b: reverse it | `git apply -R fix.patch` | 0 |
| 4b: rebuild without the fix | `cmake --build build --target RecordingRecoveryMenuTest -j4 > build-proof-red-1.log 2>&1` | 0 |
| 4b: run without the fix | `ctest -R '^RecordingRecoveryMenuTest$' … > test-proof-red-1.log 2>&1` | 8 (`FAIL!` at line 540, `14 passed, 1 failed`) |
| 4b: re-apply | `git apply fix.patch` | 0 |
| 4b: rebuild with the fix | `cmake --build build --target RecordingRecoveryMenuTest zene -j4 > build-proof-green-1.log 2>&1` | 0 |
| 4b: run with the fix | `ctest -R '^RecordingRecoveryMenuTest$' … > test-proof-green-1.log 2>&1` | 0 (15/15) |
| Gates, as instructed | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > gates-1.log 2>&1` | 1 (no table: its base listing for `3436a808b` is not cached, computing it needs `git checkout --detach` in the read-only `gate-baselines/tree`, refused by the lane guard, `CalledProcessError … exit status 97`) |
| Gates, same functions, logs in the log folder | `git archive <base> \| tar -x -C $TMPDIR/np1630-base` then `python3 logs/gates_driver.py <worktree> logs $TMPDIR/np1630-base > gates-2.log 2>&1` | 0 (8/8 rows PASS) |

Gate table (`gates-2.log`; base `3436a808b` measured on a `git archive` extraction, 3 changed files):

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

Registration: `src/core/ControlCommandsRecordingPlace.cpp` is already in `tests/fork-sources.txt` (line 1882) and
`tests/all-sources.txt` (line 1843); no product file is new and no inherited file changed, so neither list needed
an edit (`fork-sources` and `no-upstream-regression` exit 0).

## Self-check

- **The regression test FAILS with the product change reverted and PASSES with it.** Yes, run in that order:
  `git apply -R fix.patch` (EXIT 0) → build (EXIT 0) → test EXIT **8**, `FAIL!
  aTakeWithMoreAudioThanTheJournalGuaranteesIsPlacedBounded … Compared values are not the same`, `Loc:
  [../../tests/src/gui/RecordingRecoveryMenuTest.cpp(540)]`, `14 passed, 1 failed` (`test-proof-red-1.log`);
  `git apply fix.patch` (EXIT 0) → build (EXIT 0) → test EXIT **0**, 15/15 (`test-proof-green-1.log`).
- **Deterministic.** No clock, sleep, thread or timer: the take is a WAV the test writes (`writeWav`), the journal
  count is the injected `frames_on_disk` of `record.journal_update`, and the assertions read the clip the command
  placed. Undo and redo are synchronous command invocations.
- **At the cause and complete.** The bound lives in `landRecoveredTake()`, the one function both the placement and
  the redo reach (`:131`, `:147`); the count is read once (`:104-112`). The other verbs of the group are not
  affected: `record.recovery_restore` never places audio, `record.recovery_discard` removes only the journal, and
  `record.recovery_get_state` still reports `frames_recoverable` as before. No control command, params schema or
  result schema changed, so the `ControlCommandsSnapshot` canary is untouched.
- **No collateral damage.** A take whose journal already matches its file (`limit == whole.length()`) writes no
  window and serialises exactly as before; a project whose clips have no trim writes neither `srcin` nor `srcout`
  (`src/core/SampleClip.cpp:1051-1055`), so old projects load unchanged, and a trimmed recovered clip round-trips
  through save/load (`:1053-1054`, `:1129-1137`). The limit only applies to the clip this command creates — no
  other caller or existing clip can hit it. The whole recovery/recorder family passed (15/15).
- **One undo step, nothing new on the audio thread.** `addUndoStep` is unchanged (the undo still removes the
  placed track and rewrites the journal as `in_progress`), `aPlaceUndoRedoIsOneStep` passes; the placement code
  runs on the control thread and adds no allocation, lock, wait or log to any per-period path.
- **Verdict cites evidence.** REAL: `src/core/audio/TrackRecorder.cpp:262-272` (the crash window),
  `src/core/RecordingJournal.cpp:310-319` (the count is the smaller number), and round-1 placement at
  `src/core/ControlCommandsRecordingPlace.cpp:62-69`, now bounded at `:81-89`.
- **Gates.** The official tool cannot print its table in a lane (`gates-1.log`, guard refusal + read-only cache).
  The driver reuses `feature_check`'s unchanged `SIMPLE`/`RATCHET`/`REG`/`key`/`item_file` with the base listing
  measured on a `git archive` extraction of `3436a808b`, and all eight rows pass. The complexity row is vacuous on
  this box because `tests/complexity-gate.sh` reports `lizard is not installed` (exit 2, 0 items on both sides);
  the changed file is 229 lines with two small added blocks, and no new function was introduced.

## Other findings

- `tests/complexity-gate.sh` cannot measure anything here: `complexity-gate: lizard is not installed — run: pip
  install lizard` (`gate-complexity.log`, `gate-base-complexity.log`, exit 2).
- `feature_check.py` still cannot run under the lane guard/read-only `gate-baselines/` (same shape as
  `cluster-ci-28572f865f7a738c`); `submit_check.py`'s gates row therefore reads FAIL even though every gate passes
  through the driver.
- Unrelated, unchanged by this fix: a WAV whose header declares more frames than the disk holds makes
  `SampleDecoder` refuse the file (`src/core/SampleDecoder.cpp:144-151`), and placement then reports "could not
  create a sample track". Not a regression from this change; worth triage on its own.
