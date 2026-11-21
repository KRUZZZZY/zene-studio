ALREADY-FIXED

# Cluster 5493: a failed full-track render must not clobber an existing output

Verdict: **ALREADY-FIXED** in commit `f6498a18f3` ("fix(render-jobs): keep bounce
rendering off the UI thread (cluster 917)"). The only change in this round is a new
pair of lock-in tests; no product file is touched.

## Why this is already fixed

The render child is never pointed at the caller's destination. `prepareRenderTracks`
mints a sibling staging path carrying a UUID and hands *that* to the child:

- `src/core/BounceInPlace.cpp:377-379` - `next.stagedPath = outPath + ".part."
  + QUuid::... + ".wav"`, then `next.renderPath = next.trimmed ? scratch/render.wav :
  next.stagedPath`. For a whole-track bounce the child writes only the `.part.` file.
- `src/core/BounceInPlace.cpp:421-449` - `installPreparedOutput` preserves an existing
  destination by renaming it to `<out>.backup.<uuid>` first (`:431-438`), renames the
  staged file into place (`:440`), and renames the backup back on failure (`:442`).
- `src/core/BounceInPlace.cpp:471-477` - `discardPreparedRender` removes the staged
  file when it was not installed, and removes the scratch dir. A failed render leaves
  the caller's destination alone.
- `src/core/BounceInPlace.cpp:490-519` - `renderTracks`: on `renderFailed(finished,
  exitCode, prepared.renderPath)` it discards and returns `"the render failed (exit
  %1)"` (`:499-503`); `finishPreparedRender` and `installPreparedOutput` failures
  discard as well (`:506-515`). The installed output is committed and the staging
  debris discarded on success (`:516-517`).

### Historical evidence (the bug that was fixed)

Before `f6498a18f3`, `git show f6498a18f3^:src/core/BounceInPlace.cpp` line 354 was:

```cpp
const QString renderTarget = trimmed ? tempPath(QStringLiteral("wav")) : outPath;
```

i.e. a whole-track render wrote **directly to the caller's output path**, so a child
that died part-way left its partial bytes at the destination. The ancestor check
returned 0: `ANCESTOR_EXIT=0` (logs/01-evidence.log:2). Commit `f6498a18f3`
introduced the staged path above.

### Callers and siblings (from p1, re-checked by grep in the worktree)

Callers of the staged-render API:

- `src/core/ControlCommandsBounceLand.cpp:68` - `renderTrack` (`bounce.land`)
- `src/core/ControlCommandsFreeze.cpp:147,201` - `renderTrack` (freeze / unmute)
- `src/core/ControlCommandsTrackFlatten.cpp:110` - `renderTrack` (`track.flatten`)
- `src/core/ControlCommandsRangeBounce.cpp:101` - `renderTracks` (`range.bounce`)
- `src/core/ControlCommandsRenderBounce.cpp:464` - `prepareRenderTracks`, discards at
  `:470` and `:521`
- `src/core/ControlCommandsFreezeBatch.cpp:118` - `prepareRenderTracks`, discard at `:94`
- `src/core/BatchFreeze.cpp:110,338` - `installPreparedOutput` / `finishPreparedRender`,
  discards at `:250,423`
- `src/core/MixerBusFreeze.cpp:335,338,343` - install then discard
- `src/core/RenderQueueBounce.cpp:210,223,228,246,247,258,276` - finish / discard / install
- `src/core/RenderQueue.cpp:453` - discard on queue teardown
- `tests/src/core/FreezeRegionRepairTest.cpp:277,281` - `prepareRenderTracks` / discard

Sibling entry points unchanged by this cluster: `prepareRenderTracks`,
`renderArguments`, `finishPreparedRender`, `installPreparedOutput`,
`commitPreparedOutput`, `rollbackPreparedOutput`, `discardPreparedRender`,
`renderTrack` (both overloads), `renderTracks`, `trimWav`, `namesTheSameFile`,
`isAbsoluteOutputPath`. All go through the same staged-path and backup/install code,
so the failure guarantee covers them uniformly.

## Lock-in test (this round)

`tests/src/core/ControlBounceRenderTest.cpp` gains two slots:

- `failedBounceKeepsAnExistingDestination()` at `:239-256` - the fake child writes
  `"partial render"` to its output and exits 3 (`ZENE_FAKE_RENDER_PARTIAL`). The
  destination is pre-seeded with `"previous output"`; the test asserts `Refused`,
  a `"render failed"` message, an unchanged track count, **the destination still
  reads `"previous output"` at `:254`**, and no `existing.wav.part.*` debris at `:255`.
- `successfulBounceReplacesAnExistingDestination()` at `:259-271` - the positive
  control: a real successful bounce replaces the pre-seeded destination, produces the
  expected frame count, and leaves no `replace.wav.part.*` debris.

### Baseline, RED and GREEN (logs 02-08)

`LOGS` = `/home/kruzzzzy/.config/kads/zene/state/runs/5493.r1/logs`. All commands ran
from the worktree root, except ctest which ran against `build/tests`.

| Log | Exact command | Result |
| --- | --- | --- |
| `02-build-green.log` | `cmake --build build --target ControlBounceRenderTest` | **EXIT=0**, `Built target ControlBounceRenderTest` |
| `03-test-green-initial.log` | `ctest -R ControlBounceRenderTest -V` | **EXIT=0**, `16 passed, 0 failed`, both new slots PASS |
| `04-build-red.log` | `cmake --build build --target ControlBounceRenderTest` | **EXIT=0** (rebuilt `lmmsobjs` only) |
| `05-test-red.log` | `ctest -R ControlBounceRenderTest --output-on-failure` | **EXIT=8**, `12 passed, 4 failed`; key failure: `failedBounceKeepsAnExistingDestination` actual `"partial render"` vs expected `"previous output"`, `Loc: [ControlBounceRenderTest.cpp(253)]` |
| `06-build-green-final.log` | `cmake --build build --target ControlBounceRenderTest` | **EXIT=0** |
| `07-test-green-final.log` | `ctest -R ControlBounceRenderTest -V` | **EXIT=0**, `16 passed, 0 failed` (1/1 CTest target) |
| `red-mutation.patch` | `git diff -- src/core/BounceInPlace.cpp > $LOGS/red-mutation.patch; echo EXIT=$?` | **EXIT=0**; patch records the one-line `:379` mutation |
| - | `git apply -R $LOGS/red-mutation.patch; echo REVERSE_EXIT=$?` | **REVERSE_EXIT=0**; mutation reverted |
| `08-product-diff.log` | `git diff -- src/core/BounceInPlace.cpp > $LOGS/08-product-diff.log 2>&1; echo EXIT=$?` | **EXIT=0**, log is **empty (0 bytes)**: no product change |

RED provenance: the failure was produced by a temporary one-line reintroduction of
the old direct write at `src/core/BounceInPlace.cpp:379`
(`next.renderPath = ... : outPath;`), recorded verbatim in
`logs/red-mutation.patch`. The patch was then reverted; the log is empty and the
worktree shows only `tests/src/core/ControlBounceRenderTest.cpp` modified, so **no
product file remains changed**.

## p3 result (zene and the family)

- `09-build-zene.log` - `zene` built: `Linking CXX executable ../zene`,
  `Built target zene`, **EXIT=0**.
- `10-family-list.log` - the family lists **8 tests** (`Total Tests: 8`):
  BounceInPlaceTest, ControlBounceSupportTest, ControlBounceRenderTest,
  ControlRenderJobTest, BatchFreezeTest, RenderSourceGuardTest,
  FreezeRegionRepairTest, MixerBusFreezeTest.
- `11-family-test.log` - **8/8 passed**, `100% tests passed out of 8`, 14.82 s.

## Test matrix

| Case | Coverage |
| --- | --- |
| Normal positive control | `successfulBounceReplacesAnExistingDestination` (`:259-271`): real child writes a full WAV; destination is replaced, frame count matches, no staging debris. PASS in 03/07. |
| Child exits nonzero after a partial write | `failedBounceKeepsAnExistingDestination` (`:239-256`): child returns 3 with `"partial render"` written; destination bytes unchanged at `:254`, no `.part.*` file. PASS in 03/07, FAILs at `:254` in RED 05. |
| Timeout / kill | Same branch: `renderFailed(finished, exitCode, renderTarget)` (`src/core/BounceInPlace.cpp:273`, called at `:499`) is the single classifier for "did not finish" and "nonzero exit"; the discard path at `:501` is shared. Not separately timed (no sleeps, no wall-clock assertion). |
| Existing-destination reconfiguration | Both slots pre-seed the destination at the same path and then run `bounce.land` to that path, proving the staging rename is reconfiguration-safe rather than append-only. |
| Audio thread | n/a: the render runs out of process through `runCliRender` (QProcess); no audio callback is entered, so no realtime-safety claim is needed. |
| Portability | No POSIX calls. The test uses `QFile`, `QDir`, `QTemporaryDir`, `QFileInfo`, `QProcess` (via the product's `runCliRender`) and `QUuid` (staging/backup names); behaviour is identical on Linux/macOS/Windows. |

Registration is unchanged because `ControlBounceRenderTest` is already a fork source:
`tests/fork-sources.txt:2756` and `tests/CMakeLists.txt:158`. No CMake or fork-source
edit was needed.

## Self-check (the non-feature ASKs)

1. **The regression test FAILS with your product change reverted and PASSES with it
   (exit codes).** RED: `05-test-red.log` **RED_EXIT=8**, `12 passed, 4 failed`; the
   targeted failure is `failedBounceKeepsAnExistingDestination` at
   `ControlBounceRenderTest.cpp(253)`, actual `"partial render"` vs expected
   `"previous output"`. GREEN: `07-test-green-final.log` **GREEN_EXIT=0**,
   `16 passed, 0 failed`, `100% tests passed out of 1`. The real fix
   `f6498a18f3` predates this job, so the reversed product change is the one-line
   `:379` mutation recorded in `logs/red-mutation.patch`
   (`next.renderPath = ... : outPath;`), reverted with `git apply -R`
   (**REVERSE_EXIT=0**); `08-product-diff.log` is empty (0 bytes).
2. **The test is deterministic; no flakiness or timing dependence.** The seam is
   `ZENE_FAKE_RENDER_PARTIAL` in the stand-in child: it writes the fixed bytes
   `"partial render"` and exits 3. The assertion is an exact byte comparison of the
   destination; there are no sleeps, timing windows, or races.
3. **The fix is at the cause and complete; siblings and callers are covered.** No
   product change is made in this round. Every caller and sibling listed in
   "Callers and siblings" above flows through `renderTracks`/`prepareRenderTracks`
   and the shared install/discard helpers: staged path at `BounceInPlace.cpp:377-379`,
   `installPreparedOutput` backup/restore at `:421-449`, `discardPreparedRender` at
   `:471-477`, and the single failure classifier/discard in `renderTracks` at
   `:490-519`.
4. **No collateral damage.** The change is test-only: no project or data-format
   change, no product-behaviour change, no new limit, and existing projects load as
   before. The positive control `successfulBounceReplacesAnExistingDestination`
   (`:259-276`) shows a normal bounce still replaces an existing destination and
   leaves no staging debris (PASS in 03/07).
5. **If ALREADY-FIXED, the evidence.** `prepareRenderTracks` stages the child output
   at `BounceInPlace.cpp:377-379`, and `renderTracks` discards on failure at
   `:490-519`, leaving the caller's destination intact. Historically the direct-write
   line `f6498a18f3^:354` wrote to `outPath`; the ancestor check returned
   `ANCESTOR_EXIT=0` (`logs/01-evidence.log:2`), and `f6498a18f3` introduced staging.

6. **Sibling commands in the command files the diff touched.** This cluster changes no
   command file: its commits (`47a6ee64f3`, the test de-duplication commit and
   `069a0428f6`) touch only `tests/src/core/ControlBounceRenderTest.cpp` and this report.
   The `app.*` and `arrangement.*` siblings in the ASK belong to
   `src/core/ControlCommands*.cpp` changes already on the train side of the merge-base
   (`fa4a5f2e785`), not to cluster 5493; the test-only diff leaves all of them
   unchanged and consistent.

## p5 gate and completion evidence

- The two new slots shifted after the test de-duplication below; their current lines are
  `:239-256` and `:259-271`, and `ctest -R ControlBounceRenderTest` still reports
  **100% tests passed out of 1** (exit 0, `003-5493-p5-test.log`).
- The file-length gate flagged this new test file at 515 code lines (p5). The fix is a
  behaviour-preserving de-duplication: two shared file helpers (`writeFile`,
  `readFile`) replace seven open/write/read/close blocks in the same file; no slot,
  assertion or fixture was removed. The file is now at or below 500 code lines and
  `tests/file-length-gate.sh --check` passes.
- `feature_check.py` and `submit_check.py 5493` both end `RESULT: PASS` with `EXIT=0`
  (`logs/14-feature-check.log`, `logs/15-submit-check.log`).

## Other findings

- Under the RED mutation, three additional existing slots also failed
  (`successfulBounceReplacesAnExistingDestination` `:267`, `bounceLand...` `:282`,
  `flatten...` `:401`, all `"the render produced no audio"`). This is expected blast
  radius: pointing the child at the destination bypasses staging entirely, so the
  harness never sees a staged file. Only the `:253` byte comparison is the targeted
  assertion; the others are collateral and disappeared on revert (07: 16/16 PASS).
- `logs/12-build-family.log` exists from the p3 family build but was not needed for
  this report; the family result is taken from 10 and 11.
- The first p3 family run failed only because seven test executables had not been
  built. The targets were built (`12-build-family.log`) and the rerun passed 8/8
  (`11-family-test.log`); the initial failing log was overwritten by that passing
  rerun, so it is no longer on disk.
