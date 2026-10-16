IMPLEMENTED

# cluster Q050-016 — MPE expression event streams

Every acceptance criterion is met and proven by a test that was built and run on this worktree
(branch `feat/050-Q050-016-oc`, base `de5a85a96`). Logs:
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/Q050-016.r1/logs/`.

## Acceptance

| # | Criterion | Command | Result (EXIT) |
|---|---|---|---|
| 1 | A headless command test writes points at known note offsets and reads back the same values. | `cmake --build build --target ControlNoteExpressionCommandsTest -- -j4` then `ctest -R ControlNoteExpressionCommandsTest --output-on-failure` (from `build/tests`) | build `EXIT=0`, test `EXIT=0` — `expressionStreamWritesAndReadsBackAtKnownOffsets`: the three arrays (ticks 0/96/192, 0/48, 96) read back field-for-field, malformed points are `invalid_args`, and `control.undo` restores the prior stream |
| 2 | A project save and reload preserves point order and values while a note with no expression keeps its prior serialized form. | `cmake --build build --target MpeNoteStorageTest -- -j4` then `ctest -R MpeNoteStorageTest --output-on-failure` | build `EXIT=0`, test `EXIT=0` — `expressionStreamsRoundTripAndStayOptional`, `expressionStreamsPersistInAProject`: order/values survive save→load→save, the plain note carries exactly `{key,len,pan,pos,type,vol,id}`, an old project re-saves byte-identically |
| 3 | A MIDI-output test verifies pitch, pressure, and timbre changes reach the instrument at their scheduled offsets. | `cmake --build build --target MpePlaybackTest -- -j4` then `ctest -R "^MpePlaybackTest$" --output-on-failure` | build `EXIT=0`, test `EXIT=0` — `scheduledExpressionStreamReachesTheInstrumentAtItsOffsets` prints `MPE_EVIDENCE stream offsets channel=0 level=0.588401 pitch=1200@55 pressure=64@110 timbre=32@166 fpt=55.180` and asserts each value at frame `round(tick·fpt)` |
| — | No regression to the scalar MPE path / the A16 contract / the command snapshot | `ctest -R "MpeExpressionTest|MpeInputPathTest|ReversibilityContractTest|ControlCommandsSnapshot" --output-on-failure` | `EXIT=0` |

Snapshot regenerated from a live instance:
`python3 .../scratch/m0/snapshot_live.py $PWD` → `surface: 830 id(s) across 76 group(s)`, `EXIT=0`;
README canary moved 828 → 830; A16 histogram re-taken to
`rows=839 true_inverse=418 snapshot=42 irreversible=22 not_mutating=357`.

## Gates

| Gate | Exit |
|---|---:|
| `bash tests/complexity-gate.sh --check` | 1 (pre-existing; no new violation vs `../zene-cachyos`, diff empty) |
| `bash tests/file-length-gate.sh --check` | 1 (pre-existing; no new violation vs `../zene-cachyos`, diff empty) |
| `bash tests/fork-sources-gate.sh` | 0 |
| `bash tests/all-sources-reproduce.sh` | 0 |
| `bash tests/no-upstream-regression-gate.sh` | 0 |
| `tests/scripted/check-namespace` | 0 |
| `bash tests/no-tautology-gate.sh` | 0 |

Nothing is faked: `git status` is clean after the commit; the GUI curve editor is explicitly the
dependent unit Q050-137 and is recorded as a known limit.
