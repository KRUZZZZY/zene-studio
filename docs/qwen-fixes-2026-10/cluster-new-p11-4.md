# Cluster new-p11-4

**Verdict: FIXED**

At [CompSwipeStrip.cpp:286](../../src/gui/CompSwipeStrip.cpp:286), the unmarked beat end is now clamped to `m_end`. When `End` puts the cursor at the exclusive take-span end, the range is empty and [compAtCursor() returns at line 292](../../src/gui/CompSwipeStrip.cpp:292) without invoking `comp.select`. The segment-under-cursor and marked-range paths are unchanged.

The regression [enterAtExclusiveTakeSpanEndDoesNotExtendTheComp()](../../tests/src/gui/CanvasKeyboardTest.cpp:310) creates a fresh take span, sends `End` then `Return`, and checks that `comp.get_state` remains empty. Before the fix, that assertion failed because a segment was created past the takes.

## Evidence

`ctest -R '^CanvasKeyboardTest$' --output-on-failure`, run from `build/tests`, selected one CTest test (#231) on each run. The CTest test runs the `CanvasKeyboardTest` Qt test family.

| Run | Command | Log | Exit | Result |
|---|---|---|---:|---|
| RED, original code | `ctest -R '^CanvasKeyboardTest$' --output-on-failure` | `test-1.log` | 8 | The new assertion failed: a segment was created past the takes. |
| GREEN, fix applied | `ctest -R '^CanvasKeyboardTest$' --output-on-failure` | `test-2.log` | 0 | 1/1 CTest tests passed. |
| RED, production patch reversed | `ctest -R '^CanvasKeyboardTest$' --output-on-failure` | `test-3.log` | 8 | The same assertion failed again. |
| GREEN, patch restored | `ctest -R '^CanvasKeyboardTest$' --output-on-failure` | `test-4.log` | 0 | 1/1 CTest tests passed. |

All four runs were preceded by `cmake --build build --target CanvasKeyboardTest`; `build-1.log` through `build-4.log` each record `EXIT=0`. Reversal and restoration commands both exited 0:

```sh
git diff -- src/gui/CompSwipeStrip.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-4.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-4.r1/logs/fix.patch
git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-4.r1/logs/fix.patch
```

Required gates, run after the code change, both exited 0:

- `bash tests/complexity-gate.sh --check` — `complexity-gate-1.log`, EXIT=0.
- `bash tests/file-length-gate.sh --check` — `file-length-gate-1.log`, EXIT=0.

All build, test and gate output is in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-4.r1/logs`. No `fff` grep tool or executable was exposed in this environment, so I searched the logs with `rg -n` for `error`, `FAILED`, the failing assertion and `EXIT=`.

## Files committed

- `src/gui/CompSwipeStrip.cpp`
- `tests/src/gui/CanvasKeyboardTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p11-4.md`
