# Cluster new-p9-214: FIXED

## Finding and change

The first round guarded `LadspaEffect::changeSampleRate()` when controls are missing and notified the open editor with `nullptr` when re-instantiation failed. Luna's review found that the notification reached `LadspaMatrixControlDialog::updateEffectView()`, which still dereferenced its controls. This round makes that notification safe: it detaches the dialog model, replaces the matrix with an empty widget, and unsets and hides the stereo-link model before the old controls are deleted.

Evidence in the source:

- `plugins/LadspaEffect/LadspaEffect.cpp:107` is the sample-rate handler; line 127 emits the null notification before the previous controls are deleted.
- `plugins/LadspaEffect/LadspaMatrixControlDialog.cpp:185` handles the null notification; lines 189, 191, and 194 detach the dialog model, clear the matrix widgets, and unset the stereo-link model.
- `tests/src/plugins/PluginPortsMigrationTest.cpp:288` is the regression. It creates an editor at line 329, forces re-instantiation failure at line 337, and verifies controls are null while the editor remains open.

## Validation evidence

Worktree: `/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-214`. Build and test logs are under `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-214.r2/logs`. Every build below used `cmake --build build --target zene PluginPortsMigrationTest`, with stdout and stderr redirected to the named log and its exit code appended there.

| Phase | Exact test command (from `build/tests`) | Exit and result |
| --- | --- | --- |
| RED, editor open before dialog fix | `ctest -R '^PluginPortsMigrationTest$' --output-on-failure` | 8; 1/1 CTest test failed with SIGSEGV (signal 11) in the null notification path (`red-1.log`). |
| GREEN, dialog fix restored | `ctest -R '^PluginPortsMigrationTest$' --output-on-failure` | 0; 1/1 passed (`green-1.log`). |
| RED, only production fix reversed | `ctest -R '^PluginPortsMigrationTest$' --output-on-failure` | 8; 1/1 failed again with SIGSEGV after rebuilding (`red-2.log`). |
| GREEN, production fix restored | `ctest -R '^PluginPortsMigrationTest$' --output-on-failure` | 0; 1/1 passed after rebuilding (`green-2.log`). |

The affected CTest family executed 1 test (`PluginPortsMigrationTest`); no zero-test run occurred. Build command results: `build-1.log` exit 2 and `build-2.log` exit 2 while correcting test-only linkage to the plugin dialog constructor; `build-3.log` exit 0 for the red run; `build-4.log` exit 0 for the first green run; `build-5.log` exit 0 with the fix reversed; and `build-6.log` exit 0 after restoring the fix. The first two attempts failed at link time and did not run CTest.

Reverse-patch proof commands and results:

1. `git diff -- plugins/LadspaEffect/LadspaMatrixControlDialog.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-214.r2/logs/fix.patch` — exit 0.
2. `git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-214.r2/logs/fix.patch` — exit 0.
3. `cmake --build build --target zene PluginPortsMigrationTest` — exit 0 (`build-5.log`).
4. `ctest -R '^PluginPortsMigrationTest$' --output-on-failure` from `build/tests` — exit 8 (`red-2.log`).
5. `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-214.r2/logs/fix.patch` — exit 0.
6. `cmake --build build --target zene PluginPortsMigrationTest` — exit 0 (`build-6.log`).
7. `ctest -R '^PluginPortsMigrationTest$' --output-on-failure` from `build/tests` — exit 0 (`green-2.log`).

Ratchet gates after the final source and report edits:

- `bash tests/complexity-gate.sh --check` — exit 0 (`gate-3.log`).
- `bash tests/file-length-gate.sh --check` — exit 0 (`gate-4.log`).

## Files in this commit

- `plugins/LadspaEffect/LadspaMatrixControlDialog.cpp`
- `tests/src/plugins/PluginPortsMigrationTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p9-214.md`
