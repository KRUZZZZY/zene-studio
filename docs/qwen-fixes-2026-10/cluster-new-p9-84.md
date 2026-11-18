# Cluster new-p9-84: controller connection teardown

**Verdict: FIXED**

## Evidence

- [`ControllerConnection.cpp:135`](../../src/core/ControllerConnection.cpp:135) connects a non-MIDI controller's `destroyed()` signal to `deleteConnection()`. Before the fix, that slot deleted the connection directly, and [`ControllerConnection.cpp:73-77`](../../src/core/ControllerConnection.cpp:73) then called `type()` and `removeConnection()` through the dying controller pointer.
- [`ControllerConnection.cpp:236-243`](../../src/core/ControllerConnection.cpp:236) now clears the pointer when the signal sender is the connection's current controller, then deletes the connection. Its destructor still removes the connection from the live registry.
- [`MidiControllerMenuTest.cpp:141-151`](../../tests/src/gui/MidiControllerMenuTest.cpp:141) adds a real LFO teardown regression. On the original code, it observed connection count `0` at deallocation instead of `1`; after the fix it observed `1` and verified the connection registry entry was removed.

The log folder is `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-84.r1/logs`. Each build and test command listed below wrote its output to the named log, and the unpiped shell exit code was appended as `EXIT=...`. ctest commands ran from `build/tests`.

## Commands and results

| Check | Exact command | Exit | Result / log |
|---|---|---:|---|
| RED build, original code | `cmake --build build --target MidiControllerMenuTest` | 0 | `red-build-1.log` |
| RED test, original code | `ctest -R '^MidiControllerMenuTest$' --output-on-failure` | 8 | `red-test-1.log`; 1/1 test failed, actual 0 / expected 1 |
| Green build | `cmake --build build --target zene MidiControllerMenuTest` | 0 | `green-build-1.log` |
| Green test | `ctest -R '^MidiControllerMenuTest$' --output-on-failure` | 0 | `green-test-1.log`; 1/1 passed |
| Reverse only product patch | `git diff -- src/core/ControllerConnection.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-84.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-84.r1/logs/fix.patch` | 0 | Fix removed for sensitivity check |
| Revert build | `cmake --build build --target zene MidiControllerMenuTest` | 0 | `revert-build-1.log` |
| Revert test | `ctest -R '^MidiControllerMenuTest$' --output-on-failure` | 8 | `revert-test-1.log`; same 1/1 failure, actual 0 / expected 1 |
| Reapply product patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-84.r1/logs/fix.patch` | 0 | Fix restored |
| Reapplied build | `cmake --build build --target zene MidiControllerMenuTest` | 0 | `proof-green-build-1.log` |
| Reapplied test | `ctest -R '^MidiControllerMenuTest$' --output-on-failure` | 0 | `proof-green-test-1.log`; 1/1 passed |
| Related family build | `cmake --build build --target zene MidiControllerMenuTest ControllerTemplateMenuTest ClipJoinRippleTest` | 0 | `family-build-1.log` |
| Related family tests | `ctest -R '^(MidiControllerMenuTest|ControllerTemplateMenuTest|ClipJoinRippleTest)$' --output-on-failure` | 0 | `family-test-1.log`; 3/3 passed |
| Complexity gate | `bash tests/complexity-gate.sh --check` | 0 | `complexity-gate-2.log` |
| File-length gate | `bash tests/file-length-gate.sh --check` | 0 | `file-length-gate-2.log` |

No `fff grep` tool was exposed in this session; build and test logs were searched with `rg` one term at a time.

## Files in the commit

- `src/core/ControllerConnection.cpp`
- `tests/src/gui/MidiControllerMenuTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p9-84.md`
