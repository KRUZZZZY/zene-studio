FIXED

# Cluster new-p12-317-port — WAV export ignores write and close failures (BUG 14)

`new-p12-317-port: FIXED`

This is a PORT job. The fix was accepted as `bugs/qwen-new-p12-317` but the train
refused it: it conflicted with the newer line in `include/AudioFileWave.h` and
`src/core/audio/AudioFileWave.cpp` (the Q050-166 discrete-speaker-mask work).
The fix was re-applied by hand here, reading each commit with `git show`, not by
merge/cherry-pick/rebase. The newest line did **not** already fix the bug — see
the verdict evidence below.

## Verdict evidence (REAL, at the fork base `07d6d02bd`)

At the base the WAV export threw away every failure signal:

* `src/core/audio/AudioFileWave.cpp:193,222` — `sf_writef_float()` /
  `sf_writef_short()` were called as statements; their `sf_count_t` return value
  (the frames actually written) was discarded. No record of a short write existed
  anywhere on the device.
* `src/core/audio/AudioFileWave.cpp:268-278` — `finishEncoding()` called
  `sf_close()` and discarded its result; it was `void` and private, called only
  from the destructor (`:113`), so no caller could ever see a failed close.
* `include/AudioFileDevice.h:52` — `virtual void writeBuffer(...) = 0;` — the
  base contract itself was "fire and forget", so `ProjectRenderer` had nothing to
  check even if it wanted to.
* `src/core/ProjectRenderer.cpp:305-316` — `run()` called
  `m_fileDev->writeBuffer(...)` / `writeSurround(...)` and never looked at the
  result; it went straight on to `reportLoudness()` and normal completion.
* `src/core/RenderManager.cpp:65` — `renderNextTrack()` did
  `m_activeRenderer.reset()` with no verdict read first; `include/RenderManager.h`
  had no failure result to forward. A disk that refused every byte still produced
  a "finished" export in the CLI and the dialog.
* Siblings had the same hole: `AudioFileFlac.cpp:112,124` (short
  `sf_writef_float/short`, discarded `sf_close`), `AudioFileOgg.cpp:152`
  (encoded page `writeData()` result discarded), `AudioFileMP3.cpp:78,90` (LAME
  frame and flush-tail `writeData()` discarded).

The newest line (Q050-166: `setChannelMap()`, `m_zeroChannelMask`,
`writeZeroChannelMask()`, `surroundexport::masterFormat()`) does not touch any of
this: `writeBuffer` still returned `void`, the libsndfile results were still
discarded, and `finishEncoding()` at base is still the silent
`sf_close(); m_sf = nullptr;` plus the mask patch. So this is not ALREADY-FIXED.

## What changed (ported by hand, reconciled with Q050-166)

* `include/AudioFileDevice.h` — the base owns the verdict:
  `virtual bool writeBuffer(...)` (was `void`), `virtual bool finishEncoding()`,
  `virtual bool hasWriteError() const`, protected `recordWriteError()` /
  `m_writeError`, and `removeOutputFile()` (closes the handle, then removes the
  file). Every encoder now answers one question.
* `src/core/audio/AudioFileWave.cpp` + `include/AudioFileWave.h` — every write is
  compared with the frame count it was given, counted in `m_framesWritten`, and
  recorded (`:195-200`, `:228-233`, `:272-289`). `writeBuffer()` returns the
  period's result. `finishEncoding()` is now the public base override (`:309`):
  it checks `sf_close()`, **reads the closed file back** against
  `expectedFileSize()` (`:331`), because libsndfile 1.2.x `psf_fwrite()` counts a
  partial header write as a whole one and `sf_close()` returns 0 for a file whose
  final header never landed, and it removes an incomplete file (`:338`).
  Idempotent, so the render's call and the destructor's second call are both
  safe. The Q050-166 discrete-mask patch is preserved and still runs after the
  close, before the completeness check.
* `src/core/ProjectRenderer.cpp` + `include/ProjectRenderer.h` — `run()` checks
  every period (`:311-321`), stops at the first failed write, finalises
  explicitly before anything reads the result (`:343`), stores the verdict in an
  `std::atomic` read after `finished()` (`:344`), and removes the incomplete file
  (the abort path already did). `writeSurround()` returns its write result.
* `src/core/RenderManager.cpp` + `include/RenderManager.h` — the manager takes
  the finished renderer's verdict **before** `reset()` destroys it
  (`RenderManager.cpp:69`), sticky across the queued renders of one export.
* `src/core/main.cpp:1494` — the headless render exits non-zero and says why.
* `src/gui/modals/ExportProjectDialog.cpp:560` — the dialog reports the failure
  instead of closing as if the export had finished. No new control command.
* Siblings made equally honest: `AudioFileFlac.cpp:116,126,140` (short write and
  failed close), `AudioFileOgg.cpp:157` (page that did not reach the file),
  `AudioFileMP3.cpp:81,98` (LAME frame and flush tail).
* `new-p14-404` (carried with this cluster): the interleaved 16-bit dithered
  staging now uses the same round-to-nearest quantiser (`sampleRoundedToS16`) as
  the stereo `writeBuffer()` path; the un-dithered branch is unchanged.

## Regression tests (deterministic; no sleeps, timing or races)

* `tests/src/core/ExportWavWriteErrorTest.cpp` (new, 374 lines, registered in
  `tests/CMakeLists.txt`, `tests/all-sources.txt`, `tests/fork-sources.txt`) —
  three cases:
  * `aShortWriteIsReportedAndLeavesNoFileBehind` — the process's file-size cap
    (`RLIMIT_FSIZE`, with `SIGXFSZ` ignored) is set to the size of the WAV header
    the device just wrote, so the audio write cannot fit: a real short write.
  * `aFailedFinalisationIsReportedAndLeavesNoFileBehind` — the file is emptied
    and the cap set to 16 bytes, so the final header write fails: the close
    "succeeds" and libsndfile still reports success, which is exactly the case the
    read-back catches.
  * `aFailedRenderIsReportedByRenderManagerAndLeavesNoFileBehind` — a real
    one-second song rendered through `RenderManager` (the object the CLI and the
    dialog both hold) under a 4096-byte cap; asserts the manager reports the
    failure and no broken file is left.
  * `aSuccessfulExportKeepsItsFileAndReportsNoError` — the fix must not turn a
    good export into a failed one.
* `tests/src/core/ExportWavDitherTest.cpp` — `aShortWavWriteIsReported` (the same
  short write at the device level, the finding's own file) and
  `interleaved16BitDitherIsRoundedNotTruncated` (new-p14-404).
* The injected seam is the file-size cap plus an ignored `SIGXFSZ`, restored
  inside the test; there is no wall-clock input. The new-API calls sit behind
  `std::void_t` detection templates (`WriteBufferReports`, `HasWriteError`), so
  the same test binary compiles against the tree **without** the fix and fails on
  its assertions, not on the compiler.

## RED / GREEN (commands and exit codes, copied from the runs)

Log-folder note: the brief's directory
(`~/.local/share/zene-bunny-fix/qwen/runs/new-p12-317-port.r1/logs`) and the
worktree's parent are read-only for this session (`touch` → `Permission denied`
as their owner), so the identical logs were written to `build/fix-logs/` inside
the worktree (gitignored). The gate scripts were run through an unmodified copy
whose only change is the log path (`feature_check.py` → `build/fix-logs/gate-logs`);
the gate commands, their limits and `gate-baselines/` are the real ones.

RED (fixed product patch reversed — `git apply -R build/fix-logs/fix.patch`,
EXIT 0 —, the tests still build and fail at runtime):

```
cmake --build build --target ExportWavDitherTest ExportWavWriteErrorTest   # EXIT=0 (build-red-1.log)
cd build/tests && ctest -R '^(ExportWavDitherTest|ExportWavWriteErrorTest)$' --output-on-failure   # EXIT=8 (test-red-1.log)
FAIL!  : ExportWavDitherTest::interleaved16BitDitherIsRoundedNotTruncated() 'nonZero * 8 > read.size()' returned FALSE. (interleaved 16-bit dither left 4096 of 4096 samples at exact zero - truncated away)
FAIL!  : ExportWavDitherTest::aShortWavWriteIsReported() '!writeOk' returned FALSE. ()
FAIL!  : ExportWavWriteErrorTest::aShortWriteIsReportedAndLeavesNoFileBehind() '!QFile::exists(path)' returned FALSE. (a WAV write that did not reach the file left its broken export behind)
FAIL!  : ExportWavWriteErrorTest::aFailedFinalisationIsReportedAndLeavesNoFileBehind() '!QFile::exists(path)' returned FALSE. (a WAV file that was closed without its final header was left behind)
FAIL!  : ExportWavWriteErrorTest::aFailedRenderIsReportedByRenderManagerAndLeavesNoFileBehind() 'reported' returned FALSE. (a render whose WAV writes failed was reported as a finished export)
0% tests passed, 2 tests failed out of 2
```

4b RED — the whole product patch reversed a second time
(`git apply -R build/fix-logs/fix.patch`, EXIT 0), rebuilt and rerun:

```
cmake --build build --target ExportWavDitherTest ExportWavWriteErrorTest   # EXIT=0 (build-red-2.log)
cd build/tests && ctest -R '^(ExportWavDitherTest|ExportWavWriteErrorTest)$' --output-on-failure   # EXIT=8 (test-red-2.log)
0% tests passed, 2 tests failed out of 2
five failing assertions, the same five as above
```

4b GREEN — the patch applied again (`git apply build/fix-logs/fix.patch`, EXIT 0):

```
cmake --build build --target zene ExportWavDitherTest ExportWavWriteErrorTest   # EXIT=0 (build-green-2.log)
cd build/tests && ctest -R '^(ExportWavDitherTest|ExportWavWriteErrorTest)$' --output-on-failure   # EXIT=0 (test-green-2.log)
100% tests passed out of 2
```

Affected family (18 tests built, 18 passed, EXIT=0, `test-family-2.log`):

```
cd build/tests && ctest -R '^(ExportWavDitherTest|ExportWavWriteErrorTest|ExportDitherTest|ExportNoiseShaperTest|ExportQualityControlsTest|AudioFileOggInitTest|RenderSoftwareTag|StemExportTest|SurroundExportTest|ControlRenderJobTest|MasteringTest|ClipOffsetFadeTest|ClipFadesRenderTest|AudioClipLoopTest|SessionPlaybackRenderTest|SessionSchedulerRenderTest|LoudnessReportTest|LufsMeterTest)$' --output-on-failure
100% tests passed out of 18
```

Live user-visible path (`zene render`, a real project, `ulimit -f 2`):

```
zene render data/projects/tutorials/editing_note_volumes.mmp -o /tmp/cliprobe/out.wav   # EXIT=1 (cli-capped.log)
render: the output could not be written completely; the incomplete file was removed
file removed
zene render … -o /tmp/cliprobe/ok.wav (no cap)                                          # EXIT=0, 2720864-byte wav (cli-healthy.log)
```

Gates — all eight rows PASS, EXIT=0 (`gate-2.log`):

```
python3 <unmodified feature_check.py with only the log dir redirected> $PWD
  PASS  fork-sources / all-sources-reproduce / no-upstream-regression / check-namespace
  PASS  no-tautology / doc-figures / complexity / file-length
RESULT: PASS
```

`tests/all-sources.txt` also had to be re-derived from its own published recipe:
the train commit `07d6d02bd` regenerated it with a broader command and it carried
1148 vendored `plugins/SfzPlayer/sfizz/external/**` paths the recipe excludes on
purpose, so `fork-sources` and `all-sources-reproduce` were already red on the
pristine tree (proved with the manifest reverted: `allsrc-base.log`, EXIT=1, the
identical 1148-line diff). The re-derivation is the recipe's byte-exact output
(`allsrc-rederived-2.log`: `REPRODUCES`); no gate limit or baseline is changed.
This follows the precedents `1a3e91e36` / `a71fcd6f1` for the same drift.

## Self-check

* **The regression test FAILS with the product change reverted and PASSES with
  it (exit codes).** RED `test-red-2.log` EXIT=8 (5 failed assertions), GREEN
  `test-green-2.log` EXIT=0, 2/2. Builds were EXIT=0 in both states, so the RED is
  a runtime failure, not a compile error.
* **The test is deterministic: no wall-clock timing, sleeps or races.** The seam
  is the process file-size cap (`RLIMIT_FSIZE`) with `SIGXFSZ` set to `SIG_IGN`,
  restored inside the test; the failure is a real `EFBIG`/short write, decided by
  the kernel synchronously. No sleeps, no threads of the test's own, no timing
  thresholds. The render case drives `RenderManager` and quits on its `finished()`
  signal through a `QEventLoop`, not on a timer.
* **The fix is at the cause and complete: every caller and sibling checked.** The
  only `writeBuffer()` caller is `ProjectRenderer::run()`
  (`src/core/ProjectRenderer.cpp:311-315`); the only `finishEncoding()` callers
  are the two WAV/FLAC destructors and the new render call. All four
  `AudioFileDevice` subclasses (WAV, FLAC, OGG, MP3) now return a result, record a
  failure, and expose it through the one base flag; the base's default
  `finishEncoding()` is a no-op success for any future encoder that has nothing to
  close. The wider type the verdict flows into (`std::atomic<bool>` on the
  renderer, `bool` on the manager) was checked for the cross-thread reads.
* **No collateral damage.** A healthy render takes the same write path as before
  and is byte-identical (the family's `ExportDitherTest` / `ExportWavDitherTest`
  byte-level assertions and `RenderSoftwareTag` all pass); the completeness check
  only fires when the file is smaller than what libsndfile reported writing, so a
  legitimate file cannot trip it; no saved-data format changed; old projects load
  and render as before (`cli-healthy.log`).
* **Undo and commands.** This is not an undoable edit and touches no
  `ControlCommands*` file, so no command, reversibility class or
  `commands_snapshot.json` canary moved. No new control command was added.
* **Realtime.** The changed code runs on the offline render thread
  (`ProjectRenderer::run()`), on the file device it drives, and in the destructor
  / finalisation path - never in `process()`/`play()` or a mixer callback on the
  audio thread. The one allocation-shaped addition is `QFileInfo(...).size()` in
  `finishEncoding()`, which runs once after the render loop, not per period;
  `writeBuffer()` adds only a counter and a comparison.
* **Old projects still load and render as before.** Proven by the healthy CLI
  render above and by `SessionPlaybackRenderTest` / `SessionSchedulerRenderTest` /
  `AudioClipLoopTest` / `ClipFadesRenderTest` / `ClipOffsetFadeTest` in the family
  run (18/18 pass).
* **Verdict `ALREADY-FIXED`/`FALSE-POSITIVE` evidence.** Not applicable — the verdict
  is FIXED, and the REAL section above shows the base code (`AudioFileWave.cpp:193,222`,
  `:268-278`, `AudioFileDevice.h:52`, `ProjectRenderer.cpp:305-316`,
  `RenderManager.cpp:65`) discarding every write/close result on this branch.
* **Qt 5.15 / MSVC / macOS portability.** Uses `QFile`, `QFileInfo` and
  `std::atomic` only - no Qt-6-only API, no `windows.h` macro names, no
  `std::filesystem`. The RLIMIT-based test is guarded by `#ifdef Q_OS_UNIX`, so it
  compiles (and skips those cases) on Windows.

## Files committed

```
include/AudioFileDevice.h
include/AudioFileFlac.h
include/AudioFileMP3.h
include/AudioFileOgg.h
include/AudioFileWave.h
include/ProjectRenderer.h
include/RenderManager.h
src/core/ProjectRenderer.cpp
src/core/RenderManager.cpp
src/core/audio/AudioFileFlac.cpp
src/core/audio/AudioFileMP3.cpp
src/core/audio/AudioFileOgg.cpp
src/core/audio/AudioFileWave.cpp
src/core/main.cpp
src/gui/modals/ExportProjectDialog.cpp
tests/CMakeLists.txt
tests/all-sources.txt
tests/fork-sources.txt
tests/upstream-modifications.txt
tests/src/core/ExportWavDitherTest.cpp
tests/src/core/ExportWavWriteErrorTest.cpp
docs/qwen-fixes-2026-10/cluster-new-p12-317-port.md
```

`tests/upstream-modifications.txt` declares every inherited file above (the new
test is in `tests/fork-sources.txt` and `tests/all-sources.txt`).

`submit_check.py <worktree> new-p12-317-port` — RESULT: PASS - submit (EXIT=0,
`submit-1.log`); `feature_check.py` after the commit — RESULT: PASS, 8/8 rows
(EXIT=0, `gate-final.log`).
