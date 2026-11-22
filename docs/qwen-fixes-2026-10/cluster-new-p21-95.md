FALSE-POSITIVE

# Cluster new-p21-95 — "songModulationLayer assumes a live song"

Finding: `src/core/ControlModulationSupport.cpp:125` (historical line; now 123-126) dereferences
`Engine::getSong()->modulationLayer()` with no null check. Claimed crash: a modulation command during
startup/shutdown or with no open project.

Verdict: the unchecked dereference is real, but no control command can reach it with a null song. The
engine-readiness gate in `ControlRegistry::invoke()` refuses every `modulator.*` command before its handler
runs, and the song is torn down only after the event loop has returned. No product code was changed, so there
is no RED/GREEN regression test (procedure step 2: a not-real verdict skips to evidence).

## Evidence (file:line, current tree at base cf3618e61)

1. **The dereference exists.** `src/core/ControlModulationSupport.cpp:123-126`:
   `ModulationLayerPublisher& songModulationLayer() { return Engine::getSong()->modulationLayer(); }`
   It is unchecked. The same file does guard the song elsewhere: `src/core/ControlModulationSupport.cpp:76`
   (`if (route.track < 0 || Engine::getSong() == nullptr) { return nullptr; }`).

2. **Every modulator command takes the default `requiresEngine = true`.** The commands are built by hand
   (`src/core/ControlCommandsModulator.cpp:161-181` is `modulator.get_state`, and the other three
   registrations at `:249`, `:293`, `:373` follow the same pattern). None of the `ControlCommandsModulator*.cpp`
   files sets `requiresEngine` (`rg -n requiresEngine src/core/ControlCommandsModulator*.cpp` → 0 matches).
   The struct default is `include/ControlRegistry.h:104`: `bool requiresEngine = true;`.

3. **The gate runs before every handler.** `src/core/ControlRegistry.cpp:283-289`:
   `if (cmd->requiresEngine && !isEngineReady())` returns `ControlErrorKind::Busy`
   ("the engine is not addressable yet ..."). `isEngineReady()` is at `src/core/ControlRegistry.cpp:205-208`:
   `return s_ready && Engine::getSong() != nullptr && Engine::mixer() != nullptr;`.
   The handler is only reached after that gate: `src/core/ControlRegistry.cpp:293` (copy of the handler),
   `:309` (`runHandler`, inside `runOnUiThread`), and `:449` (`return handler(args);`). `handler(` is called in
   no other place in `src/core/ControlRegistry.cpp`.

4. **Startup is refused.** `s_ready` is set only at `src/core/main.cpp:1831`
   (`controlServer->registry()->setReady( true );`), after the song exists (`src/core/main.cpp:1790-1795`
   `createNewProject()`). The comment at `src/core/main.cpp:1826-1828` states that before this point every
   command returns the typed 'busy' refusal. The socket listens earlier (`src/core/main.cpp:1387`), but
   nothing is dispatched past the gate until `setReady(true)`.

5. **Shutdown cannot reach the handler with no song.** Order in `src/core/main.cpp`:
   `app->exec()` returns (`:1838`) → `controlServer->close()` (`:1849`) → `delete app` (`:1858`) →
   `Engine::destroy()` (`:1862`). `Engine::destroy()` clears the song (`src/core/Engine.cpp:96-123`).
   `MainWindow::~MainWindow` also calls `Engine::destroy()` (`src/gui/MainWindow.cpp:282`), inside the
   destructor (`src/gui/MainWindow.cpp:267`); `MainWindow::finalize()` (`:285`) is the startup path
   (`src/gui/GuiApplication.cpp:259`), not a shutdown path. The song is torn down only after the event
   loop has returned, so no queued handler runs against a torn-down song.

6. **"No open project" does not null the song.** `Engine::s_song` (`src/core/Engine.cpp:48`) is a static
   pointer, set once in `Engine::init` (`src/core/Engine.cpp:72`, `s_song = new Song`) and cleared only by
   `Engine::destroy()` (`src/core/Engine.cpp:123`). Nothing in the no-project path resets it (`main.cpp:1790-1795` create a new project in the song, they do not drop the song). The modulation layer is a `Song` accessor: the existing
   test calls it on `Engine::getSong()` (`tests/src/core/ControlModulatorCommandsTest.cpp:64-65`, `:105`).

**Residual (not a live bug).** The readiness check runs on the socket thread (`ControlRegistry.cpp:283`)
and the handler later runs on the UI thread (`:293-309`). The song is torn down only after the loop
returns, so a teardown cannot fall between the two. A defensive check inside `songModulationLayer()` is
therefore not needed for reachable paths. I did not add one. The accessor returns a reference, so a
null-checked version would change roughly 30 call sites. Behaviour is unchanged.

## Commands run and EXIT codes

| # | Command | EXIT | Result |
|---|---------|------|--------|
| 1 | `rg -n songModulationLayer src tests` and related `rg` reads (`ControlRegistry.cpp`, `ControlModulationSupport.cpp`, `main.cpp`, `MainWindow.cpp`, `Engine.cpp`) | n/a (read-only searches) | evidence above |
| 2 | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > LOG/gates-1.log 2>&1` | 1 | Failed before running any check: `PermissionError` creating the sibling log dir `zene-qwen-new-p21-95-feature-check` at `feature_check.py:71` (the write-jail blocks the parent). The log was later overwritten by mistake; the traceback's final line is quoted here verbatim. |
| 3 | Copy of `feature_check.py` in LOG FOLDER with only two lines changed: `ROOT` pointed at the original qwen dir (`feature_check.py:19`) and `logs` redirected into LOG FOLDER (`:70`). Run: `python3 -I LOG/feature_check_logdir.py <worktree> > LOG/gates-2-copy-logdir.log 2>&1` | 0 | `RESULT: PASS` (8 items: fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures, complexity, file-length — all PASS). Gate limits and baselines unchanged. |

Test runs: none. No product file changed, so no build and no ctest. Affected family: `ControlModulatorCommandsTest`
was not run. This verdict rests on code reading, not on a test.

## Self-check

- **The regression test FAILS with the product change reverted and PASSES with it.** Not applicable: no
  product change and no new test, because the verdict is FALSE-POSITIVE. There is no fix to revert. Stated
  plainly rather than skipped.
- **The test is deterministic.** Not applicable (no test added).
- **The fix is at the cause and complete.** Not applicable (no fix). The cause analysed is the readiness gate
  (`ControlRegistry.cpp:283-289`), and every `modulator.*` handler is behind it (evidence 2-3).
- **No collateral damage.** Nothing changed, so none.
- **Verdict evidence (file:line or contract).** Given above: `ControlRegistry.h:104`, `ControlRegistry.cpp:205-208`,
  `:283-289`, `:449`, `main.cpp:1831`, `:1838`, `:1849`, `:1862`.
- **Gates.** Copy-run EXIT=0 (row 3). The original script exits 1 from the write-jail (row 2), so the
  original gate was not executed as written. The copy changes only the log path.
- **Not verified by execution:** the claim that no test covers the null-song path. The test suite can't null
  `Engine::getSong()` without tearing the engine down. This is established by reading `tests/...` only.
- **submit_check.py `gates (feature_check.py)` item: FAIL, environmental.** The check runs the unmodified
  `feature_check.py`, which exits 1 with the `PermissionError` at `feature_check.py:71` (it cannot create
  `zene-qwen-new-p21-95-feature-check` beside the worktree, outside the write-jail). Log saved at
  `LOG/submit-check-1.log` (EXIT=1). The same gate logic with only the log path redirected passed (EXIT=0,
  row 3). This is not a code failure; it cannot be fixed from inside the worktree. Reviewers should re-run
  `feature_check.py` from an environment that can write beside the worktree.
