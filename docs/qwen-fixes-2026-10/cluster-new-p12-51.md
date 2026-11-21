# Cluster new-p12-51 — WASM undo metadata

**Verdict: BLOCKED.** Source inspection shows the patrol finding is a false positive: these commands use the registry's recorded-command inverse path, not a `ProjectJournal` action. Required runtime verification is unavailable in this configured build, and both required ratchet gates fail on unrelated existing source entries.

## Finding assessment

Luna patrol reported that `wasm.load`, `wasm.unload` and `wasm.set_param` return inverse metadata without recording undo actions.

- [ControlCommandsWasmEdit.cpp:78](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p12-51/src/core/ControlCommandsWasmEdit.cpp:78), [108](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p12-51/src/core/ControlCommandsWasmEdit.cpp:108) and [224](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p12-51/src/core/ControlCommandsWasmEdit.cpp:224) build `__transaction` records whose inverses carry `applies: command` and the previous path or parameter value.
- [ControlRegistry.cpp:478](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p12-51/src/core/ControlRegistry.cpp:478) records each successful mutating call's transaction; [509](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p12-51/src/core/ControlRegistry.cpp:509) reads the payload and [531](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p12-51/src/core/ControlRegistry.cpp:531) stores it. This path does not require a nonzero journal step.
- [ControlCommandsControl.cpp:361](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p12-51/src/core/ControlCommandsControl.cpp:361) dispatches a recorded `applies: command` inverse through `registry.invoke` at [369](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p12-51/src/core/ControlCommandsControl.cpp:369). An `addUndoStep` is not required for this contract.
- [ControlReversibilityTableSnapshot.cpp:225](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p12-51/src/core/ControlReversibilityTableSnapshot.cpp:225) explicitly classifies the load row as a snapshot with a recorded command inverse; the unload and set_param rows are at [238](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p12-51/src/core/ControlReversibilityTableSnapshot.cpp:238) and [246](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p12-51/src/core/ControlReversibilityTableSnapshot.cpp:246). The existing socket transcript also asserts parameter restore and module re-hosting in [control-wasm-sandbox.py:416](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p12-51/tests/control-wasm-sandbox.py:416).

No production or test code was changed. The configured build has `WANT_WASM:BOOL=OFF`, so neither `ControlWasmSandbox` nor `ControlWasmRenderTest` is registered and the dynamic proof could not be run here.

## Verification evidence

| Item | Command / evidence | Result |
| --- | --- | --- |
| Finding classification | Registry records the transaction and `control.undo` invokes its command inverse | False positive by source contract |
| RED regression | Not applicable: no behavior change is warranted | Not run |
| GREEN regression | Not applicable: no behavior change is warranted | Not run |
| WASM test family | `ctest -N -R 'ControlWasmSandbox|ControlWasmRenderTest'` from `build/tests` | 0 registered; discovery only, no test execution |
| Build target listing | `cmake --build build --target help` from `build/tests` | Exit 1: the relative `build` path does not exist there; no target was built |
| Complexity gate | `bash tests/complexity-gate.sh --check` | Exit 1 |
| File-length gate | `bash tests/file-length-gate.sh --check` | Exit 1 |

The complexity gate reports six unrelated new functions above CCN 10 at `tests/src/gui/XyPadPairTest.cpp`, `src/core/DawProjectReadTracks.cpp`, `src/core/ControlCommandsMixerTools.cpp`, `src/core/ControlCommandsMixerStrip.cpp`, `src/core/ProjectRevisions.cpp` and `src/gui/ControlCommandsShortcuts.cpp`. The file-length gate reports fourteen unrelated new files above 500 code lines. No gate baseline was changed.

Commands and captured exits (all logs are under `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-51.r1/logs`):

```text
ctest -N -R 'ControlWasmSandbox|ControlWasmRenderTest' > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-51.r1/logs/01-ctest-discovery.log 2>&1; rc=$?; echo EXIT=$rc   EXIT=0 (Total Tests: 0)
cmake --build build --target help > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-51.r1/logs/07-build-help-discovery.log 2>&1; rc=$?; echo EXIT=$rc   EXIT=1 (build/tests/build is not a directory)
bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-51.r1/logs/09-complexity-gate-final.log 2>&1; rc=$?; echo EXIT=$rc   EXIT=1
bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-51.r1/logs/10-file-length-gate-final.log 2>&1; rc=$?; echo EXIT=$rc   EXIT=1
```

## Files committed

- `docs/qwen-fixes-2026-10/cluster-new-p12-51.md` (this blocked verification report; no product fix).
