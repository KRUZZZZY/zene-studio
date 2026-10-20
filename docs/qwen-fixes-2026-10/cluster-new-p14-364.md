# Cluster new-p14-364: VST3 effect view exposes read-only parameters as knobs

**Verdict: FIXED**

The VST3 effect's generated view built one knob for every visible parameter. It filtered `hidden` but not `readOnly`, so a visible read-only meter got an editable knob. The fix skips read-only parameters in the knob grid, matching the VST3 instrument's generated view.

## Evidence

Still present before the fix (`plugins/Vst3Effect/Vst3EffectControlDialog.cpp`, the knob loop in the constructor):

- The loop filtered only `descriptor.hidden`, so the read-only "Meter" in the fixture got a knob. The RED run shows two knobs.

The fix (`plugins/Vst3Effect/Vst3EffectControlDialog.cpp:62-65`, filter at `:65`):

- `if (descriptor.hidden || descriptor.readOnly) { continue; }`, with a comment that matches `plugins/Vst3Instrument/Vst3InstrumentView.cpp:63-67`.

Sibling checks:

- `plugins/Vst3Instrument/Vst3InstrumentView.cpp:67` already has the same filter, so the effect and instrument views now agree.
- `plugins/Vst3Effect/Vst3EffectControls.cpp:52` and `:146` already treat `readOnly` as non-writable in the parameter fallback and save path. The change is consistent with those.
- Unchanged by design: `plugins/Vst3Effect/Vst3EffectControls.cpp:72-75` still connects every model (including read-only ones) to `setParamNormalized`. The instrument does the same (`plugins/Vst3Instrument/Vst3Instrument.cpp:164-166`). The finding's stated fix is the knob grid, so I did not change the model wiring. Once no knob exists, the UI path to write a read-only value is gone; the poll echo of the plug-in's own value remains, as it does on the instrument.

Regression test (`tests/src/plugins/Vst3InstrumentIntegrationTest.cpp`):

- `:195` declares `testReadOnlyParametersAreNotEffectKnobs`.
- `:872-891` hosts the test fixture's class (`Zene VST3 Test Instrument`, the one with the visible read-only "Meter") in the `vst3effect` module, as `testAnEffectsEditorOpensAtItsDeviceScale` does. It builds the generated view with `effect->controls()->createView()` and asserts exactly one knob, whose model is "Level".
- Includes added at `:77-78` (`EffectControlDialog.h`, `EffectControls.h`) for the view type.

No new files were added, so `tests/fork-sources.txt` and `tests/upstream-modifications.txt` are unchanged. The test file was already a product test not listed there.

## Commands and exit codes

Environment note: `ccache` could not create its temporary file under `/run/user/1000`, so the build needs `CCACHE_TEMPDIR=/tmp/ccache-tmp-new-p14-364` (set in the environment of each build below).

| # | Command (run from the worktree unless noted) | EXIT | Log |
| - | --- | --- | --- |
| 1 | `cmake --build build --target zene vst3effect vst3instrument Vst3InstrumentIntegrationTest -j4` | 2 | `build-1-red.log` (ccache temp-file permission error, before any test code was compiled) |
| 2 | same, with `CCACHE_TEMPDIR=/tmp/...` | 2 | `build-2-red.log` (my test used `EffectView.h`, which does not define `EffectControlDialog`; fixed to `EffectControlDialog.h` and `EffectControls.h`) |
| 3 | same, with `CCACHE_TEMPDIR=/tmp/...` | 0 | `build-3-red.log` (RED build, fix not applied) |
| 4 | `cd build/tests && ctest -R '^Vst3InstrumentIntegrationTest$' --output-on-failure` | 8 | `test-1-red.log` (**RED**: `FAIL! ... testReadOnlyParametersAreNotEffectKnobs()`, `Actual (knobs.size()): 2`, `Expected (1): 1`, `Loc: [...Vst3InstrumentIntegrationTest.cpp(888)]`, `Totals: 15 passed, 1 failed`) |
| 5 | `git diff -- plugins/Vst3Effect/Vst3EffectControlDialog.cpp > logs/fix.patch` | 0 | — |
| 6 | `git apply -R logs/fix.patch` | 0 | — |
| 7 | `cmake --build ...` (same targets, `build-5-4b-without-fix.log`) | 0 | — |
| 8 | `cd build/tests && ctest -R '^Vst3InstrumentIntegrationTest$' -V` | 8 | `test-3-4b-without-fix.log` (**FAIL without fix**: `Actual (knobs.size()): 2`, `Totals: 15 passed, 1 failed`) |
| 9 | `git apply logs/fix.patch` | 0 | — |
| 10 | `cmake --build ...` (same targets, `build-6-4b-with-fix.log`) | 0 | — |
| 11 | `cd build/tests && ctest -R '^Vst3InstrumentIntegrationTest$' -V` | 0 | `test-4-4b-with-fix.log` (**GREEN**: `Totals: 16 passed, 0 failed`; `100% tests passed out of 1`) |

Affected family (VST3 tests that compile or host the changed dialog):

| # | Command | EXIT | Log |
| - | --- | --- | --- |
| 12 | `cmake --build build --target Vst3EffectIntegrationTest Vst3HostTest Vst3ChunkProbeTest -j4` | 0 | `build-7-family.log` |
| 13 | `cmake --build build --target Vst3BusMapTest -j4` (was registered but unbuilt; first run of 5 ctest entries reported `Not Run`, not a failure of the code) | 0 | `build-8-busmap.log` |
| 14 | `cd build/tests && ctest -R '^(Vst3BusMapTest\|Vst3HostTest\|Vst3ChunkProbeTest\|Vst3EffectIntegrationTest\|Vst3InstrumentIntegrationTest)$' -V` | 0 | `test-6-family.log`: `100% tests passed out of 5`; 60 QTest `PASS` lines |

Affected-family count: 5 ctest tests (`Vst3BusMapTest`, `Vst3HostTest`, `Vst3ChunkProbeTest`, `Vst3EffectIntegrationTest`, `Vst3InstrumentIntegrationTest`), all passing with the fix applied. `Vst3InstrumentFixtureProbe` and `Vst3InstrumentTest` are registered but not built in this tree; they were not run.

Gates. `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` exits 1 before running any gate, with `PermissionError` creating `<worktree>-feature-check/` outside the worktree (`feature_check.py:71`, log not kept because it never started). I ran the same eight commands it runs, from the worktree:

| Gate | Command | EXIT | Log |
| --- | --- | --- | --- |
| fork-sources | `bash tests/fork-sources-gate.sh` | 0 | `gate-fork-sources.log` |
| all-sources-reproduce | `bash tests/all-sources-reproduce.sh` | 0 | `gate-all-sources-reproduce.log` |
| no-upstream-regression | `bash tests/no-upstream-regression-gate.sh` | 0 | `gate-no-upstream-regression.log` |
| check-namespace | `tests/scripted/check-namespace` | 0 | `gate-check-namespace.log` |
| no-tautology | `bash tests/no-tautology-gate.sh` | 0 | `gate-no-tautology.log` |
| doc-figures | `python3 tests/doc-figures-gate.py` | 0 | `gate-doc-figures.log` |
| complexity (ratchet) | `bash tests/complexity-gate.sh --check` | 1 | `gate-complexity.log`: 6 `REGRESSION` lines, none naming `Vst3EffectControlDialog` or `Vst3InstrumentIntegrationTest` (`rg -i Vst3` returns nothing) |
| file-length (ratchet) | `bash tests/file-length-gate.sh --check` | 1 | `gate-file-length.log`: 15 `REGRESSION` lines, none naming the two changed files |

The two ratchets are red on the train by design. Under the feature-check rule (only items new in changed files fail), neither ratchet has an item in my files, so those rows would PASS.

## Files committed

- `plugins/Vst3Effect/Vst3EffectControlDialog.cpp` (fix, one condition plus its comment)
- `tests/src/plugins/Vst3InstrumentIntegrationTest.cpp` (regression test, slot declaration, two includes)
- `docs/qwen-fixes-2026-10/cluster-new-p14-364.md` (this report)

## Self-check

- **The regression test FAILS with the product change reverted and PASSES with it.** RED: command 4, EXIT 8, `Actual (knobs.size()): 2`. Reverted again for 4b: command 8, EXIT 8, same assertion. With the fix: command 11, EXIT 0, 16/16 slots pass. Patch file: `logs/fix.patch`, saved from `git diff` before reverting, so the reverted state is the exact inverse.
- **The test is deterministic.** It has no timers, sleeps, or threads. It instantiates the fixture's class through the vst3effect factory and counts the knobs of a view that is built synchronously. The only state is the plug-in's own parameter list, which the fixture fixes (`tests/data/vst3-test-instrument/vst3-test-instrument.cpp:264-270`: "Level" is automatable and "Meter" is `kIsReadOnly`).
- **The fix is at the cause and complete.** The cause is the knob loop's filter, which is the only place that creates knobs for effect parameters (`rg -n "new Knob" plugins/Vst3Effect` finds one site). The sibling view (`Vst3InstrumentView.cpp:67`) already has the same filter. Model wiring is unchanged on both sides (`Vst3EffectControls.cpp:72-75`, `Vst3Instrument.cpp:164-166`), which is the same behaviour.
- **No collateral damage.** The change only removes read-only parameters from the knob grid. The data path is untouched: `saveSettings` already skips read-only parameters (`Vst3EffectControls.cpp:146`), and the fallback load already refuses them (`Vst3EffectControls.cpp:52`). Saved projects load as before. Hidden parameters behave as before. Editable parameters still get knobs: the test asserts "Level" keeps its knob. A plug-in whose only visible parameters are read-only now shows the existing "no visible parameters" label, the same as the instrument's view does; I did not change that label.
- **Not an ALREADY-FIXED or FALSE-POSITIVE case**, so no citation is needed beyond the evidence above.
- **Gates.** `feature_check.py` could not run in this write-jail (see the gates note). The eight gate commands it runs were run directly (table above): six exit 0; two ratchets exit 1 with no item in the changed files.
- **Realtime, undo, commands.** The change is in the view constructor only (GUI thread). It adds no audio-thread code, no undo step, and no control command. `ControlCommandsSnapshot` is unchanged.
- **Registration.** No new files. Both changed files are already listed or are test files outside the fork-source lists.
