IMPLEMENTED

# Q050-334.1 - Ordered, bounded local project-change events

Logs: `~/.local/share/zene-bunny-fix/qwen/runs/Q050-334.1.r1/logs`, and `.r2/logs` for round 2. Design, wire contract and
limits: `docs/050-features/Q050-334.1.md`.

## Round 3

Logs: `~/.local/share/zene-bunny-fix/qwen/runs/Q050-334.1.r3/logs`.

- **The blocking review point is fixed.** Skipped event lines now share one monotonic reply deadline
  (`tools/mcp-zene-control/zene_control/protocol.py`, `_read_reply`). This is the round 2 QA fix `a0d29643ed`, re-applied.
  `test_event_lines_do_not_reset_the_reply_deadline` uses a fake clock. It exits 1 with the old `protocol.py`
  (`py-rev.log`) and passes with the fix. `test_wire_client` runs 16 tests, exit 0 (`py-1.log`).
- **Why the QA fix was dropped: not the fix.** `ControlSocketIntegration` failed with
  `plugin.list is not broken down by format: {'ladspa': 10}`. The test needs native plug-in modules, and this tree had none
  built. I reproduced the same failure here with only `zene` built (exit 8, `test-1.log`). After building `tripleoscillator`,
  `amplifier`, `ladspaeffect`, `lv2effect` and `lv2instrument`, it exits 0 with no source change (`test-4.log`). The test
  has no build dependency on those modules, so a tree with only `zene` built will fail it the same way. This unit leaves
  that as it is.
- **Focused tests:** all 12 from the review pass, exit 0 (`test-5.log`).
- **Limits of this sandbox.** `ccache-bunny` points at a temp directory this run cannot write, so builds used
  `CCACHE_TEMPDIR=/tmp/q334-ccache-tmp`. `feature_check.py` cannot write its logs to the sibling `-feature-check` directory
  here, and `submit_check.py` reports its gates row as FAIL for that reason (`gates-1.log` has the `PermissionError`). I ran
  an unedited-logic copy with only the log path changed, in `/tmp`: every gate PASS, exit 0 (`gates-2.log`).

## Round 2

The pre-review check failed on complexity: two new test functions in `tests/control-project-events.py` were over target
(`check_overflow_and_recovery` CCN 17, `check_order_and_unsubscribe` CCN 11). They are split into smaller functions with
every assertion kept; no product code changed. `ControlProjectEvents` and `ControlEventHubTest` exit 0 on the split test
(`r2/test-1.log`), `ControlProjectEvents` exit 0 again after the last edit (`r2/test-2.log`), and `feature_check.py` is all
PASS (`r2/gates-2.log`).

## Acceptance

| Criterion | Command (from `build/tests`) | Log | Exit |
|---|---|---|---|
| Subscribed clients receive successful project changes in order; unsubscribe stops further events (real socket) | `ctest -R '^ControlProjectEvents$' --output-on-failure` (`check_order_and_unsubscribe`, `check_project_replaced`) | `test-rest.log` | 0 |
| Delivery is bounded; overflow and disconnect are detectable; clients reread and resume without claiming missed events | same test (`check_overflow_and_recovery`, `check_disconnect_and_recovery`) and `ctest -R '^ControlEventHubTest$'` (queue count and byte bounds, overflow notice, seq counting with no subscribers) | `test-rest.log` | 0 |

## Self-check

- **Round 3 reversal:** the new client test exits 1 on the old `protocol.py` and 0 on the fix (see Round 3). Only
  Python changed this round, so the C++ reversal below was not repeated.

- **Each new test fails with the product change reverted, passes restored.** `git diff HEAD~1 HEAD -- src include` reversed,
  rebuilt: `zene` built (exit 0), `ControlProjectEvents` exit 8 (`no command 'events.subscribe'`, `test-rev-sock.log`),
  `ControlEventHubTest` does not build (exit 2, `build-rev-2.log`: the header is gone, so the test cannot pass without the
  change). Restored and rebuilt: both pass, exit 0 (`test-rest.log`). That reversal ran in round 1, on the test before its
  split. Round 2 did not repeat the reverse-and-rebuild: the split test ran against `zene-cachyos/build/zene`, a build whose
  HEAD (`14a7b62cd9`) does not contain the feature commit, and exited 1 with `no command 'events.subscribe'`
  (`r2/test-rev-1.log`); on this branch's binary it exits 0.
- **Every criterion proven by a test I ran:** the table above.
- **One undo step per edit, honest class:** no edit command changed. `events.subscribe`, `events.unsubscribe` and
  `events.get_state` are `not_mutating`; `ControlEventHubTest::subscribingIsNotAnEdit` shows no transaction, journal
  step or seq moves. `ReversibilityContractTest` exit 0 after the A16 histogram update (`test-reg-2.log`).
- **Audio thread:** nothing added runs there. The diff touches `ProjectJournal` (its header: GUI/agent-side only) and
  `Song::loadProject` / `createNewProject` (UI thread, after the load). `ControlEventHub::journalChanged` posts to the UI
  thread if it is ever called elsewhere. Race reasoning, since TSan does not run here: the hub is UI-thread only; the one
  shared buffer, the Windows outbox, is mutex-guarded; key allocation is atomic.
- **Old projects:** no save or load format change; subscriptions and seq are runtime state. Two notification calls were
  added to `Song.cpp` and nothing else in it changed. No render comparison was run, since no audio path changed.
- **Portability:** Qt 5.15-safe API only (`QUuid::WithoutBraces` is 5.11; no `QJsonValue::toInteger`), no reserved
  identifiers, no `std::filesystem`, POSIX calls stay in the existing guards. **Not compiled on a real Windows, macOS or
  Qt 5 toolchain here:** `ControlServerWin32.cpp` and `ControlServerEvents.cpp` were type-checked with the real Qt 6
  headers, `Q_OS_WIN` forced and a stub `windows.h` (exit 0, `win-typecheck-4.log`). The pipe's runtime behaviour is covered only by
  `ControlNamedPipeSmoke` check 9 in hosted CI; it has **not run on a real pipe**.

## Other commands run

- `ReversibilityContractTest`, `ControlRegistryNestingTest`, `ControlShutdownHookTest`, `UndoBoundsTest`,
  `ControlCommandsMacrosTest`, `AgentSurfaceReportTest`, `ControlCommandsSnapshot`, `agent_surface`, `ControlShutdown`:
  exit 0 (`test-reg-2.log`).
- `ControlSocketIntegration`: exit 0 with `LMMS_PLUGIN_DIR` set for that run alone to `zene-cachyos/build/plugins`
  (`test-integ-3.log`). Without it, it fails with `plugin.list is not broken down by format`, because this tree builds no plug-in modules.
- `tools/mcp-zene-control/tests/test_wire_client.py`: exit 0, including the new event-line case (`test-wire-1.log`).
- Gates: all PASS (`r2/gates-2.log`). `feature_check.py` could not write its log directory beside the worktree (the run's
  write jail; `PermissionError`), so a copy in `/tmp` with only its log directory and script root changed ran it.

## Failures seen, not caused by this change

- `ControlMcpGroupCoverage` and the bridge's `test_mcp_e2e` / `test_mcp_errors` / `test_declared_surface`: the bridge server
  dies with `TypeError: Server.__init__() got an unexpected keyword argument 'on_list_tools'`, in `zene_control/server.py`,
  which this branch does not touch: the installed MCP Python SDK does not match it. The bridge's new skip is therefore
  proven by `test_wire_client.py` only, not end to end.
- `tests/control-named-pipe-smoke.py` run by hand over POSIX: checks 1-7 and the new check 9 pass (`test-smoke-1.log`), then
  exit 1 on the start-up refusal check, which expects the Windows typed line; POSIX prints `the control socket path must be
  absolute`. That path is untouched here; the test is registered on Windows only.

## Decisions to review

- `tests/agent-surface-gate.py`'s client now skips event lines: its sweep calls `events.subscribe`, so its connection
  legitimately receives them. No check was loosened; every reply is still matched as before.
- No GUI entry point: the brief puts a GUI panel or menu out of scope, and a subscription has no meaning without a
  connection to deliver to, so the commands refuse typed from Lua and menus. Interface EDITS do produce events, proven by
  `ControlEventHubTest::aJournalStepOutsideACommandIsAnEvent` at the journal seam; no test drives a real widget.
