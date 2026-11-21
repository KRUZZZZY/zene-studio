FIXED

new-p23-4: FIXED

Evidence: [ControlCommandsMacros.cpp](../../src/gui/ControlCommandsMacros.cpp) lines 145 and 184 now trim the lookup name in `removeWorkflow` and `runWorkflow`. `saveWorkflow` at line 125 and `recordStart` at line 241 already trim names. The added `ControlCommandsMacrosTest::paddedNameWorksForRun` and `paddedNameWorksForRemove` prove both paths independently.

RED/GREEN (patch contains only this cluster's two `.trimmed()` calls):

| Phase | Exact command | Exit |
| --- | --- | ---: |
| Reverse product patch | `git diff -- src/gui/ControlCommandsMacros.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p23-4.r1/logs/fix-new-p23-4.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p23-4.r1/logs/fix-new-p23-4.patch; echo EXIT=$?` | 0 |
| Rebuild reverted version | `cmake --build build --target zene ControlCommandsMacrosTest -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p23-4.r1/logs/build-5.log 2>&1; echo EXIT=$?` | 0 |
| RED: run | `./ControlCommandsMacrosTest paddedNameWorksForRun > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p23-4.r1/logs/red-6.log 2>&1; echo EXIT=$?` (cwd `build/tests`) | 1 |
| RED: remove | `./ControlCommandsMacrosTest paddedNameWorksForRemove > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p23-4.r1/logs/red-7.log 2>&1; echo EXIT=$?` (cwd `build/tests`) | 1 |
| Restore product patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p23-4.r1/logs/fix-new-p23-4.patch; echo EXIT=$?` | 0 |
| Rebuild fixed version | `cmake --build build --target zene ControlCommandsMacrosTest -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p23-4.r1/logs/build-6.log 2>&1; echo EXIT=$?` | 0 |
| GREEN: run and remove | `./ControlCommandsMacrosTest paddedNameWorksForRun > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p23-4.r1/logs/green-1.log 2>&1; echo EXIT=$?` and `./ControlCommandsMacrosTest paddedNameWorksForRemove > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p23-4.r1/logs/green-2.log 2>&1; echo EXIT=$?` (cwd `build/tests`) | 0 each |

The RED replies both said `no workflow` with the padded name. The GREEN calls acknowledge `macro run whitespace` and `macro remove whitespace`; the plain remove after removal still returns `NotFound`.

new-p22-32: FIXED

Evidence: `stepsRefusal` in [ControlCommandsMacros.cpp](../../src/gui/ControlCommandsMacros.cpp#L102) now requires each step to be an object, its `command` to be a string naming a registered non-workflow command, and `args` to be an object accepted by that command's `argsSchema` through `ControlRegistry::validateArgs`. This check runs in `saveWorkflow` before storage. `recordStop` uses the same `saveWorkflow` path. Existing stored project/shared workflows are read without migration or revalidation. The data-driven regression covers valid/malformed shapes, a schema-invalid `transport.seek`, and the existing 64/65 step boundary.

RED/GREEN (the inverse patch contains only the new `stepsRefusal` validation):

| Phase | Exact command | Exit |
| --- | --- | ---: |
| Reverse product patch | `git diff -- src/gui/ControlCommandsMacros.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p23-4.r1/logs/fix-new-p22-32.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p23-4.r1/logs/fix-new-p22-32.patch; echo EXIT=$?` | 0 |
| Rebuild reverted validator | `cmake --build build --target zene ControlCommandsMacrosTest -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p23-4.r1/logs/build-6.log 2>&1; echo EXIT=$?` | 0 |
| RED | `./ControlCommandsMacrosTest stepShapeValidation > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p23-4.r1/logs/red-8.log 2>&1; echo EXIT=$?` (cwd `build/tests`) | 3 |
| Restore product patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p23-4.r1/logs/fix-new-p22-32.patch; echo EXIT=$?` | 0 |
| Rebuild fixed validator | `cmake --build build --target zene ControlCommandsMacrosTest -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p23-4.r1/logs/build-7.log 2>&1; echo EXIT=$?` | 0 |
| GREEN | `./ControlCommandsMacrosTest stepShapeValidation > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p23-4.r1/logs/green-3.log 2>&1; echo EXIT=$?` (cwd `build/tests`) | 0 |

The reverted validator accepted the missing-args, nonobject-args, and schema-invalid rows. The final family run also passed the added exact-64 and over-64 boundary rows.

new-p23-5: FALSE-POSITIVE

Evidence: `recordStep` at line 230 is only installed as the command observer by `recordStart`. The real dispatch calls `notifyObserver(commandId, args, mutating, changed)` at [ControlRegistry.cpp](../../src/core/ControlRegistry.cpp#L552), and [ControlRegistry.h](../../include/ControlRegistry.h#L299) notifies only when the command is mutating, changed successfully, and top-level. `transport.get_state` is nonmutating by the default at `ControlRegistry.h:97` and has no mutating override in `ControlCommandsTransport.cpp:309-325`. Thus it cannot reach `recordStep` on the product path. The test `recordingIgnoresReadOnlyCommands` passes on the branch with the existing guard; no product change is needed for this finding.

Negative control: temporarily changed the `ControlRegistry.cpp:552` call to `notifyObserver(commandId, args, true, true)`, saved that ablation as `ablation-new-p23-5.patch`, and rebuilt. This deliberately bypasses the real dispatch flags; it is a test-sensitivity check, not a committed change.

| Phase | Exact command | Exit |
| --- | --- | ---: |
| Save ablation patch | `git diff -- src/core/ControlRegistry.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p23-4.r1/logs/ablation-new-p23-5.patch; echo EXIT=$?` | 0 |
| Normal dispatch GREEN | `./ControlCommandsMacrosTest recordingIgnoresReadOnlyCommands > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p23-4.r1/logs/green-5.log 2>&1; echo EXIT=$?` (cwd `build/tests`) | 0 |
| Build ablation | `cmake --build build --target zene ControlCommandsMacrosTest -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p23-4.r1/logs/build-9.log 2>&1; echo EXIT=$?` | 0 |
| Ablation RED | `./ControlCommandsMacrosTest recordingIgnoresReadOnlyCommands > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p23-4.r1/logs/red-9.log 2>&1; echo EXIT=$?` (cwd `build/tests`) | 1 |
| Restore registry call | `git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p23-4.r1/logs/ablation-new-p23-5.patch; echo EXIT=$?` | 0 |
| Rebuild original dispatch | `cmake --build build --target zene ControlCommandsMacrosTest -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p23-4.r1/logs/build-10.log 2>&1; echo EXIT=$?` | 0 |
| Restored dispatch GREEN | `./ControlCommandsMacrosTest recordingIgnoresReadOnlyCommands > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p23-4.r1/logs/green-6.log 2>&1; echo EXIT=$?` (cwd `build/tests`) | 0 |

The ablation's failure showed one recorded step immediately after the read-only query. The original dispatch records no read-only step and still records a changed `transport.seek`; the test undoes that playhead change and removes its temporary workflow.

## Regression matrix and gates

`ControlCommandsMacrosTest` exercises padded run/remove, valid and invalid saved steps, args schema validation, the 64-step limit, and recording of read-only versus changed commands. It uses a `QTemporaryDir`, fixed JSON values, and synchronous command calls; it has no sleeps or wall-clock assertions. The workflow run assertion checks its immediate queued acknowledgement only. `CustomAccessTest::workflowsAreKeptWithTheProjectWhenChosen` and `ControlCustomisation` cover project/shared scope and the command-facing save/run/remove flow. All commands run on the UI/control path; no audio-thread code, command registration, command snapshot, or reversibility row changed. Valid store/remove operations retain their existing single uistore undo step; rejected saves return before a write.

| Check | Result |
| --- | --- |
| Affected CTest family | EXIT=0; 4/4 tests passed |
| Feature gates | EXIT=0; fork-sources, all-sources-reproduce, namespace, tautology, doc-figures, complexity and file-length all PASS |

Exact family command (cwd `build/tests`): `ctest -R 'ControlCommandsMacrosTest|WorkflowFeedbackTest|CustomAccessTest|ControlCustomisation' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p23-4.r1/logs/test-1.log 2>&1; echo EXIT=$?` (EXIT=0).

Exact gate command: `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p23-4 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p23-4.r1/logs/gate-1.log 2>&1; echo EXIT=$?` (EXIT=0).

The whole-tree manifest was rederived from its published recipe after adding this test. That removed 1,148 stale `plugins/SfzPlayer/sfizz/` vendor paths excluded by the recipe and added `tests/src/gui/ControlCommandsMacrosTest.cpp`; `all-sources-reproduce` passes.

Post-commit completion command: `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/submit_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p23-4 new-p23-4 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p23-4.r1/logs/submit-1.log 2>&1; echo EXIT=$?` (EXIT=0; all checks PASS).

## Self-check

- The two real fixes each have an inverse product patch, a rebuilt RED run, a restored patch, a rebuilt GREEN run, and recorded exit codes above. The false-positive case has no product fix to revert: its ordinary path passes, and the deliberate registry-dispatch ablation fails the test before the original call is restored and passes again.
- The tests are deterministic: fixed whitespace and JSON inputs, an isolated temporary home, fixed schema cases, and synchronous registry invocations. There are no sleeps, races, or time-based checks.
- The name siblings are consistent: `workflow.save` and `workflow.record_start` already trim; `workflow.remove` and `workflow.run` now trim. `workflow.resume` takes only `skip` and resumes the normalized saved name; `workflow.list`, `workflow.status`, `workflow.record_stop`, and `workflow.set_scope` do not look up a caller-supplied name. Step validation also covers `workflow.record_stop`, which delegates to `saveWorkflow`.
- The submit check's transport siblings are unaffected: `transport.audition_start`, `transport.audition_stop`, `transport.get_state`, `transport.groove`, `transport.low_latency`, `transport.metronome_options`, `transport.midi_options`, `transport.panic`, `transport.play`, `transport.punch_clear`, `transport.punch_get_state`, `transport.punch_here`, `transport.punch_set`, `transport.record`, and `transport.record_options` have no handler or registration changes. The regression uses `transport.get_state` as a valid read-only step and `transport.seek` to exercise its own schema and the mutating recorder path; neither command's behavior was changed.
- No other callers are constrained by the new check: it validates only newly submitted workflow steps against the exact command schema. Existing project/shared data reads are unchanged; the save/load scope round-trip and command flow passed in `CustomAccessTest` and `ControlCustomisation`. A rejected save writes nothing; valid save/remove retain their existing one-step uistore inverses. No commands or reversibility classes were added.
- No new code runs on the audio thread. The changed product code is the GUI workflow handler; registry observers run only from successful top-level UI/control command dispatch. The recorder regression's `transport.seek` is synchronous and undone before cleanup.
- Portability review: the change uses Qt JSON and `QString::trimmed()` APIs available on Qt 5.15 and C++20; it adds no Windows headers/macros, filesystem code, or platform-specific branches. The local build was Linux; no cross-platform build was run.
