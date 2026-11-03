FIXED - LadspaControlDialog::updateEffectView() no longer dereferences a null controls pointer; the regression SIGSEGVs without the fix (EXIT 8) and passes with it (EXIT 0).

## Verdict

**REAL, and FIXED.** `LadspaControlDialog::updateEffectView()` dereferenced its `LadspaControls *` argument unconditionally. The \`LadspaEffect::changeSampleRate()\` reconfiguration failure path emits \`effectModelChanged(nullptr)\` (`plugins/LadspaEffect/LadspaEffect.cpp:138`), and the dialog connects that signal to \`updateEffectView\` with \`Qt::DirectConnection\` (`plugins/LadspaEffect/LadspaControlDialog.cpp:143-145`), so the notification lands inside the failed sample-rate change.

Note on reachability: in this tree \`EffectControls::createView()\` returns the matrix dialog (`plugins/LadspaEffect/LadspaControls.h:63-66`), so the plain dialog is not constructed by the shipping UI path; it is compiled into and shipped with the ladspaeffect module. The crash path is real for the shipped class and the fix mirrors the sibling dialog, which already guards the same notification (`plugins/LadspaEffect/LadspaMatrixControlDialog.cpp:187`).

## Evidence (file:line, this tree)

- `plugins/LadspaEffect/LadspaControlDialog.cpp:70` — \`updateEffectView\`; lines 78-91 are the new null guard (detach via \`setModel(nullptr)\`, null \`m_effectControls\`, unset and hide \`m_stereoLink\` when present), mirroring the matrix dialog's null path at `plugins/LadspaEffect/LadspaMatrixControlDialog.cpp:187-198`. The pre-existing group-box deletion above the guard now also clears the view for the null case. Line 92 onwards is the unchanged non-null path.
- `plugins/LadspaEffect/LadspaEffect.cpp:138` — \`controls->effectModelChanged( nullptr );\` emitted when re-instantiation fails after the swap; the audio thread never sees this (the emit is on the GUI thread, after \`m_reconfiguring\` is cleared and in-flight blocks have drained).
- Sibling with the same contract: `plugins/LadspaEffect/LadspaMatrixControlDialog.cpp:185` handles the same signal with \`&LadspaMatrixControlDialog::updateEffectView\` and a \`ladspaControls == nullptr\` early return — the pattern this fix mirrors.
- Callers of \`updateEffectView\` (both null-safe after this change): the constructor (`LadspaControlDialog.cpp:54`, non-null by construction) and the DirectConnection slot wiring (`LadspaControlDialog.cpp:143-145`).

## Regression test

`tests/src/plugins/PluginPortsMigrationTest.cpp` — new slot \`ladspaControlDialogSurvivesFailedInstantiationsDuringSampleRateChange()\`. It loads the migrated ladspaeffect module, instantiates the real effect through \`lmms_plugin_main\`, constructs \`lmms::gui::LadspaControlDialog\` on the live controls model, then forces re-instantiation failure inside \`Engine::audioEngine()->sampleRateChanged()\` (via the test's \`LadspaInstantiationFailure\` seam) and requires the effect to be not okay with no controls; without the fix the process dies with SIGSEGV inside the emit. The ctest family \`^PluginPortsMigrationTest$\` runs it (1/1 test, containing all slots in the binary).

A missing in-tree SWH fixture (\`decimator_1202.so\`) had to be built for the unrelated \`nonRealtimeLadspaEffectsAreInvalid\` slot to pass; \`cmake --build build --target decimator_1202\` (EXIT 0) placed it at \`build/plugins/ladspa/decimator_1202.so\`, the directory \`LADSPA_PATH\` points at. No test, assertion, gate or ratchet was weakened.

## Validation evidence

Worktree: \`/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p21-207\` (base \`2a7fd9f9c\`). Logs: \`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p21-207.r1/logs\`. Builds used \`cmake --build build --target <targets>\` with output redirected to the named log; tests were run from \`build/tests\` only.

| Phase | Exact command | Exit and result |
| --- | --- | --- |
| Build (fix not yet applied) | \`cmake --build build --target PluginPortsMigrationTest\` > build-1.log | 0 |
| RED, base code + new regression | from \`build/tests\`: \`ctest -R '^PluginPortsMigrationTest$' --output-on-failure\` > red-1.log | 8; 1/1 failed, SIGSEGV (signal 11) at \`LadspaControlDialog.cpp\` null-deref inside the emitted \`effectModelChanged(nullptr)\` |
| Build with fix | \`cmake --build build --target zene PluginPortsMigrationTest\` > build-2.log | 0 |
| GREEN attempt (1 pre-existing fixture failure) | from \`build/tests\`: \`ctest -R '^PluginPortsMigrationTest$' --output-on-failure\` > green-1.log | 8; the new slot PASSED; the only FAIL was \`nonRealtimeLadspaEffectsAreInvalid\` (\`decimator_1202.so\` never built in this tree) |
| Build SWH fixture | \`cmake --build build --target decimator_1202\` > build-3.log | 0 |
| GREEN | from \`build/tests\`: \`ctest -R '^PluginPortsMigrationTest$' --output-on-failure\` > green-2.log | 0; 1/1 passed |
| Reverse-patch proof, step 1 | \`git diff -- plugins/LadspaEffect/LadspaControlDialog.cpp > .../fix.patch && git apply -R .../fix.patch\` | 0 (tool output: PATCH_OK / REVERSE_OK) |
| Reverse-patch proof, RED | \`cmake --build build --target PluginPortsMigrationTest\` > build-4.log; from \`build/tests\`: \`ctest -R '^PluginPortsMigrationTest$' --output-on-failure\` > red-2.log | build 0; ctest 8; SIGSEGV again in \`ladspaControlDialogSurvivesFailedInstantiationsDuringSampleRateChange\` |
| Reverse-patch proof, GREEN restored | \`git apply .../fix.patch\`; \`cmake --build build --target zene PluginPortsMigrationTest\` > build-5.log; from \`build/tests\`: \`ctest -R '^PluginPortsMigrationTest$' --output-on-failure\` > green-3.log | apply 0; build 0; ctest 0; 1/1 passed |

Gates after the final source edit: \`python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p21-207\` > gate-1.log — EXIT 0; every gate PASS (fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures, complexity "no new item in your files", file-length "no new item in your files").

## Files in this commit

- \`plugins/LadspaEffect/LadspaControlDialog.cpp\` — the fix (null guard, mirrors the matrix dialog)
- \`tests/src/plugins/PluginPortsMigrationTest.cpp\` — the regression slot
- \`tests/CMakeLists.txt\` — compiles the plain dialog into the migration test binary + include dir
- \`tests/upstream-modifications.txt\` — registers the three above
- \`docs/qwen-fixes-2026-10/cluster-new-p21-207.md\` — this report

No product header changed, so no dependent target needed a rebuild beyond \`zene\` and \`PluginPortsMigrationTest\` (both rebuilt, EXIT 0). No files added; \`tests/fork-sources.txt\` untouched. The audio thread is untouched (the change runs on the GUI thread inside the sample-rate handler). No new control commands; undo is unaffected; old projects and saved data load exactly as before (the null path only detaches views).

## Self-check

- Regression fails without the fix and passes with it: red-1.log / red-2.log (EXIT 8, SIGSEGV) vs green-2.log / green-3.log (EXIT 0, 1/1 passed).
- Test is deterministic: no sleeps, no wall-clock, no races; the failure is injected synchronously through \`LadspaInstantiationFailure\` inside \`sampleRateChanged()\` with \`Qt::DirectConnection\`.
- Fix is at the cause, complete and behaviour-preserving elsewhere: only \`updateEffectView\` changed; both callers accounted for; sibling matrix dialog already null-safe; gates pass.
- Not push/fetch/pull/merge/rebase/stash/reset; committed on \`bugs/qwen-new-p21-207\` with explicit paths only.
