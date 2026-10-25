FIXED
5110: FIXED
4203: ALREADY-FIXED
5112: FIXED

## 5110 — selected config scale is applied before QApplication

**Decision: REAL.** `src/core/main.cpp:523-528` captures `--config`/`-c` in the first argument pass. At `src/core/main.cpp:699-704`, `appearance::applyStoredScale(configFile)` runs before the `MainApplication` constructor; the complete config is loaded later at line 1296. `src/core/Appearance.cpp:53-65,198-203` reads `ui/@scale` from that selected file, clamps it to the interface's 0.5–3 range, falls back to the stored scale file when absent, and leaves an explicit `QT_SCALE_FACTOR` untouched.

Regression: `tests/src/core/AppearanceStartupTest.cpp` checks selected-config precedence, fallback when the config has no scale, and explicit Qt environment precedence. With the product patch reversed, `selectedConfigScaleWinsOverTheFallbackFile()` failed on the wrong value; the identical test passed after restoration. The test also compiles against the original no-argument helper to make the negative control an assertion failure rather than a build failure.

## 4203 — dump/compress unreadable inputs

**Decision: ALREADY-FIXED on this branch before this batch.** `src/core/main.cpp:788-793` rejects a failed dump open, `:818-824` rejects an empty `qUncompress()` result, and `:843-848` rejects a failed compress open. These checks were present in the starting tree and are unchanged by this batch. The tests also preserve a valid compress/dump round trip.

Regression evidence: with only those three existing checks temporarily removed, `dumpRejectsMissingAndInvalidInputs()` failed because the missing input returned exit 0, and `compressRejectsMissingInputAndRoundTripsValidBytes()` failed for the same exit-code mismatch. Restoring the existing checks returned both slots to green in `MainCliStartupTest`.

## 5112 — normal startup failures leave session markers armed

**Decision: REAL.** Stem output validation runs at `src/core/main.cpp:1296-1310`, immediately after config loading and before either marker is installed or `Engine::init()` starts; missing `-o` and an uncreatable output path therefore exit as ordinary CLI refusals without starting the engine. The failed control-socket listen at `src/core/main.cpp:1386-1391` calls `crashreporter::endSession()` and `safestart::endSession()` before returning.

Regression: `failedControlSocketStartupClearsBothSessionMarkers()` launches `zene` against an occupied local socket and checks that both marker files are absent after its normal failure exit. `missingStemOutputIsRejectedBeforeSessionStartup()` and `uncreatableStemOutputIsRejectedBeforeSessionStartup()` separately cover the missing `-o` and output-directory creation failures, assert each exits before `Loading project...`, and check both markers are absent. With the product patch reversed, the socket marker assertion failed; each stem test also fails deterministically if prevalidation is moved back after engine startup. No audio-thread path is touched; these are startup/CLI paths.

## Test matrix and commands

The deterministic matrix covers selected scale, absent scale fallback, explicit Qt scale, missing and malformed dump input, missing compress input, valid compress/dump round trip, occupied control socket, missing stem output, and failed stem output-directory creation. The CLI marker cases verify both crash and safe-start marker files. Test processes use isolated temporary config/home directories; the socket conflict is synchronized by binding the socket before launch. The changed API declaration and definition were built with all direct header-dependent test targets. This run built and tested the configured Linux/Qt tree; no cross-platform build was available here. No realtime, undo, or control-command behavior was added.

| Proof | Command | EXIT |
|---|---|---:|
| Reverse changed product files: `git diff -- include/Appearance.h src/core/Appearance.cpp src/core/main.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5110.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5110.r1/logs/fix.patch` | 0 |
| Build without 5110/5112 fix: `cmake --build build --target zene AppearanceStartupTest MainCliStartupTest` (`28-build-product-reversed.log`) | 0 |
| 5110 + 5112 RED: `ctest -R '^(AppearanceStartupTest|MainCliStartupTest)$' --output-on-failure` from `build/tests` (`29-test-product-reversed.log`); selected scale and socket marker assertions failed | 8 |
| Stem sibling negative-control isolation before the QA follow-up split: `./MainCliStartupTest stemOutputFailuresKeepMarkersClean` from `build/tests` (`30-test-stem-negative-isolated.log`) | 0 |
| Restore product patch: `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5110.r1/logs/fix.patch` | 0 |
| 4203 RED control: remove the three pre-existing dump/compress checks, then `cmake --build build --target zene MainCliStartupTest` (`34-build-4203-negative.log`) | 0 |
| 4203 dump RED: `./MainCliStartupTest dumpRejectsMissingAndInvalidInputs` from `build/tests` (`35-test-4203-dump-negative.log`) | 1 |
| 4203 compress RED: `./MainCliStartupTest compressRejectsMissingInputAndRoundTripsValidBytes` from `build/tests` (`36-test-4203-compress-negative.log`) | 1 |
| Restore negative-control and product patches with `git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5110.r1/logs/4203-negative-control.patch` and `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5110.r1/logs/main-5110-5112.patch` (`37-restore-main-product.log`) | 0 |
| Final build: `cmake --build build --target zene AppearanceStartupTest MainCliStartupTest CustomAccessTest HelpAccessTest TouchWorkspaceTest WorkspaceAccessTest MainWindowGeometryTest RecordRemainingTest` (`38-build-final-targets.log`) | 0 |
| GREEN: `ctest -R '^(AppearanceStartupTest|MainCliStartupTest)$' --verbose` from `build/tests` (`39-test-regressions-final.log`); 2 CTest targets, 11 QtTest passes | 0 |
| Related family: `ctest -R '^(MainWindowTest|MainWindowGeometryTest|RecordRemainingTest|CustomAccessTest|HelpAccessTest|TouchWorkspaceTest|WorkspaceAccessTest)$' --output-on-failure` from `build/tests` (`40-test-related-family-final.log`); 7/7 targets passed | 0 |
| `git diff --check` | 0 |
| Feature gates: `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-5110` (`46-feature-check-final-report.log`); all eight checks PASS | 0 |
| Completion gate: `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/submit_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-5110 5110` (`47-submit-check-committed.log`) | 0 |

The first feature-check found that the inherited `tests/all-sources.txt` contained vendored sfizz files its own header recipe excludes. I regenerated that manifest from the published recipe, preserving its header and adding the two newly tracked startup-test sources; the excluded files are vendored dependencies and are outside the product scope. Logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5110.r1/logs`.

## QA follow-up

The first independent review caught that the `exportstems` output refusals still initialized the engine before returning. A repeated `MainCliStartupTest` run intermittently observed `CrashExit` after `Loading project...`; the checks have been moved before session-marker installation and engine startup. The updated test proves both refusals happen before project loading.

| Proof | Command | EXIT |
|---|---|---:|
| Final build of touched and direct `Appearance.h` test targets (`qa-15-build-final.log`) | `cmake --build build --target zene AppearanceStartupTest CustomAccessTest HelpAccessTest MainCliStartupTest MainWindowGeometryTest MainWindowTest RecordRemainingTest TouchWorkspaceTest WorkspaceAccessTest --parallel 12` | 0 |
| Final focused suite (`qa-16-tests-final.log`), 9/9 CTest targets | `ctest -R '^(AppearanceStartupTest|CustomAccessTest|HelpAccessTest|MainCliStartupTest|MainWindowGeometryTest|MainWindowTest|RecordRemainingTest|TouchWorkspaceTest|WorkspaceAccessTest)$' --output-on-failure` from `build/tests` | 0 |
| Final startup CLI repeat (`qa-18-maincli-repeat-final.log`), 20/20 runs | `ctest --repeat until-fail:20 -R '^MainCliStartupTest$' --output-on-failure` from `build/tests` | 0 |
| Feature gate after the QA follow-up (`qa-17-feature-check-final.log`), all eight checks PASS | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-5110` | 0 |

## Self-check

- RED/GREEN proof: 5110's selected-scale assertion and 5112's occupied-socket marker assertion failed in `29-test-product-reversed.log` (CTest EXIT 8), then both passed in `39-test-regressions-final.log` (CTest EXIT 0). For 4203, removing the unchanged guards made the missing/corrupt dump and missing compress tests fail individually (EXIT 1 each; logs 35 and 36); with the guards present both passed in the final `MainCliStartupTest` run (EXIT 0).
- Determinism: the scale test supplies a config path and temporary scale file, with no clock input. CLI tests use isolated temporary homes, `QProcess` result checks, and bind the conflicting local socket before launching the child; they use no sleeps or timing assumptions.
- Cause and siblings: `--config` and `-c` are both captured during the first argument scan; the selected scale is applied before `MainApplication`, the sidecar remains the absent-scale fallback, and explicit `QT_SCALE_FACTOR` still takes precedence. Audit of `main.cpp`'s marker returns found failed control-socket listen was the missing cleanup; render refusals and both stem output failure branches already end both markers. The `dump`/`compress` guards are unchanged and their valid round trip still passes.
- Collateral / realtime: the new scale parsing preserves the configured range and fallback behavior; CLI refusal exit codes remain failures and valid project bytes round-trip. The changed code runs only during startup/CLI handling; no audio-thread, undo, or control-command code was added.
- ALREADY-FIXED evidence: 4203's pre-existing checks are at `src/core/main.cpp:788-793,818-824,843-848`; the tests fail when those checks are temporarily removed.
- Registration: the two new test sources are in `tests/fork-sources.txt`; test targets are registered in `tests/CMakeLists.txt`; modified inherited files have reasons in `tests/upstream-modifications.txt`.
- Feature check: all eight checks PASS, including `fork-sources`, `all-sources-reproduce`, `no-upstream-regression`, `check-namespace`, `no-tautology`, `doc-figures`, complexity, and file-length (EXIT 0).
- Submit-check: all mechanical checks PASS on commit `a150d9e24` with a clean worktree (EXIT 0). Its five judgement questions are answered above with RED/GREEN logs, test setup, source line evidence, sibling-path review, and scope notes.

## Files committed

Commit `a150d9e24` (`fix: cluster 5110 startup and CLI failure handling`) contains:

- `include/Appearance.h`
- `src/core/Appearance.cpp`
- `src/core/main.cpp`
- `tests/CMakeLists.txt`
- `tests/src/core/AppearanceStartupTest.cpp`
- `tests/src/core/MainCliStartupTest.cpp`
- `tests/fork-sources.txt`
- `tests/all-sources.txt` (re-derived from its documented recipe for the source-registration gate)
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-5110.md`
