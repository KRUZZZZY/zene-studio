# Engine batch-12

Six engine-lane findings, each adjudicated against the current tree on branch
`bugs/bunny-engine-2026-10-05`.

### Cluster 5245 — Recording recovery can report more frames than the file contains
Verdict: FIXED
Evidence: `src/core/RecordingJournal.cpp` — `framesInFile()` took the WAV `data` chunk's declared size
straight from the header and converted it to frames without bounding it by the bytes physically present,
so a truncated take reported the header's frame count. A new `framesInDataChunk()` helper now clamps the
declared size by `file.size() - in.device()->pos()` and `framesInFile()` delegates to it.
Commit: 60087a7a4
Test: RecordingRecoveryMenuTest::aTruncatedTakeIsOfferedWhatIsActuallyOnDisk
Red/green: red `ctest -R '^RecordingRecoveryMenuTest$'` EXIT=8 (`frames_in_file` Actual 44100 vs Expected
22050); green build EXIT=0, `ctest -R '^RecordingRecoveryMenuTest$'` EXIT=0 (1/1); complexity-gate EXIT=0,
file-length-gate EXIT=0.

### Cluster 3419 — Loudness report accessors can return a previous render's report
Verdict: FALSE-POSITIVE
Evidence: `include/RenderManager.h:119` holds `const OutputSettings m_outputSettings`, so one instance's
loudness-report setting is fixed at construction and cannot differ between two renders on that instance;
`src/core/ProjectRenderer.cpp:178` only builds a loudness report when `outputSettings.loudnessReport()` is
set, so a render that requests no report can only follow a report-requesting render on a *different*
instance. Every caller constructs a fresh manager per export
(`src/gui/modals/ExportProjectDialog.cpp:281`, `src/core/MasteringJob.cpp:254`, `src/core/main.cpp:1320`),
so no reachable sequence produces a stale report through the accessors at `RenderManager.h:95-98`.
Commit: none
Test: none
Red/green: none

### Cluster 1600 — Conversion staging truncates an existing `.part` file
Verdict: FIXED
Evidence: `src/core/RenderQueue.cpp:70` derived the staging path as `job.out + ".part"`, and
`writeFile()` opened it with `sf_open(..., SFM_WRITE, ...)`, truncating any file already at that name;
cleanup on failure and cancellation removed it too. `stagedPath()` now mints a per-job name
(`out + ".part." + <uuid prefix>`) stored in `RenderJob::staged`, set in `runConvert()` before the job is
copied onto the worker so `finish()` commits the same file.
Commit: 3f070c2e7
Test: ProjectToolsTest::aConversionLeavesAnUnrelatedDotPartFileAlone
Red/green: red `ctest -R '^ProjectToolsTest$'` EXIT=8 (`file.open(QIODevice::ReadOnly)` returned FALSE at
ProjectToolsTest.cpp:133); green build EXIT=0, `ctest -R '^(ProjectToolsTest|TaskCenterTest)$'` EXIT=0
(2/2); complexity-gate EXIT=0, file-length-gate EXIT=0.

### Cluster 4383 — Failed staged rename can destroy the previous destination
Verdict: ALREADY-FIXED
Evidence: `src/core/RenderQueue.cpp:99-113` — `commitStaged()` moves the existing destination aside to
`out + ".old"` before renaming the staged file over it, and restores it from that aside whenever the
staged rename does not take (`if (hadOld && !QFile::exists(job.out)) { QFile::rename(aside, job.out); }`),
so a failed rename leaves the previous destination in place. The finding describes the pre-fix ordering.
Commit: none
Test: none
Red/green: none

### Cluster 5273 — Queue completion scans large files on the UI thread
Verdict: SKIPPED-SCOPE
Evidence: `src/core/RenderQueue.cpp:147-155` — `conclude()` calls `reportOf()` (lines 125-134) inline, which
hashes the output with `BounceInPlace::sha256OfFile()` and runs `control::meterMeasureFile()`. This is the
queue/UI thread, not an audio callback, so the realtime rule does not apply; but moving the report off the
thread changes when `job.report` becomes readable through `render.queue_list` and `render.queue_report`
(`src/core/ControlCommandsRenderQueue.cpp:62-69`), and no deterministic headless test can observe thread
affinity of the report without also changing that observable timing. Out of scope for a behaviour-preserving fix.
Commit: none
Test: none
Red/green: none

### Cluster 5270 — Cancelling a render can delete the previous output file
Verdict: REAL-UNTESTED
Evidence: `src/core/RenderQueue.cpp:269` passes `job.out` straight to the child render process, and
`discardOutput()` (lines 159-163) removes any `job.out` whose mtime is not older than `job.startedMs`, so a
cancelled render over an existing file can take the previous file with it. The described fix — render to a
staging path and commit — is a design change to the render path, and a RED test would need the real child
render process killed mid-write with a pre-existing destination, which is not deterministic headlessly.
Left unchanged per procedure.
Commit: none
Test: none
Red/green: none

Leftovers: clusters 5273 and 5270 remain unfixed on purpose — 5273 needs a decision on when `job.report`
becomes observable through the control surface, and 5270 needs a staged render path in `runRender()` with a
test seam that does not depend on killing a live child render.
