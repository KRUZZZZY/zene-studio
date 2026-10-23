FIXED

# Cluster new-p12-317-port — WAV export ignores write and close failures (BUG 14)

`new-p12-317-port: FIXED`

This is a PORT job, reworked after review rounds 1 and 2. The fix was accepted as
`bugs/qwen-new-p12-317` but the train refused it: it conflicted with the newer
line in `include/AudioFileWave.h` and `src/core/audio/AudioFileWave.cpp` (the
Q050-166 discrete-speaker-mask work). It was re-applied by hand here, reading
each commit with `git show` — not by merge/cherry-pick/rebase. The newest line
did **not** already fix the bug. Round 1 of the review asked for five changes;
all five are addressed below.

## Verdict evidence (REAL, at the fork base `07d6d02bd`)

At the base the WAV export threw away every failure signal:

* `src/core/audio/AudioFileWave.cpp:193,222` — `sf_writef_float()` /
  `sf_writef_short()` were called as statements; their `sf_count_t` return value
  (the frames actually written) was discarded.
* `src/core/audio/AudioFileWave.cpp:268-278` — `finishEncoding()` called
  `sf_close()` and discarded its result; it was `void` and private, called only
  from the destructor (`:113`), so no caller could ever see a failed close.
* `include/AudioFileDevice.h:52` — `virtual void writeBuffer(...) = 0;` — the
  base contract itself was "fire and forget".
* `src/core/ProjectRenderer.cpp:305-316` — `run()` never looked at the result of
  `writeBuffer()` / `writeSurround()`, and went straight on to normal completion.
* `src/core/RenderManager.cpp:65` — `renderNextTrack()` reset the renderer with no
  verdict read; `include/RenderManager.h` had no failure result to forward.
* Siblings had the same hole: `AudioFileFlac.cpp:112,124` (short
  `sf_writef_float/short`, discarded `sf_close`), `AudioFileOgg.cpp:85,152`
  (header pages and page writes whose results were dropped; `writeData()` answers
  with the byte count, so a short positive write looked like success),
  `AudioFileMP3.cpp:78,90` (LAME frame and flush tail dropped), and both OGG and
  MP3 flushed only from their destructors — after the render's verdict.

## What changed (ported by hand, reconciled with Q050-166)

* `include/AudioFileDevice.h` — the base owns the verdict: `virtual bool
  writeBuffer(...)`, `virtual bool finishEncoding()`, `virtual bool
  hasWriteError()`, `recordWriteError()` / `m_writeError`, `removeOutputFile()`,
  and (rework) `flushOutputFile()`. `writeData()` answers with the bytes QFile
  *accepted*; QFile buffers, so a full disk can refuse them only at the flush —
  every finalisation now ends with that check.
* `src/core/audio/AudioFileWave.cpp` + header — every write is compared with the
  frame count it was given and counted (`:195-200`, `:228-233`, `:272-289`).
  `finishEncoding()` (`:363`) checks `sf_close()`, then (rework) verifies the
  **finished file's own header fields** via `headerHoldsEveryFrame()` (`:323`):
  `sf_close()` rewrites the RIFF and data-chunk sizes *in place*, and a partial
  rewrite leaves those fields stale while the file's LENGTH is unchanged — a
  size-only check calls that a success. The discrete-layout mask patch
  (`writeZeroChannelMask()`, `:400`) now returns whether `QFile::write()` **and**
  `flush()` succeeded, and a failure is a failed export. An incomplete file is
  removed. Idempotent, and the Q050-166 mask patch is preserved and still runs
  after the close.
* `src/core/audio/AudioFileOgg.cpp` + header (rework) — the constructor checks
  the three header pages (`:83-101`), and when they do not land it marks the
  device unsuccessful and removes the file; `writePage()` (`:170`) requires the
  **whole** page (`writeData() == length`), so a short positive write fails; and
  the stream tail is flushed by `finishEncoding()` (`:180`) — called by the
  renderer before it reads the result — instead of only by the destructor.
* `src/core/audio/AudioFileMP3.cpp` + header (rework) — `finishEncoding()`
  (`:87`) flushes the LAME tail (`m_flushed` makes the destructor's second call a
  no-op) and flushes the buffered file, so a failed final flush is part of the
  export result.
* `src/core/audio/AudioFileFlac.cpp` (rework) — a short `sf_writef_*`, a failed
  `sf_close()` and a failed final flush are all recorded.
* `src/core/ProjectRenderer.cpp` + header — `run()` checks every period
  (`:311-321`), stops at the first failed write, finalises explicitly before
  anything reads the result (`:343`), stores the verdict in an `std::atomic`
  (`:344`) and removes the incomplete file. `writeSurround()` returns its result.
* `src/core/RenderManager.cpp` + header — the manager takes the finished
  renderer's verdict **before** `reset()` destroys it (`:69`), sticky across the
  queued renders of one export.
* `src/core/main.cpp:1494` — the headless render exits non-zero and says why.
* `src/gui/modals/ExportProjectDialog.cpp:560` — the dialog reports the failure
  instead of closing as if the export had finished. No new control command.

**Out of scope, removed (review item 5).** The round-1 change also carried
new-p14-404's interleaved 16-bit dither rounding (the `sampleRoundedToS16`
branch in `AudioFileWave::writeInterleaved`, and
`ExportWavDitherTest::interleaved16BitDitherIsRoundedNotTruncated`). That is a
different issue with a different finding, and it is **not in this change**: the
interleaved 16-bit quantiser is byte-for-byte the base behaviour again
(`AudioFileWave.cpp:282-283` is the original single
`AudioEngine::clip(...) * OUTPUT_SAMPLE_MULTIPLIER` expression), and the p14 test
is gone from `ExportWavDitherTest.cpp`. It stays with `bugs/qwen-new-p14-404`
for its own scoped change.

## Rework round 2 — the OGG page-write coverage

Review round 2 accepted the product code and asked for one thing: regression
coverage for a **short positive OGG page write** and a **partial constructor
header page**. The reviewer's own fix (`3b9c0a7ce`, 83 lines) was dropped from
the branch because it added `QSKIP`, so the coverage is re-added here without any
skip.

Getting this right needed a measurement first. `writePage()` compares
`writeData()`'s result with the page length, and `writeData()` is `QFile::write`.
Two probes against the real Qt build showed:

* `QFile::write` returns a **short positive count** only for writes that bypass
  its buffer: 5 bytes for a 16 KiB-plus request under a 5-byte cap, but the full
  count for anything buffered (58..16384 bytes). It answers `-1` for a body that
  cannot even start.
* the OGG encoder's pages here are 30..4343 body bytes, so they are always
  buffered: a file-size cap can only ever fail the buffered **flush**, never
  `writePage`'s per-page comparison. The round-2 QA test's 5-byte cap therefore
  passed for the flush's reason, not the page check's - it did not cover the code
  it named.

So the fix now exposes a one-shot `LMMS_TESTING` seam on the encoder
(`setNextPageWriteShortForTest()` / `setFirstPageWriteShortForTest()`, consumed
by exactly one page) and routes `writePage()` through `writePageBytes()`, which
honours it and otherwise returns what `writeData()` stored. The comparison in
`writePage()` is unchanged. Two deterministic cases in
`tests/src/core/AudioFileOggInitTest.cpp` (the existing OGG test target; a new
target could not be registered because reconfiguring is the parent's job):

* `aShortPositiveOggPageWriteIsReported` — two seconds of audio are fed before
  arming the one-shot for the EOS tail page. Explicit `finishEncoding()` then
  flushes the tail and must report failure; the test also checks the device's
  sticky write-error state. This exercises the finalization verdict the render
  reads, rather than relying on the destructor's result.
* `aPartialOggHeaderPageRejectsTheDevice` — the one-shot is armed across devices
  so the stream's opening page is short; asserts `successful` is false, the error
  is recorded, and no half-written `.ogg` is left behind.

A tree without the seam fails these on
`HasPageWriteSeam<AudioFileOgg>::value` — a failed assertion, not a skip — and
the seam calls sit in templates so the file still compiles there.

## Regression tests (deterministic; no sleeps, timing or races)

`tests/src/core/ExportWavWriteErrorTest.cpp` (new, 583 lines, registered in
`tests/CMakeLists.txt`, `tests/all-sources.txt`, `tests/fork-sources.txt`):

* `aFailedHeaderRewriteOnAFullLengthFileIsReported` (rework) — the injected
  `RLIMIT_FSIZE` cap stops at the **data chunk's size field**, so the in-place
  header rewrite is refused while every audio byte is already on disk: the file
  length does not change, and the test asserts that explicitly before asserting
  the failure is reported and the file removed. This is exactly the case the
  round-1 size-only check missed.
* `aFailedDiscreteChannelMaskPatchIsReported` (rework) — a discrete-6 master
  (the layout `writeZeroChannelMask()` serves) is written, then the file is made
  read-only so the patch's **reopen** is refused while the already-open writer
  handle still closes and the file stays readable for the header check. The test
  first proves the injection really fails (`QSKIP` if the process can write
  through a read-only file), then asserts the patch's failure is reported and
  the file removed. `SurroundExportTest::aDiscreteMasterWritesItsChannelCount
  WithoutSpeakerMask` covers the healthy path (six discrete layouts, real mask 0).
* The two OGG page-write cases above live in
  `tests/src/core/AudioFileOggInitTest.cpp`, the existing OGG target.
* `aFailedSiblingFinalFlushIsReported` (rework) — an OGG and an MP3 device are
  written and then finalised under a cap at the file's current length, so the
  final flush has no room; asserts `finishEncoding()` says the file is
  incomplete, for both encoders. This is the call `run()` makes before it reads
  the verdict, so the destructor-after-verdict hole is closed by construction.
* `aShortWriteIsReportedAndLeavesNoFileBehind` — the cap is the WAV header's
  size, so the audio write cannot fit: a real short write.
* `aFailedFinalisationIsReportedAndLeavesNoFileBehind` — the file is emptied and
  the cap set to 16 bytes, so the final header write fails.
* `aFailedRenderIsReportedByRenderManagerAndLeavesNoFileBehind` — a real
  one-second song rendered through `RenderManager` (the object the CLI and the
  dialog both hold) under a 4096-byte cap; the manager reports the failure and no
  broken file is left.
* `aSuccessfulExportKeepsItsFileAndReportsNoError` — a good export stays good.

`tests/src/core/ExportWavDitherTest.cpp` — `aShortWavWriteIsReported` (the same
short write at the device level, the finding's own file). The p14 case was
removed (out of scope).

The seam is the process file-size cap (`RLIMIT_FSIZE`) with `SIGXFSZ` ignored,
plus a read-only file for the mask patch; both are restored inside the test and
need no wall clock. The new-API calls go through `std::void_t` detection
templates (`WriteBufferReports`, `HasWriteError`, `HasFinishEncoding`), and the
finalisation assertion is `finalisationReportedFailure()` — true only when the
device *says* the file is incomplete, so the tree without the fix fails on its
assertion instead of passing because the API was absent.

## RED / GREEN (commands and exit codes, copied from the runs)

Log-folder note: the brief's directory
(`~/.local/share/zene-bunny-fix/qwen/runs/new-p12-317-port.r1/logs`) and the
worktree's parent are read-only for this session (`touch` → `Permission denied`
as their owner), so the identical logs were written to `build/fix-logs/` inside
the worktree (gitignored). The gate scripts ran through an unmodified copy whose
only change is the log path (`feature_check.py` → `build/fix-logs/gate-logs`);
the gate commands, their limits and `gate-baselines/` are the real ones.

**4b RED — every product change since the fork base reversed** (`git diff
07d6d02bd -- <product files> > fix.patch`, `git apply -R`, EXIT 0), the tests
still build (EXIT 0) and fail at runtime on 9 assertions across 3 tests, with
**0 skipped**:

```
cmake --build build --target ExportWavWriteErrorTest ExportWavDitherTest AudioFileOggInitTest   # EXIT=0 (build-red-final3.log)
cd build/tests && ctest -R '^(AudioFileOggInitTest|ExportWavWriteErrorTest|ExportWavDitherTest)$' --output-on-failure   # EXIT=8 (test-red-final3.log)
FAIL!  : AudioFileOggInitTest::aShortPositiveOggPageWriteIsReported() 'HasPageWriteSeam<AudioFileOgg>::value' returned FALSE. (the OGG page-write test seam is missing: a short positive page write cannot be injected, and the page-write check is not under test)
FAIL!  : AudioFileOggInitTest::aPartialOggHeaderPageRejectsTheDevice() 'HasPageWriteSeam<AudioFileOgg>::value' returned FALSE. (the OGG page-write test seam is missing: a partial header page cannot be injected, and the constructor's header-page check is not under test)
FAIL!  : ExportWavDitherTest::aShortWavWriteIsReported() '!writeOk' returned FALSE. ()
FAIL!  : ExportWavWriteErrorTest::aFailedHeaderRewriteOnAFullLengthFileIsReported() 'reportedFailure' returned FALSE. (a WAV whose final header rewrite failed was reported as a success)
FAIL!  : ExportWavWriteErrorTest::aFailedDiscreteChannelMaskPatchIsReported() 'reportedFailure' returned FALSE. (a WAVEX file whose channel-mask patch failed was reported as a success)
FAIL!  : ExportWavWriteErrorTest::aShortWriteIsReportedAndLeavesNoFileBehind() '!QFile::exists(path)' returned FALSE. (a WAV write that did not reach the file left its broken export behind)
FAIL!  : ExportWavWriteErrorTest::aFailedFinalisationIsReportedAndLeavesNoFileBehind() '!QFile::exists(path)' returned FALSE. (a WAV file that was closed without its final header was left behind)
FAIL!  : ExportWavWriteErrorTest::aFailedSiblingFinalFlushIsReported() 'reportedFailure' returned FALSE. (tail.ogg's final flush did not reach the file but was reported as a success)
FAIL!  : ExportWavWriteErrorTest::aFailedRenderIsReportedByRenderManagerAndLeavesNoFileBehind() 'reported' returned FALSE. (a render whose WAV writes failed was reported as a finished export)
0% tests passed, 3 tests failed out of 3
```

RED per rework item, each with only that fix reverted and the rest left in place:

```
# header-rewrite check only (AudioFileWave.cpp/header reverted, EXIT 0)
cmake --build build --target ExportWavWriteErrorTest                          # EXIT=0 (build-rework-red-wave.log)
./build/tests/ExportWavWriteErrorTest                                         # EXIT=1 (test-rework-red-wave.log)
FAIL!  : aFailedHeaderRewriteOnAFullLengthFileIsReported() '!finalised' returned FALSE. (a WAV whose final header rewrite failed was reported as a success)

# channel-mask patch propagation only (EXIT 0)
cmake --build build --target ExportWavWriteErrorTest                          # EXIT=0 (build-rework-red-mask2.log)
./build/tests/ExportWavWriteErrorTest                                         # EXIT=1 (test-rework-red-mask2.log)
FAIL!  : aFailedDiscreteChannelMaskPatchIsReported() '!finalised' returned FALSE. (a WAVEX file whose channel-mask patch failed was reported as a success)

# OGG page checks reverted to the pre-rework shape (negative-only page check,
# unchecked constructor header pages); both OGG cases fail, 0 skipped (EXIT 0)
cmake --build build --target AudioFileOggInitTest                             # EXIT=0 (build-r2-redogg3.log)
./build/tests/AudioFileOggInitTest                                            # EXIT=1 (test-r2-redogg3.log)
FAIL!  : aShortPositiveOggPageWriteIsReported() 'writeReportedFailure' returned FALSE. (a short positive OGG page write was reported as a success)
FAIL!  : aPartialOggHeaderPageRejectsTheDevice() '!successful' returned FALSE. (a partial OGG header page was accepted as a successful device)

# OGG/MP3 flush left in the destructor (EXIT 0)
cmake --build build --target ExportWavWriteErrorTest                          # EXIT=0 (build-rework-red-sib2.log)
./build/tests/ExportWavWriteErrorTest                                         # EXIT=1 (test-rework-red-sib2.log)
FAIL!  : aFailedSiblingFinalFlushIsReported() '!finalised' returned FALSE. (tail.ogg's final flush did not reach the file but was reported as a success)
```

**GREEN — the patch applied again** (`git apply fix.patch`, EXIT 0):

```
cmake --build build --target zene AudioFileOggInitTest ExportWavWriteErrorTest ExportWavDitherTest   # EXIT=0 (build-green-final3.log)
cd build/tests && ctest -R '^(AudioFileOggInitTest|ExportWavWriteErrorTest|ExportWavDitherTest)$' --output-on-failure   # EXIT=0 (test-green-final3.log)
100% tests passed out of 3
```

Per case, 0 skipped (`test-r2-4.log`, `test-r2-redogg3.log`):

```
AudioFileOggInitTest: 5 passed, 0 failed, 0 skipped   (incl. both OGG page-write cases)
ExportWavWriteErrorTest: 9 passed, 0 failed, 0 skipped
```

The write-error binary's own per-case run (`test-rework-7.log`):

```
PASS   : aSuccessfulExportKeepsItsFileAndReportsNoError()
PASS   : aFailedHeaderRewriteOnAFullLengthFileIsReported()
PASS   : aFailedDiscreteChannelMaskPatchIsReported()
PASS   : aShortWriteIsReportedAndLeavesNoFileBehind()
PASS   : aFailedFinalisationIsReportedAndLeavesNoFileBehind()
PASS   : aFailedSiblingFinalFlushIsReported()
PASS   : aFailedRenderIsReportedByRenderManagerAndLeavesNoFileBehind()
Totals: 9 passed, 0 failed, 0 skipped, 0 blacklisted
```

Affected family (18 tests built, 18 passed, EXIT=0, `test-family-final3.log`):

```
cd build/tests && ctest -R '^(ExportWavDitherTest|ExportWavWriteErrorTest|ExportDitherTest|ExportNoiseShaperTest|ExportQualityControlsTest|AudioFileOggInitTest|RenderSoftwareTag|StemExportTest|SurroundExportTest|ControlRenderJobTest|MasteringTest|ClipOffsetFadeTest|ClipFadesRenderTest|AudioClipLoopTest|SessionPlaybackRenderTest|SessionSchedulerRenderTest|LoudnessReportTest|LufsMeterTest)$' --output-on-failure
100% tests passed out of 18
```

Gates — all eight rows PASS, EXIT=0 (`gate-r2b.log`):

```
python3 <unmodified feature_check.py with only the log dir redirected> $PWD
  PASS  fork-sources / all-sources-reproduce / no-upstream-regression / check-namespace
  PASS  no-tautology / doc-figures / complexity / file-length
RESULT: PASS
```

`tests/all-sources.txt` also had to be re-derived from its own published recipe:
train commit `07d6d02bd` regenerated it with a broader command and it carried
1148 vendored `plugins/SfzPlayer/sfizz/external/**` paths the recipe excludes on
purpose, so `fork-sources` and `all-sources-reproduce` were already red on the
pristine tree (proved with the manifest reverted: `allsrc-base.log`, EXIT=1, the
identical 1148-line diff). The re-derivation is the recipe's byte-exact output
(`allsrc-rederived-2.log`: `REPRODUCES`); no gate limit or baseline is changed.
This follows the precedents `1a3e91e36` / `a71fcd6f1` for the same drift.

## Self-check

* **The regression test FAILS with the product change reverted and PASSES with
  it (exit codes).** The full reversal: build EXIT=0, ctest EXIT=8, 9 failing
  assertions across 3 tests with 0 skipped (`test-red-final3.log`); each rework
  item reverted alone also fails its own new case (the per-item logs above,
  including both OGG cases in `test-r2-redogg3.log`); restored: build EXIT=0,
  ctest EXIT=0, 3/3 tests, 5/5 + 9/9 cases, 0 skipped (`test-green-final3.log`).
  The RED is a runtime failure, not a compile error.
* **The test is deterministic: no wall-clock timing, sleeps or races.** The seam
  is the process file-size cap (`RLIMIT_FSIZE`) with `SIGXFSZ` set to `SIG_IGN`
  (a real `EFBIG`/short write, decided by the kernel synchronously), plus one
  read-only file whose reopen is refused for the mask patch — with a precondition
  that skips (never silently passes) where the process can write through it — and,
  for the OGG page cases, a one-shot injected short count consumed by exactly one
  page. No sleeps, no threads of the test's own, no timing thresholds; the render
  case quits on `RenderManager::finished()` through a `QEventLoop`, not on a timer.
* **The fix is at the cause and complete: every caller and sibling checked.** The
  only `writeBuffer()` caller is `ProjectRenderer::run()`
  (`src/core/ProjectRenderer.cpp:311-315`); `finishEncoding()` is called by the
  render before it reads the verdict and (idempotently) by each destructor. All
  four `AudioFileDevice` subclasses return a result, record a failure and expose
  it through the one base flag; OGG and MP3 no longer do their real finalisation
  only in a destructor. `writeData()`'s buffered contract is handled by
  `flushOutputFile()` in every finalisation.
* **No collateral damage.** A healthy render takes the same write path and is
  byte-identical: `ExportDitherTest` (byte-level off-by-default assertions),
  `ExportWavDitherTest`, `RenderSoftwareTag` and the six-layout
  `SurroundExportTest::aDiscreteMasterWritesItsChannelCountWithoutSpeakerMask`
  all pass in the family run. The header check fails only when a size field is
  smaller than the frames libsndfile reported writing, so a legitimate file
  cannot trip it; no saved-data format changed.
* **Undo and commands.** Not an undoable edit; no `ControlCommands*` file is
  touched, so no command, reversibility class or `commands_snapshot.json` canary
  moved. No new control command was added.
* **Realtime.** The changed code runs on the offline render thread
  (`ProjectRenderer::run()`), on the file device it drives, and in finalisation —
  never in `process()`/`play()` or a mixer callback. `writeBuffer()` adds a
  counter and a comparison; `finishEncoding()`'s file reads and flushes run once
  after the loop, not per period.
* **Old projects still load and render as before.** The family's
  `SessionPlaybackRenderTest` / `SessionSchedulerRenderTest` / `AudioClipLoopTest`
  / `ClipFadesRenderTest` / `ClipOffsetFadeTest` pass (18/18), and the healthy
  `zene render` probe (round 1, `cli-healthy.log`) wrote a 2720864-byte WAV with
  exit 0.
* **Verdict `ALREADY-FIXED`/`FALSE-POSITIVE` evidence.** Not applicable — the
  verdict is FIXED; the REAL section shows the base code discarding every
  write/close result.
* **Qt 5.15 / MSVC / macOS portability.** `QFile`, `QFileInfo`, `QtEndian`,
  `std::atomic` only - no Qt-6-only API, no `windows.h` macro names, no
  `std::filesystem`. The RLIMIT cases and the file permissions are guarded by
  `#ifdef Q_OS_UNIX`.

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
tests/src/core/AudioFileOggInitTest.cpp
tests/src/core/ExportWavDitherTest.cpp
tests/src/core/ExportWavWriteErrorTest.cpp
docs/qwen-fixes-2026-10/cluster-new-p12-317-port.md
```

`tests/upstream-modifications.txt` declares every inherited file above (the new
test is in `tests/fork-sources.txt` and `tests/all-sources.txt`).

Post-commit checks, after the round-2 commit (`2e8717a5e`): `feature_check.py` —
RESULT: PASS, 8/8 rows (EXIT=0, `gate-post-r2.log`); `submit_check.py <worktree>
new-p12-317-port` — RESULT: PASS - submit (EXIT=0, `submit-r2.log`);
`git status --short` empty. (Round 1's checks were `gate-post-commit.log` and
`submit-rework.log`, both PASS.)

FIXED
