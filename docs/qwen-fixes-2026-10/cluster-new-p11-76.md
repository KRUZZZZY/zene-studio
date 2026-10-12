# cluster new-p11-76 — Re-listening leaks a shutdown hook that can outlive the server

Verdict: **FIXED**

## Finding
`src/core/ControlServerSocket.cpp:451` — `close()` leaves the shutdown hook
registered, and a later `listen()` overwrites `m_shutdownHookId`. The destructor
removes only the newest hook, so a server that is closed, listened again, then
destroyed leaves the old hook behind — still capturing `this` — which the
forced-exit shutdown path would call `close()` on after destruction.

## Evidence
- `src/core/ControlServerSocket.cpp:451` (`adoptListener`): every listen
  registers a fresh hook and overwrites `m_shutdownHookId`:
  `m_shutdownHookId = m_registry->addShutdownHook([this]() { close(); });`
- `src/core/ControlServerSocket.cpp` `close()` (was lines 458-501): never touched
  `m_shutdownHookId` — the hook stayed registered after stop-listening.
- `src/core/ControlServer.cpp:153-167` (`~ControlServer`): removes only
  `m_shutdownHookId` (the newest id), so an orphaned older hook survives.
- `src/core/ControlRegistry.cpp:481-526`: hooks live in a process-lifetime store;
  `runShutdownHooks()` runs whatever is registered on the forced-exit route.

So: listen (hook A, id=A) → close (A still registered) → listen (hook B, id=B,
A orphaned) → destroy (removes only B) → A survives, capturing freed `this`.

## Fix
`close()` is the stop-listening path, so it now takes its own hook back out
before the platform branch (idempotent, `removeShutdownHook(0)` is a safe no-op,
so the destructor's own removal agrees):

```cpp
void ControlServer::close()
{
    if (m_registry != nullptr)
    {
        m_registry->removeShutdownHook(m_shutdownHookId);
        m_shutdownHookId = 0;
    }
#if !defined(Q_OS_UNIX)
    ...
```

Behaviour-preserving otherwise: while listening the hook stays registered (the
"survives a re-created registry" half is untouched); it is only un-registered
when the listener stops, which is exactly when its socket is already closed.

## Regression
New slot `aClosedListenerTakesItsHookWithItBeforeTheNextListen()` in the existing
area test `tests/src/core/ControlShutdownHookTest.cpp` (already registered in
`tests/CMakeLists.txt` LMMS_TESTS). It does listen → close → listen → destroy and
asserts `shutdownHookCount()` is 0 after `close()` and after destruction.

## Commands and EXIT codes (as run)
Build/test logs in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-76.r1/logs`.

- RED (original code):
  `cmake --build build --target ControlShutdownHookTest > .../build-1.log 2>&1` → BUILD_EXIT=0
  `ctest -R ControlShutdownHook --output-on-failure > .../test-1.log 2>&1` → **RED_EXIT=8**
  failing assertion: `Actual (registry->shutdownHookCount()): 1` (expected 0, after `close()`).
- GREEN (fixed):
  `cmake --build build --target ControlShutdownHookTest > .../build-2.log 2>&1` → BUILD_EXIT=0
  `ctest -R ControlShutdownHook --output-on-failure > .../test-2.log 2>&1` → **GREEN_EXIT=0**
  (`100% tests passed, 0 tests failed out of 1`)
- QA proof (test catches the bug):
  `git diff -- src/core/ControlServerSocket.cpp > .../fix.patch && git apply -R .../fix.patch` → REVERSED_OK
  `cmake --build build --target ControlShutdownHookTest > .../build-3.log 2>&1` → BUILD_EXIT=0
  `ctest -R ControlShutdownHook --output-on-failure > .../test-3.log 2>&1` → **REVERSED_EXIT=8** (FAIL)
  `git apply .../fix.patch` → REAPPLIED_OK
  `cmake --build build --target ControlShutdownHookTest > .../build-4.log 2>&1` → BUILD_EXIT=0
  `ctest -R ControlShutdownHook --output-on-failure > .../test-4.log 2>&1` → **REAPPLIED_EXIT=0** (PASS)
- Gates:
  `bash tests/complexity-gate.sh --check` → **COMPLEXITY_EXIT=0**
  `bash tests/file-length-gate.sh --check` → **LENGTH_EXIT=0**

Affected-family test count: 1 ctest (`ControlShutdownHookTest`), 5 slots including the new one.

## Files committed
- `src/core/ControlServerSocket.cpp` (fork-NEW, already in `tests/fork-sources.txt`)
- `tests/src/core/ControlShutdownHookTest.cpp` (existing area test, new slot)
- `docs/qwen-fixes-2026-10/cluster-new-p11-76.md` (this report)
