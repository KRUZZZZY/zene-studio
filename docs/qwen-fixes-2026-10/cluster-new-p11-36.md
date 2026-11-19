# Cluster new-p11-36

Verdict: **FIXED**

`createChannels` in `src/core/ControlCommandsPluginBuses.cpp:279-299` previously skipped a bus when `createChannelFor` failed, then returned success. It now returns `Refused` with the failed bus index and removes routes/channels created earlier in that invocation. The regression at `tests/src/core/InstrumentLayersCommandsTest.cpp:299-309` makes bus 1 routable and bus 2 inactive, then checks the refusal and complete rollback.

## Evidence

| Check | Command/result |
| --- | --- |
| RED, with the production fix removed | `./InstrumentLayersCommandsTest multiOutputBusesRouteCreateUndo` from `build/tests`; **EXIT=1**, failing `!refused.ok` (2 passed, 1 failed, 0 skipped). Log: `direct-test-3.log`. |
| GREEN, after restoring the production fix | Same command; **EXIT=0** (3 passed, 0 failed, 0 skipped). Log: `direct-test-4.log`; the final-source rerun is `direct-test-5.log`, also **EXIT=0**. |
| Affected ctest family | **1 CTest entry** (`InstrumentLayersCommandsTest`) ran. Final ctest: **EXIT=8**; QtTest reports 5 passed, 1 skipped, 1 failed. The regression slot passes. `outputBusesAnswerTyped` fails because this tree's partial plugin set returns `bus_count == 0`; it failed the same way on the original production code in `test-3.log`, before the fix. |
| Complexity gate | `bash tests/complexity-gate.sh --check` — **EXIT=0** (`gate-1.log`). |
| File-length gate | `bash tests/file-length-gate.sh --check` — **EXIT=0** (`gate-2.log`). |

The full ctest command and exit were:

```sh
ctest -R '^InstrumentLayersCommandsTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-36.r1/logs/test-8.log 2>&1; rc=$?; printf 'EXIT=%d\n' "$rc" | tee -a /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-36.r1/logs/test-8.log; exit "$rc"
```

Result: **EXIT=8**. This is recorded as a suite limitation; the unrelated failing assertion was already present in the pre-fix run. The focused affected slot passes after the change.

## Build and fix-removal proof

The plugin fixture and test target were built with:

```sh
cmake --build build --target zene clapinstrument tripleoscillator InstrumentLayersCommandsTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-36.r1/logs/build-3.log 2>&1; rc=$?; printf 'EXIT=%d\n' "$rc" | tee -a /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-36.r1/logs/build-3.log; exit "$rc"
```

Result: **EXIT=0**. The final source rebuild was `cmake --build build --target zene InstrumentLayersCommandsTest`, **EXIT=0** (`build-9.log`).

For the required fix-removal proof, `git diff -- src/core/ControlCommandsPluginBuses.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-36.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-36.r1/logs/fix.patch` returned **EXIT=0**. The rebuild and focused test then returned **EXIT=0** and **EXIT=1**, respectively (`build-5.log`, `direct-test-3.log`). Reapplying the patch returned **EXIT=0**; the rebuild and same focused test returned **EXIT=0** and **EXIT=0** (`build-6.log`, `direct-test-4.log`).

`fff grep` was not available among this session's tools, so log matches were searched with `rg -n`.

## Files committed

- `src/core/ControlCommandsPluginBuses.cpp`
- `tests/src/core/InstrumentLayersCommandsTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p11-36.md`
