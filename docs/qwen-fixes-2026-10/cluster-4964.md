FIXED

# Cluster 4964 — Consent dialog reports saved after rejection

Verdict: REAL. Before the product fix, `openTelemetryConsentScreen()` discarded `dialog.exec()` and returned literal `saved: true` (`27c1d8652^:src/core/ControlCommandsTelemetry.cpp:253-260`). Closing without Save therefore reported a save that had not happened.

Save intentionally leaves the dialog open (`src/gui/TelemetryConsentDialog.cpp:129-133`), so the later `exec()` close result cannot say whether Save was pressed. The dialog now initializes `SavedProperty` to false for each opening and sets it true only after the existing `Telemetry::saveConsent()` call (`src/gui/TelemetryConsentDialog.cpp:41,129-133`). The command reads the persisted consent as before, then returns `dialog.wasSaved()` (`src/core/ControlCommandsTelemetry.cpp:253-260`). No consent or project data format changed.

The Help menu action and registry handler both call the same `openTelemetryConsentScreen()` implementation (`src/gui/MainWindow.cpp:551`, `src/core/ControlCommandsTelemetry.cpp:289`). The regression schedules Close or Save-then-Close with `QTimer::singleShot(0)` inside `QDialog::exec()` (`tests/src/gui/TelemetryConsentDialogTest.cpp:163-183`). There are no sleeps, wall-clock inputs, or timing races.

The previous pre-submit failure was also fixed. `tests/fork-sources-gate.sh` failed because `tests/all-sources.txt` listed 1,148 vendored `plugins/SfzPlayer/sfizz/` files that its own published recipe excludes. I re-derived the entry list from that recipe. The correction removes exactly those 1,148 excluded paths and adds none; the gate scans the same vendor exclusion, and all first-party paths remain measured.

## Evidence

Round-two logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/4964.r2/logs/`.

| Check | Exact command and working directory | Result | Log |
| --- | --- | --- | --- |
| Product-only reverse patch | `git show --format= --binary 27c1d8652 -- include/TelemetryConsentDialog.h src/core/ControlCommandsTelemetry.cpp src/gui/TelemetryConsentDialog.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/4964.r2/logs/fix.patch` (worktree) | EXIT=0 | `fix.patch` |
| Reverse patch applicability | `git apply --check -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/4964.r2/logs/fix.patch` (worktree) | EXIT=0 | command result |
| Reverse product change | `git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/4964.r2/logs/fix.patch` (worktree) | EXIT=0 | command result |
| Build with product change reversed | `cmake --build build --target zene TelemetryConsentDialogTest` (worktree) | EXIT=0 | `build-1-reverted.log` |
| RED, product change reversed | `ctest -R TelemetryConsentDialogTest --output-on-failure` (`build/tests`) | EXIT=8; 5 passed, 1 failed at `saved.property("zeneConsentSaved").toBool()` | `test-1-reverted-red.log` |
| Restore product change | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/4964.r2/logs/fix.patch` (worktree) | EXIT=0 | command result |
| Build restored product and telemetry tests | `cmake --build build --target zene TelemetryConsentDialogTest TelemetryTest TelemetryTransportTest` (worktree) | EXIT=0 | `build-2-restored.log` |
| GREEN | `ctest -R TelemetryConsentDialogTest --output-on-failure` (`build/tests`) | EXIT=0; 1/1 passed | `test-2-restored-green.log` |
| Telemetry test family | `ctest -R Telemetry --output-on-failure` (`build/tests`) | EXIT=0; 3/3 passed: `TelemetryTest`, `TelemetryTransportTest`, `TelemetryConsentDialogTest` | `test-3-family.log` |
| Source gates before manifest repair | `bash tests/fork-sources-gate.sh`; `bash tests/all-sources-reproduce.sh` (worktree) | Both EXIT=1; the whole-tree list included 1,148 recipe-excluded sfizz paths | `gate-1-fork-sources.log`, `gate-1-all-sources-reproduce.log` |
| Source gate after manifest repair | `bash tests/fork-sources-gate.sh`; `bash tests/all-sources-reproduce.sh` (worktree) | Both EXIT=0; source list reproduces and all scanned sources are registered | `gate-3-fork-sources.log`, `gate-3-all-sources-reproduce.log` |
| Comparative feature gates | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-4964` (worktree) | EXIT=0; PASS for fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures, complexity, and file-length | `gate-3-feature-check.log` |

The manifest entry list was generated using the command in its header (EXIT=0):

```sh
{ git ls-files '*.c' '*.cc' '*.cpp' '*.cxx' '*.h' '*.hpp' | grep -vE '^(src/3rdparty/|tests/reference/|plugins/NeuralAmp/(rtneural|nam|tests)/|plugins/RnnoiseDenoiser/rnnoise/|plugins/SfzPlayer/sfizz/)' ; git ls-files tests/control-detect-commands.py tests/control_detect_fixtures.py ; } | LC_ALL=C sort -u > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/4964.r2/logs/gate-2-all-sources.expected
```

The generated 3,067-entry list replaced the manifest's entry section while its comment header was preserved (Python replacement command EXIT=0). `git diff --stat` reported 1,148 deletions and zero additions; all removals were under `plugins/SfzPlayer/sfizz/`.

## Self-check

- **Regression proof:** the committed product-only patch was reversed, the dependent targets rebuilt, and the focused test failed with EXIT=8 (5 passed, 1 failed). After restoring the patch and rebuilding, the same test passed with EXIT=0 (1/1). Commands and logs are above.
- **Determinism:** the test uses `QTimer::singleShot(0)` to deliver button clicks in the dialog's own event loop. It has no sleeps, wall-clock values, or races.
- **Cause and callers:** Save leaves the dialog open, so `exec()` cannot represent the Save action. The dialog marker is set immediately after the existing persistence call, and `openTelemetryConsentScreen()` forwards it. Both callers were checked: the Help action (`MainWindow.cpp:551`) and registry handler (`ControlCommandsTelemetry.cpp:289`) use this same function.
- **Collateral behavior:** consent is still loaded and saved through the same `Telemetry::loadConsent()` and `Telemetry::saveConsent()` paths; only the per-opening result marker changed. No project loading, saved-data schema, audio-thread, undo, or command-registration behavior changed. The scope-manifest repair removes only vendor paths already excluded by its recipe and by the registration gate; it changes no runtime behavior or gate limit.
- **Verdict evidence:** this is REAL, not ALREADY-FIXED or FALSE-POSITIVE. The pre-fix implementation at `27c1d8652^:src/core/ControlCommandsTelemetry.cpp:254-259` hardcoded `saved` to true after discarding `exec()`; current code returns the dialog's save marker at `src/core/ControlCommandsTelemetry.cpp:260`.
- **Sibling commands:** `telemetry.consent` reports whether this dialog opening saved, as fixed. `telemetry.status` (`ControlCommandsTelemetry.cpp:295-308`) is read-only and returns consent/payload state; it does not open the dialog or report a `saved` field, so it is unaffected.

## Files in the cluster 4964 changes

- `include/TelemetryConsentDialog.h`
- `src/core/ControlCommandsTelemetry.cpp`
- `src/gui/TelemetryConsentDialog.cpp`
- `tests/src/gui/TelemetryConsentDialogTest.cpp`
- `tests/all-sources.txt`
- `docs/qwen-fixes-2026-10/cluster-4964.md`
