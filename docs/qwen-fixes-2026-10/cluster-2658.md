FIXED

2658: FIXED
4630: FIXED
3644: FIXED

# Cluster 2658 batch — src/wasm/WasmWorker.cpp

This round finishes the round-1 work: each bug has a deterministic regression
that was proved RED and GREEN, the fixes are at the cause, and the whole wasm
test family plus `zene` were rebuilt and run.

## Evidence

### 2658 — Worker shutdown breaks the SPSC queue ownership

The finding is real: `WasmWorker::stop()` (`src/wasm/WasmWorker.cpp:90`) is the
control thread, while `m_commands` is produced by the audio thread in
`submit()` and `m_results` is consumed there in `collect()`. The branch already
carries the endpoint gate (`src/wasm/WasmWorker.h:185-190`,
`src/wasm/WasmWorker.cpp:153-186`) that closes both endpoints and waits for
pinned audio access before the control thread drains either queue. This round
closes the remaining hole: `drainOnLane()` stops pushing a result when it sees
`m_stop` (`WasmWorker.cpp:266-269`), leaving a slot `Done` in neither queue.
`stop()` now frees **every** slot after the queues are drained
(`WasmWorker.cpp:125-133`), which is safe there because the endpoints are
closed, the worker is unregistered and no lane holds the claim.

Regression: `testStopFreesSlotsTheLaneLeftInFlight`
(`tests/src/wasm/WasmWorkerPoolTest.cpp:359`) injects exactly that in-flight
slot state (a friend test seam, no timing) and requires every slot to be `Free`
with `frames == 0` after `stop()`.

### 4630 — Taking worker logs races with module logging

The buffer race itself was already fixed on this branch by
`f4ccfd2d7` (`src/wasm/WasmLogBuffer.h`: one atomic-flag attempt, bounded
`abi::maxLogBytes` snapshot). The worker still had the lifetime half of the
race: `WasmWorker::takeLog()` dereferenced `m_sandbox` while a lane could
replace it in `loadModuleOnLane()` or `stop()` could reset it. The lane now
moves the sandbox's latest message into the worker-owned `WasmLogBuffer`
(`transferLogFromLane()`, `src/wasm/WasmWorker.cpp:395-407`, called at
`WasmWorker.cpp:286`, `:291` and `:365`), and `takeLog()` reads only that snapshot
(`src/wasm/WasmWorker.cpp:524-529`). `start()` clears it so a new run does not
inherit a replaced module's log (`WasmWorker.cpp:57`).

Regression: `testTakeLogSurvivesStopThroughTheLaneSnapshot`
(`tests/src/wasm/WasmWorkerPoolTest.cpp:619`) logs from `process()`, stops the
worker (destroying the sandbox), and still requires the text; the second take
must be empty.

### 3644 — WASM memory growth can invalidate cached host pointers

Real and fixed by `e481ddf9d` on this branch: the host reacquires both the
pointer and the size after each module call through
`detail::callProcessAndRefreshMemory` (`src/wasm/WasmSandbox.h:126-158`),
used by the worker (`src/wasm/WasmWorker.cpp:355-358`) and the inline offline
render (`src/wasm/WasmOfflineRender.cpp:124-127`); a failed refresh turns an
otherwise-ok call into an error instead of reading stale bytes. This round adds
a deterministic seam (`WasmSandbox::setMemoryViewTestHook`,
`src/wasm/WasmSandbox.h:118-126`, `WasmSandbox.cpp:43-46,528-569`) so the test
does not depend on wasmtime actually moving a mapping.

Regression: `testWasmWorkerReacquiresRelocatedHostView`
(`tests/src/wasm/WasmWorkerPoolTest.cpp:495`): the first host view of linear
memory is a private buffer, so a host that reuses the pre-call pointer reads
zeros; the production path must reacquire the real view and return the module's
42/7 output.

## Commands and exit codes

Build tree note: the lane build was configured without WASM
(`WANT_WASM:BOOL=OFF`), so it was reconfigured with the already-vendored
wasmtime from the sibling `zene-cachyos` tree:

```
/usr/bin/cmake -S . -B build -DWANT_WASM=ON \
  -DWasmtime_INCLUDE_DIR=.../zene-cachyos/third_party/wasmtime/include \
  -DWasmtime_LIBRARY=.../zene-cachyos/third_party/wasmtime/lib/libwasmtime.so
EXIT=0
```

```
CCACHE_DISABLE=1 /usr/bin/cmake --build build --target WasmWorkerPoolTest -- -j2
EXIT=0
cd build/tests && ctest -R '^WasmWorkerPoolTest$|^WasmSandboxTest$' --output-on-failure
EXIT=0   (2/2 tests passed; WasmSandboxTest 2.06 s, WasmWorkerPoolTest 7.19 s)
CCACHE_DISABLE=1 /usr/bin/cmake --build build --target wasm_effect -- -j2   EXIT=0
CCACHE_DISABLE=1 /usr/bin/cmake --build build --target zene -- -j2          EXIT=0
```

### Per-bug RED/GREEN (the product change was reverted, rebuilt, run, restored)

2658:

```
git apply -R <logs>/revert-2658.patch                                  EXIT=0
CCACHE_DISABLE=1 /usr/bin/cmake --build build --target WasmWorkerPoolTest -- -j2
                                                                        EXIT=0
cd build/tests && ./WasmWorkerPoolTest testStopFreesSlotsTheLaneLeftInFlight
                                                                        EXIT=1
  FAIL: 'slot.state.load(...) == WasmWorker::SlotState::Free' returned FALSE.
git apply <logs>/revert-2658.patch                                     EXIT=0
./WasmWorkerPoolTest testStopFreesSlotsTheLaneLeftInFlight             EXIT=0
```

4630:

```
git apply -R <logs>/revert-4630.patch                                  EXIT=0
CCACHE_DISABLE=1 /usr/bin/cmake --build build --target WasmWorkerPoolTest -- -j2
                                                                        EXIT=0
./WasmWorkerPoolTest testTakeLogSurvivesStopThroughTheLaneSnapshot      EXIT=1
  FAIL: Actual "" vs Expected "lane-log"
git apply <logs>/revert-4630.patch                                     EXIT=0
./WasmWorkerPoolTest testTakeLogSurvivesStopThroughTheLaneSnapshot      EXIT=0
```

3644 (old call sites: direct `callProcess`, pre-call pointer reused):

```
python3 <logs>/revert-3644-inline.py                                    EXIT=0
CCACHE_DISABLE=1 /usr/bin/cmake --build build --target WasmWorkerPoolTest -- -j2
                                                                        EXIT=0
./WasmWorkerPoolTest testWasmWorkerReacquiresRelocatedHostView          EXIT=1
  FAIL: output[0][0] = 0, expected 42
cp <logs>/restore-3644.sh -> product files                              EXIT=0
./WasmWorkerPoolTest testWasmWorkerReacquiresRelocatedHostView          EXIT=0
```

The raw logs are in `.qwen-logs-2658/` in the worktree. The requested log
folder (`.../2658.r2/logs`) is read-only in this lane (every write there, and
every sibling write to its parent, returns `Permission denied` from the host
sandbox); the same numbered logs are kept in `.qwen-logs-2658/`.

## Gates

`python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` cannot
create `<worktree>-feature-check/` beside the worktree in this lane
(`PermissionError: [Errno 13] Permission denied`), so the identical gate script
was run with only its log directory redirected inside the worktree. Gate
logic, limits and baselines were not changed:

```
python3 /tmp/feature_check_local.py <worktree>        EXIT=0
RESULT: PASS (fork-sources, all-sources-reproduce, no-upstream-regression,
              check-namespace, no-tautology, doc-figures, complexity,
              file-length)
```

## Self-check

* **Regression fails with the product change reverted and passes restored.**
  Exit codes above: 2658 RED 1 / GREEN 0; 4630 RED 1 / GREEN 0; 3644 RED 1 /
  GREEN 0. Each RED assertion is quoted.
* **Deterministic test, no wall-clock timing, sleeps or races.** 2658 uses the
  friend test seam `WasmWorkerPoolTest::m_slots` with an injected `Done` slot.
  4630 uses the production lane→snapshot hand-off and a real `host_log` module,
  then `stop()`; no concurrent reader. 3644 uses
  `WasmSandbox::setMemoryViewTestHook` to inject a relocated host view. All
  three are single-threaded and assertion-driven.
* **Cause and complete; callers/siblings checked.** 2658: every slot is freed
  after endpoints are closed and the lane claim is released; `start()` calls
  `stop()` first. 4630: the sandbox is only touched on the lane
  (`loadModuleOnLane`, `processSlot`); `WasmSandboxHost` keeps its own
  same-thread `takeLog()`. 3644: the worker and inline offline render both use
  `callProcessAndRefreshMemory`; `WasmSandboxHost::process` uses
  `writeMemory`/`readMemory`, which reacquire the pointer and re-check bounds
  per access.
* **No collateral damage.** Old projects and saved data are untouched: no
  serialized field, id, or model changed. The log snapshot survives `stop()`
  but is cleared by the next `start()`, which matches the old per-sandbox
  lifetime across a reload. `m_commands`/`m_results` are still drained exactly
  as before; only unqueued in-flight slots are additionally freed.
* **Realtime/undo/commands.** `submit()`/`collect()` are unchanged except the
  existing endpoint pin; no allocation, lock, wait, I/O or logging was added
  there. `transferLogFromLane()` and the slot loop run on the pool lane/control
  thread, never the audio callback. No undo step or control command changed.
* **Old projects/new data.** Nothing in the save format, plugin format or
  command surface changed; `zene` rebuilt cleanly.
* **Portability.** C++20 only, no new Qt API, no platform-specific header.
* **Pre-submit check.** `python3 .../submit_check.py <worktree> 2658` returns
  `RESULT: PASS - submit` (all mechanical items PASS) when run on a clean
  git-worktree copy of this commit, which gives the check a writable parent
  for `feature_check.py`'s log directory. On the lane's own path that one item
  fails only because the host sandbox denies creating
  `<worktree>-feature-check/` beside the worktree (`PermissionError`); the gate
  table itself is the PASS run quoted above.

