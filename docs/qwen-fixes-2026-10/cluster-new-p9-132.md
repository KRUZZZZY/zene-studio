# Cluster new-p9-132

**Verdict: FALSE-POSITIVE**

## Evidence

At [EffectSelectDialog.cpp:202](../../src/gui/modals/EffectSelectDialog.cpp#L202), the picker creates its selection model for `m_model`. After installation on the table, a runtime probe found the selection model's QObject parent was `DualColumnFilterProxyModel`, the `m_model` member declared at [EffectSelectDialog.h:115](../../include/EffectSelectDialog.h#L115). A `QPointer` lifetime probe passed after the dialog went out of scope, so the model destroys its selection model with the picker. The observed parent and lifetime contradict the finding's claimed leak. Qt documents the model-only and model-plus-parent constructor overloads in its [QItemSelectionModel API](https://doc.qt.io/qt-6/qitemselectionmodel.html).

No production fix or permanent regression test was added. The temporary probe expecting the parent to be the dialog itself failed because the actual parent is the proxy model; that assertion was too strict and does not indicate a leak.

## Verification evidence

The commands below ran from `build/tests` for ctest and from the worktree root for builds and gates. `fff grep` was not available in this session, so the log folder was searched with `rg` using the requested terms one at a time.

| Check | Exact command | Exit | Result |
|---|---|---:|---|
| GREEN evidence (lifetime on unchanged code) | `ctest -R EffectSelectDialogTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-132.r1/logs/test-1.log 2>&1; code=$?; echo EXIT=$code; exit $code` | 0 | 1/1 ctest passed; the selection model's `QPointer` was null after dialog destruction. |
| RED evidence (dialog-parent assumption only) | `ctest -R EffectSelectDialogTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-132.r1/logs/test-2.log 2>&1; code=$?; echo EXIT=$code; exit $code` | 8 | The temporary assertion expecting `parent() == &dialog` failed; Qt reported `DualColumnFilterProxyModel` as the parent. This falsifies the assumption; it is not a valid regression red. |
| Affected ctest family count | — | — | 1 ctest target per invocation; run twice as the two exploratory assertions above. |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-132.r1/logs/gate-1.log 2>&1; code=$?; echo EXIT=$code; exit $code` | 0 | Passed. |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-132.r1/logs/gate-2.log 2>&1; code=$?; echo EXIT=$code; exit $code` | 0 | Passed. |

There is no valid fix red/green pair because no product bug was found. Build commands and their exit codes are recorded here as well:

| Build log | Exact command | Exit | Result |
|---|---|---:|---|
| `build-1.log` | `cmake --build build --target EffectSelectDialogTest zene > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-132.r1/logs/build-1.log 2>&1; code=$?; echo EXIT=$code; exit $code` | 2 | The new target was not yet present in generated build files. |
| `build-2.log` | `cmake --build build --target zene > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-132.r1/logs/build-2.log 2>&1; code=$?; echo EXIT=$code; exit $code` | 0 | Rebuilt `zene`; CMake's normal build check regenerated the test target. |
| `build-3.log` | `cmake --build build --target EffectSelectDialogTest zene > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-132.r1/logs/build-3.log 2>&1; code=$?; echo EXIT=$code; exit $code` | 2 | Temporary probe needed `using namespace lmms` for `ConfigManager`; corrected before the successful builds. |
| `build-4.log` | `cmake --build build --target EffectSelectDialogTest zene > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-132.r1/logs/build-4.log 2>&1; code=$?; echo EXIT=$code; exit $code` | 0 | Built the lifetime probe. |
| `build-5.log` | `cmake --build build --target EffectSelectDialogTest zene > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-132.r1/logs/build-5.log 2>&1; code=$?; echo EXIT=$code; exit $code` | 0 | Built the dialog-parent assumption probe. |

## Files committed

- `docs/qwen-fixes-2026-10/cluster-new-p9-132.md` — this false-positive report.
