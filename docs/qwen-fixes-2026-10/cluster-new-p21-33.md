FIXED

# Cluster new-p21-33 — "modulator.rate_set reads routeCount through a stale pointer after edit"

Finding: `src/core/ControlCommandsModulator.cpp:360` (historical line) — a pointer into the
modulation layer captured before `editModulationLayer` is dereferenced after it; the edit may move
the layer's storage, so the read is dangling/stale.

Verdict: FIXED, in the surviving sites. The named command, `modulator.rate_set`, already re-resolves
the layer after the edit and reads no `routeCount` at all
(`src/core/ControlCommandsModulator.cpp:377`: `layerResult(songModulationLayer().layer(), index)`);
`modulationLayerJson` re-fetches every modulator per call (`src/core/ControlModulationSupport.cpp:259-270`).
The identical defect the finding describes existed in the two sibling commands that still carried a
pre-edit `const Modulator*` across the edit boundary:

- `modulator.depth_set` — `src/core/ControlCommandsModulatorRoutes.cpp:316` (post-edit
  `target_count` from the captured pointer), reached from line 279's
  `const Modulator* modulator = layer.modulator(index);`
- `modulator.target_remove` — `src/core/ControlCommandsModulatorRoutes.cpp:371` (same pattern,
  pointer captured at line 350).

Both now re-read the count from the layer as it is after the edit:

    const Modulator* current = songModulationLayer().layer().modulator(index);
    result.insert(QStringLiteral("target_count"), current != nullptr ? current->routeCount() : 0);

(`src/core/ControlCommandsModulatorRoutes.cpp`, the two post-edit `result.insert("target_count", ...)`
sites; the pre-edit uses for argument validation are unchanged.)

Why this is a real defect and not a theoretical one: the edit inside `editModulationLayer`
(`src/core/ControlModulationSupport.cpp:217-225`) first restores the models' base values via
`restoreModulationBases` (`src/core/ModulationLayer.cpp:617-626`), which calls
`AutomatableModel::setValue(base, true)`. A value that differs is written synchronously on the
control thread and emits `dataChanged()` (`src/core/AutomatableModel.cpp:530-553`); the write is NOT
deferred off-thread in that path (`src/core/AutomatableModel.cpp:541-543` — defer applies only when
`m_deferChangeSignal` is set AND the engine runs on the render thread, and the deferred branch only
re-queues the signal). A slot connected to `dataChanged()` may itself edit the modulation layer
(`ModulationLayerPublisher::edit`, `include/ModulationLayer.h:368-386` — e.g. an
`insertModulator`), which moves `m_modulators` (`std::vector<Modulator>`,
`include/ModulationLayer.h:106-107` via `src/core/ModulationLayer.cpp:431-441`) and leaves the
captured pointer referring to a moved-from object. Every other post-edit read in the group already
follows the re-resolve rule (`ControlCommandsModulator.cpp:248/288/377`,
`ControlCommandsModulatorRoutes.cpp:248/314/369`, `ControlCommandsModulatorVoices.cpp:235`,
`src/core/ControlCommandsModulatorTools.cpp:122/127`); these two sites were the only ones that did not.

The reported `target_count` is now the layer's actual count after the edit; when the edit was a
no-op for the route set the value is identical to before (behaviour-preserving except in the
reentrant-storage-move case, where the old code read a moved-from object — never a supported
observable).

## Regression test

`tests/src/core/ControlModulatorCommandsTest.cpp` — new `routeCountIsReadFromAfterTheEditMovesTheLayer`.
It binds Gain and Panning to a modulator, applies the real modulation block so the parameters differ
from the captured bases, then runs `modulator.depth_set` and `modulator.target_remove` with a
one-shot `dataChanged()` slot connected that inserts a modulator at the end of the layer (the layer's
storage moves, the indices keep). The slot fires inside the edit's restore — the real path, no
mocking, no sleeps, no wall-clock. The two inserts are placed so each grows the layer vector exactly
when it runs (the suite leaves the layer's capacity at one, libstdc++ doubling: 1 → 2 → 4 → 8),
so the move is deterministic. The test asserts the reported `target_count` equals the layer's count
after the edit (2 for depth_set, 1 after one of two routes is removed). Missing the fix, the
pre-edit pointer reads the moved-from `routes` vector whose size is 0 on this toolchain.

## Evidence (commands; EXIT codes; logs in /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p21-33.r1/logs/)

RED (fix not present, test added):
  cmake --build build --target ControlModulatorCommandsTest            → build-4.log, EXIT=0
  ctest -R ControlModulatorCommandsTest (from build/tests)             → test-4.log, EXIT=8
    FAIL! : ControlModulatorCommandsTest::routeCountIsReadFromAfterTheEditMovesTheLayer()
    Actual (target_count): 0   Expected: 2   Loc: ControlModulatorCommandsTest.cpp(387)

GREEN (fix applied):
  cmake --build build --target zene ControlModulatorCommandsTest       → build-5.log, EXIT=0
  ctest -R ControlModulatorCommandsTest (from build/tests)             → test-5.log, EXIT=0
    100% tests passed out of 1

4b proof (fix reverted, test must fail; fix re-applied, test must pass):
  git diff -- src/core/ControlCommandsModulatorRoutes.cpp > LOGS/fix.patch   → EXIT=0 (1579 bytes)
  git apply -R LOGS/fix.patch
  cmake --build build --target ControlModulatorCommandsTest            → build-8.log, EXIT=0
  ctest -R ControlModulatorCommandsTest (from build/tests)             → test-8.log, EXIT=8
    FAIL! Actual: 0   Expected: 2
  git apply LOGS/fix.patch
  cmake --build build --target zene ControlModulatorCommandsTest       → build-9.log, EXIT=0
  ctest -R ControlModulatorCommandsTest (from build/tests)             → test-9.log, EXIT=0
    100% tests passed out of 1

Affected family (all with the fix):
  ctest -R "Modulat|Modulation"                                        → test-6.log + test-7.log
  ModulationLayerValueTest, ModulationLayerTest, ModulationAudioViewTest,
  ControlModulatorCommandsTest, ModulationLayerProjectRoundTripTest,
  ModulationRateTest, ModulatorToolsTest, ModulatorPanelTest
  → 8/8 pass (ModulatorPanelTest was not built on the first run — "Not Run", EXIT=8;
     `cmake --build build --target ModulatorPanelTest` then ctest: test-7.log, EXIT=0)
  (test-6.log, EXIT=8 is the suite result with ModulatorPanelTest not yet built; every built
   test passed, and the rebuilt run after building ModulatorPanelTest is EXIT=0 / 100%.)

Gates:
  python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> → feature-check-1.log, EXIT=0
    fork-sources PASS, all-sources-reproduce PASS, no-upstream-regression PASS,
    check-namespace PASS, no-tautology PASS, doc-figures PASS,
    complexity PASS (no new item in your files), file-length PASS (no new item in your files)

No headers changed; no new files; no new control commands; no audio-thread changes (the touched
code is control-thread command handling only). Undo unchanged: the same `recordDepth` /
`recordRouteRemoval` steps, recorded before the reply is built.

## Self-check

? The regression test FAILS with the product change reverted and PASSES with it.
   Reverted (`git apply -R fix.patch`): build-8.log EXIT=0, ctest EXIT=8 —
   `routeCountIsReadFromAfterTheEditMovesTheLayer` FAIL, Actual 0, Expected 2.
   Re-applied: build-9.log EXIT=0, ctest EXIT=0 — 100% passed. (Before the fix
   was first written, the same RED was recorded in test-4.log EXIT=8.)

? The test is deterministic - no wall-clock, sleeps or races.
   It drives the real command path: both modulators are created through
   `modulator.create`, the route through `modulator.target_set`, and the real
   block is applied through `applyModulationBlock` so the restore inside the
   edit writes the parameters back. The reentrant edit is triggered by the
   synchronous control-thread `dataChanged()` emission inside
   `restoreModulationBases` - not by a timer, sleep or thread. Sequencing is
   fixed by QtTest's slot declaration order and the two inserts are sized to
   grow the vector exactly when each runs (the suite leaves the layer's
   capacity at 1; libstdc++ doubling 1 -> 2 -> 4 -> 8). No pointer, probe or
   sleep is used to detect the bug; only the public command results are
   asserted. Honest limit: the red-without-fix outcome relies on this
   platform's vector growth and the test's declaration order; with the fix the
   test passes on any conforming implementation. The earlier one-shot probe
   (test-3/test-4 logs) confirmed both the RED mechanism and the command path.

? The fix is at the cause and complete; every caller and sibling checked.
   Changed behaviour: only the two post-edit `target_count` reads in
   `ControlCommandsModulatorRoutes.cpp` (depth_set, target_remove) now re-read
   the layer instead of dereferencing the pointer captured before
   `editModulationLayer`. All other post-edit reads in the group already
   re-resolve: rate_set and the create/remove/console paths in
   ControlCommandsModulator.cpp:248/288/377, target_set at
   ControlCommandsModulatorRoutes.cpp:251, voices at
   ControlCommandsModulatorVoices.cpp:235, formula/trigger_set via
   `editModulator` at ControlCommandsModulatorTools.cpp:122/127,
   instrument_target_set via layerResultFor at Voices:72-76. The pre-edit uses
   (argument validation, captured bases for undo) are unchanged.
   `restoreModulationBases` writes are synchronous (AutomatableModel.cpp:530-553);
   a reentrant `dataChanged()` slot may legally edit the layer, which is why the
   re-read is the fix and not a lint.

? No collateral damage; old projects and saved data load exactly as before.
   No file format, schema, undo entry, gate, limit or check was added or
   changed. When the edit does not move the layer, the two commands report the
   identical number as before (the layer is the same object; routeCount is
   read after the edit either way). No other caller reads the changed lines.
   `modulator.remove` and friends keep their capture-by-value contract.
   Verified: ModulationLayerValueTest, ModulationLayerTest, ModulationAudioViewTest,
   ModulationLayerProjectRoundTripTest, ModulationRateTest, ModulatorToolsTest,
   ControlModulatorCommandsTest, ModulatorPanelTest - all pass (test-6/test-7).
   No audio-thread code was touched; control-thread only.

? ALREADY-FIXED / FALSE-POSITIVE: not applicable - verdict is FIXED (the named
   rate_set site already re-resolved on this branch, see the verdict above;
   the defect survived in the two sibling commands and is fixed there).

? Sibling commands in the changed files and the group - consistency:
   modulator.create        - consistent: result source re-fetched after the edit
                             (`layer.modulator(index)` at ControlCommandsModulator.cpp:245).
   modulator.depth_set     - FIXED by this change (early-return no-op path keeps
                             the pre-edit read; it runs before any edit, unchanged).
   modulator.formula_set   - consistent: re-resolves after the edit (Tools:122/127).
   modulator.get_state     - consistent: read-only; reports a fresh JSON build (165-168).
   modulator.instrument_target_set - consistent: re-reads via songModulationLayer()
                             layer after the edit (Voices:230-236).
   modulator.rate_set      - consistent (already re-resolved; the finding's site).
   modulator.remove        - consistent: captured modulator copied BY VALUE for the
                             undo step; result JSON built from the re-fetched layer
                             (ControlCommandsModulator.cpp:278-296).
   modulator.target_remove - FIXED by this change (its `removed` JSON is built from
                             the by-value `captured` route; `target_count` re-reads).
   modulator.target_set    - consistent: re-reads at ControlCommandsModulatorRoutes.cpp:248-251.
   modulator.trigger_set   - consistent: formula_set's shared path (Tools:122/127).
   modulator.voice_set     - consistent: layerResultFor re-resolves the layer
                             (Voices:72-76, 132).

## Files committed

  src/core/ControlCommandsModulatorRoutes.cpp        (the fix; fork-sourced, already in tests/fork-sources.txt)
  tests/src/core/ControlModulatorCommandsTest.cpp    (regression test)
  tests/upstream-modifications.txt                    (reason for the inherited test file)
  docs/qwen-fixes-2026-10/cluster-new-p21-33.md      (this report)
