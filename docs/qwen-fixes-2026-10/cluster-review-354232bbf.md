# Cluster review-354232bbf

## Verdict: FIXED

Round 3. The MIDI export's temporary file is now owned by the call that CREATED it, not by an
existence check made before the export: the export creates the path exclusively and reports what it
did, and the handler removes only what this call created. A collision with a file the user keeps
beside the target is never written to and never removed, the partial output of a failed export is
removed, and the atomic (QSaveFile) replacement of the target is unchanged.

## Evidence

### The defect this round (Luna r2)

`src/core/ControlCommandsProjectLifecycle.cpp` inferred ownership of the temporary path from a
prior existence check and removed the path on failure:

```cpp
const bool ownedTemp = !QFileInfo::exists(temp);   // a guess, made BEFORE the export
const bool exported = Engine::getSong()->exportProjectMidi(temp);
if (!exported || !written.exists() || written.size() <= 0)
{
    if (ownedTemp) { QFile::remove(temp); }        // removes a path this call never created
    return refused(...);
}
```

A file that appears between that check and the export's exclusive open is deleted by the cleanup:
the file the user keeps beside the target is destroyed, and the previous export at the target is
left in place while the command claims there was no output.

### The fix, at the cause

- `include/ExportFilter.h:45` - a new `enum class ExportOutcome { NotCreated, Incomplete, Written }`.
  A filter SAYS what it did with the output path instead of leaving the caller to guess from the
  file's presence on disk: `NotCreated` = this call created nothing (the path was taken, or the
  filter could not be loaded), `Incomplete` = this call created the path but did not finish writing
  it, `Written` = this call created the path and wrote the whole export.
- `plugins/MidiExport/MidiExport.cpp:83` - the output is still created with
  `QIODevice::WriteOnly | QIODevice::NewOnly` (exclusive). A failed open now returns
  `ExportOutcome::NotCreated` (line 85) and touches nothing; a half-written file returns
  `ExportOutcome::Incomplete` (line 278) and a complete one `ExportOutcome::Written`.
- `include/Song.h:656` / `src/core/Song.cpp:2782` - `Song::exportProjectMidi` returns that outcome
  (a filter that cannot be loaded returns `NotCreated`).
- `src/core/ControlCommandsProjectLifecycle.cpp:296` - the handler takes ownership from the
  outcome, never from an existence check:
  - `NotCreated` -> the path was taken: it is left exactly as it was (line 297-303),
  - `Incomplete` -> the partial output is ours and is removed (line 301),
  - `Written` -> the temporary file is ours and is removed after the replacement (line 318),
    whether the replacement succeeded or not.
  The replacement itself is unchanged: `controlReplaceFileAtomically` (QSaveFile) puts the new bytes
  on the target only on a complete commit, so a failed replacement leaves the previous export byte
  for byte (`src/core/ControlCommandsProjectLifecycle.cpp:311-316`).

### The regression (deterministic, no race)

`tests/src/core/MidiExportTest.cpp:345`
`aCollidingTemporaryPathKeepsTheUserFileAndThePreviousExport` drives `project.export_midi` and
checks both things the finding is about:

- the file that occupies the temporary path survives the call whole (not written to, not removed),
- the previous export at the target survives it byte for byte,
- and the call is refused (it does not report a fresh export).

The collision is forced through a product-side test seam, so there is no wall-clock timing, no
sleep and no race:

- `include/ControlProjectAssets.h:196` - `ExportMidiTempPathHook` is
  `std::function<void(const QString& tempPath)>`; it is called with the temporary path just BEFORE
  the export claims it (`src/core/ControlCommandsProjectLifecycle.cpp:291`), which is exactly where
  the collision has to land to be caught. The test puts its file there.
  Nothing in the product sets the hook; the test clears it in `init()`
  (`tests/src/core/MidiExportTest.cpp:200`).

### Commands and exit codes (all logs in the run's log folder)

| Step | Command | Exit |
|---|---|---|
| RED (seam + test, before the fix) | `cmake --build build --target zene midiexport MidiExportTest -j12` | 0 (`build-1.log`) |
| RED | `ctest -R MidiExportTest --output-on-failure` (from `build/tests`) | **8** (`test-1-red.log`) - 7 passed, 1 failed: `aCollidingTemporaryPathKeepsTheUserFileAndThePreviousExport() 'kept.open(QIODevice::ReadOnly)' returned FALSE` (the cleanup DELETED the colliding file) |
| GREEN | `cmake --build build --target zene midiexport MidiExportTest -j12` | 0 (`build-2.log`) |
| GREEN | `ctest -R MidiExportTest --output-on-failure` | **0** (`test-2-green.log`) - 1/1 passed |
| family | `ctest -R "MidiExportTest\|ControlSuccessPathsTest\|SmfInterchange" --output-on-failure` | **0** (`test-3-family.log`) - 4/4 passed (MidiExportTest, ControlSuccessPathsTest, SmfInterchangeTest, SmfInterchangeRoundTripTest) |
| 4b RED | `git diff -- include/ExportFilter.h include/Song.h plugins/MidiExport/MidiExport.h plugins/MidiExport/MidiExport.cpp src/core/ControlCommandsProjectLifecycle.cpp src/core/Song.cpp > fix.patch` | 0 (`fix.patch`, 206 lines) |
| 4b RED | `git apply -R fix.patch` | 0 |
| 4b RED | `cmake --build build --target zene midiexport MidiExportTest -j12` | 0 (`build-4-reverted.log`) |
| 4b RED | `ctest -R MidiExportTest --output-on-failure` | **8** (`test-4-reverted-red.log`) - the same case fails: the colliding file is deleted again |
| 4b GREEN | `git apply fix.patch` | 0 |
| 4b GREEN | `cmake --build build --target zene midiexport MidiExportTest -j12` | 0 (`build-5-restored.log`) |
| 4b GREEN | `ctest -R MidiExportTest --output-on-failure` | **0** (`test-5-restored-green.log`) - 1/1 passed |
| command surface | `ctest -R ControlCommandsSnapshot --output-on-failure` | **0** (`test-6-snapshot.log`) - 1/1: the command's description and schema are unchanged |
| final | `ctest -R "MidiExportTest\|ControlSuccessPathsTest\|SmfInterchange" --output-on-failure` | **0** (`test-7-final-family.log`) - 4/4 passed |
| gate 4 | `bash tests/complexity-gate.sh --check` | **1** (`gate-complexity-after.log`) - red on the train: the REGRESSION list is byte-identical to the pre-change run (`gate-complexity-before.log`), so nothing new |
| gate 7 | `bash tests/file-length-gate.sh --check` | **1** (`gate-filelength-after.log`) - same: the REGRESSION list is identical to `gate-filelength-before.log` |
| all gates | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | **0** - PASS on all eight rows (fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures, complexity, file-length) |

### Files committed

- `include/ExportFilter.h` - `ExportOutcome`, and `tryExport` returns it.
- `include/Song.h` - `exportProjectMidi` returns `ExportOutcome`.
- `plugins/MidiExport/MidiExport.h`, `plugins/MidiExport/MidiExport.cpp` - the exclusive create
  reports `NotCreated` / `Incomplete` / `Written`.
- `src/core/Song.cpp` - `exportProjectMidi` propagates the outcome.
- `src/core/ControlCommandsProjectLifecycle.cpp` - ownership follows the outcome; the test seam.
- `include/ControlProjectAssets.h` - the temporary path helper and the test seam.
- `tests/src/core/MidiExportTest.cpp` - the regression.
- `tests/upstream-modifications.txt` - the divergence ledger for the inherited files.
- `docs/qwen-fixes-2026-10/cluster-review-354232bbf.md` - this report.

Two commits: `4af7b548e` (the seam + the regression, RED on purpose) and the fix (GREEN).

## Self-check

- **The regression test FAILS with the product change reverted and PASSES with it.**
  RED: `ctest -R MidiExportTest --output-on-failure` -> **8** (the case
  `aCollidingTemporaryPathKeepsTheUserFileAndThePreviousExport` fails with
  `'kept.open(QIODevice::ReadOnly)' returned FALSE`, i.e. the colliding file was deleted).
  GREEN: the same command -> **0** (1/1). Repeated with only the product hunks reverted
  (`git apply -R fix.patch`): build 0, ctest **8**; restored (`git apply fix.patch`): build 0,
  ctest **0**. Logs: `test-1-red.log`, `test-2-green.log`, `test-4-reverted-red.log`,
  `test-5-restored-green.log`.
- **The test is deterministic.** The collision is created by the product's own test seam
  (`setControlExportMidiTempPathHook`), which is called with the temporary path immediately before
  the export claims it. No sleep, no wall-clock timing, no race: the file is at the path before the
  exclusive open, every time.
- **The fix is at the cause and complete.** The cause was the ownership guess
  (`!QFileInfo::exists(temp)` before the export); it is gone - the ownership now comes from the
  creation itself. Every caller was checked: `ExportFilter::tryExport` has exactly one
  implementation in this tree (`MidiExport`) and one caller (`Song::exportProjectMidi`); 
  `Song::exportProjectMidi` has two callers (the `project.export_midi` handler and
  `tests/src/core/MidiExportTest.cpp`); the handler's other paths (`project.new`,
  `project.save_as_template`, `project.import`) do not touch the export and are unchanged. The
  sibling export path in the same area, `interchange.smf_export`, writes with QSaveFile directly
  (`src/core/SmfInterchange.cpp:298-313`) and has no temporary file and no ownership question -
  nothing to change there. The GUI path is the same command (`File > Export MIDI` dispatches
  `project.export_midi`; the command's description and schema are unchanged, and the
  `ControlCommandsSnapshot` check passes).
- **No collateral damage.** The command's contract is unchanged: a successful export replaces the
  target atomically, a failed one refuses and leaves the previous export byte for byte. Old
  projects and saved data are untouched (no project format, no journal, no undo entry is involved:
  the export writes a file outside the project and the command is not undoable). No new limit or
  check was added that another caller could hit. The only new API is the outcome enum and the test
  seam, both of which have exactly the callers listed above.
- **Realtime, undo and commands.** Nothing here runs on the audio thread: the export is a
  user-initiated file write on the control/GUI thread, and no new allocation, lock, wait, log or
  signal was added to any per-period path. The undo stack is untouched (the export was never
  undoable, and stays so), and no new control command was added - the `project.export_midi` id, its
  schemas and its description are exactly as they were, which the `ControlCommandsSnapshot` check
  confirms.
- **Sibling commands in the command files I changed** (`src/core/ControlCommandsProjectLifecycle.cpp`
  registers `project.new`, `project.save_as_template`, `project.import`, `project.export_midi` and
  `transport.set_metronome`): only `project.export_midi` writes a file; `project.new`,
  `project.save_as_template` and `project.import` keep their own paths untouched, and
  `transport.set_metronome` is unrelated. The shared helper `controlReplaceFileAtomically` is used
  only by the export handler and is unchanged. The completion check lists the whole `transport.*`
  group, but the only transport command in the file I touched is `transport.set_metronome` (a
  boolean on the metronome); the rest live in other command files that this change does not touch.
- **The verdict is FIXED, not ALREADY-FIXED or FALSE-POSITIVE.** The defect is present in the code
  this branch started from: `src/core/ControlCommandsProjectLifecycle.cpp` removed the temporary
  path on failure after an existence check made before the export (`const bool ownedTemp =
  !QFileInfo::exists(temp);` followed by `if (ownedTemp) { QFile::remove(temp); }`), and the
  regression is red against exactly that code (`test-4-reverted-red.log`, exit 8).
- **Portability (Qt 5.15 / MSVC / macOS < 10.15).** No new API beyond `QFile::open` with
  `QIODevice::NewOnly` (Qt 5.1+), `QFileInfo::size()`, `QFile::remove()` and `std::function` - all
  available in Qt 5.15. No `windows.h` macro names, no `std::filesystem`, no Qt-6-only call.
