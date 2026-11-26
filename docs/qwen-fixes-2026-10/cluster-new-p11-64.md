FIXED

# cluster new-p11-64: authored patcher wiring is not saved with the chain

The finding is **REAL** and is now **FIXED**. `EffectChain::saveSettings` writes an authored wiring
as a `<patchwiring>` child of `<fxchain>`, and `loadSettings` restores it (or the derived wiring when
the element is absent) before the graph rebuild that applies it.

History: the product fix and the regression test were committed earlier on this branch
(`7c85ff576f`), but that session could not build (host disk full), so the GREEN half was unproven
then. This session re-verified the whole chain of evidence: the test was seen RED with the product
change reverted (EXIT 8) and GREEN with it (EXIT 0), the family and wider save/load tests pass, and
the gates pass. The one product change made this session is the header comment in
`include/PatchWiring.h` that still claimed the opposite of the implemented behaviour (see below).

## Evidence that the bug is real (read this session)

- The pre-fix `EffectChain::saveSettings` (`git show 7c85ff576f~1:src/core/EffectChain.cpp`,
  function at `158`) ends after the `<effect>` loop: it writes `enabled` and `numofeffects` and the
  devices, and never writes `m_patch`, the authored wiring (`include/EffectChain.h:136`
  `patchWiring()`, `:138` `patchActive()`).
- The pre-fix `loadSettings` (same revision, tail at `258`) never restores `m_patch` and never resets
  it, and `clear()` (`src/core/EffectChain.cpp:543`, `m_patchDropped = false` at `:565`) does not
  reset `m_patch` either. A reload therefore loses an authored wiring, and loading XML into a chain
  that already has one keeps the stale wiring.
- This was a documented gap, not an intended contract: `docs/PATCHER-GRAPH.md` §5 ("What is NOT
  done") said "The wiring is session state ... a patch does not survive a save/load"; §5 now
  documents the element. `include/PatchWiring.h` carried the same stale claim, which this session
  corrected (see "The fix").
- The GUI authors wiring through the same command (`src/gui/PatcherCanvas.cpp`, the "Wire the
  Chain..." menu in `src/gui/shell/ShellCommandMenus.cpp`), so the loss was reachable by a user.
- Not on the brief's by-design list; an fff search of `AGENTS.md` for `wiring` / `patcher` found no
  entry.
- Proof by execution: with the product change reverted the regression test fails on exactly the two
  persistence cases (`38-test-red.log`, EXIT 8), which cannot happen if the pre-fix code persisted
  the wiring.

## The fix

- **`include/PatchWiring.h`, `src/core/PatchWiring.cpp`:** `PatchWiring::saveXml`
  (`src/core/PatchWiring.cpp:257`) writes
  `<patchwiring output="effect:N"><edge from=".." fromport=".." to=".." toport=".."/>..</patchwiring>`.
  An empty (derived) wiring writes nothing, so a chain that is not patched saves exactly as before.
  `PatchWiring::loadXml` (`:275`) reads that element: no element (every older file) means the
  derivation; a malformed element means the derivation and returns false.
- **`src/core/EffectChain.cpp`:** `saveSettings` calls `m_patch.saveXml()` after the effects
  (`:187`); `loadSettings` calls `loadPatchWiring(_this)` before `rebuildRoutingGraph()` (`:269`).
  That rebuild still drops a wiring the loaded effects cannot take, and sets `patchDropped()`, as
  before.
- **`include/EffectChain.h:237`, `src/core/EffectChainPatcher.cpp:249`:** the private
  `EffectChain::loadPatchWiring()` reports a malformed element through `Song::collectError`.
- **This session's product change:** `include/PatchWiring.h` (the file's opening comment) still
  said "no `<routinggraph>` element is written into `<fxchain>`, so a patch does not survive a
  save/load", which is the behaviour this fix removes. The bullet is replaced by a paragraph that
  states an authored wiring is written as `<patchwiring>` and restored by `loadSettings()`, while a
  derived wiring writes nothing. Comment only; no behaviour change.
- **Docs and row text:** `docs/PATCHER-GRAPH.md` §5 and the reason text of the
  `patcher.set_wiring` row (`src/core/ControlReversibilityTableRouting.cpp:219`) say the chain's
  `<fxchain>` form now saves an authored wiring. The row stays `snapshot`, and no control command
  was added.
- **Registration:** `tests/fork-sources.txt` and `tests/all-sources.txt` list the new test file;
  `tests/upstream-modifications.txt` gives reasons for `include/EffectChain.h` and
  `src/core/EffectChain.cpp`; `tests/CMakeLists.txt` registers `PatcherWiringPersistenceTest`.

## Regression test

`tests/src/core/PatcherWiringPersistenceTest.cpp` has four cases:

1. `authoredWiringSurvivesSaveAndLoad` (`:159`) - a non-linear wiring (input -> effect:1 ->
   effect:0) is saved, loaded into a fresh chain and compared edge for edge; the chain still routes
   through its graph and nothing was dropped.
2. `loadingXmlWithoutWiringRestoresDerivation` (`:182`) - loading XML without `<patchwiring>` over
   a chain that had an authored wiring puts it back on the derivation (the stale-wiring path).
3. `derivedChainSavesNoWiringElement` (`:201`) - a derived chain writes no child, so old projects
   keep their format.
4. `malformedWiringFallsBackToDerivation` (`:213`) - a malformed `<patchwiring>` loads without
   crashing and leaves the chain on its derivation.

### Commands and EXIT codes

All logs are in `/home/kruzzzzy/.config/kads/zene/state/runs/new-p11-64.r1/logs`. Test runs were
made from `build/tests`; builds with `-j4`.

| Step | Command (log) | EXIT | Result |
|---|---|---|---|
| GREEN build | `cmake --build build --target zene PatcherWiringPersistenceTest -j4` (`35-build-green.log`) | 0 | |
| GREEN test | `ctest -R PatcherWiringPersistenceTest --output-on-failure` (`36-test-green.log`) | 0 | 100% passed out of 1 |
| 4b patch | `git diff 7c85ff576f~1 -- include/EffectChain.h include/PatchWiring.h src/core/EffectChain.cpp src/core/EffectChainPatcher.cpp src/core/PatchWiring.cpp src/core/ControlReversibilityTableRouting.cpp > logs/fix.patch` | 0 | 242 lines, 6 product files, no test files |
| 4b revert | `git apply -R logs/fix.patch` | 0 | six product files reverted |
| 4b RED build | `cmake --build build --target zene PatcherWiringPersistenceTest -j4` (`37-build-red.log`) | 0 | |
| 4b RED test | `ctest -R PatcherWiringPersistenceTest --output-on-failure` (`38-test-red.log`) | 8 | `FAIL! authoredWiringSurvivesSaveAndLoad() 'loaded.patchActive()' returned FALSE`; `FAIL! loadingXmlWithoutWiringRestoresDerivation() '!chain.patchActive()' returned FALSE`; 4 passed, 2 failed |
| 4b reapply | `git apply logs/fix.patch` | 0 | only the header comment left modified |
| 4b GREEN build | `cmake --build build --target zene PatcherWiringPersistenceTest -j4` (`39-build-green2.log`) | 0 | |
| 4b GREEN test | `ctest -R PatcherWiringPersistenceTest --output-on-failure` (`40-test-green2.log`) | 0 | 100% passed out of 1 |
| Family build | `cmake --build build --target zene ControlArmReversibilityTest ReversibilityContractTest ReversibilityUndoTest ReversibilityUndoDeviceTest PatcherCommandsTest PatcherWiringPersistenceTest PatcherCanvasTest -j4` (`41-build-family.log`) | 0 | |
| Family test | `ctest -R 'ControlArmReversibilityTest\|ReversibilityContractTest\|ReversibilityUndoTest\|ReversibilityUndoDeviceTest\|PatcherCommandsTest\|PatcherWiringPersistenceTest\|PatcherCanvasTest\|ControlReversibilityTranscript\|ControlCommandsSnapshot' --output-on-failure` (`42-test-family.log`) | 0 | 100% passed out of 9, including the ControlCommandsSnapshot canary |
| Wider build | `cmake --build build --target zene MissingDevicesRound2Test ModulationLayerProjectRoundTripTest HybridTrackRoundTripTest RoutingGraphTest -j4` (`43-build-wide.log`) | 0 | |
| Wider test | `ctest -R 'MissingDevicesRound2Test\|ModulationLayerProjectRoundTripTest\|HybridTrackRoundTripTest\|RoutingGraphTest' --output-on-failure` (`44-test-wide.log`) | 0 | 100% passed out of 4 |
| Gates | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` (`49-feature-check.log`) | 0 | every row PASS, `RESULT: PASS` |

A pre-commit run of the completion check (`46-submit-check-pre.log`) found exactly one FAIL,
`working tree clean - M include/PatchWiring.h`, because the header comment was not committed yet.
After the commit, `submit_check.py` (`48-submit-check.log`) reports every item PASS and
`RESULT: PASS - submit`.

Final gate table (`49-feature-check.log`):
```
  PASS  fork-sources            exit 0
  PASS  all-sources-reproduce   exit 0
  PASS  no-upstream-regression  exit 0
  PASS  check-namespace         exit 0
  PASS  no-tautology            exit 0
  PASS  doc-figures             exit 0
  PASS  complexity              no new item in your files
  PASS  file-length             no new item in your files
RESULT: PASS
```

## Self-check

- **The regression test FAILS with the product change reverted and PASSES with it (exit codes).**
  Yes: `git apply -R logs/fix.patch` EXIT 0, rebuild EXIT 0, `ctest` EXIT 8 (`38-test-red.log`,
  two `FAIL!` lines quoted above); `git apply logs/fix.patch` EXIT 0, rebuild EXIT 0, `ctest`
  EXIT 0 (`40-test-green2.log`). The patch holds only the six product files, not the test.
- **Deterministic.** No timing, sleeps, threads or wall-clock values. The test builds chains and
  hands them a fixed non-linear wiring (`reversedWiring()`), saves to a `QDomDocument` in memory and
  loads that element back; the only engine setup is `Engine::init(true)` with the dummy device
  stopped in `initTestCase()`, which the other chain tests in this tree do the same way.
- **At the cause and complete.** The `<fxchain>` serializer is the only writer/reader of a chain:
  `EffectChain::nodeName()` is `"fxchain"` (`include/EffectChain.h:73-75`), and a track, mixer
  channel or rack chain reaches it through `saveState`/`restoreState`, while a clip's chain goes
  through the same pair (`SampleClip::saveClipEffects`/`loadClipEffects`,
  `src/core/SampleClipEffects.cpp:59-84`, which call `m_clipEffects->saveState`/`restoreState`).
  No other code writes `m_patch` (`setPatchWiring` and `rebuildRoutingGraph`,
  `src/core/EffectChainPatcher.cpp`). `loadSettings` calls `clear()` first, and `loadPatchWiring`
  then replaces whatever wiring it kept - with the parsed one, or with the derivation when the
  element is absent or malformed - before the rebuild. `patcher.set_wiring` always gives a non-empty
  wiring an output, so `loadXml`'s required `output` attribute cannot reject a wiring the app
  itself saved (round-trip case 1).
- **No collateral damage.** An `<fxchain>` without `<patchwiring>` loads as the derived wiring, as
  before; a derived chain writes no new element (case 3); a malformed element is reported and
  falls back (case 4). The `<effect>` loop in `loadSettings` only reads `effect` elements, so the
  new child does not disturb it (cases 1 and 2 load two effects each). No limit or check was added
  that another caller must exceed. The adjacent save/load and routing tests pass: 4/4
  (`44-test-wide.log`), and the patcher/reversibility family 9/9 (`42-test-family.log`).
- **ALREADY-FIXED / FALSE-POSITIVE:** not applicable; the verdict is FIXED.
- **Audio thread.** Nothing new runs there. Save and load run on the control thread;
  `loadSettings` already holds `requestChangesGuard()` across `clear()` and both rebuilds
  (`src/core/EffectChain.cpp:201`), and the new call sits inside that guard. The audio thread gets
  no new work, allocation or lock. Local TSan does not run on this box, so this is reasoned from the
  design and the guard, not measured.
- **Undo and commands.** No control command was added and the `patcher.set_wiring` row stays
  `snapshot` with its inverse-command undo; only its reason text changed. The ControlCommandsSnapshot
  canary and the three Reversibility tests pass (`42-test-family.log`).
- **Sibling commands in the changed command file.** The completion check lists `bus.create`,
  `bus.list`, `bus.remove`, `feedback.disable`, `feedback.enable`, `feedback.get_state`,
  `patcher.get_state`, `patcher.set_wiring`, `pdc.report`, `pdc.set_offset`, `port.get_state`,
  `port.set_pin`, `port.set_pins`, `routing.get_state`, `routing.matrix`. The diff of
  `src/core/ControlReversibilityTableRouting.cpp` in `fix.patch` is one hunk inside the
  `patcher.set_wiring` row: four removed lines and four added lines of reason text. No row's class,
  flag, undo text or handler changed, so `bus.*`, `feedback.*`, `pdc.*`, `port.*`, `routing.*` and
  the read-only `patcher.get_state` are untouched; `patcher.set_wiring`'s class and undo are
  unchanged too.
- **Header changed:** `include/PatchWiring.h` (comment only) and `include/EffectChain.h` (the
  private `loadPatchWiring` declaration, committed earlier). `zene` and every test target used here
  were rebuilt from the final tree (`35`, `39`, `41`, `43` logs).
- **Completion check.** `submit_check.py <worktree> new-p11-64` prints `RESULT: PASS - submit`
  (`48-submit-check.log`): commits on the branch, clean tree, report verdict, tests, test
  registration and gates all PASS, and the only ask left is the sibling-command list answered
  above. The pre-commit run (`46-submit-check-pre.log`) had exactly one FAIL, the uncommitted
  header comment.

## Other findings

- `EffectChain::clear()` (`src/core/EffectChain.cpp:543-577`) resets `m_patchDropped` but not
  `m_patch`. `loadSettings` is covered by `loadPatchWiring`, but a chain cleared for another reason
  (a preset replacing the device list) keeps its authored wiring in memory; the next rebuild drops
  it if it no longer fits, and the next save would now write it. This is pre-existing session-state
  behaviour outside this finding (loss on save/reload), so it was not changed here.
