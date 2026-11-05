IMPLEMENTED

# Q050-501 - tell users when a newer Zene version is available

Base `050/m0-train-24` (`2a7fd9f9c7`). Full acceptance table and limits: `docs/050-features/Q050-501.md`. Logs in the run's `logs/`.

| # | Criterion | Command (from `build/tests`) | Exit |
|---|---|---|---|
| 1 | Consent, Settings, socket, saved | `ctest -R ^UpdateCheckWindowsTest$`, `^UpdateCheckTest$`, `^ControlUpdateCheck$` | 0, 0, 0 |
| 2 | Daily and on demand, SemVer, pre-release opt-in, non-modal notice, page button | `ctest -R ^UpdateCheckTest$`, `^UpdateCheckWindowsTest$` | 0, 0 |
| 3 | Failures never block, one quiet line, zero requests while off | `ctest -R ^UpdateCheckTest$`, `^UpdateCheckWindowsTest$` | 0, 0 |
| 4 | Hidden when updated by something else; only the User-Agent identifies | `ctest -R ^UpdateCheckHiddenTest$`, `^ControlUpdateCheck$`, `^UpdateCheckTest$`; OFF build syntax-only for 5 files | 0, 0, 0, 0 each |
| 5 | Loopback fixture only | the same tests; fixtures bind 127.0.0.1 | 0 |
| 6 | Manual and limitations | `ctest -R ^ControlUpdateCheck$` (`check_docs`) | 0 |

Neighbours run: `ReversibilityContractTest`, `ControlRegistryTest`, `CustomAccessTest`, `WorkspaceAccessTest`, `AccessibilityWalkTest`, `ControlCommandsSnapshot`: all 0. The whole suite was not run (per the brief).

Not done, and said plainly: `-DZENE_UPDATE_CHECK=OFF` was syntax-compiled, not linked or run (the guard refuses a second configure). The real GitHub feed was never contacted. Under `offscreen` the 4 s start-up timer's `isUnattendedRun` branch is skipped, so the tests call `runUpdateCheckStartup` directly; `check_agent_start` holds that an agent instance starts no check of its own, but the person-only timer line in `WorkspaceMenus.cpp` has not fired in a test.

## Integration onto `050/m0-train-25` (round 2, 2026-11-05)

Accepted patch `feat/050-Q050-501-oc` @ `2b8ab575d5` applied three-way onto `050/m0-train-25` @ `081630835b`. No design change; the accepted code, tests and docs are kept as accepted. Conflicts and how each was resolved:

| File | Conflict | Resolution |
|---|---|---|
| `src/gui/modals/SetupDialog.cpp` | train's Q050-212 `settings.set` for `app/autotrackchannel` vs this patch's `app.update_check_set` block, same spot in `accept()` | both kept, Q050-212 first; they touch different settings and run one after the other on the GUI thread. `TrackAddRoutingTest` (Q050-212's side) and `UpdateCheckWindowsTest` (this side) both 0 |
| `tests/CMakeLists.txt` | train's `CarlaVst2HostingTest` block vs this patch's `UpdateCheck*` test properties | both kept; the train's `if(CMAKE_VERSION ...)` gets its own `endif()` |
| `tests/upstream-modifications.txt` | appended rows on both sides | both kept |
| `docs/KNOWN-LIMITATIONS.md` | train's Q050-214 section vs this patch's update-check section | both kept, as separate sections |
| `docs/RELEASE-NOTES-v0.3.0-alpha.md` | A16 histogram `measured`/`configuration` lines | `configuration` line from this patch (adds `app.update_check_get`); `measured` re-taken: train's 926/457/42/25/402 + this patch's 5 rows (1 true_inverse, 4 not_mutating) = `rows=931 true_inverse=458 snapshot=42 irreversible=25 not_mutating=406`. `ReversibilityContractTest` measured this build at 922 = the train's 917 + 5 and passes against the re-taken line |
| `tools/mcp-zene-control/zene_control/commands_snapshot.json`, `README.md` canary | id counts | snapshot regenerated live with `snapshot_live.py` (922 ids / 78 groups, `wasm.*` out, `app.update_check_get/_run/_set`, `window.update_check`, `window.update_download_page` present); README canary 922 / 78; `docs/050-features/Q050-501.md` figures updated |

Semantic difference from the accepted patch: none intended. The only changed lines are the conflict resolutions above and the re-measured figures.

Evidence (logs in `~/.local/share/zene-bunny-fix/qwen/runs/Q050-501.r2/logs/`, ctest from `build/tests`):

- Build `zene` + the new tests: `build-2.log` EXIT=0. (`build-1.log` EXIT=2 was the environment, not the code: ccache's configured `temporary_dir` is outside this lane's write limits. `CCACHE_TEMPDIR=/tmp/...` fixed it.)
- Green: `UpdateCheckTest` 0, `UpdateCheckWindowsTest` 0, `UpdateCheckHiddenTest` 0, `ControlUpdateCheck` 0, `ControlRegistryTest` 0, `ReversibilityContractTest` 0 after re-taking the histogram (8 before, the expected stale figure), `ControlCommandsSnapshot` 0, `DocFiguresGate` 0, `CustomAccessTest` 0, `WorkspaceAccessTest` 0, `TelemetryConsentDialogTest` 0, `AccessibilityWalkTest` 0, `TrackAddRoutingTest` 0.
- Red on the integrated tree, the same reversal as round 1 (registration lines, Help menu entry and start-up timer, the SetupDialog block, `UpdateChecker::check` returning Off, `managedBy` returning empty): `build-4-red.log` EXIT=0; `UpdateCheckTest` 8 (31 failed slots), `UpdateCheckWindowsTest` 8, `UpdateCheckHiddenTest` 8, `ControlUpdateCheck` 8 (`red-*.log`). Restored from byte copies, rebuilt (`build-5-green.log` 0): all four 0 again, plus `ControlCommandsSnapshot` 0 (`green-*.log`).
- Gates: `feature_check.py` cannot create its log dir beside the worktree (PermissionError, `gates-1.log`). Run unchanged through `/tmp/q501-wrap/fc_wrap.py`, which only redirects the log dir: all 8 rows PASS, RESULT PASS (`gates-2.log`).

Hazards rechecked after the merge: no project-format or audio-thread code is touched by the merge; portability is unchanged from the accepted patch (no new API in the resolutions). Still not done (as in round 1): an OFF build was not linked here; Qt 5.15, MSVC and macOS were not built; the real GitHub feed was never contacted; TSan does not run on this box (single-threaded design, Qt async network on the GUI thread).

## Self-check

- **Each new test fails with the product change reverted, passes restored.** Reversed the non-test hunks of `ControlRegistryRegistrations.cpp`, `WorkspaceMenus.cpp`, `SetupDialog.cpp`, and made `UpdateChecker::check` return Off and `managedBy` return empty. The build switch and the new files stayed, because a real reversal removes the targets and rebuilds the whole tree. Red: `UpdateCheckTest` 8 (31 failed), `UpdateCheckWindowsTest` 8, `UpdateCheckHiddenTest` 8, `ControlUpdateCheck` 8. Restored and rebuilt: all 0. `check_docs` separately: 8 with a manual sentence changed, 0 restored. Within `UpdateCheckWindowsTest` the red stops at `initTestCase` (no menu entry), so its individual slots were not each shown red.
- **Every criterion proven by a test run here:** the table above; logs `green-*.log`.
- **One undo step, honest classes:** `app.update_check_set` true_inverse (`settingsSaveThroughTheCommandOneUndoStep`: depth +1, `control.undo` restores all flags). This test found the Settings path recording nothing because `SetupDialog` holds the journal off; fixed. Get, run, `window.update_check`, `window.update_download_page`: not_mutating (run stamps the time in config). `ReversibilityContractTest` 0; histogram re-published with an `option` line for the build switch. Consecutive sets within the coalescing window group by the registry's design.
- **Audio thread:** nothing added runs there; all main-thread timers, menu actions and command handlers, with Qt's asynchronous network. TSan does not run here; the design has one thread.
- **Projects:** no project code or format touched; all data is in the user's configuration, so nothing to round-trip beyond the config reload tested. No render test run because none applies.
- **Portability:** no Qt-6-only API (`NoLessSafeRedirectPolicy`, `setMaximumRedirectsAllowed`, `QSysInfo` are in 5.15); no reserved identifiers; no `std::filesystem`; no POSIX calls. Compiled only on this box against Qt 6; Qt 5.15, MSVC and macOS were not built here.
- **Gates:** `feature_check.py` itself cannot run in this lane: it creates a directory beside the worktree, outside the write limits (`PermissionError`). Its own gate list and comparison were run through a wrapper importing it, logs in `logs/gates-*`; results in the final section below.

## Gate results (`logs/gates-2.log`)

fork-sources 0, all-sources-reproduce 0, no-upstream-regression 0, check-namespace 0, no-tautology 0, doc-figures 0; complexity and file-length: no new item in this branch's files against the cached base listing. Overall PASS. `submit_check.py`'s own "gates" row runs `feature_check.py`, which cannot create its log directory from this lane; it needs the loop's own run.
