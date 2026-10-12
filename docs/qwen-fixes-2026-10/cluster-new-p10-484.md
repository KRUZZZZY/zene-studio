# Cluster new-p10-484 — oversized navigator scroll positions

**Verdict: FIXED**

`arrangement.scroll` accepted an integer position without an upper schema bound, but
`QJsonValue::toInt()` converted values above `INT_MAX` to zero. In
[`ArrangerNavigator.cpp:395`](../../src/gui/ArrangerNavigator.cpp#L395), the handler now reads
the validated JSON number as a `double`, clamps it to `[0, scrollbar->maximum()]`, and only then
converts it to `int`. The integer schema is unchanged at
[`ArrangerNavigator.cpp:442`](../../src/gui/ArrangerNavigator.cpp#L442).

The regression at [`ArrangerTimelineTest.cpp:368`](../../tests/src/gui/ArrangerTimelineTest.cpp#L368)
sends `2147483648` and checks that the scrollbar reaches its positive maximum.

## Evidence

| Evidence | Exact command or files | Result |
| --- | --- | --- |
| RED on original handler | `QT_QPA_PLATFORM=offscreen ctest -R '^ArrangerTimelineTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-484.r1/logs/test-1.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit "$rc"` (run from `build/tests`) | EXIT=8; 1/1 failed, actual scrollbar value was 0. |
| GREEN with fix restored | `QT_QPA_PLATFORM=offscreen ctest -R '^ArrangerTimelineTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-484.r1/logs/test-4.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit "$rc"` (run from `build/tests`) | EXIT=0; 1/1 passed. |
| Patch reversal proof | Reverting only `src/gui/ArrangerNavigator.cpp`, rebuilding, and rerunning the same test; then restoring the patch and rerunning. | RED test-3 EXIT=8; GREEN test-4 EXIT=0. |
| Affected family count | `ctest -R '^ArrangerTimelineTest$'` | 1 CTest test (`ArrangerTimelineTest`); QtTest ran the full test executable. |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-484.r1/logs/gate-3.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit "$rc"` | EXIT=0, PASS. |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-484.r1/logs/gate-4.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit "$rc"` | EXIT=0, PASS. |
| Files committed | `src/gui/ArrangerNavigator.cpp`; `tests/src/gui/ArrangerTimelineTest.cpp`; `docs/qwen-fixes-2026-10/cluster-new-p10-484.md` | Commit `73549aa5e`. |

The build and test output was written to new files under
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-484.r1/logs`.
The commands below include the actual output paths and exit-code wrapper used.

## Commands and exit codes

Build commands were run from the worktree root. The first attempt exposed a missing direct
`QScrollBar` test include; that include was added before any regression result was accepted.

1. `cmake --build build --target zene ArrangerTimelineTest -- -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-484.r1/logs/build-1.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit "$rc"` — **EXIT=2** (test compile failed on incomplete `QScrollBar`).
2. `cmake --build build --target zene ArrangerTimelineTest -- -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-484.r1/logs/build-2.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit "$rc"` — **EXIT=0**.
3. `cmake --build build --target zene ArrangerTimelineTest -- -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-484.r1/logs/build-3.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit "$rc"` — **EXIT=0** (fixed handler).
4. `cmake --build build --target zene ArrangerTimelineTest -- -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-484.r1/logs/build-4.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit "$rc"` — **EXIT=0** (production patch temporarily reversed).
5. `cmake --build build --target zene ArrangerTimelineTest -- -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-484.r1/logs/build-5.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit "$rc"` — **EXIT=0** (patch restored).

CTest commands were run from `build/tests` with `QT_QPA_PLATFORM=offscreen`:

1. `QT_QPA_PLATFORM=offscreen ctest -R '^ArrangerTimelineTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-484.r1/logs/test-1.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit "$rc"` — **EXIT=8**, RED on original handler.
2. `QT_QPA_PLATFORM=offscreen ctest -R '^ArrangerTimelineTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-484.r1/logs/test-2.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit "$rc"` — **EXIT=0**, GREEN with fix.
3. `QT_QPA_PLATFORM=offscreen ctest -R '^ArrangerTimelineTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-484.r1/logs/test-3.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit "$rc"` — **EXIT=8**, RED with production patch reversed.
4. `QT_QPA_PLATFORM=offscreen ctest -R '^ArrangerTimelineTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-484.r1/logs/test-4.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit "$rc"` — **EXIT=0**, GREEN after restoring the patch.

Patch proof commands, run from the worktree root:

- `git diff -- src/gui/ArrangerNavigator.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-484.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-484.r1/logs/fix.patch` — **EXIT=0**.
- `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-484.r1/logs/fix.patch` — **EXIT=0**.

Required gates, run from the worktree root:

- `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-484.r1/logs/gate-1.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit "$rc"` — **EXIT=0**.
- `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-484.r1/logs/gate-2.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit "$rc"` — **EXIT=0**.

## Files committed

- `src/gui/ArrangerNavigator.cpp`
- `tests/src/gui/ArrangerTimelineTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p10-484.md`
