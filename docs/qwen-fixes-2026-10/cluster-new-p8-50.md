# Cluster new-p8-50

Verdict: **FIXED**

`record.disarm_track` and `record.disarm_all` stop active capture and retire take journals. Their `mutating` metadata is now `true`, and their A16 rows classify them as irreversible with the take files as the manual fallback. Successful calls now leave irreversible transaction records.

## File and line evidence

- `src/core/ControlCommandsRecordingRoutes.cpp:210,221,232,242` — the handlers stop one or all capture routes; registration at `:380,397` declares both commands mutating.
- `src/core/ControlReversibilityTableRecording.cpp:112,120` — both commands have `RC::Irreversible`, `reversible=false`, and a non-empty fallback.
- `tests/src/core/ReversibilityContractTest.cpp:313` — checks registration and irreversible contract metadata.
- `tests/control-record-inputs.py:178,319,469` — checks the wire metadata, single-route file/journal effects, and both irreversible transaction records.
- `docs/RELEASE-NOTES-v0.3.0-alpha.md:955,2163` — updated histogram and A16 description.

## Regression evidence

The first RED run used the original product code. `ReversibilityContractTest` failed at `command->mutating` for `record.disarm_track`; `ControlRecordInputs` reported both commands as non-mutating and found neither transaction. Each invocation ran one CTest test. With the fix applied, both families passed 1/1.

The required patch-removal proof also passed: reversing only the two production-file changes made both families fail again; reapplying the patch made both pass again.

| Family | RED command / exit | GREEN command / exit | CTest count |
|---|---|---|---:|
| Reversibility contract | `ctest -R ReversibilityContractTest --output-on-failure` / 8 | `ctest -R ReversibilityContractTest --output-on-failure` / 0 | 1 |
| Recording control surface | `ctest -R ControlRecordInputs --output-on-failure` / 8 | `ctest -R ControlRecordInputs --output-on-failure` / 0 | 1 |

The first RED logs are `test-1.log` and `test-2.log`; the initial GREEN logs are `test-3.log` and `test-3b.log`. Patch-removal RED logs are `test-4.log` and `test-5.log`; the restored GREEN logs are `test-6.log` and `test-7.log`, all under `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-50.r1/logs`.

### Build commands and exit codes

Every build below exited 0. Each command redirected its output to the named new log and printed `EXIT=0`.

```sh
cmake --build build --target ReversibilityContractTest > "$LOGS/build-1.log" 2>&1; rc=$?; echo EXIT=$rc
cmake --build build --target zene > "$LOGS/build-2.log" 2>&1; rc=$?; echo EXIT=$rc
cmake --build build --target ReversibilityContractTest > "$LOGS/build-3.log" 2>&1; rc=$?; echo EXIT=$rc
cmake --build build --target zene > "$LOGS/build-4.log" 2>&1; rc=$?; echo EXIT=$rc
cmake --build build --target ReversibilityContractTest > "$LOGS/build-5.log" 2>&1; rc=$?; echo EXIT=$rc
cmake --build build --target zene > "$LOGS/build-6.log" 2>&1; rc=$?; echo EXIT=$rc
cmake --build build --target ReversibilityContractTest > "$LOGS/build-7.log" 2>&1; rc=$?; echo EXIT=$rc
cmake --build build --target zene > "$LOGS/build-8.log" 2>&1; rc=$?; echo EXIT=$rc
```

`LOGS` was `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-50.r1/logs` for those commands. The CTest commands ran from `build/tests`; the commands below show the exact invocations and their captured CTest exit codes:

```sh
(cd build/tests && ctest -R ReversibilityContractTest --output-on-failure > "$LOGS/test-1.log" 2>&1); rc=$?; echo EXIT=$rc  # 8, original code
(cd build/tests && ctest -R ControlRecordInputs --output-on-failure > "$LOGS/test-2.log" 2>&1); rc=$?; echo EXIT=$rc  # 8, original code
(cd build/tests && ctest -R ReversibilityContractTest --output-on-failure > "$LOGS/test-3.log" 2>&1); rc=$?; echo EXIT=$rc  # 0
(cd build/tests && ctest -R ControlRecordInputs --output-on-failure > "$LOGS/test-3b.log" 2>&1); rc=$?; echo EXIT=$rc  # 0
(cd build/tests && ctest -R ReversibilityContractTest --output-on-failure > "$LOGS/test-4.log" 2>&1); rc=$?; echo EXIT=$rc  # 8, production patch removed
(cd build/tests && ctest -R ControlRecordInputs --output-on-failure > "$LOGS/test-5.log" 2>&1); rc=$?; echo EXIT=$rc  # 8, production patch removed
(cd build/tests && ctest -R ReversibilityContractTest --output-on-failure > "$LOGS/test-6.log" 2>&1); rc=$?; echo EXIT=$rc  # 0, patch restored
(cd build/tests && ctest -R ControlRecordInputs --output-on-failure > "$LOGS/test-7.log" 2>&1); rc=$?; echo EXIT=$rc  # 0, patch restored
```

Patch-removal commands: `git diff -- src/core/ControlCommandsRecordingRoutes.cpp src/core/ControlReversibilityTableRecording.cpp > "$LOGS/fix.patch"` returned `DIFF_EXIT=0`; `git apply -R "$LOGS/fix.patch"` returned `REVERSE_APPLY_EXIT=0`; `git apply "$LOGS/fix.patch"` returned `APPLY_EXIT=0`.

### Ratchet gates

| Gate command | Exit |
|---|---:|
| `bash tests/complexity-gate.sh --check > "$LOGS/gate-1.log" 2>&1; rc=$?; echo EXIT=$rc` | 0 |
| `bash tests/file-length-gate.sh --check > "$LOGS/gate-2.log" 2>&1; rc=$?; echo EXIT=$rc` | 0 |

The `fff grep` tool was not available in the tool list or shell (`type -a fff` reported not found), so the saved logs were searched with `rg -n`, one search term at a time.

## Files committed

- `src/core/ControlCommandsRecordingRoutes.cpp`
- `src/core/ControlReversibilityTableRecording.cpp`
- `tests/src/core/ReversibilityContractTest.cpp`
- `tests/control-record-inputs.py`
- `tests/upstream-modifications.txt`
- `docs/RELEASE-NOTES-v0.3.0-alpha.md`
- `docs/qwen-fixes-2026-10/cluster-new-p8-50.md`
