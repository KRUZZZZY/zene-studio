FALSE-POSITIVE

# Cluster new-p21-76: applyAttachment and landAudio dereference Engine::getSong() without a null guard

Branch `bugs/qwen-new-p21-76` (base `cf3618e61`). No product code changed: the reported path cannot happen on this tree.

## The claim

`src/core/ControlCommandsVideo.cpp:101` (`applyAttachment`: `Engine::getSong()->setModified()`) and the `Engine::getSong()` dereferences in `landAudio` (`:159`, `:173`) and `stateJson` (`:84`, `:88`) have no null check. The finding says `video.attach`, `video.detach`, `video.set_offset` and `video.extract_audio` can run with no song.

## Why that path is unreachable

1. **Every `video.*` command requires the engine.** They are registered through `add(...)` at `src/core/ControlCommandsVideo.cpp:260-274`, which calls `defineCommandWithSchema`. Its `requiresEngine` parameter defaults to `true` (`include/ControlCommandDef.h:53`; implementation `src/core/ControlCommandDef.cpp:34-45`), and the command field defaults to `true` as well (`include/ControlRegistry.h:104`).
2. **`invoke()` refuses them before the handler runs.** `src/core/ControlRegistry.cpp:283`: `if (cmd->requiresEngine && !isEngineReady())` returns `busy`. `isEngineReady()` (`src/core/ControlRegistry.cpp:207`) is `s_ready && Engine::getSong() != nullptr && Engine::mixer() != nullptr`.
3. **The song pointer is set for the whole engine lifetime.** `Engine::init` creates it (`src/core/Engine.cpp:72`) and `Engine::destroy` is the only place that clears it (`src/core/Engine.cpp:96`, `:123` `deleteHelper(&s_song)`). `Engine::getSong()` returns the pointer (`include/Engine.h:71-73`).
4. **The handler runs later, but the song cannot disappear meanwhile.** `invoke()` runs the handler through `runOnUiThread` (`src/core/ControlRegistry.cpp:296`, `BlockingQueuedConnection` for non-UI callers). The only song teardown is `Engine::destroy`, called from `~MainWindow` (`src/gui/MainWindow.cpp:267-282`, call at `:282`) and from the exit path in `src/core/main.cpp:1858-1862` (`delete app` at `:1858`, then `Engine::destroy()` at `:1862`). Neither runs a command.
5. **No other route reaches a video handler without the gate.** The direct `->handler(...)` calls are `src/core/ControlCommandsRecordTools.cpp:66-69` (`handlerOf`, called only with `transport.punch_set` / `transport.punch_clear`, `:107-114`) and `src/core/ControlCommandsNoteAcross.cpp:120-130` (runs inside an outer command that already passed `invoke()`, so the song is set). `rg` for `video.` in these files found nothing.

## Convention

Handlers of `requiresEngine` commands dereference `Engine::getSong()` directly all over the codebase, e.g. `src/core/ControlCommandsAlternatives.cpp:154`, `src/core/ControlCommandsProject.cpp:444`, `src/core/ControlCommandsRenderStems.cpp:394`. The video handlers follow that convention. Guarding them alone would diverge from it.

## Residual notes (not changed)

- Handler-level null guards are absent. That is defence in depth against a path that the registry gate already closes.
- A test cannot make `Engine::getSong()` null without a seam: `Engine` has no setter, and `VideoTrackTest` drives the engine through `Engine::init(true)` / `Engine::destroy()` in `initTestCase` / `cleanupTestCase` (`tests/src/core/VideoTrackTest.cpp:237-247`). So there is no reproducible RED test, and I did not write one.
- Unverified, flagged for the owner: a background extraction (`ControlCommandsVideo.cpp`, `landExtracted`/`landAudio`) that completes and posts its landing after `Engine::destroy` would reach `landAudio` with a null song. I found no path where that delivery happens, and I did not check whether `~MainWindow` can run while the event loop is still live.

## Read-only evidence commands

Run from the worktree as `rg` / `sed` / `awk` searches; each returned the lines cited above. Their exit codes were not captured, so treat the cited lines as what the search printed, not as exit-coded results.

- `rg -n "getSong" src/core/ControlCommandsVideo.cpp`; `sed -n 1,70p` and `sed -n 132,282p` of the same file
- `rg -n "s_song|getSong" src/core/Engine.cpp include/Engine.h`
- `rg -n "setReady|ready|Requires" src/core/ControlRegistry.cpp`; `sed -n 200,300p src/core/ControlRegistry.cpp`
- `rg -n "requiresEngine" src/ include/`; `rg -A12 "void defineCommandWithSchema" src/core/*.cpp`
- `rg -n "Engine::destroy\(" src`; `sed -n 270,290p src/gui/MainWindow.cpp`; `sed -n 1850,1866p src/core/main.cpp`
- `rg -n "handlerOf\(" src/core/ControlCommandsRecordTools.cpp`; `sed -n 120,131p src/core/ControlCommandsNoteAcross.cpp`
- `rg -n "VideoReference" src/core/Song.cpp src/core/Engine.cpp`; `sed -n 237,250p tests/src/core/VideoTrackTest.cpp`

## Gates and tests

No product files changed, so no regression test was written, no RED/GREEN run was needed, and no `fix.patch` was produced (procedure step 2: not real, go to step 6). Gate results are in the `## Self-check` section below.

## Self-check

Logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p21-76.r1/logs/`.

- **Regression test RED/GREEN (ask 1):** not applicable. No product file changed and no regression test was written. The reported path is unreachable on this tree (see the evidence above), so a test that fails on this tree could only be written by faking a null song, which needs a seam `Engine` does not have. A test that passed either way would prove nothing.
- **Deterministic test (ask 2):** not applicable, since no test was added.
- **Fix at the cause, siblings checked (ask 3):** no fix. The cause, the registry gate, covers every `video.*` command, because all of them use the default `requiresEngine = true` (`ControlCommandsVideo.cpp:260-274`, `include/ControlCommandDef.h:53`). The sibling handlers that dereference `Engine::getSong()` directly (`ControlCommandsAlternatives.cpp:154`, `ControlCommandsProject.cpp:444`, `ControlCommandsRenderStems.cpp:394`) follow the same convention.
- **No collateral damage (ask 4):** nothing changed in product code, saved data or limits.
- **FALSE-POSITIVE evidence (ask 5):** `src/core/ControlRegistry.cpp:283` (gate), `:207` (`isEngineReady` requires the song), `src/core/Engine.cpp:72` and `:123` (song created at init, cleared only at destroy), `include/Engine.h:71-73` (`getSong`). Full chain in the Verdict section.
- **Gates:** `feature_check.py` could not run. It creates `<worktree>-feature-check/` next to the worktree, outside the write jail, and failed with `PermissionError` (`feature-check-1.log`, `EXIT=1`). I ran its eight gate commands directly from the worktree: `fork-sources` EXIT=0 (`gate-fork-sources-2.log`), `all-sources-reproduce` EXIT=0 (`-3`), `no-upstream-regression` EXIT=0 (`-4`), `check-namespace` EXIT=0 (`-5`), `no-tautology` EXIT=0 (`-6`), `doc-figures` EXIT=0 (`-7`). `complexity` EXIT=1 (`gate-complexity-8.log`, 7 train-wide REGRESSION lines) and `file-length` EXIT=1 (`gate-file-length-9.log`, 19 train-wide lines). Neither list names `ControlCommandsVideo.cpp` or this report. The only changed file is this report, so the script's own changed-file rule would pass both rows, but I did not run the script itself.
- **Completion check:** `submit_check.py` EXIT=1 before the commit (`submit-check-10.log`): commit missing, working tree dirty, and the gates row failing for the reason above.
