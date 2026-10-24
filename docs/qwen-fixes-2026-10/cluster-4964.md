BLOCKED

# Cluster 4964 — Consent dialog reports saved after rejection

Finding: REAL. Before the fix, `openTelemetryConsentScreen()` discarded `dialog.exec()` and inserted literal `true` for `saved`. The Help action and registry handler both use this shared screen (`src/gui/MainWindow.cpp:551`, `src/core/ControlCommandsTelemetry.cpp:288`).

The dialog's Save button persists consent but leaves the dialog open (`src/gui/TelemetryConsentDialog.cpp:129`), so the final `exec()` result describes the later close and cannot serve as the save result. The dialog now keeps an invocation-local `zeneConsentSaved` marker, initially false and set true after Save. The command reports `dialog.wasSaved()` alongside the state reloaded from the existing consent store (`include/TelemetryConsentDialog.h:51`, `src/gui/TelemetryConsentDialog.cpp:41`, `src/gui/TelemetryConsentDialog.cpp:132`, `src/core/ControlCommandsTelemetry.cpp:260`). The Help action still shares the same implementation, and the read-only `telemetry.status` sibling is unchanged.

The regression runs the modal dialog with queued Qt callbacks: one closes without saving; the other clicks Save and then Close. Both `exec()` calls return Rejected, while only the latter reports the save marker. The test has no sleeps or wall-clock timing (`tests/src/gui/TelemetryConsentDialogTest.cpp:163-183`).

## Evidence

| Check | Command (working directory) | Result | Log / evidence |
| --- | --- | --- | --- |
| RED, original code | `ctest -R TelemetryConsentDialogTest --output-on-failure` (`build/tests`) | EXIT=8; 5 passed, 1 failed at the saved marker assertion | `test-1.log` |
| GREEN | Same focused command (`build/tests`) | EXIT=0; 1/1 test passed | `test-2.log` |
| Telemetry family | `ctest -R Telemetry --output-on-failure` (`build/tests`) | EXIT=0; 3/3 passed (`TelemetryTest`, `TelemetryTransportTest`, `TelemetryConsentDialogTest`) | `test-3.log`, `test-6.log`, `test-7.log` |
| Reverse-patch proof | `git diff -- include/TelemetryConsentDialog.h src/core/ControlCommandsTelemetry.cpp src/gui/TelemetryConsentDialog.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/4964.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/4964.r1/logs/fix.patch` | EXIT=0 | `fix.patch` contains product files only |
| RED, product patch reversed | Focused ctest command (`build/tests`) after rebuilding | EXIT=8; same saved marker assertion failed | `build-4.log`, `test-4.log` |
| Restore patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/4964.r1/logs/fix.patch` | EXIT=0 | Product fix restored |
| GREEN, restored | Focused ctest command (`build/tests`) after rebuilding | EXIT=0; 1/1 passed | `build-5.log`, `test-5.log` |

Build commands all exited 0: `cmake --build build --target zene TelemetryConsentDialogTest` (`build-1.log`, `build-2.log`, `build-4.log`, `build-5.log`, `build-6.log`) and `cmake --build build --target zene TelemetryTest TelemetryTransportTest TelemetryConsentDialogTest` (`build-3.log`). The final family run after the last source edit passed 3/3 tests.

## Gate results

| Gate | Command | Exit | Result |
| --- | --- | --- | --- |
| Prescribed feature check | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-4964` | 1 | BLOCKED before its PASS/FAIL table. The Bunny guard rejected its attempt to run `git checkout -q --detach aac861e62bba78448c3ff14e7077cc7c9712e4f9` in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/gate-baselines/tree` under AGENTS.md rules 1, 2, 11, and 12. I did not bypass the guard or change the baseline. See `gate-3.log`. |
| `fork-sources-gate.sh` | `bash tests/fork-sources-gate.sh` | 1 | Existing source inventory mismatch: the all-sources recipe finds sfizz vendored Abseil paths absent from `tests/all-sources.txt`; this change adds no files or source-list entries. `gate-2-fork-sources.log`. |
| `all-sources-reproduce.sh` | `bash tests/all-sources-reproduce.sh` | 1 | Same sfizz vendored-source inventory mismatch, unrelated to the changed paths. `gate-2-all-sources-reproduce.log`. |
| `no-upstream-regression-gate.sh` | `bash tests/no-upstream-regression-gate.sh` | 0 | PASS. `gate-2-no-upstream-regression.log`. |
| `check-namespace` | `tests/scripted/check-namespace` | 0 | PASS. `gate-2-check-namespace.log`. |
| `no-tautology-gate.sh` | `bash tests/no-tautology-gate.sh` | 0 | PASS. `gate-2-no-tautology.log`. |
| `doc-figures-gate.py` | `python3 tests/doc-figures-gate.py` | 0 | PASS. `gate-2-doc-figures.log`. |
| Raw complexity scan | `bash tests/complexity-gate.sh --check` | 1 | Its baseline was not written; the scan reports no Telemetry files among its regression findings. The official changed-file comparison is blocked with the feature check. `gate-2-complexity.log`. |
| Raw file-length scan | `bash tests/file-length-gate.sh --check` | 1 | Its baseline was not written and it lists existing over-500-line files; none of the changed files is listed, and this change adds no file. The official changed-file comparison is blocked with the feature check. `gate-2-file-length.log`. |

`feature_check.py` exited 1 again after the final source edit and did not emit its table. Its underlying checkout attempt is the blocker; the standalone raw ratchet exits above are not represented as a passing comparative gate.

## Self-check

- **Regression proof:** RED on the original code (focused ctest EXIT=8), RED after reversing only the product patch (EXIT=8), GREEN after restoring it (EXIT=0). Commands and logs are listed above.
- **Determinism:** the test uses `QTimer::singleShot(0)` to deliver button clicks inside `QDialog::exec()`'s event loop. It uses no sleeps, clock values, or races.
- **Cause and callers:** the registry handler (`ControlCommandsTelemetry.cpp:288`) and Help menu action (`MainWindow.cpp:551`) call the shared `openTelemetryConsentScreen()`. Save deliberately leaves the dialog open; the dialog marker records the actual save click. `telemetry.status` is read-only and not affected.
- **Collateral behavior:** the telemetry consent store and its saved fields are unchanged; the result still reads persisted consent with `Telemetry::loadConsent()`. No project data format, audio-thread path, undo action, or command registration changed. Save still keeps the dialog open; Close without Save leaves stored consent unchanged.
- **Verdict evidence:** this is not ALREADY-FIXED or FALSE-POSITIVE. The pre-fix literal `true` contradicted the Save/Close behavior; the current result reads the per-dialog save marker at `ControlCommandsTelemetry.cpp:260`.
- **Sibling commands:** `telemetry.consent` is fixed as described. `telemetry.status` only reports consent and payload state and does not open the dialog or return a `saved` field, so it is unaffected.

## Files in the cluster 4964 commit

- `include/TelemetryConsentDialog.h`
- `src/core/ControlCommandsTelemetry.cpp`
- `src/gui/TelemetryConsentDialog.cpp`
- `tests/src/gui/TelemetryConsentDialogTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-4964.md`
