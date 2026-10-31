IMPLEMENTED

# Q050-334.1 - Ordered, bounded local project-change events

Logs: `~/.local/share/zene-bunny-fix/qwen/runs/Q050-334.1.r1/logs`. Design, wire contract and limits:
`docs/050-features/Q050-334.1.md`.

## Acceptance

| Criterion | Command (from `build/tests`) | Log | Exit |
|---|---|---|---|
| Subscribed clients receive successful project changes in order; unsubscribe stops further events (real socket) | `ctest -R '^ControlProjectEvents$' --output-on-failure` (`check_order_and_unsubscribe`, `check_project_replaced`) | `test-rest.log` | 0 |
| Delivery is bounded; overflow and disconnect are detectable; clients reread and resume without claiming missed events | same test (`check_overflow_and_recovery`, `check_disconnect_and_recovery`) and `ctest -R '^ControlEventHubTest$'` (queue count and byte bounds, overflow notice, seq counting with no subscribers) | `test-rest.log` | 0 |

## Self-check

- **Each new test fails with the product change reverted, passes restored.** `git diff HEAD~1 HEAD -- src include` reversed,
  rebuilt: `zene` built (exit 0), `ControlProjectEvents` exit 8 (`no command 'events.subscribe'`, `test-rev-sock.log`),
  `ControlEventHubTest` does not build (exit 2, `build-rev-2.log`: the header is gone, so the test cannot pass without the
  change). Restored and rebuilt: both pass, exit 0 (`test-rest.log`).
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
- Gates: all PASS (`gates-3.log`). `feature_check.py` could not write its log directory beside the worktree (the run's
  write jail), so a copy with only the log directory changed ran it.

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
