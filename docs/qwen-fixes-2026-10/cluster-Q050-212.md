IMPLEMENTED

Q050-212, track creation output routing and auto-channel assignment. Design and format: `docs/050-features/Q050-212.md`.
"Bus" is decided as a regular mixer channel, because a parallel bus takes no track input.

## Acceptance

Logs: `~/.local/share/zene-bunny-fix/qwen/runs/Q050-212.r1/logs`.

| Criterion | Command | Exit (restored) | Exit (behaviour disabled) |
|---|---|---|---|
| `track.add` output, UI destination choice, add-N-to-new-bus | `ctest -R '^TrackAddRoutingTest$' --output-on-failure` | 0 (`test-9-restored`) | 8, 14/14 cases fail (`test-8-reverted`) |
| Auto-assignment, rename sync, disabled mode | same | 0 | 8 |
| Save/load, undo, redo for explicit and automatic | same | 0 | 8 |

Reversal method: `.cpp` bodies were neutralised (output and auto-channel ignored, sync and `mixlink` off, the toolbar and
checkbox entries removed, the bus command's build skipped), keeping the headers so the test compiles. A full reverse of
the commits cannot compile the test, because it uses the new API.

Neighbours (`test-4`, `test-5`, `test-6`): `ControlCommandsSnapshot`, `ReversibilityContractTest`, `ReversibilityUndoTest`,
`TrackFolderMenuTest`, `StableTrackIdsTest`, `MixerRoutingBackwardCompatTest`, `ControlRoutingCommands` all passed.
`ControlEditCommandsTest` exit 8, one failure, `trackSetArmManyReportsOnlyTheTracksItChanged`: the arm cannot create its take
file under `~/Documents`, which this session's write jail forbids. No change here touches arming; not re-run outside the jail.

Not run: the whole ctest suite and `run-all-gates.sh` (the brief forbids the suite; these belong to the merge point).
Not tested: the `src/core/main.cpp` readiness fix (see the feature doc). Hosted CI not run, so Qt 5.15 and MSVC are reasoned
only: the local build is Qt 6.

## Self-check
