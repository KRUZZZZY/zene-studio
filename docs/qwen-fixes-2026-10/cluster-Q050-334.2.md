IMPLEMENTED

# Q050-334.2 - Versioned public TypeScript session SDK (stage 2 of 2 of Q050-334)

Branch `feat/050-Q050-334.2-oc` from `050/m0-train-26` (`af010bc3cc`). It now also contains merge `00ccb2579a` of `050/m0-train-31` (stage 1, with its C++) and the round-3 fix commits (`6662d9e339`, `5a9e50574f`, `27eaa073f5`, `696b59269a`, `0abc754cb0`). The package is in `tools/zene-session-sdk/`
(`@zene-studio/session-sdk` 0.1.0), and it uses stage 1's event protocol unchanged. This unit changes no C++, adds or
changes no command, and changes no project format. The feature record is `docs/050-features/Q050-334.2.md`. Logs are
in `~/.local/share/zene-bunny-fix/qwen/runs/Q050-334.2.r1/logs/` (round 1) and `.../Q050-334.2.r2/logs/` (round 2).

## Round 2 (review REWORK): changes during a read were lost

The reviewer showed that a change arriving after a subscription started, but before any listener attached, reached
nobody, while the subscription's cursor counted it. That window is `resync()`'s read, `LiveSession`'s sync, and the
read that carries the subscribe reply. So `LiveSession` went on from a later seq without reporting the gap.

**Fix (`1de9c4a0a2`):**
- A new `HeldListeners` (`src/listeners.ts`) holds pushed changes until the first listener attaches, then delivers
  them in order, a microtask later, before anything newer.
- `Subscription.onEvent` uses it, and `onEnd` on an ended subscription fires only after the held changes.
  `LiveSession.onChange` uses it too, since `start()` resolves before a caller can listen.
- Both hold at most 8192 changes. Past that, a subscription nobody listens to ends `overflow`.

**Regression (in `ControlSessionSdk`):**
- `checkResyncKeepsEventsDuringRead`: an edit is made inside `resync()`'s read. The test waits until
  `events.get_state` shows it written to the reader, then pings on the reading connection, so the SDK has received it
  before the read returns. The first `onEvent` attached after `resync()` must then get it at `cursor.seq + 1`, and the
  next change at `+ 2`.
- The same check runs inside `LiveSession`'s first read: `onChange`, attached after `start()`, must get it, and
  `cursor` counts it.

**Round 2 evidence (logs in `Q050-334.2.r2/logs/`):**

| Run | Command | Exit |
|---|---|---|
| Fixed sources + new checks | `ctest -R "SessionSdk" -V` (`ctest-3-restored.log`): both pass; 49 `PASS:` lines (47 SDK checks + 2 launcher lines) | 0 |
| Round-1 `src/` + new checks (the fix reverted) | `ctest -R "^ControlSessionSdk$"` (`ctest-2-reverted.log`): "timed out waiting for the held event", which reproduces the review's lost event | 8 |
| Only `live-session.ts` reverted | `ctest-4-livesession-reverted.log`: "timed out waiting for the change made during the first read" | 8 |
| Recovery mutation (no `recover()`), still caught | `ctest-5-mutated-recovery.log` | 8 |
| 5 repeats | `ctest -R "^ControlSessionSdk$" --repeat until-fail:5` (`ctest-6-repeat.log`): 5 of 5 passed | 0 |
| Gates (copy of `feature_check.py`, log path only) | `feature-check-2.log`: all 8 rows PASS | 0 |

The round-1 tables below still stand. The criterion-2 test now has 56 checks.

## Round 3: startup and resync cleanup, pipe smoke, verification

Merge `00ccb2579a` (2 parents; brings train-31 into this branch). Fix commit `6662d9e339` closes `LiveSession`'s
connection when the initial read fails, and `5a9e50574f` adds the named-pipe smoke. Logs are in
`~/.local/share/zene-bunny-fix/qwen/runs/Q050-334.2.r3/logs/`.

| Command | Log file | Exit |
|---|---|---|
| `cmake --build build --target zene -- -j4` (CMake re-ran: "Generating done" count 1; the WIN32 block parses) | `017-build-reconfigure.log` | 0 |
| `ctest -N -R SessionSdk` (lists `ZeneSessionSdkTypecheck` and `ControlSessionSdk` only) | `018-ctest-list-sdk-final.log` | 0 |
| `ctest -R "^ZeneSessionSdkTypecheck$" --output-on-failure` | `019-ctest-typecheck-final.log` | 0 |
| `ctest -R "^ControlSessionSdk$" --output-on-failure` (`LastTest.log`: 53 `PASS:` lines) | `020-ctest-sdk-final.log` | 0 |
| `ctest -R "^ControlSessionSdk$" --repeat until-fail:5` (5 of 5 "Passed") | `021-ctest-sdk-repeat5.log` | 0 |
| Mutation A: `resync()` catch without `subscription.unsubscribe()` (`client.ts`), restored | `022-ctest-sdk-mutation-resync.log` | 8 |
| Mutation B, first attempt: a sed line-offset error removed the `start` signature and connect line, not the cleanup. Discarded; the tree was restored | `023-ctest-sdk-mutation-startup.log` | 8 (invalid) |
| Mutation B, valid: `LiveSession.start()` try/catch removed (`live-session.ts`), restored | `024-ctest-sdk-mutation-startup.log` | 8 |
| `feature_check.py` before the fix (7 of 8 rows PASS; `complexity` FAIL: new `run` in `tests/session-sdk-pipe-smoke.py`, CCN 16) | `025-feature-check-final.log` | 1 |
| `python3 tests/session-sdk-pipe-smoke.py build/zene <node> tools/zene-session-sdk` after the refactor (SUMMARY `ControlSessionSdkPipeSmoke: PASS`, same PASS lines) | `026-sdk-pipe-smoke-refactored.log` | 0 |
| `feature_check.py` after the refactor (all 8 rows PASS, `complexity` included) | `027-feature-check-refactor.log` | 0 |
| `bash tests/fork-sources-gate.sh` after the refactor (`PASS: every tracked source in scope is registered`) | `028-gate-fork-sources-refactor.log` | 0 |
| `ctest -R "^ControlSessionSdk$" --output-on-failure` re-run for the count (`build/tests/Testing/Temporary/LastTest.log`: 53 `PASS:` lines) | `029-ctest-sdk-recheck-029.log` | 0 |
| `ctest -R "^ControlSessionSdk$" --output-on-failure` after `checkQueueLimitBoundary` was added (`build/tests/Testing/Temporary/LastTest.log`: 56 `PASS:` lines, the three new ones included) | `034-ctest-sdk-boundary.log` | 0 |

The complexity failure was fixed by splitting `run()` into `check_ready`, `run_sdk` and `check_quit` (all CCN 10 or
below). The baseline was not edited and `--reanchor` was not used.

The mutation failures are "timed out waiting for the failed resync to unsubscribe" (A) and the child-exit check (B).
The pipe smoke is CI-only on Windows; this Linux host runs only the POSIX socket path.

## Acceptance

Round 1 evidence: the logs in this table are in `~/.local/share/zene-bunny-fix/qwen/runs/Q050-334.2.r1/logs/`. All runs
were from `build/tests` with `QT_QPA_PLATFORM=offscreen`, no `LMMS_PLUGIN_DIR`, and Node v22.23.1. The round-3 count is
re-measured in `Q050-334.2.r3/logs/029-ctest-sdk-recheck-029.log`.

| Criterion | Test | Command | Exit |
|---|---|---|---|
| A versioned package exports public types, includes a usage example that typechecks, and documents connection, read/edit calls, subscriptions, errors, and resync | `ZeneSessionSdkTypecheck`: strict `tsc` over `src/`, `examples/live-view.ts` (imported by package name) and `test/`, then the `dist` build emitting `index.js` + `index.d.ts`. `ControlSessionSdk`: `SDK_VERSION` equals the `package.json` semver, and the example runs against a real instance. The docs are `tools/zene-session-sdk/README.md` (sections Connect, Read and edit, Subscribe to changes, Errors, Resync) and `CHANGELOG.md` | `ctest -R "SessionSdk" --output-on-failure` (`r1/logs/ctest-9-restored-final.log`) | 0 |
| An integration test uses the SDK against a real Zene instance to connect, read state, edit, receive the corresponding event, unsubscribe, and exercise recovery after disconnect or overflow | `ControlSessionSdk`: `tests/control-session-sdk.py` starts a headless `zene` and runs `tools/zene-session-sdk/test/integration.ts`, 56 PASS lines (round 3, measured in `r3/logs/034-ctest-sdk-boundary.log`; 53 in `029`, before the boundary check) through the SDK's own exports, covering connect, read, the SDK's `track.add` arriving as its event, unsubscribe stopping events, overflow (held 4 of 4, notice detected, auto-resync, missed count, state complete) and disconnect (stale, retries, `missed === 2`, nothing replayed, next change arrives) | same run; also `ctest -R "^ControlSessionSdk$" --repeat until-fail:5` (`r1/logs/ctest-2-repeat.log`): 5 of 5 passed | 0 |

## Revert proof

Round 1 evidence, in `Q050-334.2.r1/logs/`. The round-3 restored runs are in the table under "Round 3" above.

| Change | Command | Exit |
|---|---|---|
| Product reverted: `tools/zene-session-sdk/src/` moved away (example and test unchanged) | `ctest -R "SessionSdk" --output-on-failure` (`ctest-7-reverted-final.log`): typecheck fails with TS2307 "Cannot find module '@zene-studio/session-sdk'", and ControlSessionSdk fails with ERR_MODULE_NOT_FOUND | 8 (both failed) |
| Behavioural mutation: `LiveSession` does not call `recover()` | `ctest -R "^ControlSessionSdk$"` (`ctest-8-mutated-final.log`): "timed out waiting for the resync after the overflow" | 8 |
| Restored | `ctest -R "SessionSdk" --output-on-failure` (`ctest-9-restored-final.log`); round 3 restored runs: `020-ctest-sdk-final.log` (exit 0) and `021-ctest-sdk-repeat5.log` (exit 0) in `r3/logs/` | 0 |

## Gates

Round 3 gates, run unmodified from the shared qwen folder against this worktree:

- `feature_check.py $W` before the fix: exit 1 (`025-feature-check-final.log`), 7 of 8 rows PASS. `complexity` FAILed on
  the new `run` in `tests/session-sdk-pipe-smoke.py` (CCN 16, over the target of 10).
- The fix (`27eaa073f5`) split `run()` into `check_ready`, `run_sdk` and `check_quit`, all at CCN 10 or below. The
  baseline was not edited and `--reanchor` was not used.
- `feature_check.py $W` after the fix: exit 0 (`027-feature-check-refactor.log`), all 8 rows PASS: fork-sources,
  all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures, complexity, file-length.
- `bash tests/fork-sources-gate.sh`: exit 0 (`028-gate-fork-sources-refactor.log`).

Round 1 and round 2 gate logs are not in the round-3 folder; see the round folders named above.

## Self-check

- **Each new test fails with the product change reverted, then passes restored.** Yes. With `src/` moved away,
  `ZeneSessionSdkTypecheck` and `ControlSessionSdk` both failed (ctest exit 8). A behavioural mutation, removing
  `LiveSession`'s recovery, also failed `ControlSessionSdk` (exit 8). Restored, both passed (exit 0). See the
  revert table above. This unit has no C++ change, so no rebuild applies; `zene` was rebuilt once after the
  CMakeLists change (`build-2.log`, EXIT=0).
- **Every acceptance criterion is proven by a test I ran.** Yes. Criterion 1 is `ZeneSessionSdkTypecheck` plus
  `ControlSessionSdk`, and criterion 2 is `ControlSessionSdk`; both exit 0, and `ControlSessionSdk` passed 5 of 5
  repeats.
- **One undo step per edit; honest reversibility classes.** No command is new or changed. The SDK's edits are the
  existing registered commands (`track.add`, `track.rename`, `control.undo`/`redo`), each with its unchanged row.
  Subscribing uses stage 1's `events.*`, which are `not_mutating`. The snapshot is not affected, so it was not
  regenerated.
- **Nothing on the audio thread.** This unit adds no C++ to Zene. The SDK is an external Node process that talks to
  the control socket, which is served on Zene's UI thread (stage 1's design). TSan does not run on this box; this
  is reasoned, not measured.
- **Old projects load and render as before; new data round-trips.** Nothing in Zene's load, save or render path
  changed, and the SDK stores nothing in a project (subscriptions are runtime state). No render comparison was
  run, because no rendering code changed.
- **Qt 5.15 / MSVC / macOS < 10.15 portability.** No C++ or Qt API was added. The CMake change only adds
  `find_program(node)` and two `add_test` entries behind it. `ControlSessionSdk` is registered with the UNIX
  control suite only, like `ControlProjectEvents`. The Windows named-pipe path of the SDK (`net.connect` with a
  `\\.\pipe\` name) was **not run here**: there is no Windows toolchain on this box.
- **GUI and agent through the same command (WARN rows).** No DAW-side user action is added: the brief puts a GUI
  panel out of scope, and the package is for external programs. Every edit it makes is an existing registered
  command, the same one the menus use, so there is no new GUI entry point to test. The manual describes the
  package (`#session-sdk`).
- **Not verified:** the typecheck on CI. It needs `npm ci` in the package directory (or `$ZENE_TSC`); without one it
  reports Skipped (77) and does not pass. I ran it locally with the pinned TypeScript 5.9.3 and `@types/node`
  22.20.5. `ControlSessionSdk` needs Node >= 22.6; it reports Skipped (77) on older Node and is not registered
  without node.
- **Round 3 regressions fail with the product hunk removed, and pass restored.** Removing `resync()`'s read-failure
  unsubscribe fails `ControlSessionSdk` (`022-ctest-sdk-mutation-resync.log`, exit 8). Removing `LiveSession.start()`'s
  failed-initial-read cleanup fails it on the child-exit check (`024-ctest-sdk-mutation-startup.log`, exit 8). The
  unmutated tree passes (`020-ctest-sdk-final.log`, exit 0). `023` is an invalid mutation attempt (a sed line-offset
  error) and is discarded; it is kept only as a record.
- **Pipe smoke, what ran here.** `ControlSessionSdkPipeSmoke` is CI-only on Windows. On this Linux host it runs only the
  POSIX socket path: `011-sdk-pipe-smoke-posix.log` (SUMMARY PASS) and the STEP-2 launcher rerun after the refactor,
  `026-sdk-pipe-smoke-refactored.log` (exit 0, SUMMARY PASS).
- **No new or changed command, no snapshot change, no A-boilerplate row changed.** The round-3 change is a Python
  refactor of a test launcher and two docs. No command was added or changed, so no snapshot was regenerated.
- **GUI surface: none for this unit.** There is no GUI for events (`docs/050-features/Q050-334.1.md:68-69`: no GUI
  panel or menu), and every SDK edit is an existing registered command. So there is no new GUI check to run here.

## Round 3 completion check

Run in this pass (logs in `Q050-334.2.r3/logs/`): `submit_check.py $W Q050-334.2` exit 0, `RESULT: PASS - submit`, no FAIL
row, three WARN rows explained below (`031-submit-check-final.log`); `feature_check.py $W` exit 0, 8 of 8 rows PASS
(`032-feature-check-submit.log`). The round-3 ctest and mutation runs (018 to 029) are listed under "Round 3" above. A later pass added the queue_limit
boundary check: `034` ctest exit 0 (56 PASS), `035` typecheck exit 0, `036` feature_check exit 0 (8 of 8 rows PASS).

- **Each new test fails with the product change reverted, then passes restored.** Round 1 (table above): with `src/`
  moved away, `ctest -R "SessionSdk" --output-on-failure` exit 8 (`ctest-7-reverted-final.log`); restored, exit 0
  (`ctest-9-restored-final.log`). Round 3, changed behaviour: removing `resync()`'s unsubscribe, exit 8
  (`022-ctest-sdk-mutation-resync.log`); removing `LiveSession.start()`'s cleanup, exit 8
  (`024-ctest-sdk-mutation-startup.log`); restored, `ctest -R "^ControlSessionSdk$"` exit 0 (`020`, 53 PASS). Not re-run
  in this pass.
- **Every acceptance criterion is proven by a test I ran.** Criterion 1 (typed package, example, docs): `ZeneSessionSdkTypecheck`
  exit 0 (`019`), and `ControlSessionSdk` exit 0 (`020`, `SDK_VERSION` and the example). Criterion 2 (connect, read,
  edit, event, unsubscribe, recovery after disconnect or overflow): `ControlSessionSdk` exit 0 (`020`, 53 PASS),
  `--repeat until-fail:5` exit 0 (`021`, 5 of 5), recheck exit 0 (`029`, 53 PASS), boundary run exit 0 (`034`, 56 PASS). The user-visible path is the agent
  control socket, driven through the SDK's exports in `tools/zene-session-sdk/test/integration.ts`.
- **Each edit is one undo step; honest reversibility classes.** No command is new or changed: the submit gate's
  agent-command row reports "no command file changed" (PASS, `031`). The SDK's edits go through the existing registered
  commands `track.add` and `track.rename`, and `control.undo` is the undo. The existing check is
  `tools/zene-session-sdk/test/integration.ts:129-143`, run in `020` (exit 0): one `control.undo` produced exactly one
  `undo` event (line 143), and the track count was `before + 1` afterwards, so one undo removed the last edit only. No
  new reversibility class exists. The `events.*` class is stage 1's (`not_mutating`, per the report); not re-checked here.
- **Nothing on the audio thread.** This unit's own commits change no C++: `git diff --name-only 00ccb2579a HEAD -- src/`
  lists 0 files, and so does `c20ef591b5..HEAD` (`c20ef591b5` is the train-31 tip). The 148 `src/` files that differ from
  `af010bc3cc` come from train-31 (stage 1, Q050-334.1), not from this unit. The submit gate's audio-path row (no
  allocation, lock, signal or I/O added inside audio-path functions) is PASS (`031`). TSan was not run on this box.
- **Old projects load and render as before; new data round-trips.** No `src/` file changed in this unit, so the load,
  save and render code is unchanged by it, and the SDK stores nothing in a project. The project format is unchanged. No
  load or render run was done in round 3, and no round-trip test was added, because no format changed.
- **Completeness: real user path and agent path, with edge cases.** The user-visible path is the agent control socket
  (no GUI for events). Edge cases and where each is covered:
  - default `queue_limit` 256: `tests/control-project-events.py:95` (stage 1, `ControlProjectEvents`; not re-run).
  - `queue_limit` 0 gives `invalid_args`: `control-project-events.py:98` (stage 1; not re-run).
  - documented max 4096: **covered** by `checkQueueLimitBoundary` in `tools/zene-session-sdk/test/integration.ts` (`034`,
    exit 0). `queue_limit` 4096 is accepted and reported back; 4097 is refused `invalid_args` on `events.subscribe`, from the
    argsSchema bound `integerProperty(1, 4096)` at `src/core/ControlCommandsEvents.cpp:92`, before the command body runs;
    the connection still answers afterwards. This is the SDK-layer proof; the earlier finding that no test asked for 4097
    predates it.
  - second subscribe gives `busy`: `control-project-events.py:97`; SDK `integration.ts:119-120` (`020`, exit 0).
  - unsubscribe after end: stage 1 `control-project-events.py:141-142` (`not_found`); the SDK returns `null` client-side
    (`integration.ts:162`, `020`).
  - proto other than 1 is refused: `tests/control-socket-integration.py:1910` (`proto=99` gives `refused`), server at
    `src/core/ControlServer.cpp:96`; stage 1, not re-run.
  - proto replies carry 1: `src/core/ControlCommandsControl.cpp:112`; SDK connect check `integration.ts:111` (`020`).
  - overflow by burst: `control-project-events.py:166-227` (stage 1) and `integration.ts:262-271` (`020`).
  - oversized `args_omitted`: `tests/src/core/ControlEventHubTest.cpp:169` (stage 1 C++ unit test; not re-run).
  - disconnect and reconnect with seq and session comparison: `control-project-events.py:95` (stage 1), and
    `integration.ts` (`missed === 2`, nothing replayed; `020`).
  - renamed or missing target: `integration.ts:127` (`not_found` for `track.rename` on a missing track, `020`) and
    `control-project-events.py:106`.
  - undo shifting rows: `integration.ts:143` (undo is its own event kind, `020`). Stale generation: the disconnect and
    reconnect path above. Reopen after save: **not exercised**; this unit does not touch save or load.
  - Zero-test check: `ControlSessionSdk` runs show 56 PASS lines in `LastTest.log` (`034`, the current run); `020` and `029` show 53, from before the boundary
    check; `018` lists exactly
    the two SDK tests. I did not re-verify that the round-3 ctest runs were from `build/tests`.
- **Portability: Qt 5.15, MSVC and macOS.** This unit adds no C++ and no Qt API, so no Qt-6-only, `windows.h` or
  `std::filesystem` item is present. POSIX is verified on this Linux host: `ControlSessionSdk` (`020`, `029`) and the
  POSIX pipe smoke (`026`, exit 0). The Windows named pipe is CI-only: `ControlSessionSdkPipeSmoke` is registered only
  under WIN32 (`tests/CMakeLists.txt:8829-8835`) and was not run here.
- **GUI test drives the visible control.** There is no GUI surface for this unit. Events have no GUI panel or menu
  (`docs/050-features/Q050-334.1.md:68-69`), and the SDK is an external program. The submit gate's three WARN rows (GUI
  path, GUI test, agent command) are explained by that: no GUI entry point or GUI test was added, and no command file
  changed. No GUI behaviour is claimed.
