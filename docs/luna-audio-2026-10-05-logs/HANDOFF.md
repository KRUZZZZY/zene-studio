# Audio lifetime lane: owner-deferred WIP

Deferred on the owner's steering on 2026-10-05. No production fix was implemented,
no regression was executed, and no success is claimed. Baseline: `7796ff6b8`.
Branch: `bugs/luna-audio-2026-10-05`; worktree: `zene-fix-audio`.

The only code edit is `tests/src/core/ArrangingCatchUpTest.cpp`, already registered
in `tests/fork-sources.txt`. Two new slots are uncompiled/unexecuted at handoff:

- `queuedInstrumentHandlesAreRemovedBeforeTheirTrackCanDie` holds the model guard,
  adds two counted real `InstrumentPlayHandle`s on real tripleoscillator tracks,
  checks type and track filtering, and expects the selected staged handle to be
  destroyed before a render. It cleans up baseline survivors before asserting.
- `switchingMonitoringOffRemovesAHandleQueuedInTheSamePeriod` enables/disables a
  real sample track's monitor under that guard, renders, and checks that the old
  handle did not enter the active list; it cleans up a survivor before asserting.

## Verified ownership facts and remaining hypothesis

`ControlCommandsDocuments.cpp:93` creates the live document's Bass with `lb302`.
`Lb302.cpp:245` queues an instrument handle; `Lb302Synth` has no explicit destructor.
`InstrumentTrack::~InstrumentTrack` calls `silenceAllNotes(true)` before deleting
the instrument. `AudioEngine::removePlayHandlesOfTypes` sweeps active handles but
its staged sweep selects only NotePlayHandles (`AudioEngine.cpp:852`). The staged
LB302 handle can therefore reach the next render after instrument deletion. This
matches the parent's repeated crash in `InstrumentPlayHandle::play`, but the new
deterministic regression still needs execution before claiming the root cause.

Simply broadening the staged sweep needs careful ownership verification:
`InstrumentPlayHandle::isFromTrack` dereferences its instrument. The migration
harness directly deletes instantiated LB302 instruments before deleting their
tracks (`PluginPortsMigrationTest.cpp`); the reference LB302 likewise has no
destructor cleanup. Matching against a freed instrument can explain the historical
failure attributed to a double free by commit `6f5894cba`. Capturing track identity
in the handle and early LB302 destructor cleanup were considered, not implemented.
The same staged exclusion also affects `SampleTrack` monitor shutdown, motivating
the second regression. Other handle types and realtime safety remain unverified.

## Existing build: keep slot A reserved until it completes

Configure completed with unpiped exit **0**. SDK source checkouts were copied from
`../zene-luna-r7/build/{clap,vst3sdk}` into this worktree's own `build/`; no build
binaries are shared. Configuration uses RelWithDebInfo, Qt6, Werror, official
uarch, compile cache, debug CPack, stem split, CLAP, VST3 and the VST3 test fixture,
with `CMAKE_EXE_LINKER_FLAGS=-Wl,--strip-debug` and the task's dependency pkgconfig
path. Configure output is `/tmp/zene-audio-configure.log`.

The existing targeted build continues; do not start another lane in its CPU slot
until it exits. At handoff it was at 86% of core compilation, with no reported
compile failure. Unified exec session: **14298**; shell PID **1353121**; cmake PID
**1353128**. Exact command, run from this worktree:

```sh
cmake --build build --target zene ArrangingCatchUpTest PluginPortsMigrationTest SessionTuningTest Vst3InstrumentIntegrationTest -j12 > /tmp/zene-audio-baseline-build.log 2>&1; echo EXIT=$?
```

The command has not returned, so its exit code is unknown. No ctest, full suite or
gates were run by this lane. The parent owns integration and any later validation.
The prior parent reproduction is `/tmp/zene-luna-arranging-repro.log`, with durable
evidence in baseline `docs/luna-r7-2026-10-05-logs/full-gates.md`.
