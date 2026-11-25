FIXED

# cluster new-p17-146 — Failed stem files are silently skipped (`src/core/RenderManager.cpp`)

## Decision: REAL (the write-error half of the finding was already fixed on this branch; the
## "no output file at all" half was not)

`RenderManager::render()` created a `ProjectRenderer` and, when `isReady()` was false, logged
`qDebug("Renderer failed to acquire a file device!")` and called `renderNextTrack()`
(`src/core/RenderManager.cpp:230-234` before this change). Nothing recorded the failure, so the
queue drained and `renderNextTrack()` emitted `finished()` for an export that wrote no stem at
all — with `hasWriteError()` still false, so both callers reported success:

* `src/core/main.cpp:1587` — the headless render's exit code stayed `EXIT_SUCCESS`;
* `src/gui/modals/ExportProjectDialog.cpp:560` — `onRenderFinished()` closed the dialog as a
  finished export.

The sibling path (a renderer that *did* start and whose write/close failed) had already been
fixed on this branch by `new-p12-317`: `ProjectRenderer::hasWriteError()` plus
`m_writeFailed` in `renderNextTrack()` (`src/core/RenderManager.cpp:66-78`,
`include/RenderManager.h:101-111`). The not-ready path bypassed that verdict entirely, so a
destination that cannot take the file was still "a finished export". That is the reported bug,
and it is real.

Why the finding is not already fixed by `new-p12-317`: that fix only reads
`m_activeRenderer->hasWriteError()` from a renderer that *ran*. A renderer with
`isReady() == false` never runs, never sets `m_writeFailed`, and `hasWriteError()` answered
false after `finished()`.

Reproduction before the fix (logged, see below): `STEM_EVIDENCE UNREADY rendered=0
reported-failure=0` — no stem file, and the export's verdict said it had succeeded.

## The fix (at the cause, both callers)

* `src/core/RenderManager.cpp:243-262` — the not-ready branch now records the failure
  (`m_writeFailed = true`, sticky across the queued renders, exactly as the write-error path
  already did), keeps the renderer's own reason (`ProjectRenderer::refusal()`, or
  "an output file could not be created" when the device simply could not be opened) in the new
  `m_renderError`, and warns with the file and that reason. It still advances the queue, so a
  stem export with one bad target renders the remaining stems (a partial result) and then ends
  with a failed verdict instead of a finished one.
* `include/RenderManager.h:101-116,145-150` — `hasWriteError()`'s contract now covers both
  halves (no file at all, or an incomplete one), and `renderError()` is the preserved reason.
  Both existing callers already ask `hasWriteError()`, so the CLI exit code and the export
  dialog's failure warning come right without any GUI change.
* `src/core/main.cpp:1575-1593` — the headless failure line prints `renderError()` (the old
  write-only sentence was a lie for a render that created no file); the exit code is unchanged.

No control command was added, nothing new runs on the audio thread (all of this is the
control/CLI thread: `render()` and `renderNextTrack()` are called from `exportStems()`,
`renderProject()` and `renderTracks()`), and no undo step exists or changed here.

Registration: `include/RenderManager.h`, `src/core/RenderManager.cpp`, `src/core/main.cpp`
appended to `tests/upstream-modifications.txt` in the same commit.

## Test matrix

| Path | Covered by |
| --- | --- |
| normal stem export (ready renderers) | `StemExportTest::stemsAreExportedNonSilentAndDistinct` and the rest of the family, unchanged and still green |
| renderer never ready (refused render), stem export | `StemExportTest::unreadyRendererFailsTheExport` (new) |
| renderer never ready (file cannot be created, "no space"), whole-project render | `ExportWavWriteErrorTest::aRenderWhoseFileCannotBeCreatedFailsTheExport` (new, `Q_OS_UNIX`) |
| write/close failure while rendering | `ExportWavWriteErrorTest::aFailedRenderIsReportedByRenderManagerAndLeavesNoFileBehind`, still green |
| exhaustion / queue continues (partial export) | the new tests drive all three stems/tracks through the failing branch; the export still emits `finished()` once, still restores mute state and the Song's export-length settings (asserted) |
| reconfiguration | `StemExportTest::wholeProjectRenderIsUnchangedByAStemExport` — a stem export leaves the next whole-project render unchanged; still green |
| audio-thread constraints | nothing added on the render thread: the branch is on the control thread and only sets a bool, a QString and a `qWarning` |
| supported platforms | the stem case is portable (no POSIX API); the "file cannot be created" case uses `RLIMIT_FSIZE` and is compiled only for `Q_OS_UNIX`, the same seam `ExportWavWriteErrorTest` already uses |

## Commands and exit codes (copied from the logs in
`~/.config/kads/zene/state/runs/new-p17-146.r1/logs`)

RED (test first, before the product change):

```
cmake --build build --target StemExportTest -j4 > logs/build-1-red.log 2>&1        EXIT=0
cd build/tests && ctest -R '^StemExportTest$' --output-on-failure > logs/test-1-red.log 2>&1   EXIT=8
  FAIL!  : StemExportTest::unreadyRendererFailsTheExport() 'failed' returned FALSE.
            (an export that wrote no stem at all reported completion)
  STEM_EVIDENCE UNREADY rendered=0 reported-failure=0
```

GREEN (fix applied):

```
cmake --build build --target zene -j4 > logs/build-2-green.log 2>&1                 EXIT=0
cmake --build build --target StemExportTest -j4 > logs/build-3-green.log 2>&1       EXIT=0
build/tests/StemExportTest unreadyRendererFailsTheExport > logs/test-4-green-single.log 2>&1  EXIT=0
  STEM_EVIDENCE UNREADY rendered=0 reported-failure=1
```

Step 4b — the test catches the bug (product files only reverted, tests kept):

```
git diff -- src/core/RenderManager.cpp include/RenderManager.h src/core/main.cpp > logs/fix.patch   EXIT=0
git apply -R logs/fix.patch                                                        EXIT=0
cmake --build build --target zene StemExportTest ExportWavWriteErrorTest -j4 > logs/build-8-red-reverted.log 2>&1  EXIT=0
build/tests/StemExportTest unreadyRendererFailsTheExport > logs/test-9-red-stem.log 2>&1            EXIT=1
build/tests/ExportWavWriteErrorTest aRenderWhoseFileCannotBeCreatedFailsTheExport > logs/test-10-red-noroom.log 2>&1  EXIT=1
git apply logs/fix.patch                                                           EXIT=0
cmake --build build --target zene StemExportTest ExportWavWriteErrorTest -j4 > logs/build-9-green-restored.log 2>&1  EXIT=0
build/tests/StemExportTest unreadyRendererFailsTheExport > logs/test-11-green-stem.log 2>&1         EXIT=0
build/tests/ExportWavWriteErrorTest aRenderWhoseFileCannotBeCreatedFailsTheExport > logs/test-12-green-noroom.log 2>&1  EXIT=0
```

(An earlier cycle of the same pair ran before the second test existed:
`logs/build-4-red-reverted.log` EXIT=0, `logs/test-5-red-reverted.log` EXIT=1,
`logs/build-5-green-restored.log` EXIT=0, `logs/test-6-green-restored.log` EXIT=0.)

The `ctest -R` family, from `build/tests`, after the fix:

```
ctest -R '^(StemExportTest|ExportWavWriteErrorTest|SurroundExportTest|MasteringTest|ExportProjectDialogPresetTest|ControlStemExportTest|ControlStemExportVerb)$' --output-on-failure > logs/test-7-family.log 2>&1   EXIT=0
  100% tests passed out of 7
```

`include/RenderManager.h` is included by `zene`, `StemExportTest`, `ExportWavWriteErrorTest`,
`SurroundExportTest`, `MasteringTest` and `ExportProjectDialogPresetTest`; all six were rebuilt
(the commands above) and their tests run.

## What a caller sees now

* `lmms render --render <unwritable dir>` / `render.stems`: the child process exits
  `EXIT_FAILURE` and prints `render: <renderError()>`, e.g.
  `render: an output file could not be created` or the renderer's own refusal text.
* The export dialog, unchanged: `hasWriteError()` is true, so `onRenderFinished()` shows the
  "Export failed" warning instead of closing as a finished export.
* A partial export (some stems written, some not) still writes the stems it can, keeps their
  track numbers, and reports failure overall.

## Evidence that the reason is preserved (from `logs/test-11-green-stem.log`)

```
QWARN  : StemExportTest::unreadyRendererFailsTheExport() Render failed, no output file for
  .../stems-unready/03_Lead.wav: a surround export needs a surround master bus, and the
  master is stereo: set the master's format (5.1, 7.1, quad or LCR) or export stereo
STEM_EVIDENCE UNREADY rendered=0 reported-failure=1
```

## Self-check

* **The regression test FAILS with the product change reverted and PASSES with it (exit codes).**
  `git apply -R logs/fix.patch` → `EXIT=0`; rebuild `EXIT=0`; `StemExportTest
  unreadyRendererFailsTheExport` → `EXIT=1` (`FAIL! ... 'failed' returned FALSE`,
  `logs/test-9-red-stem.log`) and `ExportWavWriteErrorTest
  aRenderWhoseFileCannotBeCreatedFailsTheExport` → `EXIT=1` (`logs/test-10-red-noroom.log`).
  `git apply logs/fix.patch` → `EXIT=0`; rebuild `EXIT=0`; both → `EXIT=0`
  (`logs/test-11-green-stem.log`, `logs/test-12-green-noroom.log`). Neither test references a
  symbol this fix introduces (`renderError()`), so the RED run is a behavioural failure, not a
  compile error.
* **Determinism.** No sleeps, no wall-clock measurement, no threads of the test's own. The
  stem case is made deterministic by settings that make `ProjectRenderer` refuse before opening
  a file (`OutputSettings::ChannelMode::Surround` while the master bus is `Stereo`); the
  whole-project case by `RLIMIT_FSIZE = 0` held only for the render, which makes libsndfile's
  header write fail with EFBIG the first time. Both failure modes are synchronous — `finished()`
  is emitted inside `exportStems()`/`renderProject()` — so the tests use a flag and only enter
  the event loop when it has not fired (`if (!ended) { loop.exec(); }`); the 120 s timer is a
  hang guard, never part of the measurement. The real close/write path is unchanged and still
  covered by `ExportWavWriteErrorTest`'s existing cases.
* **Fix at the cause, every caller and sibling consistent.** One branch (`render()`) is the only
  place a not-ready renderer exists; it is shared by `renderProject()`, `renderTracks()` and
  `exportStems()` (all three go through `render()`), and all three are covered — stems by the
  new stem test, the whole-project path by the new no-room test, `renderTracks()` by
  `StemExportTest::perTrackExportSanitizesTrackNames` and `ExportWavWriteErrorTest`. The two
  readers of the verdict (`main.cpp:1587`, `ExportProjectDialog.cpp:560`) already ask
  `hasWriteError()`, which is now the complete verdict, so both report the failure without a
  signature change; the CLI's message was made true for both causes. The write-error sibling
  keeps its exact behaviour and message text (now carried by `renderError()`).
* **No collateral damage.** A ready render takes exactly the old path (`if
  (m_activeRenderer->isReady())` branch untouched). A render that succeeds leaves
  `hasWriteError()` false and `renderError()` empty, so nothing that used to report success now
  reports failure — the 7-test family above is green, including the 11 pre-existing
  `StemExportTest` cases and the whole of `ExportWavWriteErrorTest`. Mute state and the Song's
  export-length settings are restored on the failure path too (asserted in the new stem test).
  Nothing about loading projects or saved data is touched.
* **GUI path.** `src/gui` was deliberately not changed: both callers of the verdict already
  report failure once `hasWriteError()` is true, so `ExportProjectDialog::onRenderFinished()`
  needs no edit, and the fix introduces no widget, no dialog and no new user action. The
  dialog's wording ("the incomplete file was removed") still describes the write-error case;
  see Other findings.

## Other findings

* `AudioFileDevice`'s constructor calls `exit(EXIT_FAILURE)` when it cannot open the output file
  and no GUI is present (`src/core/audio/AudioFileDevice.cpp:60-63`). In a headless render that
  is a whole-process exit from a constructor — the CLI cannot report, clean up or fall back to
  a per-stem failure, and no headless test can drive that path. That is why the new stem test
  enters the same `RenderManager` branch through a refused render instead. Not changed here:
  it is a design question (who owns the error) outside this finding.
* A render that cannot create its file leaves a 0-byte file behind (with `RLIMIT_FSIZE = 0`
  the open succeeds and the header write fails). The write-error path removes an incomplete
  file; the not-ready path cannot tell "we truncated the file" from "we never touched it"
  (`ProjectRenderer` does not say whether it opened the file), and removing it unconditionally
  would delete a previous good export of the same name. Left as it is, and recorded here.
* The export dialog's failure text names the write case only ("so the incomplete file was
  removed"). It is shown for both causes now, which is right for the user's next step but
  imprecise about the file. A wording change is a `src/gui` change and was not needed for the
  verdict.
