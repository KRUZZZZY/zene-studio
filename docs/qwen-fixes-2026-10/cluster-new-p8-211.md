FIXED

# cluster new-p8-211 - adjacent comp segments for the same lane cause an audible dip

## Verdict and evidence

REAL. There are two layers, and both had to be fixed:

1. **Authoring normalisation (round 1, kept).** `comp.rebuild` clamps the composite to the named
   span and then calls `fillGaps()`, which fills each hole with the base lane at slip 0.
   `fillGaps()` did not merge a filled gap into a base-lane segment that touched it, so a
   composite could end up with two contiguous, same-lane, same-slip segments even though
   `mergeAdjacentSegments()` exists for exactly that case (`src/core/TakeLane.cpp:81-103`,
   `continuesTake` at `src/core/TakeLane.cpp:81-86`). `fillGaps()` now merges after filling
   (`src/core/TakeLane.cpp:460-467`).
2. **Render gate coalescing (round 3, the reviewer's point).** The model deliberately keeps two
   same-lane neighbours apart when their slips differ - a different slip is a seam in the take's
   read (`src/core/TakeLane.cpp:81-86`; `docs/COMPING.md:68-70`). Playback snapshotted every
   segment as its own span and `applyComp()` ramped at both edges of every span
   (`src/core/SamplePlayHandle.cpp:406-425`), so those two spans faded out and back in at their
   shared tick although the take's lane never changed - the reported dip. `snapshotComp()` now
   folds contiguous spans of the clip's own lane in place in its fixed `m_compSpans` array
   (`src/core/SamplePlayHandle.cpp:385-403`) before `applyComp()` sees them. Only the gate is
   coalesced: the slipped reads still come from `comp.segments()` through `CompSlipReaders`
   (`src/core/CompSlip.cpp`), so a slip still changes what is read at the seam. Genuine lane
   switches remain holes between runs and keep their 64-frame ramps at the run's outer edges.

Measured evidence (real `SamplePlayHandle`, constant-level take):

- new different-slip test: seam mean `SLIP_SEAM_EVIDENCE ... seam(=112)=0.1885` without the
  coalescing; `0.2500` with it, and the slipped region stays `0.2500` (slip read preserved);
- same-slip rebuild test: `REBUILD_DIP_EVIDENCE at Q(=56) 0.3770` without the model merge
  (round 1), `0.5000` with it.

The coalescing is bounded by `MaxCompSpans` (256), runs only in the handle's construction-time
snapshot, and allocates nothing, takes no lock and logs nothing. `applyComp()` itself is
unchanged in behaviour apart from seeing the coalesced runs.

## Regression tests

`tests/src/tracks/TakeLaneCompPlaybackTest.cpp`:

1. `rebuildMergesFilledGapsIntoTheBaseLaneSoItDoesNotDip()` (round 1, kept): rebuild fills a
   base-lane gap that touches an existing base-lane span; `[0,2Q)` must be one segment and the
   real handle must not dip at Q (`0.3770` before the model fix, `0.5000` after).
2. `adjacentSameLaneDifferentSlipSegmentsDoNotDip()` (round 3, the reviewer's scenario): with
   `makeStepTake` (first second 0.5, second second 0.25) it selects
   `selectSegment(0,2Q,0,2Q)` and `selectSegment(2Q,4Q,0,0)`. Both sides of 2Q read the take's
   0.25, but the model correctly keeps them as 2 segments because their slips differ. The test
   asserts:
   - the model still has 2 segments;
   - the slipped region `[guard, at(Q)-guard)` is `0.25` (the slip is still read);
   - the mean over `[at(2Q)-128, at(2Q)+128)` is `0.25 +/- 1e-4` (`0.1885` with only the
     snapshotComp coalescing reversed).

Test matrix:

- normal path: different-slip same-lane seam plays at full level; same-slip rebuilt gap merges to
  one model span and one gate run.
- failure path: without coalescing the seam reads `0.1885` (the two 64-frame ramps); without the
  model merge the rebuilt gap reads `0.3770`.
- exhaustion: `MaxCompSpans` is unchanged; coalescing only shrinks the run list, so an
  over-budget authoring result is still refused by the existing
  `refusesRebuildAndLaneRemovalBeyondThePlaybackSpanBudget()` (passes in the family run).
- reconfiguration: `selectSegment`, `removeLane` and `loadSettings` already normalise contiguous
  same-slip segments; different-slip neighbours intentionally stay two model segments and are now
  joined only for the gate.
- audio thread: the coalescing loop is in the construction-time snapshot over a fixed array, no
  allocation, lock, wait or log; `applyComp()` is untouched and allocation-free.
- platforms: plain C++20, no platform-specific branch; built and tested on this Linux worktree.

## Commands and exit codes

Round 3 output is in `/home/kruzzzzy/.config/kads/zene/state/runs/new-p8-211.r3/logs/`.

RED (new test added, product code before this round's render fix):

```
cmake --build build --target TakeLaneCompPlaybackTest -j4 > .../logs/build-1-red.log 2>&1
EXIT=0
cd build/tests && ctest -R '^TakeLaneCompPlaybackTest$' --output-on-failure > .../logs/test-1-red.log 2>&1
EXIT=8
```

`test-1-red.log`:

```
SLIP_SEAM_EVIDENCE slipped[0,Q)=0.2500 seam(=112)=0.1885
FAIL!  : TakeLaneCompPlaybackTest::adjacentSameLaneDifferentSlipSegmentsDoNotDip() 'std::fabs(atSeam - 0.25) < 1e-4' returned FALSE. (the same-lane slip seam dips: 0.188477)
Totals: 16 passed, 1 failed, 0 skipped, 0 blacklisted, 3450ms
```

GREEN (coalescing in, every target that includes the changed header rebuilt):

```
cmake --build build --target zene TakeLaneCompPlaybackTest TakeLaneCompTest TakeLaneTest LanesUpconvertTest AudioClipLoopTest AudioEditingTest AudioResamplerRatioTest AutomationRenderAllocationTest ClipEffectsTest ClipFadesRenderTest ClipOffsetFadePairTest ClipOffsetFadeTest ClipPolarityTest FreezeRegionRepairTest FrozenTakeHandoffTest FrozenTakeRoundTwoTest LoopImportWarpTest MixerToolsTest RecordToolsTest SampleClipRubberBandTest SampleClipStretchTest SampleClipTransientRepeatSafetyTest SampleClipTransientRepeatTest SampleClipWarpEditTest SampleClipWarpModesTest SampleClipWarpTest SampleClipWindowTest W3EngineTest -j4 > .../logs/build-2-green.log 2>&1
EXIT=0
cd build/tests && ctest -R '^(TakeLaneCompPlaybackTest|TakeLaneCompTest|TakeLaneTest|LanesUpconvertTest)$' --output-on-failure > .../logs/test-2-green-family.log 2>&1
EXIT=0
QT_QPA_PLATFORM=offscreen ./TakeLaneCompPlaybackTest adjacentSameLaneDifferentSlipSegmentsDoNotDip > .../logs/test-3-green-case.log 2>&1
EXIT=0
```

`test-2-green-family.log`: `100% tests passed out of 4`. `test-3-green-case.log`:
`SLIP_SEAM_EVIDENCE slipped[0,Q)=0.2500 seam(=112)=0.2500`.

Step 4b (reverse only the product/docs changes, keep the tests):

```
git diff -- src/core/SamplePlayHandle.cpp include/SamplePlayHandle.h include/CompSlip.h docs/COMPING.md > .../logs/fix.patch
EXIT=0
git apply -R .../logs/fix.patch
EXIT=0
cmake --build build --target TakeLaneCompPlaybackTest -j4 > .../logs/build-4-red-without-fix.log 2>&1
EXIT=0
cd build/tests && ctest -R '^TakeLaneCompPlaybackTest$' --output-on-failure > .../logs/test-4-red-without-fix.log 2>&1
EXIT=8
git apply .../logs/fix.patch
EXIT=0
cmake --build build --target zene TakeLaneCompPlaybackTest ... (same target list as GREEN) > .../logs/build-5-green-restored.log 2>&1
EXIT=0
cd build/tests && ctest -R '^(TakeLaneCompPlaybackTest|TakeLaneCompTest|TakeLaneTest|LanesUpconvertTest)$' --output-on-failure > .../logs/test-5-green-restored.log 2>&1
EXIT=0
QT_QPA_PLATFORM=offscreen ./TakeLaneCompPlaybackTest adjacentSameLaneDifferentSlipSegmentsDoNotDip > .../logs/test-6-green-case-restored.log 2>&1
EXIT=0
```

`test-4-red-without-fix.log` shows only the new seam test failing (`seam(=112)=0.1885`) while the
same-slip test still passes (`REBUILD_DIP_EVIDENCE ... 0.5000`, model merge stays committed);
`test-6-green-case-restored.log` shows `seam(=112)=0.2500`.

Gates:

```
python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > .../logs/gates-1.log 2>&1
EXIT=0
RESULT: PASS
(fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology,
 doc-figures, complexity, file-length all PASS)
```

Registration: `src/core/TakeLane.cpp` and `include/CompSlip.h` are fork-NEW
(`tests/fork-sources.txt`), so no upstream ledger entry is needed. The changed inherited files -
`src/core/SamplePlayHandle.cpp`, `include/SamplePlayHandle.h`, `docs/COMPING.md` and the
regression test file - gained cluster-specific reasons in `tests/upstream-modifications.txt`.

State sequence (`src/core/TakeLane.cpp` matches the `src/core/Take*` rule, so one is required).
`adjacentSameLaneDifferentSlipSegmentsDoNotDip` drives these comp/read states in order:

1. after `selectSegment(0, 2Q, 0, 2Q)`: one segment `[0,2Q)` lane 0 slip 2Q;
2. after `selectSegment(2Q, 4Q, 0, 0)`: two segments `[0,2Q)` slip 2Q and `[2Q,4Q)` slip 0 (the
   model keeps them apart because the slip differs);
3. render: the gate sees one contiguous run `[0,4Q)`, while the slip readers still read 2Q ahead
   over `[0,2Q)` and the take's own place over `[2Q,4Q)` - the seam is `0.2500` with coalescing
   and `0.1885` without it.

The same sequence is recorded in `logs/proof.json` as the required `state-sequence` artifact; the
round-1 same-slip states (empty -> `[0,Q)` lane 0 -> `[0,Q)`+`[2Q,3Q)` -> merged three-segment
rebuild) are covered by `rebuildMergesFilledGapsIntoTheBaseLaneSoItDoesNotDip()`.

## Completion check

```
python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/submit_check.py <worktree> new-p8-211 > .../logs/submit-1.log 2>&1
EXIT=0
RESULT: PASS - submit
(PASS: commits on the branch, working tree clean, report present, tests changed,
 registration, gates)
```

Every `?` item is answered below.

## Self-check

Answers to every `?` item printed by
`python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/submit_check.py <worktree> new-p8-211`
(saved at `.../logs/submit-1.log`):

- **The regression test FAILS with the product change reverted and PASSES with it (exit codes).**
  Round 3: `git apply -R fix.patch` then build EXIT=0 and
  `ctest -R '^TakeLaneCompPlaybackTest$'` EXIT=8 (`test-4-red-without-fix.log`, seam 0.1885);
  `git apply fix.patch` then build EXIT=0 and the four-test family ctest EXIT=0
  (`test-5-green-restored.log`). The evidence line is
  `SLIP_SEAM_EVIDENCE slipped[0,Q)=0.2500 seam(=112)=0.2500` in
  `test-6-green-case-restored.log` (EXIT=0). The round-1 same-slip test remains red without its
  model fix and green with it.
- **The test is deterministic: no wall-clock timing, sleeps or races.** Both tests read rendered
  sample values from the real `SamplePlayHandle` at frame indices computed from ticks with
  `Engine::framesPerTick(m_rate)`; there is no timer, sleep, thread or wall-clock input. The
  injected values are the fixed composite slips (`2Q` then `0`, or the base-lane rebuild) and the
  constant/step take samples created by `makeTake`/`makeStepTake`.
- **The fix is at the cause and complete.** Two causes, two fixes: `fillGaps()` missing its merge
  (`src/core/TakeLane.cpp:460-467`, kept) and the render gate ramping inside one lane's run
  (`src/core/SamplePlayHandle.cpp:385-403`). The model paths were re-checked: `selectSegment`
  (`TakeLane.cpp:395-416`), `loadSettings` (`TakeLanePersistence.cpp:146-163`) and `removeLane`
  (`TakeLane.cpp:328-341` via `segmentsAfterRemovingLane`, `TakeLane.cpp:107-125`) already merge
  same-slip neighbours; different-slip neighbours must stay apart for the read, so they are now
  joined only for the gate. `CompSlipReaders` reads `comp.segments()`, not `m_compSpans`, so the
  slip behaviour is untouched.
- **No collateral damage.** Project files are written and read by untouched persistence code;
  `loadSettings()` already sorted and merged before the model change, so old projects load exactly
  as before. No limit or refusal changed: `MaxCompSpans` still bounds the snapshot, coalescing only
  shrinks it, and `refusesRebuildAndLaneRemovalBeyondThePlaybackSpanBudget()` still refuses the
  257-span result (passes in `test-2-green-family.log`). Genuine lane switches keep a hole and
  therefore keep their ramps; the same-slip rebuild test and the four-test comp/lane family pass.
- **ALREADY-FIXED/FALSE-POSITIVE evidence.** Not applicable: the verdict is FIXED.
- **Sibling commands in the command files you changed.** The diff touches no command file. Round 3
  changes `src/core/SamplePlayHandle.cpp`, `include/SamplePlayHandle.h`, `include/CompSlip.h`,
  `docs/COMPING.md` and the test; the `app.*`/`arrangement.*` list in the ask comes from other
  lanes' changes already on this train branch. `comp.rebuild` still reports `segments_after` from
  `model.rebuild()` (the round-1 normalised count); the round-3 gate coalescing does not change any
  command result or undo step.

Also: no GUI change, so no screenshot/recording is required. `src/core/TakeLane.cpp` matches the
`src/core/Take*` state-sequence rule; the required artifact and the states are described above and
in `logs/proof.json`.

## Other findings

- Same-lane neighbours with a *different* slip remain two segments in the model - that is the
  documented read seam (`docs/COMPING.md:68-70`) - but the gate no longer ramps at their shared
  tick. Only genuine lane switches and a run's outer edges ramp now.
