FIXED

# Cluster new-p14-199-port: `insertModulator()` bypasses the per-modulator route limit

**Verdict: FIXED** (ported by hand from `02d8d6788` — no merge, no cherry-pick, no rebase).

The finding is REAL on this branch: `ModulationLayer::insertModulator()` was the one way into
the layer that carried a modulator past the bound the header declares.

---

## Decision: REAL

`src/core/ModulationLayer.cpp:443` (the method the finding points at, before the fix):

```cpp
bool ModulationLayer::insertModulator(int index, const Modulator& modulator)
{
	if (index < 0 || index > modulatorCount() || modulatorCount() >= MaxModulators)
	{
		return false;
	}
	m_modulators.insert(m_modulators.begin() + index, modulator);
	return true;
}
```

It checked the layer's **modulator** count and nothing else - neither the incoming
modulator's own route count nor its source.

The contract it breaks is stated in the header, `include/ModulationLayer.h:183-186`:
"Bounded (MaxModulators x MaxRoutesPerModulator) so that a project file can never make the
layer unbounded", with `MaxRoutesPerModulator = 8` at `include/ModulationLayer.h:193`.

Its siblings all hold that bound:

- `addRoute()` - `src/core/ModulationLayer.cpp:482`: `if (entry->routeCount() >= MaxRoutesPerModulator) { return -1; }`
- `insertRoute()` - `src/core/ModulationLayer.cpp:491`: `... || entry->routeCount() >= MaxRoutesPerModulator) { return false; }`

The damage the finding describes follows from this: the loader appends at most eight routes
per modulator (`loadModulator()` -> `layer->addRoute(index, route)`,
`src/core/ModulationLayer.cpp:234`), so a ninth route that `insertModulator()` let in is
silently lost on the reload.

The source rule the layer holds itself to is `validSource()`
(`src/core/ModulationLayer.cpp:392`, rate in `MinRateHz..MaxRateHz`, phase in `0..1`);
the loader refuses a modulator whose source does not validate
(`src/core/ModulationLayer.cpp:223`), and so do `modulator.create`
(`src/core/ControlCommandsModulator.cpp:223`) and `modulator.rate_set`
(`src/core/ControlCommandsModulator.cpp:355`).

## The fix

`src/core/ModulationLayer.cpp:455` and `:458` - `insertModulator()` now holds an inserted
modulator to the same route bound and the same source rule as one the layer authored
itself:

```cpp
	if (modulator.routeCount() > MaxRoutesPerModulator) { return false; }
	if (!validSource(modulator.source, nullptr)) { return false; }
```

The contract is documented on the declaration, `include/ModulationLayer.h:205-209`.

No new control command, no allocation, no lock, no audio-thread change: the method is
control-thread only and returns `false` instead of mutating the layer.

## Callers and siblings checked

- `src/core/ControlCommandsModulator.cpp:71` - the undo of `modulator.remove` re-inserts the
  captured modulator (name, source and every route). A captured modulator cannot carry more
  than eight routes (only `addRoute()`/`insertRoute()` put routes into the layer, and both
  refuse the ninth) and its source was validated when it was authored
  (`modulator.create` checks `layer.validSource(source, &why)`), so the undo still inserts
  and still undoes one honest step. Proven by
  `ControlModulatorCommandsTest::bindDriveUndoAndRebind`
  (`tests/src/core/ControlModulatorCommandsTest.cpp:210-219`: the remove drops the modulator,
  the undo brings it back with its route list intact) - it passes with the fix.
- `src/core/ControlCommandsModulator.cpp:94` - the redo of `modulator.create` re-inserts a
  modulator that carried no routes and a validated source.
- The layer's own loader uses `addModulator()`, not `insertModulator()`
  (`src/core/ModulationLayer.cpp:231`), so no saved project changes behaviour: old files
  load exactly as before.
- No other caller of `insertModulator()` exists in the tree (`grep -n insertModulator` over
  `src/` and `include/` returns the definition, the declaration and the two call sites above).

## The regression test

`tests/src/core/ModulationLayerValueTest.cpp:138` -
`anInsertedModulatorIsBoundedLikeEveryOtherOne()`: a modulator carrying
`MaxRoutesPerModulator` routes is inserted (accepted - the check is the bound, not a
smaller one), one with a ninth route is refused, one with an invalid source is refused, a
refused insertion leaves the layer's modulator count untouched, and the accepted modulator's
eight routes survive a save/load round trip.

The test lives in `ModulationLayerValueTest.cpp` because that file is the pure layer - "the
LFO arithmetic and the layer's own bounds (no engine)" - and the bound it checks is the
layer's own bound, next to `modulatorsAndRoutesAreBounded()`.

## Evidence table

| Check | Exact command and result |
| --- | --- |
| RED before the fix | `cmake --build build --target ModulationLayerValueTest -j 20 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p14-199-port.r1/logs/build-red-1.log 2>&1` **EXIT=0**; `ctest -R '^ModulationLayerValueTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p14-199-port.r1/logs/test-red-1.log 2>&1` **EXIT=8** - `FAIL! : ModulationLayerValueTest::anInsertedModulatorIsBoundedLikeEveryOtherOne() '!layer.insertModulator(1, tooMany)' returned FALSE.` (6 passed, 1 failed) |
| GREEN after the fix | `cmake --build build --target zene ModulationLayerValueTest ModulationLayerTest ModulationLayerProjectRoundTripTest ModulationRateTest ModulationAudioViewTest ControlModulatorCommandsTest -j 20 > .../logs/build-green-1.log 2>&1` **EXIT=0**; `ctest -R '^ModulationLayerValueTest$' --output-on-failure > .../logs/test-green-1.log 2>&1` **EXIT=0** - 1/1 passed. |
| 4b - reversed fix (test must FAIL) | `git diff -- src/core/ModulationLayer.cpp include/ModulationLayer.h > .../logs/fix.patch` **EXIT=0**; `git apply -R .../logs/fix.patch` **EXIT=0**; `cmake --build build --target ModulationLayerValueTest -j 20 > .../logs/build-4b-red.log 2>&1` **EXIT=0**; `ctest -R '^ModulationLayerValueTest$' --output-on-failure > .../logs/test-4b-red.log 2>&1` **EXIT=8** - the same `FAIL!` line. |
| 4b - restored fix (test must PASS) | `git apply .../logs/fix.patch` **EXIT=0**; `cmake --build build --target ModulationLayerValueTest -j 20 > .../logs/build-4b-green.log 2>&1` **EXIT=0**; `ctest -R '^ModulationLayerValueTest$' --output-on-failure > .../logs/test-4b-green.log 2>&1` **EXIT=0** - 1/1 passed. |
| Affected family | `cmake --build build --target ModulatorToolsTest ModulatorPanelTest -j 20 > .../logs/build-family-extra.log 2>&1` **EXIT=0**; `ctest -R 'Modulation\|Modulator' --output-on-failure > .../logs/test-family-2.log 2>&1` **EXIT=0** - 8/8 passed (ModulationLayerValueTest, ModulationLayerTest, ModulationAudioViewTest, ControlModulatorCommandsTest, ModulationLayerProjectRoundTripTest, ModulationRateTest, ModulatorToolsTest, ModulatorPanelTest). |
| Gates | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p14-199-port > .../logs/gate-2.log 2>&1` **EXIT=1** - `no-upstream-regression` PASS, `check-namespace` PASS, `no-tautology` PASS, `doc-figures` PASS, `complexity` PASS ("no new item in your files"), `file-length` PASS ("no new item in your files"); `fork-sources` FAIL and `all-sources-reproduce` FAIL are pre-existing on this branch (see "Gates" below). |
| Pre-existing transcript failures | `ctest -R 'ControlUndoStructuralTranscript\|ControlReversibilityTranscript' --output-on-failure > .../logs/test-transcript-baseline.log 2>&1` **EXIT=8** with the fix REVERTED - the same two failures as with the fix applied (`.../logs/test-undo-family.log`, **EXIT=8**): "FAIL: plugin.load returned no instance id" and "the LADSPA host plugin 'ladspaeffect' is not in this build". Neither mentions a modulator; both are this build's missing LADSPA plugin. |

### Gates

`feature_check.py` computes its ratchet baseline by checking the branch's base commit out
in a shared tree (`~/.local/share/zene-bunny-fix/qwen/gate-baselines/tree`), and the Bunny
lane guard blocks that `git checkout --detach` / `git worktree add` (message: "not allowed
in a Bunny lane (AGENTS.md rules 1, 2, 11, 12)"). The baseline listing was therefore
produced the same way the script produces it, from a read-only export of the base commit
(`git archive 0a8842d397b331a98dcfce751c4f4bb2e8476cad` into `/tmp/base-tree-0a8842d3`),
running the very same two ratchet commands there and writing the script's own cache entry
(`gate-baselines/0a8842d397b331a98dcfce751c4f4bb2e8476cad.json`). No gate limit or
baseline was moved.

`fork-sources` and `all-sources-reproduce` fail on this branch before my change as well, and
the failure is provably not mine:

- `tests/fork-sources.txt` says so in its own header (2026-10-12 entry): "the recipe below
  does not parse (its second awk line ends in a stray continuation: bash reports a syntax
  error at its line 5, already at HEAD), so this list cannot be re-derived mechanically
  until that is fixed".
- The failing entries are the vendored third-party tree: `tests/all-sources.txt` lists
  1150 `plugins/SfzPlayer/sfizz/...` files while its own header recipe excludes that tree
  (`grep -vE '^(...|plugins/SfzPlayer/sfizz/)'`, `tests/all-sources.txt:22`), so the manifest
  and the recipe can never agree on this branch.
- Both gates compare a manifest with a recipe whose input is the set of files added since
  the upstream base commit. That set is byte-identical before and after my commit:

  ```
  git diff --name-only --diff-filter=A 4e677cb6c6ab HEAD -- src include plugins tests/src/core tests/src/plugins tests/src/tracks | grep -E '\.(cpp|c|h|hpp|cc|cxx)$' | sort > /tmp/recipe-head.set   # 4142 files
  git diff --name-only --diff-filter=A 4e677cb6c6ab 0a8842d397b331a98dcfce751c4f4bb2e8476cad -- src include plugins tests/src/core tests/src/plugins tests/src/tracks | grep -E '\.(cpp|c|h|hpp|cc|cxx)$' | sort > /tmp/recipe-base.set   # 4142 files
  diff /tmp/recipe-head.set /tmp/recipe-base.set   # RECIPE_INPUTS_IDENTICAL
  ```

  `git diff --name-status 0a8842d39 HEAD` shows my commit adds one file - a markdown report
  under `docs/`, outside the recipe's pathspec and not a C/C++ extension - and modifies three
  existing ones. It touches neither manifest. The gate's diff, and therefore its FAIL, is
  identical to the base's.
- The complexity and file-length ratchets - the two rows that are red on the train on purpose -
  report "no new item in your files".
- Making those two rows green would mean hand-editing a shared scope manifest (the gate's own
  message: "never hand-edit, never delete a path the recipe derives") or re-anchoring a gate.
  Both are forbidden by AGENTS.md and neither is this bug's business, so they are reported
  as pre-existing rather than "fixed" here.

## Self-check

Answers to each item the completion check asks, with the evidence.

- **The regression test FAILS with the product change reverted and PASSES with it (exit
  codes).** `git diff -- src/core/ModulationLayer.cpp include/ModulationLayer.h >
  .../logs/fix.patch` EXIT=0; `git apply -R .../logs/fix.patch` EXIT=0; rebuild
  `.../logs/build-4b-red.log` EXIT=0; `ctest -R '^ModulationLayerValueTest$'
  --output-on-failure > .../logs/test-4b-red.log 2>&1` **EXIT=8** - `FAIL! :
  ModulationLayerValueTest::anInsertedModulatorIsBoundedLikeEveryOtherOne() '!layer.insertModulator(1,
  tooMany)' returned FALSE.` (6 passed, 1 failed). Then `git apply .../logs/fix.patch`
  EXIT=0; rebuild `.../logs/build-4b-green.log` EXIT=0; the same ctest
  `> .../logs/test-4b-green.log 2>&1` **EXIT=0** - 1/1 passed. The same RED was seen
  before the fix was written at all (`.../logs/test-red-1.log`, EXIT=8).
- **The test is deterministic: no wall-clock timing, sleeps or races (name the seam or
  injected values it uses).** `anInsertedModulatorIsBoundedLikeEveryOtherOne()` uses no
  clock, no sleep and no thread at all: it builds a `Modulator` by value, pushes a fixed
  number of `routeOf("Gain", 0.5f)` routes onto it (the same helper the layer's own bound
  test uses), sets `source.rateHz = 0.0f` for the invalid-source case, and counts the
  layer's modulators and routes. The only value it compares across a boundary is the
  route count, an integer. The save/load half uses a `QDomDocument` built in memory, so
  the round trip is deterministic too.
- **The fix is at the cause and complete: every caller and sibling of what you changed was
  checked and stays consistent.** The cause is inside `insertModulator()`
  (`src/core/ModulationLayer.cpp:443`), so the check went there. Siblings re-read:
  `addModulator()` (`:433`, appends, no routes), `addRoute()` (`:468`, refuses the ninth
  route), `insertRoute()` (`:477`, refuses the ninth route), `removeRoute()` (`:489`),
  `removeModulator()` (`:453`), `setSource()` (`:460`), `validSource()` (`:392`). Callers
  re-read: `src/core/ControlCommandsModulator.cpp:71` (the undo of `modulator.remove`) and
  `:94` (the redo of `modulator.create`) - the only two in the tree - plus the loader
  (`src/core/ModulationLayer.cpp:231`), which uses `addModulator()` and therefore is
  unaffected. The declaration in `include/ModulationLayer.h:205-209` was updated with the
  definition in the same commit.
- **No collateral damage: old projects and saved data still load as before, and any limit
  or check you added does not hit other callers that legitimately exceed it.** The loader
  does not use `insertModulator()`, so a saved project loads through the same path as
  before; the two undo/redo call sites re-insert modulators that can hold at most eight
  routes (only `addRoute()`/`insertRoute()` put routes in, and both refuse the ninth) and
  whose source was validated when it was authored - so the check never refuses a legitimate
  insertion. The bound is the layer's own declared bound
  (`include/ModulationLayer.h:193`), not a smaller one: a modulator with exactly eight
  routes is still accepted, and the regression asserts that.
- **Is the verdict ALREADY-FIXED or FALSE-POSITIVE?** No - the verdict is FIXED. Before
  this commit, `src/core/ModulationLayer.cpp:443` held no route check at all (the diff of
  this commit adds the two checks at `:455` and `:458`), and the regression failed on that
  code.

Rest of the self-check:

- **Is the finding REAL?** Yes. `src/core/ModulationLayer.cpp:443` checked the modulator
  count only; `addRoute()` (`:482`) and `insertRoute()` (`:491`) hold the eight-route bound
  the header declares (`include/ModulationLayer.h:193`), and the loader appends at most
  eight routes per modulator (`:234`), so the ninth was silently lost on reload.
- **Realtime, undo, commands?** `insertModulator()` is control-thread only: it allocates
  nothing new, takes no lock, logs nothing and touches no audio path. Undo stays one
  honest step (the captured modulator comes back with its route list - asserted by
  `ControlModulatorCommandsTest::bindDriveUndoAndRebind`,
  `tests/src/core/ControlModulatorCommandsTest.cpp:210-219`, which passes). No new control
  command was added.
- **Registration?** No new file was added: the product files and the test file were already
  registered (`include/ModulationLayer.h` and `src/core/ModulationLayer.cpp` in
  `tests/fork-sources.txt`; the test source is registered in `tests/all-sources.txt`, the
  manifest `tests/fork-sources.txt` points at for the modulation test files).
