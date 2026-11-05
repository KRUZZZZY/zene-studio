IMPLEMENTED

# Q050-221.1 - Seeded scale-aware melodic contours (stage 1/3 of Q050-221)

Branch `feat/050-Q050-221.1-oc`, cut from `050/m0-train-24` (`2a7fd9f9c`). Feature record:
`docs/050-features/Q050-221.1.md`. Logs: `~/.local/share/zene-bunny-fix/qwen/runs/Q050-221.1.r1/logs`.

**What was built.** `note.generate_melody` (`src/core/ControlCommandsNoteMelody.cpp`). It writes a line of ordinary
notes whose every key is in the active root and scale, custom scales included. You choose a contour (walk / rise /
fall / arch / valley) and a seed; leave the seed out and the project seed is used. `target` new writes notes;
`target` existing re-pitches the notes in scope and keeps their rhythm. Each generation is one MidiClip journal
checkpoint (`true_inverse`). GUI entry: Notes > Tools > Generate a Melody..., which opens the same command's form.

## Acceptance

| # | Criterion | Test(s) | Command | Exit with change | Exit reverted |
| --- | --- | --- | --- | --- | --- |
| 1 | Same seed + starting clip/settings give identical notes; a different seed changes the pitch line | `NoteMelodyTest::{theSameSeedRepeatsAndAnotherSeedChangesTheLine, aSeedsLineIsPinned, theProjectSeedIsTheDefault}`; `ControlNoteMelody` (socket) | `ctest -R '^(NoteMelodyTest\|ControlNoteMelody)$' --output-on-failure` from `build/tests` | 0 [test-7.log, test-8.log] | 8 / 8 [test-revert-1.log: 6 QtTest failures; test-revert-2.log: 6 socket checks FAILED] |
| 2 | Every pitch in the active root and scale, custom scales too; undoable; no generative model | `NoteMelodyTest::{everyKeyIsInTheActiveScale, aGenerationIsOneUndoStep, theNoteEditorMenuRunsTheSameCommand}`; `ControlNoteMelody` | same | 0 [test-7.log, test-8.log] | 8 / 8 [test-revert-1.log, test-revert-2.log] |

The revert: `git apply -R` of the product hunks (registration header and call, `src/core/CMakeLists.txt`, the
reversibility row, the menu entry), then `cmake --build build --target zene NoteMelodyTest -- -j4` (exit 0,
build-revert-1.log) and the two tests (exit 8 each). Re-applying the patch and rebuilding gave exit 0 for both
(build-6.log, test-6.log). The last rebuild, after a complexity split and an `<cmath>` include, also passed: build-8.log
exit 0, test-8.log exit 0, 6/6 including the neighbours.

## Self-check

- **Each new test FAILS reverted, PASSES restored.** With the product hunks reversed and the tree rebuilt (exit 0):
  `NoteMelodyTest` exited 8, with all 6 test functions failing, `theNoteEditorMenuRunsTheSameCommand` among them
  (test-revert-1.log). `ControlNoteMelody` exited 8, with 6 checks FAILED (test-revert-2.log). Restored and rebuilt:
  both exited 0 (test-6.log, then test-7.log and test-8.log).
- **Every criterion proven by a test I ran.** Criterion 1: `NoteMelodyTest` (exit 0) and `ControlNoteMelody` (exit 0,
  9/9 checks, e.g. "same seed, same empty clip: identical notes ok" and "another seed: another pitch line ok"). Criterion
  2: the same two tests ("every key in D major ok", "every key in the custom scale ok", "control.undo takes the line
  back out ok", and the QtTest undo-depth and GUI cases). The table above has the logs.
- **One undo step per edit; honest class.** `aGenerationIsOneUndoStep` asserts journal depth +1 per generation, and that
  one `control.undo` restores the clip exactly. The two refusals (no scale; fewer than two scale keys in range) leave
  the depth unchanged. The row is `R("note.generate_melody", RC::TrueInverse, ...)`, with the MidiClip journal
  checkpoint as mechanism (`src/core/ControlReversibilityTableDevices.cpp`), the same as `note.generate`.
  `ReversibilityContractTest` exit 0 (test-5.log) after the A16 histogram was updated to 888 rows / 435 true_inverse.
- **Audio thread.** Nothing added runs there. `generateMelody`, `repitch`, `melodyIndices`, `walkIndices`, `draw`,
  `melodySeed` and `melodyPool` are a command handler and its helpers. They run on the control/GUI thread through
  `ControlRegistry::invoke`, like every `note.*` command. The menu action dispatches the same handler from the GUI.
  The handler edits a clip through `MidiClip::addNote` / `Note::setKey` after `addJournalCheckPoint()`, the same path
  as `note.generate` and `note.transpose_in_key`. No audio-path function was changed. Local TSan does not run on this
  box; by design the only shared state touched is the clip's notes, edited the way the neighbouring note tools edit
  them.
- **Old projects; round-trip.** No format change: no save/load code changed and no project, header or config data was
  added. The generated notes are ordinary notes that the existing note serialisation saves and loads. I did not run a
  separate save/reopen render for this unit, because nothing in the load or render path changed.
- **Portability.** Standard C++17 and Qt APIs that exist in Qt 5.15 (`QJsonArray`, `QVector`, `QLatin1String`). No
  Qt-6-only call, no `std::filesystem`, no POSIX call. Identifiers avoid `min`/`max`/`small`/`near`/`far`/`interface`.
  `std::abs(double)` comes from `<cmath>`. `QVector::size()` is cast to `int`, which is correct for both Qt 5 `int`
  and Qt 6 `qsizetype`. Seeded output does not depend on the standard library: `std::mt19937`'s sequence is fixed by
  the standard, and the range mapping is the file's own 64-bit multiply, not `uniform_int_distribution`.
  `aSeedsLineIsPinned` pins one exact line. Only built here with GCC and Qt 6; hosted CI was not run.

## Gates and neighbours

- Snapshot regenerated live (`snapshot_live.py`, exit 0, 879 ids / 77 groups). `tests/doc-figures-gate.py` exit 0 after
  the README canary update (docfig-2.log).
- `submit_check.py` reports "gates (feature_check.py)" as FAIL. The cause: `feature_check.py` tries to create
  `zene-studio-program/zene-feat-q050-221.1-feature-check/`, which is outside this lane's write-jail, and stops with
  PermissionError (feature-check-1.log). I ran a copy that differs only in its log directory, which points into the run
  folder (`/tmp/q221-fc/feature_check.py`). Every gate PASSED: fork-sources, all-sources-reproduce,
  no-upstream-regression, check-namespace, no-tautology, doc-figures, complexity, file-length (feature-check-4.log,
  exit 0). The first run flagged `melodyIndices` at CCN 11; it was split into `walkIndices` and re-checked.
- Neighbours passing: `NoteToolsTest`, `NoteEditorTest`, `ControlNoteEditor`, `ControlCommandsSnapshot`,
  `ReversibilityContractTest`, `ReversibilityUndoTest`, `AgentSurfaceReportTest`, `agent_surface`.
- Two neighbours fail in this tree for reasons outside this change. Neither was re-run on the base, so this is
  unverified there:
  - `ControlReversibilityTranscript` fails with "the LADSPA host plugin 'ladspaeffect' is not in this build".
  - `ControlMcpGroupCoverage` fails because the installed Python `mcp` package's `Server.__init__` rejects
    `on_list_tools` (test-9.log).
- Environment note: ccache's configured temp directory was not writable, so builds ran with
  `CCACHE_TEMPDIR=/tmp/ccache-q221`.
