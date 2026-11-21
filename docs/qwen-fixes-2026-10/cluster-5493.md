ALREADY-FIXED

# Cluster 5493: a failed full-track render must not clobber an existing output

Verdict: **ALREADY-FIXED** in ancestor commit `f6498a18f3` ("fix(render-jobs): keep
bounce rendering off the UI thread (cluster 917)"). This job changes **no product
code**; it only moves/refreshes the lock-in test and this report. `git diff --
src/core/BounceInPlace.cpp` is empty (log 30).

## Why this is already fixed

The render child is never pointed at the caller's destination. `prepareRenderTracks`
mints a sibling staging path carrying a UUID and hands *that* to the child:

- `src/core/BounceInPlace.cpp:377-379` - `next.stagedPath = outPath + ".part." +
  QUuid::... + ".wav"`, then `next.renderPath = next.trimmed ?
  scratch/render.wav : next.stagedPath`. For a whole-track bounce the child writes
  only the `.part.` file (`:379`).
- `src/core/BounceInPlace.cpp:421-449` - `installPreparedOutput` preserves an existing
  destination by renaming it to `<out>.backup.<uuid>` first (`:429-438`), renames the
  staged file into place (`:440`), and renames the backup back on failure (`:442`).
- `src/core/BounceInPlace.cpp:471-477` - `discardPreparedRender` removes the staged
  file when it was not installed, and removes the scratch dir. A failed render leaves
  the caller's destination alone.
- `src/core/BounceInPlace.cpp:490-503` - `renderTracks`: on `renderFailed(finished,
  exitCode, prepared.renderPath)` (`:499`) it discards (`:501`) and returns `"the
  render failed (exit %1)"` (`:502`). The single classifier is at `:273`:
  `!finished || exitCode != 0 || !rendered.exists() || rendered.size() == 0`.

### Historical evidence (the bug that was fixed)

Before `f6498a18f3`, `git show f6498a18f3^:src/core/BounceInPlace.cpp` line 354 was:

```cpp
const QString renderTarget = trimmed ? tempPath(QStringLiteral("wav")) : outPath;
```

i.e. a whole-track render wrote **directly to the caller's output path**, so a child
that died part-way left its partial bytes at the destination. The ancestor check
returned `ANCESTOR_EXIT=0` (logs/01-evidence.log:2). Commit `f6498a18f3` introduced the
staged path above. No ancestor commit in this job's history modifies it: `git log -S
'next.stagedPath'` and `-S 'installPreparedOutput'` both report only `f6498a18f3`
(log 30).

### Callers and siblings (re-checked by grep in the worktree)

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

## Lock-in test (current location)

The output-preservation pair lives in `tests/src/core/BounceInPlaceTest.cpp:144-177`
(moved there by `f403e2541a`; the old `ControlBounceRenderTest.cpp` slots no longer
exist):

- `failedBounceKeepsAnExistingDestination()` at `:144-162` - the fake child writes
  `"partial render"` to its output and exits 3 (`ZENE_FAKE_RENDER_PARTIAL`). The
  destination is pre-seeded with `"previous output"`; the test asserts `Refused`, a
  `"render failed"` message, an unchanged track count, **the destination still reads
  `"previous output"` at `:160`**, and no `existing.wav.part.*` debris at `:161`.
- `successfulBounceReplacesAnExistingDestination()` at `:164-177` - the positive
  control: a real successful bounce replaces the pre-seeded destination at `:175` and
  leaves no `replace.wav.part.*` debris.

## RED/GREEN evidence (this job)

`LOGS` = `/home/kruzzzzy/.config/kads/zene/state/runs/5493.r1/logs`. All commands ran
from the worktree root, except ctest which ran against `build/tests`.

| Log | Exact command | Result |
| --- | --- | --- |
| `18-build-baseline.log` | `cmake --build build --target BounceInPlaceTest` | EXIT=0, target built |
| `19-test-baseline.log` | `ctest -R BounceInPlaceTest -V` | **EXIT=0**, 10 passed (1/1 CTest target) |
| `20-build-red.log` | `cmake --build build --target BounceInPlaceTest` | EXIT=0 (rebuilt after reversal) |
| `21-test-red.log` | `ctest -R BounceInPlaceTest --output-on-failure` | **RED_EXIT=8**, 8 passed / 2 failed |
| `23-build-green.log` | `cmake --build build --target BounceInPlaceTest` | EXIT=0 |
| `24-test-green.log` | `ctest -R BounceInPlaceTest -V` | **GREEN_EXIT=0**, `Totals: 10 passed, 0 failed`, 100% out of 1 |
| `25-family-test.log` | `ctest` on the 8-test bounce/freeze family | **FAMILY_EXIT=0**, 8/8, 100% out of 8 |
| `26-build-zene.log` | `cmake --build build --target zene` | EXIT=0, `Built target zene` |
| `30-feature-check.log` | `python3 .../feature_check.py` | **RESULT: PASS**, EXIT=0 |
| `31-submit-check.log` | `python3 .../submit_check.py <tree> 5493` | **RESULT: PASS - submit**, EXIT=0 |

RED provenance: the failure was produced by the one-line historical reversal in
`logs/fix.patch`, which changes `src/core/BounceInPlace.cpp:379` back to
`next.renderPath = ... : outPath;` (direct write). Under it, log 21 shows
`failedBounceKeepsAnExistingDestination` failing at `BounceInPlaceTest.cpp:160`, actual
`"partial render"` vs expected `"previous output"`, plus the expected positive-control
collateral failure `successfulBounceReplacesAnExistingDestination` at `:173`
(`"the render produced no audio"`). The patch was reverted; `git diff --
src/core/BounceInPlace.cpp` is empty and the worktree is clean, so **no product file
remains changed**.

## Test matrix

| Case | Coverage |
| --- | --- |
| Normal positive control | `successfulBounceReplacesAnExistingDestination` (`BounceInPlaceTest.cpp:164-177`): a real child writes a full WAV; destination is replaced at `:175`, frame count > 0, no staging debris. PASS in logs 19/24. |
| Child exits nonzero after a partial write | `failedBounceKeepsAnExistingDestination` (`:144-162`): child returns 3 with `"partial render"` written; destination bytes unchanged at `:160`, no `.part.*` file at `:161`. PASS in 24; FAILs at `:160` in RED 21. |
| Timeout / kill | Same branch: `renderFailed(finished, exitCode, renderTarget)` (`src/core/BounceInPlace.cpp:273`, called at `:499`) is the single classifier for "did not finish" and "nonzero exit"; the discard path at `:501` is shared. Not separately timed (no sleeps, no wall-clock assertion). |
| Existing-destination reconfiguration | Both slots pre-seed the destination at the same path and then run `bounce.land` to that path, proving the staging rename is reconfiguration-safe rather than append-only. |
| Audio thread | n/a: the render runs out of process through `runCliRender` (QProcess); no audio callback is entered, so no realtime-safety claim is needed. |
| Portability | No POSIX calls. The test uses `QFile`, `QDir`, `QTemporaryDir`, `QFileInfo`, `QProcess` (via the product's `runCliRender`) and `QUuid` (staging/backup names); behaviour is identical on Linux/macOS/Windows. |

Registration is unchanged: `BounceInPlaceTest` is already a fork source
(`tests/fork-sources.txt`) and is already registered in `tests/CMakeLists.txt`. No
CMake or fork-source edit was needed.

## Self-check (the six ASKs from log 31)

1. **The regression test FAILS with the product change reverted and PASSES with it
   (exit codes).** RED: `21-test-red.log` **RED_EXIT=8** (`ctest -R BounceInPlaceTest
   --output-on-failure`), 8 passed / 2 failed; targeted failure
   `failedBounceKeepsAnExistingDestination` at `BounceInPlaceTest.cpp:160`, actual
   `"partial render"` vs expected `"previous output"`, plus positive-control collateral
   at `:173`. GREEN: `24-test-green.log` **GREEN_EXIT=0**, `Totals: 10 passed, 0
   failed`, 100% out of 1. The real fix `f6498a18f3` predates this job, so the reversed
   change is the one-line historical reversal saved in `logs/fix.patch`
   (`next.renderPath = ... : outPath;`); it was reverted and `git diff --
   src/core/BounceInPlace.cpp` is empty (log 30).
2. **The test is deterministic; no wall-clock timing, sleeps or races.** The seam is
   the `ZENE_FAKE_RENDER_PARTIAL` environment variable consumed by the stand-in child:
   it writes the fixed bytes `"partial render"` and exits 3. The assertion at `:160`
   is an exact byte comparison of the destination; there are no sleeps, timeouts based
   on wall clock, or timing windows (`19`/`24` repeat identically).
3. **The fix is at the cause and complete; every caller and sibling stays consistent.**
   No product change is made in this job. The completeness walk covers every caller
   and sibling listed above: they all flow through `renderTracks` /
   `prepareRenderTracks`, the shared staged path at `BounceInPlace.cpp:377-379`, the
   backup/restore in `installPreparedOutput` at `:421-449`, `discardPreparedRender` at
   `:471-477`, and the single classifier/discard in `renderTracks` at `:490-503`.
4. **No collateral damage.** Only `tests/src/core/BounceInPlaceTest.cpp`,
   `tests/src/core/ControlBounceRenderTest.cpp` and docs changed in this cluster
   (log 32); no project/data format, product behaviour or new limit changed, so old
   projects load as before. The positive control
   `successfulBounceReplacesAnExistingDestination` passes in `19`/`24`, and the 8-test
   family passes 8/8 in `25`.
5. **ALREADY-FIXED evidence.** The staging/install/discard code is from ancestor
   `f6498a18f3` (confirmed by `git log -S` in log 30), at current lines
   `BounceInPlace.cpp:377-379`, `:421-449`, `:471-477`, `:490-503`, classifier at
   `:273`; the lock-in test proves the guarantee at `BounceInPlaceTest.cpp:144-177`
   (assertion `:160`).
6. **Sibling commands in the command files the diff touched.** Cluster 5493 touches
   no `ControlCommands*` / app / arrangement command file: `git show --stat` for
   `47a6ee64f3`, `6f4f428d71` and `f403e2541a` shows only
   `tests/src/core/ControlBounceRenderTest.cpp`, `tests/src/core/BounceInPlaceTest.cpp`
   and this report (log 32). The checker's `app.*` / `arrangement.*` list
   (`app.about` ... `arrangement.get_state`) comes from the branch-vs-base train
   content, not from this cluster; those commands are untouched and unaffected.

## Gate and completion evidence

- `30-feature-check.log`: all gates PASS, `RESULT: PASS`, EXIT=0.
- `31-submit-check.log`: `RESULT: PASS - submit`, EXIT=0; the "tests added or
  changed" line prints only the first three branch-wide files, so
  `BounceInPlaceTest.cpp` being absent there is not evidence it is missing.
- `32-cluster-commits.log`: the three cluster commits touch only `tests/` and `docs/`.
- `34-submit-check-final.log`: rerun after the report commit, `RESULT: PASS`, EXIT=0;
  `35-status.log` is empty, proving the tree is clean.

## Other findings

- Under the RED reversal, the positive control also failed (`:173`, `"the render
  produced no audio"`) because pointing the child at the destination bypasses staging;
  that collateral disappeared on revert (24: 10/10 PASS).
- The file-length gate is satisfied: the lock-in test was de-duplicated in
  `6f4f428d71` and then moved to `BounceInPlaceTest.cpp` in `f403e2541a`; no slot,
  assertion or fixture was removed.
