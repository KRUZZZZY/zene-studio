ALREADY-FIXED

new-p14-249: ALREADY-FIXED

Decision: REAL test flaw, already corrected in commit `8327def668c9f284c70e5795449e6367fc0035bc`. The regression at `tests/src/core/ReversibilityUndoTest.cpp:383-460` saves the original state, changes a numeric parameter, loads the saved state, confirms the load replaced the changed value, follows the recorded inverse, and checks that the changed pre-load value returns.

RED/GREEN evidence is from the prior round and was accepted by Luna: the wrong-state closure mutation failed the readback assertion, and the restored implementation passed.

- `git diff -- src/core/ControlCommandsPluginState.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p14-249.r3/logs/003-mutation-249.patch` — EXIT=0.
- `cmake --build build --target zene ReversibilityUndoTest` with the wrong-state mutation — EXIT=0 (`004-mutation-249-build.log`).
- `ctest -R '^ReversibilityUndoTest$' --output-on-failure` with the mutation — EXIT=8 (`005-red-249-ctest.log`).
- `git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p14-249.r3/logs/003-mutation-249.patch` — EXIT=0.
- `cmake --build build --target zene ReversibilityUndoTest` after restoring — EXIT=0 (`006-green-249-build.log`).
- `ctest -R '^ReversibilityUndoTest$' --output-on-failure` — EXIT=0, 1/1 (`007-green-249-ctest.log`).

5281: ALREADY-FIXED

Decision: REAL test flaw, already corrected in commit `6a38c2aa6c843acdcde76a028986a550c412ec38`. The regression at `tests/src/core/ReversibilityUndoDeviceTest.cpp:332-403` no longer pushes a newer undo step: it unloads through `plugin.unload`, undoes that command, then injects a restore refusal and checks that the same command's `control.undo` refuses while the device remains absent.

- `git diff -- src/core/ControlCommandsControl.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p14-249.r3/logs/008-mutation-5281.patch` — EXIT=0.
- `cmake --build build --target zene ReversibilityUndoDeviceTest` with the discarded-failure mutation — EXIT=0 (`009-mutation-5281-build.log`).
- `ctest -R '^ReversibilityUndoDeviceTest$' --output-on-failure` with the mutation — EXIT=8 (`010-red-5281-ctest.log`).
- `git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p14-249.r3/logs/008-mutation-5281.patch` — EXIT=0.
- `cmake --build build --target zene ReversibilityUndoDeviceTest` after restoring — EXIT=0 (`011-green-5281-build.log`).
- `ctest -R '^ReversibilityUndoDeviceTest$' --output-on-failure` — EXIT=0, 1/1 (`012-green-5281-ctest.log`).

new-p8-205: FIXED

Decision: REAL coverage gap. The five named rows now apply the mutating command, run its recorded inverse or `control.undo`, and compare the owning state with the exact prior value. `arrangement.fit_tracks` seeds distinct heights and compares project-file readbacks before the command and after undo.

Evidence locations:

- `record.input_set`: `tests/control-record-inputs.py:224-237` captures the full configured plan, writes 8 channels, undoes, and compares all plan fields before reapplying 8 for the existing restart check.
- `export.set_loudness_report`: `tests/control-render-surround.py:161-171` captures the prior setting, enables it, undoes, and reads the exact prior boolean back.
- `vca.set_solo`: `tests/control-vca-commands.py:119-133,251-270` captures every VCA solo flag and mixer mute, reads the latest transaction's inverse, replays it, and compares both maps.
- `transport.punch_clear`: `tests/control-punch-transcript.py:273-291` sets a known region, clears it, undoes, compares the complete punch state, then clears again for the existing no-region save check.
- `arrangement.fit_tracks`: `tests/control-customisation.py:45-56,396-413` seeds distinct heights, saves and parses each track's `trackheight`, then compares that list with a second project-file readback after undo.

Each row was mutation-tested in isolation. Mutation patches and build/test logs are under `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p14-249.r4/logs/`.

| Row | Isolated inverse defect | RED build / test | Restored GREEN build / test |
|---|---|---|---|
| `record.input_set` | Recorded inverse uses the new plan instead of the previous plan. | Build EXIT=0 (`006`); `ctest -R '^ControlRecordInputs$' --output-on-failure` EXIT=8 (`007`), exact-plan assertion failed. | Build EXIT=0 (`008`); same CTest EXIT=0 (`009`), 1/1. |
| `export.set_loudness_report` | Undo closure writes the requested value instead of the captured previous value. | Build EXIT=0 (`011`); `ctest -R '^ControlRenderSurround$' --output-on-failure` EXIT=8 (`012`), loudness readback assertion failed. | Build EXIT=0 (`013`). Full CTest still exits 8 on two separate CLI diagnostic-text checks; the focused socket checks containing the loudness inverse pass, EXIT=0 (`015`, repeated as `052`). |
| `vca.set_solo` | Recorded inverse args request solo=true instead of restoring solo=false. | Build EXIT=0 (`035`); `ctest -R '^ControlVcaCommands$' --output-on-failure` EXIT=8 (`036`), group flags and mixer mutes differed. | Build EXIT=0 (`037`); same CTest EXIT=0 (`038`), 1/1. |
| `transport.punch_clear` | Move the Timeline checkpoint after clearing, so undo captures the empty state. | Build EXIT=0 (`040`); `ctest -R '^ControlPunchTranscript$' --output-on-failure` EXIT=8 (`041`), exact punch state differed. | Build EXIT=0 (`042`); same CTest EXIT=0 (`043`), 1/1. |
| `arrangement.fit_tracks` | Undo closure applies fitted heights instead of the captured prior heights. | Build EXIT=0 (`045`); `ctest -R '^ControlCustomisation$' --output-on-failure` EXIT=8 (`046`), saved heights remained uniform. | Build EXIT=0 (`047`); same CTest EXIT=0 (`048`), 1/1. |

The final five-test CTest run executed all five affected families: 4/5 passed; `ControlRenderSurround` exited nonzero only because its existing `check_cli` checks at `tests/control-render-surround.py:114-143` received no diagnostic text for two invalid CLI channel cases. The new loudness inverse check passed in that run, and its focused socket portion passed separately. This limitation is recorded rather than reported as a full CTest pass (`051-final-p8-ctest.log`; focused run `052-green-export-focused.log`).

## Final evidence

- `cmake --build build --target zene ReversibilityUndoTest ReversibilityUndoDeviceTest` — EXIT=0 (`049-green-p14-build.log`).
- `ctest -R '^ReversibilityUndoTest$|^ReversibilityUndoDeviceTest$' --output-on-failure` — EXIT=0, 2/2 (`050-green-p14-ctest.log`).
- Five affected p8 CTests: `ctest -R '^(ControlRecordInputs|ControlRenderSurround|ControlVcaCommands|ControlPunchTranscript|ControlCustomisation)$' --output-on-failure` — EXIT=8, 4/5 passed (`051-final-p8-ctest.log`).
- `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p14-249` — EXIT=0 (`054-feature-check-final.log`): all gates PASS, including complexity and file-length.
- The focused export socket run uses the same `check_the_instance` path with only the unrelated CLI diagnostics check excluded; EXIT=0 (`052-green-export-focused.log`).

## Self-check

- Decisions: both ReversibilityUndoTest findings are REAL and ALREADY-FIXED with prior RED/GREEN proof; new-p8-205 is FIXED by five behavioral checks.
- Regression proof: each p8 row has an isolated inverse mutation that fails its named readback, then passes after restoration. State-load and unload mutation proofs remain in the prior-round r3 logs accepted by Luna.
- Determinism: no sleeps, races, or wall-clock assumptions were added. VCA uses a recorded inverse; fit-tracks compares persisted heights; other rows compare command readbacks.
- Scope: no production C++ changes remain. Only the five existing regression scripts and this report are changed; project formats and audio-thread code are untouched.
- Test status: the undo CTest family passes 2/2. Four p8 CTests pass; the fifth has the separately documented CLI diagnostic failure, while the requested loudness socket assertions pass.
- Gate status: `feature_check.py` EXIT=0. `submit_check.py` is run after this report is written; its result is recorded after the run.

Files for this commit:

- `tests/control-record-inputs.py`
- `tests/control-render-surround.py`
- `tests/control-vca-commands.py`
- `tests/control-punch-transcript.py`
- `tests/control-customisation.py`
- `docs/qwen-fixes-2026-10/cluster-new-p14-249.md`
