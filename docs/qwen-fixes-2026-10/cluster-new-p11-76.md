# cluster new-p11-76 — Re-listening leaks a shutdown hook that can outlive the server

Verdict: **FIXED**

## Finding and QA rework

`ControlServer::close()` must unregister the hook for the listener it stops so a
later `listen()` cannot orphan a callback capturing the server. Luna's round-one
review found that calling the non-static remover through `m_registry` was unsafe:
the documented shutdown path can destroy that registry while the live server's
hook remains in the process-wide store.

## Evidence and fix

- `include/ControlRegistry.h:273`: `removeShutdownHook()` is static. Its
  implementation in `src/core/ControlRegistry.cpp:489-505` only accesses the
  process-lifetime hook store, so removal does not need a registry instance.
- `src/core/ControlServerSocket.cpp:458-469`: `close()` unregisters its id using
  `ControlRegistry::removeShutdownHook()` before stopping the transport, then
  clears the id. The earlier re-listen cleanup remains in place.
- `src/core/ControlServer.cpp:153-164`: destruction also uses the static remover,
  covering a server whose registry was destroyed before the server itself.
- `tests/src/core/ControlShutdownHookTest.cpp:117-145` keeps a server listening,
  destroys its registry, recreates the registry, and runs the surviving hook;
  the socket is removed and the new registry's hook store is empty.
- `tests/src/core/ControlShutdownHookTest.cpp:149-154` checks that hook removal
  is invocable with only an id. It gives a deterministic red result if the
  process-lifetime removal API becomes instance-bound again.

## Verification evidence

Build and test logs are under
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-76.r2/logs`.
The affected family is `ControlShutdownHookTest`: **1/1 ctest passed**, covering
7 QtTest slots on this Unix build. The red runs report 8 passes and the expected
single failure in `shutdownHookRemovalDoesNotRequireARegistryInstance()`.

| Evidence | Exact command | Exit |
|---|---|---:|
| RED build before static-removal fix | `cmake --build build --target ControlShutdownHookTest > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-76.r2/logs/build-3.log' 2>&1; build_rc=$?; echo EXIT=$build_rc` | 0 |
| Initial test-target compile attempt (macro issue) | `cmake --build build --target ControlShutdownHookTest > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-76.r2/logs/build-2.log' 2>&1; build_rc=$?; echo EXIT=$build_rc` | 2 |
| RED ctest, run from `build/tests` | `ctest -R ControlShutdownHook --output-on-failure > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-76.r2/logs/test-1.log' 2>&1; test_rc=$?; echo EXIT=$test_rc` | 8 |
| GREEN rebuild | `cmake --build build --target zene ControlShutdownHookTest > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-76.r2/logs/build-4.log' 2>&1; build_rc=$?; echo EXIT=$build_rc` | 0 |
| GREEN ctest, run from `build/tests` | `ctest -R ControlShutdownHook --output-on-failure > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-76.r2/logs/test-2.log' 2>&1; test_rc=$?; echo EXIT=$test_rc` | 0 |
| Save product-only reversal patch | `git diff -- include/ControlRegistry.h src/core/ControlServer.cpp src/core/ControlServerSocket.cpp > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-76.r2/logs/fix.patch'; diff_rc=$?; echo EXIT=$diff_rc` | 0 |
| Reverse product patch | `git apply -R '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-76.r2/logs/fix.patch'; proof_rc=$?; echo EXIT=$proof_rc` | 0 |
| Reversed rebuild | `cmake --build build --target zene ControlShutdownHookTest > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-76.r2/logs/build-5.log' 2>&1; build_rc=$?; echo EXIT=$build_rc` | 0 |
| RED ctest with only product patch reversed, from `build/tests` | `ctest -R ControlShutdownHook --output-on-failure > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-76.r2/logs/test-3.log' 2>&1; test_rc=$?; echo EXIT=$test_rc` | 8 |
| Reapply product patch | `git apply '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-76.r2/logs/fix.patch'; apply_rc=$?; echo EXIT=$apply_rc` | 0 |
| Reapplied rebuild | `cmake --build build --target zene ControlShutdownHookTest > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-76.r2/logs/build-6.log' 2>&1; build_rc=$?; echo EXIT=$build_rc` | 0 |
| Reapplied GREEN ctest, run from `build/tests` | `ctest -R ControlShutdownHook --output-on-failure > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-76.r2/logs/test-4.log' 2>&1; test_rc=$?; echo EXIT=$test_rc` | 0 |
| Final rebuild after header comment clarification | `cmake --build build --target zene ControlShutdownHookTest > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-76.r2/logs/build-7.log' 2>&1; build_rc=$?; echo EXIT=$build_rc` | 0 |
| Final GREEN ctest, run from `build/tests` | `ctest -R ControlShutdownHook --output-on-failure > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-76.r2/logs/test-5.log' 2>&1; test_rc=$?; echo EXIT=$test_rc` | 0 |
| Final complexity gate | `bash tests/complexity-gate.sh --check > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-76.r2/logs/gate-complexity-2.log' 2>&1; gate_rc=$?; echo EXIT=$gate_rc` | 0 |
| Final file-length gate | `bash tests/file-length-gate.sh --check > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-76.r2/logs/gate-file-length-2.log' 2>&1; gate_rc=$?; echo EXIT=$gate_rc` | 0 |

The first red ctest fails the new instance-independence assertion against the
round-one implementation. Reversing the static API and dispatch changes
reproduces the same failure after rebuilding both requested targets. Restoring
the patch makes the same ctest pass. After the red/green proof, the header comment
was clarified to name both removal paths; the final build, ctest, and both gates
were rerun. Every green ctest run reports `1/1` and `0 tests failed`.

The first logged rebuild attempt exposed a test-only `QVERIFY2` macro parsing
issue with template commas. Parenthesizing the trait expression fixed it; the
subsequent `build-3.log` test-target build succeeded. Its repeat build in
`build-2.log` exited 2 before that correction.

The `fff` grep tool was not available in this session (`command -v fff` returned
no path), so log matching was done with `rg` for `error`, `failed`, and
`assertion`, plus short summaries. No full logs were printed.

## Files committed

- `include/ControlRegistry.h`
- `src/core/ControlServer.cpp`
- `src/core/ControlServerSocket.cpp`
- `tests/src/core/ControlShutdownHookTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p11-76.md`
