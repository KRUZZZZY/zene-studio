FIXED

## Decision and evidence

REAL. The file-level contract says Windows test hosts must skip module-loading slots (`tests/src/plugins/PluginPortsMigrationTest.cpp:13`), and `testHostCanLoadPluginModules()` returns false under `Q_OS_WIN` (`:668`). The SF2 failed-load slot previously called `partc::loadPluginModule()` without that check. It now checks the host and calls `QSKIP(PluginModuleHostSkipMessage)` at `:5651` before the module load at `:5656`, matching the surrounding module-loading tests.

The deterministic regression at `tests/src/plugins/PluginPortsMigrationTest.cpp:5630` reads the slot body and requires the host guard, skip, and module load in that order. It failed on the unguarded source, passed after the fix, failed again after the guard was reversed, and passed after restoring it.

The three direct `partc::loadPluginModule()` test call sites in this file are at `:5656`, `:5877`, and `:5923`. The two GigPlayer call sites already guard at `:5871` and `:5917`; the SF2 call site now follows the same contract.

## Test matrix

| Path | Expected | Result |
|---|---|---|
| Linux/macOS host with the SF2 module and soundfont fixture | Run the SF2 valid-load / failed-replacement assertions | Not available in this build: `PART_C_SF2_FILE` is `NOTFOUND` in `build/CMakeCache.txt`; the existing behavior slot reports “Sf2Player is not built in this configuration.” |
| Windows test host | Skip before calling the module loader | The source-contract regression proves guard/skip/load ordering. The current worktree is Linux, so Windows runtime execution was unavailable. |
| SF2 module or soundfont unavailable | Skip the SF2 behavior test | Observed in the CTest run; the source-contract regression still ran and passed. |
| Audio-thread path | No new work on the audio thread | Not applicable: the change and regression run only in the Qt test harness; no product/audio code changed. |
| Supported-platform builds | Compile the test on Linux and Windows | Linux target built successfully. Windows compilation was unavailable on this host. The guard uses the existing `Q_OS_WIN` helper. |

## Commands and results

Commands below ran in the worktree unless a different working directory is shown. Build and test output was redirected to new files in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p18-73.r1/logs`.

| Command | Exit | Evidence |
|---|---:|---|
| `cmake --build build --target PluginPortsMigrationTest` | 0 | `build-1.log`; the regression target/dependencies exist. |
| `cmake --build build --target zene PluginPortsMigrationTest` | 0 | `build-2.log` (pre-fix regression build). |
| `QT_QPA_PLATFORM=offscreen ./PluginPortsMigrationTest sf2FailureTestChecksPluginModuleHostBeforeLoading` (from `build/tests`) | 1 | `test-1.log`; expected RED, assertion reports that the SF2 test must skip unsupported hosts before module load. |
| `cmake --build build --target zene PluginPortsMigrationTest` | 0 | `build-3.log` (after the guard). |
| `QT_QPA_PLATFORM=offscreen ./PluginPortsMigrationTest sf2FailureTestChecksPluginModuleHostBeforeLoading` (from `build/tests`) | 0 | `test-2.log`; focused regression passed. |
| `git diff -- tests/src/plugins/PluginPortsMigrationTest.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p18-73.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p18-73.r1/logs/fix.patch` | 0 | Reversed only the guard hunk. Because this bug’s fix is in a test slot, the regression additions were staged separately and stayed present for the proof. |
| `cmake --build build --target zene PluginPortsMigrationTest` | 0 | `build-4.log`; rebuilt after reversing the guard. |
| `QT_QPA_PLATFORM=offscreen ./PluginPortsMigrationTest sf2FailureTestChecksPluginModuleHostBeforeLoading` (from `build/tests`) | 1 | `test-3.log`; expected RED after reversal. |
| `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p18-73.r1/logs/fix.patch` | 0 | Restored the guard. |
| `cmake --build build --target zene PluginPortsMigrationTest` | 0 | `build-5.log`; rebuilt with the guard restored. |
| `QT_QPA_PLATFORM=offscreen ./PluginPortsMigrationTest sf2FailureTestChecksPluginModuleHostBeforeLoading` (from `build/tests`) | 0 | `test-4.log`; expected GREEN after restoration. |
| `ctest -R '^(PluginPortsMigrationTest|OpulenZ)$' --output-on-failure` (from `build/tests`) | 8 | `test-5.log`; one unrelated existing test lacked `decimator_1202.so`. |
| `cmake --build build --target decimator_1202` | 0 | `build-6.log`; produced the existing fixture at `build/plugins/ladspa/decimator_1202.so`. |
| `ctest -R '^(PluginPortsMigrationTest|OpulenZ)$' --output-on-failure` (from `build/tests`) | 0 | `test-6.log`; 2/2 CTest registrations passed. |
| `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p18-73` | 1, then 0 | `gate-1.log` initially found 1,148 stale vendored sfizz entries in `tests/all-sources.txt`, contrary to that file’s documented recipe. The inventory was re-derived from the recipe and documented; final result is `gate-3.log`, with every gate PASS. |
| `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/submit_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p18-73 new-p18-73` | 0 | `submit-1.log`; commit, clean tree, report, regression, and feature gates passed. |

The affected CTest family count is two registrations: `PluginPortsMigrationTest` and `OpulenZ`. Both passed on the final run. The behavior slot’s SF2 skip is due to the missing configured soundfont fixture, not the Windows host guard.

## Self-check

- Regression with the change reversed: `sf2FailureTestChecksPluginModuleHostBeforeLoading` failed with exit 1 in `test-1.log`, and failed again with exit 1 in `test-3.log` after applying the reverse patch and rebuilding. With the guard restored, the same command passed with exit 0 in `test-2.log` and `test-4.log`.
- Determinism: the regression reads the test source and compares the order of three fixed strings (host guard, `QSKIP`, module load). It uses no clock, sleeps, races, or timing assumptions.
- Cause and siblings: the omission was in the SF2 test slot itself. I checked all three direct module-loader call sites; the two GigPlayer siblings already had the guard. The fix uses the existing helper and skip message.
- Collateral and realtime: this changes test code only. No product path, project data, saved format, commands, undo behavior, or audio-thread code changes.
- Verdict evidence: this is not an already-fixed or false-positive finding; the pre-fix RED and reversed-patch RED both show the required guard was missing, while the helper at `:668` establishes why Windows must skip.
