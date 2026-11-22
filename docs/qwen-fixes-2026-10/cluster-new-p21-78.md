FALSE-POSITIVE

# Cluster new-p21-78 — `automation.latency_compensation` dereferences `Engine::getSong()` unguarded

Branch `bugs/qwen-new-p21-78`, base `7641e9493d` (`050/m0-train-18` snapshot). Finding: `src/core/ControlCommandsW3Engine.cpp:73`.

## Verdict

The unguarded `Engine::getSong()->setModified()` at `src/core/ControlCommandsW3Engine.cpp:73` is only reached when the
engine is ready, and readiness requires a non-null song. The handler cannot run with `Engine::getSong() == nullptr`, so
the finding's trigger ("toggled with no song") cannot occur through any input the control surface accepts. No product
change is made, so there is no regression test to add: no input reaches the null branch, and a test could not fail
without a fix.

## Evidence (file:line)

1. **The call site.** `src/core/ControlCommandsW3Engine.cpp:65-80`: `latencyCompensation()` toggles
   `AutomationLatency`, pushes an undo step (`:71-72`), then calls `Engine::getSong()->setModified()` (`:73`). The
   function sits in an anonymous namespace (`:60`), so it is reachable only through the registry.
2. **Registration requires the engine.** `ControlCommandsW3Engine.cpp:270-275` registers it with
   `defineCommand(...)`, which is the 7-argument overload (no `requiresEngine` parameter).
   `src/core/ControlCommandDef.cpp:50-54` forwards to `defineCommandWithSchema`, which sets
   `cmd.requiresEngine = requiresEngine` at `ControlCommandDef.cpp:45`. The default is `true`
   (`include/ControlCommandDef.h:53`, `include/ControlRegistry.h:104`).
3. **The dispatcher gates it.** `ControlRegistry.cpp:283-287`: when `cmd->requiresEngine && !isEngineReady()`, `invoke()`
   returns `ControlErrorKind::Busy` and the handler never runs.
4. **Readiness includes the song.** `ControlRegistry.cpp:205-207`:
   `isEngineReady()` = `s_ready && Engine::getSong() != nullptr && Engine::mixer() != nullptr`.
5. **Single path to the handler.** The handler is reached only through `invoke()` → `ControlRegistry.cpp:296`
   (`runOnUiThread`) → `:309` (`runHandler`) → `:514` (`runHandlerGuarded`) → `handler(args)` (`:449`). A grep of `src/`
   finds no other caller of `runHandler`/`runHandlerGuarded` outside `ControlRegistry.cpp`.
6. **The UI-thread hop is blocking.** `ControlRegistry.cpp:258` uses `Qt::BlockingQueuedConnection`, so the song cannot
   be torn down between the gate at `:283` and the handler at `:309`.
7. **Shutdown order.** `src/core/main.cpp:1849` closes the control server before `Engine::destroy()` at `main.cpp:1862`;
   `Engine.cpp:123` is the only place `s_song` is deleted. `setReady(true)` is at `main.cpp:1831`.

**Residual, outside this finding.** The readiness check on the socket thread (`:283`) is not re-checked inside the
queued lambda (`:296`). In principle a command queued just before shutdown could run after `Engine::destroy()`. That
gap applies to every `requiresEngine` command, not just this one. It is not reproduced here and is not what this
finding describes. I did not change it.

## Commands run

| # | Command | Result |
| --- | --- | --- |
| 1 | `git status --short; git log --oneline -1` (worktree) | clean; `7641e9493d` |
| 2 | Read-only `rg` / `sed` on `src/core/ControlCommandsW3Engine.cpp`, `ControlRegistry.cpp`, `ControlCommandDef.cpp`, `main.cpp`, `Engine.cpp`, `include/ControlRegistry.h`, `include/ControlCommandDef.h` | evidence above |
| 3 | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > LOG/feature-check-1.log 2>&1; echo EXIT=$?` | **EXIT=1**, `PermissionError` on `<worktree>-feature-check/` (the script writes logs to the worktree's parent, which the write-jail forbids). Log: `runs/new-p21-78.r1/logs/feature-check-1.log`. |
| 4 | `python3 -I $TMPDIR/fc-p21-78/feature_check_logdir.py <worktree>` (a copy of the gate, identical except: `ROOT` points at the real qwen dir, and per-gate logs go to `LOG/feature-check-1/`) | **EXIT=0**, `RESULT: PASS` (table below). Log: `runs/new-p21-78.r1/logs/feature-check-1.summary.log` |

Gate table (command 4, unchanged tree, 0 changed files, base `7641e9493`):

| Gate | Result |
| --- | --- |
| fork-sources | PASS (exit 0) |
| all-sources-reproduce | PASS (exit 0) |
| no-upstream-regression | PASS (exit 0) |
| check-namespace | PASS (exit 0) |
| no-tautology | PASS (exit 0) |
| doc-figures | PASS (exit 0) |
| complexity | PASS (no new item in your files) |
| file-length | PASS (no new item in your files) |

Command 3 failed only because of the log location, not because of a gate result. Command 4 changes where logs are written
and nothing else: the gate logic, the checks and the baseline cache (`gate-baselines/7641e9493d*.json`, already present)
are the same.

No build or ctest was run. No product file changed, so there was nothing new to compile or test.

## Why no RED test

Step 3 of the procedure asks for a RED test before a fix. A RED test needs an input that reaches the null branch. Through
`ControlRegistry::invoke`, every such input returns `Busy` at `ControlRegistry.cpp:283` before the handler runs. The
handler itself is in an anonymous namespace and cannot be called directly from a test. A test calling `invoke` with no
song would therefore pass both with and without the guard. That would prove nothing, so I did not write one.

## Self-check

Command: `python3 .../qwen/submit_check.py <worktree> new-p21-78` → EXIT=1 (log `runs/new-p21-78.r1/logs/submit-check-1.log`).
Its items, answered:

- **Commits on the branch / working tree clean.** These two FAILs are procedural. They clear when this report is committed
  (the commit is the next step) and the worktree is clean afterwards. Re-run output is recorded below.
- **Report present / tests added or changed.** PASS. The report is present. No test was added because there is no
  product change (see "Why no RED test").
- **Gates (feature_check.py).** The official script exits 1 in this sandbox with `PermissionError` on
  `<worktree>-feature-check/` (the script writes logs to the worktree's parent, outside the write-jail). Its checks did
  not run. My copy with only the log folder moved (`$TMPDIR/fc-p21-78/feature_check_logdir.py`) exits 0 (`RESULT: PASS`,
  8/8 rows, table above). That is a real pass of the same checks, but it is not the official invocation. The official
  gate needs a run where the parent folder is writable.
- **Regression test RED/GREEN.** Not applicable: no product code changed, so there is no fix to revert or reapply
  (step 4b). The claim rests on the code evidence above, which is the FALSE-POSITIVE requirement.
- **Deterministic test.** Not applicable (no test).
- **Fix at the cause and complete.** Not applicable (no fix). I checked the siblings anyway: other engine-gated commands
  call `Engine::getSong()` directly (for example `ControlCommandsW3Engine.cpp:221` and `:259`). They rely on the same
  dispatch gate at `ControlRegistry.cpp:283`, so they are not changed. Changing this one line alone would be inconsistent.
- **No collateral damage.** No product file changed. Saved projects and the control surface are untouched.
- **Verdict evidence.** file:line citations are in "Evidence" items 1-7 above.
