# Cluster new-p10-115: FIXED

## Finding and evidence

An unfiled effect opened “Move to Folder...” with the first existing folder selected. Pressing Return therefore filed it, contrary to the menu's no-op expectation.

`src/gui/modals/EffectPickerOrganiser.cpp:55` now handles the folder dialog in a small helper. It prepends a unique “No folder” choice, selects it for an unfiled effect (`:60-64`), and maps it back to an empty folder (`:67-70`). When the effect is already filed, its current folder remains selected. `fillKeyMenu` connects the action to this helper at `:165`.

`tests/src/gui/EffectHostingMenuTest.cpp:148` adds a deterministic GUI regression. It creates an existing folder, opens the real dialog, presses Return, and verifies the effect remains unfiled at `:161-171`. The test uses a temporary config file (`:81-82`). The inherited test change is recorded in `tests/upstream-modifications.txt`.

The RED assertion was `pluginorganiser::entry(key).folder.isEmpty()` returning false. The regression failed on the original code, failed again with a `-1` initial index, and passed with the explicit choice. The final reversible proof below uses the final production diff.

## Required evidence

All build, test, and gate output is in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-115.r1/logs/`. Commands below were run from the worktree root unless marked `build/tests`; each command redirected output to its numbered log and appended its exit code.

| Check | Exact command | Exit | Evidence |
|---|---|---:|---|
| Final RED, with production diff reversed | `ctest -R EffectHostingMenuTest --output-on-failure` (`build/tests`) | 8 | Log `30-test-adjusted-red-proof.log`; the regression assertion failed. |
| Final GREEN, with fix restored | `ctest -R EffectHostingMenuTest --output-on-failure` (`build/tests`) | 0 | Log `32-test-adjusted-green-proof.log`; CTest reports 1/1 passed. |
| Rebuild product and affected test | `cmake --build build --target zene EffectHostingMenuTest` | 0 | Logs `29-build-adjusted-red-proof.log` and `31-build-adjusted-green-proof.log`. |
| Complexity ratchet | `bash tests/complexity-gate.sh --check` | 0 | Log `27-complexity-gate-final-adjustment.log`. |
| File length ratchet | `bash tests/file-length-gate.sh --check` | 0 | Log `28-file-length-gate-final-adjustment.log`. |

The affected CTest family is one target, `EffectHostingMenuTest`; the final GREEN run executed and passed that target (1/1). The RED QtTest summary recorded 3 passed, 1 failed, and 1 skipped.

## Reversible proof commands

The final product patch was removed and restored with these commands; both exited 0:

```sh
git diff -- src/gui/modals/EffectPickerOrganiser.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-115.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-115.r1/logs/fix.patch
git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-115.r1/logs/fix.patch
```

With the patch reversed, `cmake --build build --target zene EffectHostingMenuTest` exited 0 and `ctest -R EffectHostingMenuTest --output-on-failure` exited 8 (`29-build-adjusted-red-proof.log`, `30-test-adjusted-red-proof.log`). After applying the patch, the same build exited 0 and the same CTest command exited 0 (`31-build-adjusted-green-proof.log`, `32-test-adjusted-green-proof.log`).

## Command history

These additional build/test runs are recorded as well:

| Command | Exit | Log |
|---|---:|---|
| `cmake --build build --target EffectHostingMenuTest` (first compile attempt) | 2 | `01-build-test-compile.log` |
| `cmake --build build --target EffectHostingMenuTest` (after correcting the test's parent argument) | 0 | `02-build-test.log` |
| `ctest -R EffectHostingMenuTest --output-on-failure` (original behavior, RED) | 8 | `03-test-red-initial.log` |
| `cmake --build build --target zene EffectHostingMenuTest` (trial `-1` selection and later explicit-choice builds) | 0 | `04-build-minus-one-fix.log`, `06-build-explicit-choice.log` |
| `ctest -R EffectHostingMenuTest --output-on-failure` (the `-1` trial still failed; explicit choice passed) | 8, 0 | `05-test-minus-one-fix-red.log`, `07-test-explicit-choice-green.log` |
| Intermediate reversible proof before helper extraction: the same build command | 0 | `08-build-proof-red.log`, `10-build-proof-green.log` |
| Intermediate reversible proof before helper extraction: the same CTest command | 8, 0 | `09-test-proof-red.log`, `11-test-proof-green.log` |
| First complexity gate (before helper extraction) | 1 | `12-complexity-gate-first.log` |
| First file-length gate | 0 | `13-file-length-gate-first.log` |
| Final pre-proof build and CTest after helper extraction | 0, 0 | `14-build-final.log`, `15-test-final.log` |
| Post-extraction complexity and file-length gates | 0, 0 | `16-complexity-gate.log`, `17-file-length-gate.log` |
| Final adjustment build and CTest | 0, 0 | `25-build-final-adjustment.log`, `26-test-final-adjustment.log` |
| Final adjustment complexity and file-length gates | 0, 0 | `27-complexity-gate-final-adjustment.log`, `28-file-length-gate-final-adjustment.log` |
| Final reversible proof builds and tests | 0, 8, 0, 0 | `29-build-adjusted-red-proof.log`, `30-test-adjusted-red-proof.log`, `31-build-adjusted-green-proof.log`, `32-test-adjusted-green-proof.log` |

The first test compile failed because the test object was passed where `QWidget*` was required; the test now passes `nullptr`. The initial `-1` selection did not prevent Return from accepting the first folder, so the fix uses the explicit choice. The first complexity run measured `fillKeyMenu` at CCN 12; extracting the dialog logic brought it under the limit. A shell quoting typo prevented one file-length gate wrapper from starting (shell exit 2); the corrected gate run is log `24-file-length-gate-final.log` and exited 0.

## Files in the commit

- `src/gui/modals/EffectPickerOrganiser.cpp`
- `tests/src/gui/EffectHostingMenuTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p10-115.md`
