# cluster new-p11-33 — BUG 11: repeating an unchanged modulator edit journalled a step

**Verdict: FIXED**

## The finding

`editModulator` (src/core/ControlCommandsModulatorTools.cpp, the shared handler of
`modulator.formula_set` and `modulator.trigger_set`) wrote the layer, pushed an undo step and called
`Song::setModified()` on EVERY successful edit — it never compared the modulator before and after.
Repeating an edit that changed nothing therefore added an undo step that shadows the real one under
it (the first Ctrl+Z undoes nothing) and dirtied a project that was saved a second ago.

Real, not already fixed on this branch: the original code (line numbers of the pre-fix tree)
had no comparison at all — `src/core/ControlCommandsModulatorTools.cpp:71-77` went straight from
`edit(&after, &error)` to `putModulator` / `addUndoStep` / `setModified()`. Sibling commands already
kept this contract (`ControlCommandsCueMix.cpp:100`, `ControlCommandsPluginPaths.cpp:73`,
`ControlCommandsPluginOrganise.cpp:86-91`, `ControlCommandsMixerSurround.cpp:206`), so the modulator
tools were the outlier.

## The fix (product file: `src/core/ControlCommandsModulatorTools.cpp`)

- `sameProgram` (line 64), `sameRoute` (75), `sameLfo` (82), `sameSource` (90), `sameModulator` (97):
  equality over every field the layer SAVES (name, formula text, routes, source incl. the compiled
  MX-118 program, MX-121 `triggerTrack`, `perVoice`), i.e. the persistent state, never runtime voices.
- `editModulator` (line 111), new guard at line 120: when `after` equals `before` the command replies
  with the unchanged modulator JSON and records NOTHING — no layer write, no undo step, no
  `setModified()`. A changed edit keeps the exact old behaviour (write + one undo step + dirty).
- An invalid formula is still refused: `edit()` runs (and compiles) before the comparison.

Test file changed: `tests/src/core/ModulatorToolsTest.cpp` — new slot
`repeatingAnUnchangedEditRecordsNoUndoStep` (line 167). Both files are already fork-new entries in
`tests/fork-sources.txt`; no registration change was needed.

## Evidence — exact commands and EXIT codes

LOGS = `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-33.r1/logs`

| step | command | EXIT | log |
|---|---|---|---|
| RED build | `cmake --build build --target ModulatorToolsTest > $LOGS/build-1.log 2>&1` | 0 | build-1.log |
| **RED run** | `ctest -R ModulatorToolsTest --output-on-failure > $LOGS/test-1-red.log 2>&1` (from `build/tests`) | **8** | test-1-red.log |
| GREEN build | `cmake --build build --target zene ModulatorToolsTest > $LOGS/build-2.log 2>&1` | 0 | build-2.log |
| **GREEN run** | `ctest -R ModulatorToolsTest --output-on-failure > $LOGS/test-2-green.log 2>&1` | **0** | test-2-green.log |
| family build | `cmake --build build --target ModulationLayerValueTest ModulationLayerTest ModulationAudioViewTest ControlModulatorCommandsTest ModulationLayerProjectRoundTripTest ModulationRateTest ModulatorPanelTest > $LOGS/build-3.log 2>&1` | 0 | build-3.log |
| **family run** | `ctest -R "Modulator\|Modulation" --output-on-failure > $LOGS/test-4-family.log 2>&1` | **0** | test-4-family.log |
| revert fix | `git diff -- src/core/ControlCommandsModulatorTools.cpp > $LOGS/fix.patch; git apply -R $LOGS/fix.patch` | 0 / 0 | fix.patch |
| revert build | `cmake --build build --target zene ModulatorToolsTest > $LOGS/build-4-reverted.log 2>&1` | 0 | build-4-reverted.log |
| **revert run (test must catch the bug)** | `ctest -R ModulatorToolsTest --output-on-failure > $LOGS/test-5-reverted-red.log 2>&1` | **8** | test-5-reverted-red.log |
| re-apply fix | `git apply $LOGS/fix.patch` | 0 | — |
| re-apply build | `cmake --build build --target zene ModulatorToolsTest > $LOGS/build-5-restored.log 2>&1` | 0 | build-5-restored.log |
| **re-apply run** | `ctest -R ModulatorToolsTest --output-on-failure > $LOGS/test-6-restored-green.log 2>&1` | **0** | test-6-restored-green.log |
| undo family | `ctest -R "Undo" --output-on-failure > $LOGS/test-8-undo.log 2>&1` | 8 (see pre-existing) | test-8-undo.log |
| final family | `ctest -R "Modulator\|Modulation" --output-on-failure > $LOGS/test-10-final-family.log 2>&1` | **0** | test-10-final-family.log |
| **complexity gate** | `bash tests/complexity-gate.sh --check > $LOGS/gate-complexity-final.log 2>&1` | **0** | gate-complexity-final.log |
| **file-length gate** | `bash tests/file-length-gate.sh --check > $LOGS/gate-filelength-final.log 2>&1` | **0** | gate-filelength-final.log |

The failing assertion, identical in both RED runs (test-1-red.log and test-5-reverted-red.log):

```
FAIL!  : ModulatorToolsTest::repeatingAnUnchangedEditRecordsNoUndoStep() Compared values are not the same
   Actual   (Engine::projectJournal()->undoDepth()): 3
   Expected (beforeFormula + 1)                    : 2
   Loc: [../../tests/src/core/ModulatorToolsTest.cpp(180)]
```

i.e. the unchanged repeat of `modulator.formula_set` pushed one undo step too many (the fix removes
it: Actual 2 = Expected 2, and the same assertion pair for `modulator.trigger_set` at line 187).

**Test counts:** `ctest -R ModulatorToolsTest` = 1 test executed (5 sub-cases passed, 1 failed →
after the fix 6 passed, 0 failed). Affected family `ctest -R "Modulator|Modulation"` = **8 tests
executed, 8 passed** (test-10-final-family.log: `100% tests passed, 0 tests failed out of 8`).
Gate exits: complexity **0**, file-length **0** (both `PASS (check mode: no regressions; baseline
not written)`).

**Pre-existing, unrelated:** `DeviceMovesUndoTest` fails with AND without this fix —
`'!plugin.isEmpty()' returned FALSE. (the amplifier fixture must be built)` at
DeviceMovesUndoTest.cpp:117 (test-8-undo.log with the fix, test-9-devicemoves-nofix.log after
`git apply -R fix.patch`, both EXIT=8, same assertions). The build tree has no plugin binaries
(`plugin-scan: 0 file(s)`), so the fixture cannot load; not caused by this change.

## Files committed

- `src/core/ControlCommandsModulatorTools.cpp` (product fix)
- `tests/src/core/ModulatorToolsTest.cpp` (regression test)
- `docs/qwen-fixes-2026-10/cluster-new-p11-33.md` (this report)
