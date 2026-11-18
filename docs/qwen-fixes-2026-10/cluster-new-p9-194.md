# Cluster new-p9-194

**Verdict: FIXED**

`InstrumentOutputBuses::loadSettings()` now checks the success flag from parsing a saved channel ID and skips malformed values. Before the change, `channel="bad"` was stored as route ID `0`. The regression in `tests/src/core/InstrumentRoutingTest.cpp:165` checks that the malformed route is ignored; it failed before the fix with actual route `0` and expected `-1`.

## Evidence

| Check | Exact command | Exit | Result |
|---|---|---:|---|
| Initial RED | `ctest -R '^InstrumentRoutingTest$' --output-on-failure` (run from `build/tests`) | 8 | 1/1 CTest test failed at `malformedOutputBusChannelIsIgnored`; actual `0`, expected `-1`. |
| Reverse-patch RED | Same ctest command after reversing the production patch | 8 | 1/1 CTest test failed at the same assertion. |
| GREEN | `ctest -R '^InstrumentRoutingTest$' --output-on-failure` (run from `build/tests`) | 0 | 1/1 CTest test passed. |
| Complexity gate | `bash tests/complexity-gate.sh --check` | 0 | Passed. |
| File-length gate | `bash tests/file-length-gate.sh --check` | 0 | Passed. |

The affected-family run executed one CTest test, `InstrumentRoutingTest`. The test output was saved in `test-1.log` through `test-4.log` under `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-194.r1/logs`.

The regression proof patch was created with `git diff -- src/core/InstrumentOutputBuses.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-194.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-194.r1/logs/fix.patch` (exit 0), then restored with `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-194.r1/logs/fix.patch` (exit 0).

## File evidence

- `src/core/InstrumentOutputBuses.cpp:172` checks `channelValid` before accepting the parsed channel.
- `tests/src/core/InstrumentRoutingTest.cpp:180` asserts that the malformed saved channel produces no route.

## Build commands

- `cmake --build build --target InstrumentRoutingTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-194.r1/logs/build-1.log 2>&1; build_rc=$?; printf 'EXIT=%s\n' "$build_rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-194.r1/logs/build-1.log; exit "$build_rc"` — exit 0.
- `cmake --build build --target zene InstrumentRoutingTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-194.r1/logs/build-2.log 2>&1; build_rc=$?; printf 'EXIT=%s\n' "$build_rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-194.r1/logs/build-2.log; exit "$build_rc"` — exit 0.
- `cmake --build build --target zene InstrumentRoutingTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-194.r1/logs/build-3.log 2>&1; build_rc=$?; printf 'EXIT=%s\n' "$build_rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-194.r1/logs/build-3.log; exit "$build_rc"` — exit 0.
- `cmake --build build --target zene InstrumentRoutingTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-194.r1/logs/build-4.log 2>&1; build_rc=$?; printf 'EXIT=%s\n' "$build_rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-194.r1/logs/build-4.log; exit "$build_rc"` — exit 0.

## CTest commands

Each command ran from `/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-194/build/tests`.

- `ctest -R '^InstrumentRoutingTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-194.r1/logs/test-1.log 2>&1; test_rc=$?; printf 'EXIT=%s\n' "$test_rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-194.r1/logs/test-1.log; exit "$test_rc"` — exit 8 (initial RED).
- `ctest -R '^InstrumentRoutingTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-194.r1/logs/test-2.log 2>&1; test_rc=$?; printf 'EXIT=%s\n' "$test_rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-194.r1/logs/test-2.log; exit "$test_rc"` — exit 0 (GREEN).
- `ctest -R '^InstrumentRoutingTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-194.r1/logs/test-3.log 2>&1; test_rc=$?; printf 'EXIT=%s\n' "$test_rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-194.r1/logs/test-3.log; exit "$test_rc"` — exit 8 (reverse-patch RED).
- `ctest -R '^InstrumentRoutingTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-194.r1/logs/test-4.log 2>&1; test_rc=$?; printf 'EXIT=%s\n' "$test_rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-194.r1/logs/test-4.log; exit "$test_rc"` — exit 0 (restored GREEN).

## Gate commands

- `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-194.r1/logs/gate-1.log 2>&1; gate_rc=$?; printf 'EXIT=%s\n' "$gate_rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-194.r1/logs/gate-1.log; exit "$gate_rc"` — exit 0.
- `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-194.r1/logs/gate-2.log 2>&1; gate_rc=$?; printf 'EXIT=%s\n' "$gate_rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-194.r1/logs/gate-2.log; exit "$gate_rc"` — exit 0.

## Commit files

- `src/core/InstrumentOutputBuses.cpp`
- `tests/src/core/InstrumentRoutingTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p9-194.md`
