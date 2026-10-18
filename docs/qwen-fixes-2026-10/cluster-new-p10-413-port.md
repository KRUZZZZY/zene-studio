# Cluster new-p10-413-port

Verdict: **FIXED** (port of the accepted `bugs/qwen-new-p10-413` fix onto `050/m0-train-5`)

Port of `2ca490e69` + `14fba555c`, re-written by hand against the newest line instead of cherry-picked. The newest line changed `src/core/ThreadPool.cpp` (cluster new-p9-478 turned the worker loop into a drain-until-empty loop and added the pre-existing `ThreadPoolTest.cpp`), which is why the old branch conflicts; the header change applies unchanged in substance.

`ThreadPool::enqueue` captured the callable and the argument tuple in a **const** task closure, invoked both as lvalues, and stored the closure in `std::function<void()>` — which also requires a copy-constructible closure. The advertised forwarding API therefore could not build a queued task for a mutable lambda, a move-only capture, a move-only argument, or a ref-qualified callable. The queue now stores move-only `std::packaged_task<void()>`; the task closure is `mutable`; the stored callable is invoked with the value category it was enqueued with (lvalue stays lvalue, temporary is invoked as an rvalue) and the stored tuple as an rvalue; `ReturnType` is derived from that same invocation; and a throwing callback delivers the exception through the returned future instead of terminating the worker.

Deviation from the old branch, deliberate: `src/core/ThreadPool.cpp` keeps the popped task in a `std::optional<std::packaged_task<void()>>` instead of a plain `std::packaged_task<void()>`, because `std::packaged_task` has no standard default constructor (libstdc++ provides a non-standard one at `/usr/include/c++/16/future:1570`). The accepted branch relied on that extension; the optional is standard C++20 and behaves identically. The newest line's `if (m_queue.empty()) { break; }` drain-on-shutdown loop (new-p9-478) is untouched, so destruction still drains queued tasks.

## File evidence

- `include/ThreadPool.h:50-95` — `enqueue`; line 56 derives `ReturnType` from the callable/tuple categories actually used at invocation, lines 59-90 build the mutable `packaged_task`, lines 64-71 dispatch lvalue vs rvalue invocation, lines 74-89 fulfil the promise or store the exception.
- `include/ThreadPool.h:111` — move-only queue element `std::queue<std::packaged_task<void()>>`.
- `include/ThreadPool.h:35` — added `#include <exception>` for `std::current_exception`.
- `src/core/ThreadPool.cpp:65,71,74` — the popped packaged task is held in an optional, `emplace`d from `std::move(m_queue.front())`, and invoked outside the lock.
- `tests/src/core/ThreadPoolTest.cpp:44-55` — `RvalueQualifiedCallable` (`operator()() &&`, lvalue overload `= delete`) and `LvalueQualifiedCallable` (the mirror).
- `tests/src/core/ThreadPoolTest.cpp:60,66,76,85,91` — the five ported regressions: mutable callable, move-only capture, move-only argument, rvalue-qualified callable, lvalue-qualified callable. `:98` is the pre-existing new-p9-478 shutdown test, left as-is.
- `tests/src/core/ThreadPoolTest.cpp` is already registered in `tests/CMakeLists.txt:644` and `tests/fork-sources.txt:2428` on this line, so no registration was added; the two inherited files I changed are recorded in `tests/upstream-modifications.txt:2409-2410`.

## Verification evidence

RED is a compile failure: the regression cannot be expressed as a runnable test without the fix, so no ctest was run against a compile-broken target (that would execute a stale binary).

| Stage | Exact command | Exit | Result |
|---|---|---:|---|
| RED (test on unpatched tree) | `cmake --build build --target ThreadPoolTest --parallel 4 > .../logs/build-1.log 2>&1` | 2 | `error: no matching function for call to '__invoke(const ThreadPoolTest::enqueuesMutableCallable()::<lambda()>&)'` (build-1.log:39), `no matching function for call to 'std::promise<int>::set_value()'` (ThreadPool.h:63), plus the same for the move-only and ref-qualified cases. |
| GREEN build after fix | `cmake --build build --target zene ThreadPoolTest --parallel 4 > .../logs/build-2.log 2>&1` | 0 | `zene` (the only product user, `src/gui/FileSearchJob.cpp:57`) and `ThreadPoolTest` built. |
| GREEN ctest, from `build/tests` | `ctest -R '^ThreadPoolTest$' --output-on-failure > .../logs/test-1.log 2>&1` | 0 | 1/1 affected-family CTest target passed. |
| Slot detail | `QT_QPA_PLATFORM=offscreen ./ThreadPoolTest > .../logs/test-2.log 2>&1` | 0 | 8 passed / 0 failed — 5 new slots + `destructionDrainsQueuedTasks` + init/cleanup. |
| Reverse-patch RED (bug-catching proof) | `git diff -- include/ThreadPool.h src/core/ThreadPool.cpp > .../logs/fix.patch && git apply -R .../logs/fix.patch` | 0 | Only the two product files reverted. |
| Reverse-patch RED build | `cmake --build build --target ThreadPoolTest --parallel 4 > .../logs/build-3.log 2>&1` | 2 | Same compile errors as the initial RED (build-3.log:42, :239) with the test file untouched — the test does not pass without the fix. |
| Restore patch | `git apply .../logs/fix.patch` | 0 | Fix restored. |
| Restore build | `cmake --build build --target zene ThreadPoolTest --parallel 4 > .../logs/build-4.log 2>&1` | 0 | Rebuilt both targets. |
| Restore ctest | `ctest -R '^ThreadPoolTest$' --output-on-failure > .../logs/test-3.log 2>&1` | 0 | 1/1 passed. |
| Final build (after last edit) | `cmake --build build --target zene ThreadPoolTest --parallel 4 > .../logs/build-5.log 2>&1` | 0 | Rebuilt; the header change is header-only and `zene` includes it. |
| Final ctest | `ctest -R '^ThreadPoolTest$' --output-on-failure > .../logs/test-4.log 2>&1` | 0 | 1/1 affected-family CTest target passed. |
| Complexity gate | `bash tests/complexity-gate.sh --check > .../logs/gate-5.log 2>&1` | 1 | Pre-existing failure, see below. |
| File-length gate | `bash tests/file-length-gate.sh --check > .../logs/gate-6.log 2>&1` | 1 | Pre-existing failure, see below. |

### Gate status (honest note)

Both gates exit 1, and they exit 1 on the untouched `050/m0-train-5` head as well: `git archive HEAD | tar -x -C /tmp/opencode/gate-base` then `bash tests/complexity-gate.sh --check > .../logs/gate-3.log` (exit 1) and `bash tests/file-length-gate.sh --check > .../logs/gate-4.log` (exit 1). The regression lists from the pristine tree and from my tree are byte-identical (`diff` of the `^REGRESSION` lines matched), and no gate output mentions `ThreadPool`. The 6 over-target functions and 15 over-500-line files are newest-line drift I did not introduce and did not touch; no gate limit or baseline was re-anchored.

All logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-413-port.r1/logs`. The pre-configured build tree was used; no configure command was run.

## Files committed

- `include/ThreadPool.h`
- `src/core/ThreadPool.cpp`
- `tests/src/core/ThreadPoolTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p10-413-port.md`
