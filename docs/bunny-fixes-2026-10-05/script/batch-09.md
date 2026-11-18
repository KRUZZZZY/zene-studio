# Space Bunny fixer lane `script` — batch-09

All five findings in this batch sit in `src/wasm/`, which this build tree does **not** compile, and every
candidate regression test (`WasmWorkerPoolTest`, `WasmSandboxTest`, `WasmAbiConformanceTest`) is registered only
under `if(WANT_WASM)` (`tests/CMakeLists.txt:1852`, `:1905`, `:1935`). `WANT_WASM` degrades to OFF here:
`build/CMakeCache.txt` holds `WANT_WASM:BOOL=ON` but `Wasmtime_INCLUDE_DIR:PATH=Wasmtime_INCLUDE_DIR-NOTFOUND`
and `Wasmtime_LIBRARY:FILEPATH=Wasmtime_LIBRARY-NOTFOUND`, so `CMakeLists.txt:1015-1022` flips it OFF, and
`src/CMakeLists.txt:35-37` drops the whole `wasm` subdirectory. Evidence: no `Wasm*.o` anywhere under `build/`,
`nm -C build/lib/lmmsobjs.a | grep -c WasmWorkerPool` → `0`, and `ctest -N` lists 402 tests with no WASM entry.

So no finding in this batch can be given a red/green regression here, and per procedure step 2 **no product
code was changed**. Verdicts below are from reading the current tree at the cited lines. Note that batch-07
already reached the same wall for the same subsystem and recorded it in its `Leftovers` line.

### Cluster 1802 — Nonzero WASM process results are treated as success
Verdict: FALSE-POSITIVE
Evidence: `src/wasm/WasmAbi.h:40` documents the convention `process(...) -> i32 (0 = ok)`, but
`src/wasm/WasmSandbox.cpp:462-465` only records the value into `CallResult::returnValue` and leaves
`status = Ok`; `src/wasm/WasmWorker.cpp:253` branches on `result.ok()`, which is `status == Status::Ok`
(`src/wasm/WasmSandbox.h:54`) — the wasmtime call status, never the value. `docs/WASM-EFFECT-ABI.md:239-244`
documents that outcome deliberately: "**A module cannot signal an error by returning a non-zero value** … the
host does not implement a check for it." An existing conformance case pins it:
`tests/src/wasm/WasmAbiConformanceTest.cpp:407-419` `abiProcessReturnValueIsRecordedNotActedOn` loads a module
returning `7` and requires `QVERIFY(result.ok()); QCOMPARE(result.returnValue, 7);` under the comment "A
non-zero result must NOT be treated as an error (s8)". Implementing the reported fix would fail that case, i.e.
it would mean weakening an existing test (rule 5). This is the same finding as batch-07's Cluster 5438, which
reached this conclusion from the same evidence.
Commit: none
Test: `WasmAbiConformanceTest::abiProcessReturnValueIsRecordedNotActedOn` (existing, pins the behaviour; not
built or run in this lane)
Red/green: none

### Cluster 4630 — Taking worker logs races with module logging
Verdict: REAL-UNTESTED
Evidence: real, but confined to code this tree cannot build or exercise. `src/wasm/WasmSandbox.cpp:561-570`
`takeLog()` reads and clears the non-atomic `m_impl->logSize` and copies out of `m_impl->log` (a plain
`std::array<char, abi::maxLogBytes>`), while the sandbox's `hostLogCallback` at `:226-230` writes exactly those
two objects from whichever lane is executing the module. `WasmWorker::takeLog()` (`src/wasm/WasmWorker.cpp:389-396`)
and `WasmSandboxHost::takeLog()` (`src/wasm/WasmSandboxHost.cpp:278-281`) reach it with no synchronisation, and
the first is reachable from the control path while lanes are live. `src/wasm/WasmSandbox.h:59-61` documents the
sandbox as deliberately single-threaded ("must only be used by the thread that owns it"), which is precisely the
precondition this breaks. A correct fix needs a lock-free handoff from the lane (the producer) to the control
thread, and the only deterministic proof is a threaded race test that must link wasmtime — impossible here.
Commit: none
Test: none — would need a new threaded case in `tests/src/wasm/WasmWorkerPoolTest.cpp` (the pool test already
owns a live `WasmWorker` and its lane) or a `WasmSandboxTest` case with a logging module plus a concurrent
`takeLog()` thread; both targets do not exist in this tree.
Red/green: none — `cmake --build build --target WasmWorkerPoolTest` → `EXIT=2`
(`gmake: *** No rule to make target 'WasmWorkerPoolTest'.  Stop.`)

### Cluster 3642 — Pool block statistics undercount queued blocks
Verdict: REAL-UNTESTED
Evidence: real. `src/wasm/WasmWorkerPool.cpp:173-178` increments `m_blocks` by exactly **1** per worker whose
`drainOnLane()` returned true, but `src/wasm/WasmWorker.cpp:146-175` shows `drainOnLane()` loops popping and
processing **every** queued block (`while (m_commands.pop(index))`) and bumps `m_processed` once per block. A
single drain of 6 queued blocks therefore reports `blocks += 1`. This contradicts the documented contract at
`src/wasm/WasmWorkerPool.h:95-96` ("`blocks` is the number of queued blocks the lanes processed"), and the
counter is user-visible through `wasm.pool` → `blocks` at `src/core/ControlCommandsWasmRender.cpp:145`.
Commit: none
Test: none — a regression would queue several blocks, drain one lane by hand via the public `drainOnce()`
(`src/wasm/WasmWorkerPool.h:118`) and compare `stats().blocks` against `WasmWorker::processedBlocks()`; that
test needs a loaded module and the pool lanes, so it must live in `WasmWorkerPoolTest`, which does not build here.
Red/green: none — `cmake --build build --target WasmWorkerPoolTest` → `EXIT=2` (no such target)

### Cluster 5475 — Removed workers can make the pool reject new workers prematurely
Verdict: REAL-UNTESTED
Evidence: real. `src/wasm/WasmWorkerPool.cpp:94-95` uses `m_workerCount` as the live-worker count for the
capacity check, but that member is only ever the length of the **occupied prefix**: `addWorker` stores `i + 1`
(`:101`) and `removeWorker` shrinks it only when the removed slot was the last occupied one (`:121-130`). So
removing a worker from slot 0 of a full 64-slot pool leaves `m_workerCount == 64`, and the next `addWorker` hits
`if (count >= maxWorkers) return false;` at `:95` even though slot 0 is free. `stats().workers`
(`src/wasm/WasmWorkerPool.cpp:211`) publishes the same prefix length, so it also over-reports while the state
lasts. The reachable case needs a full 64-worker pool; the fix is to track a live count separately from the
prefix length. Not a false positive, but nothing in this lane can execute a 64-worker pool.
Commit: none
Test: none — the natural case fills `WasmWorkerPool::maxWorkers` (64) slots with started `WasmWorker`s, removes
one from a non-final slot and asserts `addWorker` succeeds and `stats().workers` drops; that belongs in
`WasmWorkerPoolTest::testPoolIsSharedAndBounded`, which does not build here.
Red/green: none — `cmake --build build --target WasmWorkerPoolTest` → `EXIT=2` (no such target)

### Cluster 4627 — Reusing a worker slot can hide later workers
Verdict: REAL-UNTESTED
Evidence: real, and it is the same `m_workerCount` line as Cluster 5475 seen from the scan side. With three
workers in slots 0-2, `removeWorker` at `:121-130` does not shrink the count (slot 1 is not the last occupied
slot), so it stays 3; the next `addWorker` finds the hole at slot 1 and executes
`m_workerCount.store(i + 1, ...)` at `:101`, lowering the count to 2. `drainOnce` scans only `i < count`
(`:164-165`) and `hasPendingWork` likewise (`:152-153`), so the worker still sitting in slot 2 stops being
scanned and never gets drained. The report's example is confirmed line-for-line. Related-but-distinct from
Cluster 5475: this one needs `addWorker` to preserve `max(count, i + 1)`; 5475 needs a live count separate from
the prefix. The two fixes touch the same store and should land together.
Commit: none
Test: none — three real `WasmWorker`s plus a lane pass, i.e. `WasmWorkerPoolTest`, which does not build here.
Red/green: none — `cmake --build build --target WasmWorkerPoolTest` → `EXIT=2` (no such target)

Leftovers: four real defects left unfixed (4630 log race, 3642 `m_blocks` undercount, 5475 premature
`addWorker` rejection, 4627 slot reuse hiding later workers) — none can be regression-tested in this tree
because `WANT_WASM` degrades OFF without the wasmtime C API, so all five of this batch's code paths are compiled
out; a wasmtime-enabled build tree is the prerequisite for all four. The two pool defects share one store at
`src/wasm/WasmWorkerPool.cpp:101` and should be fixed as a pair (live count separate from the prefix length, and
`addWorker` preserving `max(count, i + 1)`). Cluster 1802 must NOT be "fixed": the behaviour is documented at
`docs/WASM-EFFECT-ABI.md:239-244` and pinned by `abiProcessReturnValueIsRecordedNotActedOn`. Both gates were run
and pass on the unchanged tree: `bash tests/complexity-gate.sh --check` → `EXIT=0`,
`bash tests/file-length-gate.sh --check` → `EXIT=0`.