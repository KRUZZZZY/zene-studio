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
that died part-way left its partial bytes at the destination. The command was run on
the merge ancestor and returned cleanly: `ANCESTOR_EXIT=0` (logs/01-evidence.log:2).
Commit `f6498a18f3` introduced the staged path above.

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

- `failedBounceKeepsAnExistingDestination()` at `:233-256` - the fake child writes
  `"partial render"` to its output and exits 3 (`ZENE_FAKE_RENDER_PARTIAL`). The
  destination is pre-seeded with `"previous output"`; the test asserts `Refused`,
  a `"render failed"` message, an unchanged track count, **the destination still
  reads `"previous output"` at `:253`**, and no `existing.wav.part.*` debris at `:255`.
- `successfulBounceReplacesAnExistingDestination()` at `:259-276` - the positive
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
| `08-product-diff.log` | product-diff check | **EXIT=0**, log is **empty (0 bytes)**: no product change |

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
| Normal positive control | `successfulBounceReplacesAnExistingDestination` (`:259-276`): real child writes a full WAV; destination is replaced, frame count matches, no staging debris. PASS in 03/07. |
| Child exits nonzero after a partial write | `failedBounceKeepsAnExistingDestination` (`:233-256`): child returns 3 with `"partial render"` written; destination bytes unchanged at `:253`, no `.part.*` file. PASS in 03/07, FAILs at `:253` in RED 05. |
| Timeout / kill | Same branch: `renderFailed(finished, exitCode, renderTarget)` (`src/core/BounceInPlace.cpp:273`, called at `:499`) is the single classifier for "did not finish" and "nonzero exit"; the discard path at `:501` is shared. Not separately timed (no sleeps, no wall-clock assertion). |
| Existing-destination reconfiguration | Both slots pre-seed the destination at the same path and then run `bounce.land` to that path, proving the staging rename is reconfiguration-safe rather than append-only. |
| Audio thread | n/a: the render runs out of process through `runCliRender` (QProcess); no audio callback is entered, so no realtime-safety claim is needed. |
| Portability | No POSIX calls. The test uses `QFile`, `QDir`, `QTemporaryDir`, `QFileInfo`, `QProcess` (via the product's `runCliRender`) and `QUuid` (staging/backup names); behaviour is identical on Linux/macOS/Windows. |

Registration is unchanged because `ControlBounceRenderTest` is already a fork source:
`tests/fork-sources.txt:2756` and `tests/CMakeLists.txt:148`. No CMake or fork-source
edit was needed.

## Self-check (the five non-feature ASKs)

1. **Build/registration** - the test target already exists; `tests/fork-sources.txt:2756`
   lists `tests/src/core/ControlBounceRenderTest.cpp` and `tests/CMakeLists.txt:148`
   builds it. 02/06 built it and 03/07 ran it, so no registration change is required.
2. **Portability** - only QtCore/QtXml/QtTest and libsndfile; `QFile`/`QDir`/
   `QTemporaryDir` for the destination, `QUuid` for staging names, `QProcess` for the
   child. No platform-specific path or process API.
3. **Realtime/audio thread** - n/a by construction: `runCliRender` starts a separate
   process, and `installPreparedOutput` runs on the control thread. The test asserts
   file bytes, never timing.
4. **Timeout/kill** - both map to `renderFailed`'s "not finished or nonzero exit"
   branch (`BounceInPlace.cpp:273`, used at `:499`), which discards the staged file
   at `:501`. The test drives the nonzero-exit arm; the timeout arm needs no separate
   timing test.
5. **No collateral product change** - `08-product-diff.log` is empty and `git diff
   --stat` lists only `tests/src/core/ControlBounceRenderTest.cpp`; the RED mutation
   exists only in `logs/red-mutation.patch` and was reverted.

## Other findings

- Under the RED mutation, three additional existing slots also failed
  (`successfulBounceReplacesAnExistingDestination` `:270`, `bounceLand...` `:288`,
  `flatten...` `:412`, all `"the render produced no audio"`). This is expected blast
  radius: pointing the child at the destination bypasses staging entirely, so the
  harness never sees a staged file. Only the `:253` byte comparison is the targeted
  assertion; the others are collateral and disappeared on revert (07: 16/16 PASS).
- `logs/12-build-family.log` exists from the p3 family build but was not needed for
  this report; the family result is taken from 10 and 11.
