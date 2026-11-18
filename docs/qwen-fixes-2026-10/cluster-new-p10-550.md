# Cluster new-p10-550: non-finite per-note modulation

**Verdict: FIXED**

`HostedPlugin::setNoteParamMod()` previously queued NaN and infinity values for delivery as `CLAP_EVENT_PARAM_MOD`. It now returns before constructing or queueing the event unless `amount` is finite. The check is on the existing real-time-safe path and adds no allocation or lock.

## File evidence

- `plugins/ClapEffect/ClapHostNotes.cpp:269` rejects non-finite amounts with `std::isfinite`.
- `tests/src/plugins/ClapHostTest.cpp:707` adds `testInstrumentRejectsNonFiniteParamMods()`. It exercises NaN, positive infinity, negative infinity, and a finite control amount against the instrument fixture. On the original code, the fixture received three invalid events (`state.eventCount` was 3 instead of 0).
- `tests/upstream-modifications.txt:768` records the inherited test-file change.

## Evidence table

All test commands below ran from `build/tests`; build and gate commands ran from the worktree root. Logs are under `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-550.r1/logs/`.

| Evidence | Exact command | Exit/result |
| --- | --- | --- |
| RED, original implementation | `ctest -R ClapHostTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-550.r1/logs/test-red-1.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 8; `state.eventCount` actual 3, expected 0 |
| GREEN, with fix | `ctest -R ClapHostTest --output-on-failure -V > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-550.r1/logs/test-green-1.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 0; 19 passed, 0 failed, 0 skipped |
| Affected family count | `ctest -R ClapHostTest --output-on-failure -V > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-550.r1/logs/test-final-1.log 2>&1; result=$?; echo EXIT=$result; exit $result` | One CTest target; QtTest total 19 passed |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-550.r1/logs/complexity-gate-final-1.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 0 |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-550.r1/logs/file-length-gate-final-1.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 0 |

## Exact build and fix-removal proof commands

Each build below completed with `EXIT=0`:

```sh
cmake --build build --target ClapHostTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-550.r1/logs/build-red-1.log 2>&1; result=$?; echo EXIT=$result; exit $result
cmake --build build --target ClapHostTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-550.r1/logs/build-green-1.log 2>&1; result=$?; echo EXIT=$result; exit $result
```

The product-only patch was captured and removed; both subcommands exited 0:

```sh
git diff -- plugins/ClapEffect/ClapHostNotes.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-550.r1/logs/fix.patch; diff_result=$?; git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-550.r1/logs/fix.patch; reverse_result=$?; echo DIFF_EXIT=$diff_result REVERSE_EXIT=$reverse_result; test "$diff_result" -eq 0 && test "$reverse_result" -eq 0
```

With that patch removed, the rebuilt regression failed again (test exit 8):

```sh
cmake --build build --target ClapHostTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-550.r1/logs/build-revert-red-1.log 2>&1; result=$?; echo EXIT=$result; exit $result
ctest -R ClapHostTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-550.r1/logs/test-revert-red-1.log 2>&1; result=$?; echo EXIT=$result; exit $result
```

The patch was restored (exit 0), rebuilt, and the same suite passed (19 passed, 0 failed, 0 skipped):

```sh
git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-550.r1/logs/fix.patch; result=$?; echo APPLY_EXIT=$result; exit $result
cmake --build build --target ClapHostTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-550.r1/logs/build-proof-green-1.log 2>&1; result=$?; echo EXIT=$result; exit $result
ctest -R ClapHostTest --output-on-failure -V > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-550.r1/logs/test-proof-green-1.log 2>&1; result=$?; echo EXIT=$result; exit $result
```

Final product-path rebuild and family rerun also passed:

```sh
cmake --build build --target zene > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-550.r1/logs/build-zene-final-1.log 2>&1; result=$?; echo EXIT=$result; exit $result
ctest -R ClapHostTest --output-on-failure -V > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-550.r1/logs/test-final-1.log 2>&1; result=$?; echo EXIT=$result; exit $result
```

Both commands exited 0; the final QtTest total was 19 passed, 0 failed, 0 skipped.

## Files committed

- `plugins/ClapEffect/ClapHostNotes.cpp`
- `tests/src/plugins/ClapHostTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p10-550.md`
