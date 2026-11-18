# Cluster review-354232bbf

## Verdict: FIXED

Round 4. The MIDI export now reads the output file's error AFTER closing it, so a flush error or
an error the close itself reports is never missed: such an export is `Incomplete`, the handler
removes the partial temporary file and refuses, and the previous export at the target survives byte
for byte. The ownership rule from round 3 (the temporary file belongs to the call that CREATED it)
and the atomic QSaveFile replacement of the target are unchanged.

## Evidence

### The defect this round (Luna r3)

`plugins/MidiExport/MidiExport.cpp` decided the export's outcome from the file's error BEFORE the
close:

```cpp
const bool complete = midiout.status() == QDataStream::Ok && f.error() == QFileDevice::NoError;
f.close();
return complete ? ExportOutcome::Written : ExportOutcome::Incomplete;
```

Qt's `QFileDevice::close()` flushes the device and records a flush or close error in the device
status - and that error is only there AFTER the close. The old order therefore reported `Written`
for an export whose file did not close cleanly, and the handler then copied that incomplete
temporary output over the previous export: the user's last good MIDI file was replaced by a
half-written one.

Measured on this build (Qt 6.12, `QFile` on `/dev/full`, whose writes are buffered and fail at the
flush):

| | before `close()` | after `close()` |
|---|---|---|
| write error (a large write fails at once) | `WriteError` | `NoError` - a successful close RESETS the status |
| flush error (a small write is buffered, the flush fails) | `NoError` | `WriteError` - it only shows up after the close |

So the error has to be read TWICE: before the close for a failed write, and after it for a flush or
close error. Reading it only before the close missed the second; reading it only after it would
have lost the first.

### The fix, at the cause

- `plugins/MidiExport/MidiExport.cpp:273-283` - the error is read before the close (the failed
  write) and the file's state is read again after it (the flush or close error), and the export
  reports `Incomplete` unless both are clean.
- `include/ExportFilter.h:58-72` - `exportOutcomeAfterClose(const QFileDevice& file, bool writeOk)`
  is where that verdict is decided, with the test seam beside it.
- `src/core/Song.cpp:2799-2818` - `exportOutcomeAfterClose()` returns `Incomplete` when the file
  carries an error after the close, and `setExportCloseFailureHook()` forces that verdict for a
  test.
- `src/core/ControlCommandsProjectLifecycle.cpp:296-312` - unchanged from round 3, and it is what
  turns `Incomplete` into the safe outcome: the partial temporary file is removed and the call is
  refused, so the target keeps its previous bytes.

### The regression (deterministic, no race)

`tests/src/core/MidiExportTest.cpp:396`
`aFailedCloseLeavesThePreviousExportInPlace` drives `project.export_midi` and checks the three
things the review names:

- the call is REFUSED (an export whose file did not close cleanly is not a successful export),
- the partial temporary file this call created is REMOVED (the path is captured through the
  round-3 temporary-path seam),
- the previous export at the target SURVIVES byte for byte.

The failed close is forced through the product's own seam, so the case is a fact of the test and
not a broken filesystem:

- `include/ExportFilter.h:74-81` - `setExportCloseFailureHook(std::function<bool()>)`: while a
  hook is set, a true result forces the close to count as FAILED - the case a flush error or an
  error the close itself reports produces. Nothing in the product sets it; the test clears it in
  `init()` (`tests/src/core/MidiExportTest.cpp:203`).

### Commands and exit codes (all logs in the run's log folder)

| Step | Command | Exit |
|---|---|---|
| RED (seam + test, before the plugin change) | `cmake --build build --target zene midiexport MidiExportTest -j12` | 0 (`build-8-round4-seam.log`) |
| RED | `ctest -R MidiExportTest --output-on-failure` (from `build/tests`) | **8** (`test-9-round4-red.log`) - 8 passed, 1 failed: `aFailedCloseLeavesThePreviousExportInPlace() '!result.ok' returned FALSE` (the export reported success although its file did not close cleanly) |
| GREEN | `cmake --build build --target zene midiexport MidiExportTest -j12` | 0 (`build-9-round4-fix.log`) |
| GREEN | `ctest -R MidiExportTest --output-on-failure` | **0** (`test-10-round4-green.log`) - 1/1 passed |
| family | `ctest -R "MidiExportTest\|ControlSuccessPathsTest\|SmfInterchange" --output-on-failure` | **0** (`test-11-round4-family.log`) - 4/4 passed (MidiExportTest, ControlSuccessPathsTest, SmfInterchangeTest, SmfInterchangeRoundTripTest) |
| 4b RED | `git diff -- plugins/MidiExport/MidiExport.cpp > fix-round4.patch` | 0 (`fix-round4.patch`) |
| 4b RED | `git apply -R fix-round4.patch` | 0 |
| 4b RED | `cmake --build build --target zene midiexport MidiExportTest -j12` | 0 (`build-11-round4-reverted.log`) |
| 4b RED | `ctest -R MidiExportTest --output-on-failure` | **8** (`test-12-round4-reverted-red.log`) - the same case fails again |
| 4b GREEN | `git apply fix-round4.patch` | 0 |
| 4b GREEN | `cmake --build build --target zene midiexport MidiExportTest -j12` | 0 (`build-12-round4-restored.log`) |
| 4b GREEN | `ctest -R MidiExportTest --output-on-failure` | **0** (`test-13-round4-restored-green.log`) - 1/1 passed |
| gate 4 | `bash tests/complexity-gate.sh --check` | **1** (`gate-complexity-round4.log`) - red on the train: the REGRESSION list is byte-identical to the pre-change run (`gate-complexity-before.log`), so nothing new |
| gate 7 | `bash tests/file-length-gate.sh --check` | **1** (`gate-filelength-round4.log`) - same: the REGRESSION list is identical to `gate-filelength-before.log` |
| all gates | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | **0** - PASS on all eight rows (fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures, complexity, file-length) |

### Files committed this round

- `plugins/MidiExport/MidiExport.cpp` - the error is read before AND after the close; the verdict
  comes from `exportOutcomeAfterClose()`.
- `include/ExportFilter.h` - `exportOutcomeAfterClose()` and the close-failure test seam.
- `src/core/Song.cpp` - `exportOutcomeAfterClose()` and the seam's storage.
- `tests/src/core/MidiExportTest.cpp` - the regression.
- `tests/upstream-modifications.txt` - the divergence ledger for the inherited files.
- `docs/qwen-fixes-2026-10/cluster-review-354232bbf.md` - this report.

Two commits this round: `9f2e71e2f` (the seam + the regression, RED on purpose) and the fix
(GREEN). The round-3 commits (`4af7b548e`, `4c384371c`, `df31da743`) are their base.

## Self-check

- **The regression test FAILS with the product change reverted and PASSES with it.**
  RED: `ctest -R MidiExportTest --output-on-failure` -> **8** (the case
  `aFailedCloseLeavesThePreviousExportInPlace` fails with `'!result.ok' returned FALSE`).
  GREEN: the same command -> **0** (1/1). Repeated with only the product hunk reverted
  (`git apply -R fix-round4.patch`): build 0, ctest **8**; restored (`git apply fix-round4.patch`):
  build 0, ctest **0**. Logs: `test-9-round4-red.log`, `test-10-round4-green.log`,
  `test-12-round4-reverted-red.log`, `test-13-round4-restored-green.log`.
- **The test is deterministic.** The failed close is forced through the product's own seam
  (`setExportCloseFailureHook`), not by a slow disk, a full filesystem or a sleep: the same
  command, the same file, the same result, every time. The temporary path is captured through the
  round-3 seam (`setControlExportMidiTempPathHook`), so the "the partial file is removed" check
  names the file the call really created.
- **The fix is at the cause and complete.** The cause was the moment the file's error was read:
  before the close, where a flush or close error does not exist yet. The error is now read before
  the close (the failed write, which a successful close resets) and after it (the flush or close
  error). Every caller and sibling was checked: `MidiExport::tryExport` is the only implementation
  of `ExportFilter::tryExport` in this tree and `Song::exportProjectMidi` its only caller;
  `exportOutcomeAfterClose()` is called from that one place; the handler's `Incomplete` branch
  (`src/core/ControlCommandsProjectLifecycle.cpp:301`) is the consumer of the verdict and is
  unchanged; the sibling export path `interchange.smf_export` writes with QSaveFile directly
  (`src/core/SmfInterchange.cpp:298-313`) and has no temporary file and no ownership question.
- **No collateral damage.** A successful export is unchanged (the close succeeds, the file carries
  no error, the export reports `Written`); the command's contract, description and schemas are
  unchanged (`ControlCommandsSnapshot` passed in round 3 and the command text was not touched
  here). Old projects and saved data are untouched: no project format, no journal and no undo
  entry is involved. The new check is on the export's own file, and no other caller writes one.
- **Realtime, undo and commands.** Nothing here runs on the audio thread: the export is a
  user-initiated file write, and no allocation, lock, wait, log or signal was added to any
  per-period path. The undo stack is untouched (the export was never undoable and stays so), and no
  new control command was added.
- **Portability (Qt 5.15 / MSVC / macOS < 10.15).** No new API beyond `QFileDevice::error()`,
  `QFile::close()` (void since Qt 6 - the verdict comes from the device status, not from a return
  value) and `std::function` - all available in Qt 5.15. No `windows.h` macro names, no
  `std::filesystem`, no Qt-6-only call.
- **Sibling commands in the command files I changed** (`src/core/ControlCommandsProjectLifecycle.cpp`
  registers `project.new`, `project.save_as_template`, `project.import`, `project.export_midi` and
  `transport.set_metronome`): only `project.export_midi` writes a file, and this round touched
  neither the handler nor the other commands - the change is in the export filter and in the
  verdict the handler reads. The completion check lists the whole `transport.*` group, but the
  only transport command in the file is `transport.set_metronome` (a boolean on the metronome);
  the rest live in other command files that this change does not touch.
- **The verdict is FIXED, not ALREADY-FIXED or FALSE-POSITIVE.** The defect is present in the code
  this branch started from: `plugins/MidiExport/MidiExport.cpp` read the file's error before
  `f.close()`, so a flush or close error was invisible and the export reported `Written`; the
  regression is red against exactly that code (`test-12-round4-reverted-red.log`, exit 8).
