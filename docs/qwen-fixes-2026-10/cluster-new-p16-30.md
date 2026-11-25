FALSE-POSITIVE

# Cluster new-p16-30 — `record.recovery_place` places audio beyond the guaranteed recovery count

Finding (patrol, historical line): `src/core/ControlCommandsRecordingPlace.cpp:62` — "The command places the
original WAV in a clip without limiting playback to `frames_recoverable` … a crashed take has more frames in its
WAV than the journal guarantees; `record.recovery_place` creates a clip that plays the entire file." Proposed fix:
"Place only the guaranteed frame range, using a bounded clip region or a separate truncated recovery file."

Line 62 is still where the clip is made: `landRecoveredTake()` → `track->createClip(TimePos(0))` →
`clip->setSampleFile(audio)` (`src/core/ControlCommandsRecordingPlace.cpp:62-69`). Nothing in the branch limits
the clip to `frames_recoverable`, so the premise of the finding ("the code does not clamp") is true. The defect
claim built on it is not: `frames_recoverable` is a documented **floor** (a guarantee), and the clip cannot play
audio that is not on disk, nor — for a take this program wrote — audio past the count.

## Why this is not a bug

**1. The count is the guaranteed minimum, not a ceiling, and that is written down.**
- `include/RecordingJournal.h:122-125`: `recoverableFrames()` — "THE REPORTED BOUND: `min(journal.framesOnDisk,
  framesInFile())`. Never more than the file holds, so the number cannot promise material that is not on disk."
  The stated purpose is to avoid *over*-promising, i.e. a lower bound.
- `include/RecordingJournal.h:126-128`: `updateIntervalFrames()` — "The most the journal's count can
  **understate** what the writer has already written." The journal count lags the file by construction.
- `src/core/ControlRecordingSupport.cpp:77-83`: `frames_recoverable` and `frames_beyond_the_journal` are reported
  side by side; the surplus is a reported fact, not an error, and
  `tests/control-recording-recovery.py:226` ("the material beyond the journal is inside the stated lag bound")
  locks the surplus as legitimate recorded audio within the documented bound.
- Placement's own contract is "the recovered file", untouched: `ControlCommandsRecordingPlace.cpp:173-178`
  ("holding the recovered file in a clip at bar 1 … The take's WAV is never moved, rewritten or deleted") and the
  result field `audio_untouched: true` (`ControlCommandsRecordingPlace.cpp:148`). The proposed remedy — trim the
  clip, or write "a separate truncated recovery file" — contradicts both, and the result schema
  (`:180-190`) has no field in which a truncation could be reported.

**2. For a take this program crashed on, the count, the header and the clip are the same number.**
`TrackRecorder::commitTake()` refreshes the WAV header and writes the journal from the same value in one step
(`src/core/audio/TrackRecorder.cpp:262-272`: `SFC_UPDATE_HEADER_NOW`, `sf_write_sync`, then
`m_journalFrames = m_framesRecorded` and `writeJournal()`). So the data chunk always declares exactly the
journalled count; frames written after the commit grow the file beyond the declaration. Both the journal's own
measurement (`src/core/RecordingJournal.cpp:310-319`: `min(declaredSize, present)`) and the decoder
(`src/core/SampleDecoder.cpp:144-151`: allocate `sfInfo.frames`, refuse a short read) stop at the declaration.
Measured this session with libsndfile 1.2.2 (`logs/probe-1.log`, BUILD_EXIT=0, RUN_EXIT=0):

```
take_declared_1000_present_3000.wav: frames libsndfile reports = 1000 (rate 48000, 1 ch)
take_declared_3000_present_1000.wav: frames libsndfile reports = 1000 (rate 48000, 1 ch)
```

A file physically holding 3000 frames but declaring 1000 decodes 1000 — the surplus bytes are not played, so the
clip does not "play the entire file" in the scenario the finding describes. (Probe source kept at
`logs/probe-declared-vs-present.cpp`; the same bound is what `framesInDataChunk` computes for the offer.)

**3. Where the file really does hold more than the journal (a count set by `record.journal_update`, or a journal
written smaller than the take), the extra frames are real PCM that is on disk and decodable.** In that case
`frames_recoverable` understates the take. Playing it is the point of recovery; trimming would silently cut up to
`lossBoundFrames` of audio the crash did not lose (`include/RecordingJournal.h:132-134`,
"THE BOUND: the most audio a crash **loses**"), and would make the placed clip shorter than the file the user was
told is untouched.

No product file is changed by this report. `record.recovery_restore` (resolve only) and the GUI entry
(`src/gui/RecordingRecoveryMenu.cpp:68`, the startup offer, `File > Recover Recordings`) all call the same
`recoveryPlace` handler, so there is no sibling path with a different rule.

## What I ran (all logs in `~/.config/kads/zene/state/runs/new-p16-30.r1/logs`)

| Step | Command | Exit |
| --- | --- | --- |
| Build the recovery-menu test (unmodified branch) | `cmake --build build --target RecordingRecoveryMenuTest -j4 > logs/build-1.log 2>&1` | 0 |
| Run it from `build/tests` | `cd build/tests && ctest -R '^RecordingRecoveryMenuTest$' --output-on-failure > logs/test-1.log 2>&1` | 0 (1/1 passed, 0.72 s) |
| libsndfile bound probe, build | `c++ -std=c++20 -O0 logs/probe-declared-vs-present.cpp -o $WORK/probe $(pkg-config --cflags --libs sndfile) >> logs/probe-1.log 2>&1` | 0 |
| libsndfile bound probe, run | `$WORK/probe $WORK >> logs/probe-1.log 2>&1` | 0 (output above) |
| Gates, as instructed | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > logs/gates-1.log 2>&1` | 1 (did not reach the gates: its base listing is not cached and it cannot create one — `bunny-guard: 'git -C …/gate-baselines/tree checkout -q --detach 9913d36d5…' is not allowed in a Bunny lane`, then `OSError: [Errno 30] Read-only file system` on that cache) |
| Gates, same functions, logs in the log folder | `python3 logs/gates_driver.py <worktree> logs > logs/gates-2.log 2>&1` | 0 (8/8 rows PASS) |

`gates_driver.py` imports `feature_check` and reuses its `SIMPLE`, `RATCHET`, `REG`, `run`, `key` and
`item_file` unchanged, with the base listing measured in the base tree — which here *is* this worktree
(`merge-base HEAD 050/m0-train-52` = `HEAD` = `9913d36d5`, tree clean, 0 changed files). No gate limit, baseline
entry or script is edited. Table (`logs/gates-2.log`):

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

Caveat on the complexity row: the gate binary is not installed on this box —
`complexity-gate: lizard is not installed — run: pip install lizard` (`logs/gate-complexity.log`, exit 2), so it
measured nothing. It cannot hide a regression from this job because no `src/`, `include/` or `plugins/` file is
changed by it.

Because no product file changed, `tests/fork-sources.txt` and `tests/upstream-modifications.txt` need no entry;
the registration and no-upstream-regression gates agree.

## Test matrix considered (no test added — nothing to regress against)

| Path | State |
| --- | --- |
| Normal path: place a take, one undo step, redo | covered and green by `RecordingRecoveryMenuTest` (`logs/test-1.log`, exit 0) |
| Failure: a take with no audio, or an unreadable file, is refused and the offer kept | `RecordingRecoveryMenuTest::aTakeWithNoAudioCannotBePlaced` |
| Truncated header (declares more than the disk holds) | `RecordingRecoveryMenuTest::aTruncatedTakeIsOfferedWhatIsActuallyOnDisk`; `framesInDataChunk` |
| Exhaustion / reconfiguration | not on this path (placement is one clip, one track, no buffer sizing) |
| Audio thread | placement runs on the control thread (`recoveryPlace`); no code added, so nothing new allocates, locks, waits or logs |
| Supported platforms | no code added; the probe is a local diagnostic, not committed |

## Self-check

- **The regression test FAILS with your product change reverted and PASSES with it.** No product change exists:
  the verdict is FALSE-POSITIVE, so a red/green pair would be a test of nothing. The proof is the contract and
  the mechanism above, plus the libsndfile measurement (`probe-1.log`, BUILD_EXIT=0, RUN_EXIT=0) and the existing
  family green on the unmodified branch (`test-1.log`, exit 0). `submit_check.py` records this as claim-only
  closure, which is the branch it takes for ALREADY-FIXED / FALSE-POSITIVE.
- **The test is deterministic.** The evidence is a static probe with no clock, thread or sleep: it writes two WAV
  files whose data-chunk size is patched, then reads `SF_INFO.frames`. `RecordingRecoveryMenuTest` drives the
  registry with injected journals and WAVs (`writeWav`/`writeTruncatedWav`), no wall-clock timing.
- **The fix is at the cause and complete: every caller and sibling checked.** There is no fix. All three entry
  points (agent command, `File > Recover Recordings`, the startup offer) reach the one handler `recoveryPlace`
  (`src/gui/RecordingRecoveryMenu.cpp:65-72`, `:163-167`); the sibling verb `record.recovery_place`'s only
  same-family neighbour, `record.recovery_restore`, resolves the offer and never places audio
  (`ControlCommandsRecordingRecovery.cpp:124-131`). Nothing is left inconsistent because nothing changed.
- **No collateral damage.** Zero files changed outside this report, so old projects load and render exactly as
  before and no limit or check was added that could reject another caller. Had I added the proposed clamp, it
  would have shortened every recovered clip by up to `lossBoundFrames` (8896 frames, 0.185 s at 48 kHz) and
  broken the documented `audio_untouched` contract — which is why I did not.
- **Verdict cites the code or the intended contract.** Yes: `include/RecordingJournal.h:122-128`,
  `src/core/ControlRecordingSupport.cpp:77-83`, `src/core/audio/TrackRecorder.cpp:262-272`,
  `src/core/RecordingJournal.cpp:310-319`, `src/core/SampleDecoder.cpp:144-151`,
  `src/core/ControlCommandsRecordingPlace.cpp:148` and `:173-178`, plus the measurement in `logs/probe-1.log`.

## Other findings

- `feature_check.py` cannot run in a lane when the base commit's listing is not already cached: the lane guard
  refuses its own `git checkout --detach` in `gate-baselines/tree` (exit 97) and that directory is read-only for
  the session (`OSError: [Errno 30] Read-only file system`). Same shape as `cluster-ci-28572f865f7a738c`. Handled
  here with the driver above; the tool itself still exits 1 (`logs/gates-1.log`).
- `tests/complexity-gate.sh` reports `lizard is not installed` (exit 2), so the complexity ratchet measures
  nothing on this box.
- `src/core/SampleDecoder.cpp:144-151` refuses a sample whose header declares more frames than the disk holds
  (short read → empty buffer → `landRecoveredTake` removes the track and `record.recovery_place` reports "could
  not create a sample track"). A crashed take cannot reach that state (`commitTake` syncs the header), but an
  OS-level crash that loses only the tail of the data chunk would make the offer unplaceable. Out of scope here;
  noted for triage.
