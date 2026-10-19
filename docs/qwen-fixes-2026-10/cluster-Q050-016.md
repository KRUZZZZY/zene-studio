IMPLEMENTED

# cluster Q050-016 — MPE expression event streams (round 2: review rework)

Every acceptance criterion is met and proven by a test that was built and run on this worktree
(branch `feat/050-Q050-016-oc`, base `de5a85a96`). Round 1 landed the model, commands, playback and
serialization; round 2 answers the Luna REWORK review point by point. Logs:
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/Q050-016.r2/logs/`
(`build-N.log` builds, `test-N-*.log` focused ctests, `gate-N-*.log` gates).

## Acceptance

| # | Criterion | Command | Result (EXIT) |
|---|---|---|---|
| 1 | A headless command test writes points at known note offsets and reads back the same values. | `cmake --build build --target ControlNoteExpressionCommandsTest -- -j4` then `ctest -R ControlNoteExpressionCommandsTest --output-on-failure` (from `build/tests`) | build `EXIT=0` (build-5/6/7/9), test `EXIT=0` — `expressionStreamWritesAndReadsBackAtKnownOffsets`: the three arrays (ticks 0/96/192, 0/48, 96) read back field-for-field, malformed points are `invalid_args`, and `control.undo` restores the prior stream |
| 2 | A project save and reload preserves point order and values while a note with no expression keeps its prior serialized form. | `cmake --build build --target MpeNoteStorageTest -- -j4` then `ctest -R MpeNoteStorageTest --output-on-failure` | build `EXIT=0`, test `EXIT=0` — `expressionStreamsRoundTripAndStayOptional`, `expressionStreamsPersistInAProject`: order/values survive save→load→save, the plain note carries exactly `{key,len,pan,pos,type,vol,id}`, an old project re-saves byte-identically |
| 3 | A MIDI-output test verifies pitch, pressure, and timbre changes reach the instrument at their scheduled offsets. | `cmake --build build --target MpePlaybackTest mpe_test_consumer -- -j4` then `ctest -R "^MpePlaybackTest$" --output-on-failure` | build `EXIT=0`, test `EXIT=0` — `scheduledExpressionStreamReachesTheInstrumentAtItsOffsets` prints `MPE_EVIDENCE stream offsets channel=0 level=0.588401 pitch=1200@55 pressure=64@110 timbre=32@166 fpt=55.180` and asserts each value at frame `round(tick·fpt)` |
| R1 | Realtime: streamed-note handle creation is allocation-free on the audio thread. | same `MpeNoteStorageTest` / `MpePlaybackTest` targets above | `EXIT=0` — `streamedNoteCopiesAllocateNothing` (copy ctor + assign of a 3-axis note: **0** allocations under `AllocationProbe`) and `streamedHandleAcquisitionAllocatesNothingExtra` (`NotePlayHandleManager::acquire`, the audio thread's own path: `MPE_EVIDENCE handle allocations plain=2 streamed=2`, the 2 being the handle's own detuning object either way). Sensitivity proven: with an allocating copy restored the slots fail (`Actual (allocations): 8`, `streamed=6 vs plain=2`) |
| R2 | LLP64-safe frame math with coverage past 2³¹ frames. | same `MpeNoteStorageTest` target above | `EXIT=0` — `streamPointOffsetsStay64BitPast2ToThe31Frames` drives the real product function `NotePlayHandle::mpeStreamPointOffset` (now public static, `llround`/`int64_t`): 2400000000 for tick 100000 @ 24000 fpt, 107 for a period starting at frame 5000015900. Sensitivity proven: with 32-bit truncation restored the slot fails (`Actual (at): 0`). On this LP64 box the old `long` spelling also computes these (64-bit `long`); the test pins the contract for LLP64/Windows CI |
| R3 | GUI stream editing through the shared command and undo path. | `cmake --build build --target NoteTransformActionsTest -- -j4` then `ctest -R NoteTransformActionsTest --output-on-failure` | build `EXIT=0`, test `EXIT=0` — `mpePitchRampWritesStreamsThroughTheRegistry` (menu trigger writes `{0:0, len:1200}` pitch via `note.expression_stream_set`, pressure kept, unselected note untouched, one `control.undo` per write) and `clearMpeStreamsRemovesStreamsThroughTheRegistry` (clear empties streamed notes only; a clear with nothing to clear pushes no undo step) |
| R4 | Probe header registered; manifests regenerate. | `bash tests/fork-sources-gate.sh`, `bash tests/all-sources-reproduce.sh` | both `EXIT=0` with `tests/src/plugins/MpeConsumerProbe.h` (+ the equally fork-new `MpeTestConsumer.cpp`) added to `tests/fork-sources.txt` beside their recipe pathspecs |
| — | No regression to the scalar MPE path / the A16 contract / the command snapshot | `ctest -R "^(ControlNoteExpressionCommandsTest|MpeExpressionTest|MpeInputPathTest|ReversibilityContractTest)$" --output-on-failure` and `ctest -R "^ControlCommandsSnapshot$" --output-on-failure` | `EXIT=0` (4/4, then snapshot `Passed`). No new commands: snapshot stays 830 ids / 76 groups, A16 histogram unchanged |

## What changed in round 2 (file:line)

- Realtime (`include/Note.h`, `src/core/Note.cpp`): `m_mpeStream` is `std::shared_ptr<MpeExpressionStream>`,
  never mutated in place — copies share the immutable snapshot (one lock-free reference count, the
  detuning helper's shape), `mpeStreamForEdit()`/`setMpeStream()` detach copy-on-write on the
  UI/control thread. `NotePlayHandle` construction (audio thread) allocates nothing for the stream.
- 64-bit (`include/NotePlayHandle.h`, `src/core/MpeExpressionStreamPlayback.cpp`):
  `mpeStreamPointOffset()` is a public static pure function using `std::llround`/`std::int64_t`.
- GUI (`src/gui/NoteTransformActions.cpp`, `include/NoteTransformActions.h`): the piano roll's
  note-tools menu gains **MPE pitch ramp...** and **Clear MPE expression streams**, both running the
  shared `note.expression_stream_set` command (one MidiClip-checkpoint undo step per note). No new
  commands, no snapshot/canary/histogram change. Documented in `docs/NOTE-EDITOR.md`,
  `docs/KNOWN-LIMITATIONS.md`, `docs/MPE.md`.
- Registration: `tests/src/plugins/MpeConsumerProbe.h` + `tests/src/plugins/MpeTestConsumer.cpp` in
  `tests/fork-sources.txt` with recipe pathspecs; `tests/upstream-modifications.txt` Q050-016 rows
  updated (shared snapshot, 64-bit offset).
- Format: unchanged (three optional attributes, written only when non-empty). Old projects
  load/render/save exactly as before.

## Gates

| Gate | Exit |
|---|---:|
| `bash tests/complexity-gate.sh --check` | 1 (pre-existing train-5 red; the full 68-function violation list names none of this unit's files/functions) |
| `bash tests/file-length-gate.sh --check` | 1 (pre-existing train-5 red; none of the 14 listed files is this unit's) |
| `bash tests/fork-sources-gate.sh` | 0 |
| `bash tests/all-sources-reproduce.sh` | 0 |
| `bash tests/no-upstream-regression-gate.sh` | 0 |
| `tests/scripted/check-namespace` | 0 |
| `bash tests/no-tautology-gate.sh` | 0 |

Known limits: no GUI curve editor (drawing/dragging streams is Q050-137); point resolution is a
tick; step-hold, no interpolation; TSan does not run on this box, so the race argument is by design
(shared immutable snapshot + writer-side detach). `git status` is clean after the commits.

Round 3 (Opus lane, 2026-10-19): the round-2 shared_ptr publication described above is REPLACED.
`m_mpeStream` is now `std::atomic<const MpeStreamSnapshot*>` (immutable, reference-counted, published
by one atomic exchange, never freed on the realtime path); see docs/050-features/Q050-016.md
"Round 3" and include/MpeStreamSnapshot.h.
