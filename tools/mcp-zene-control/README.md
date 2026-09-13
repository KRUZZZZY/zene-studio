# MCP-ZENE-CONTROL — `zene-control` stdio MCP bridge (AGENT-TOOLING §6 slice S4)

> **Snapshot note — added 2026-09-13, and it is why this README under-states the DAW today.**
> `zene_control/commands_snapshot.json` is the committed copy of **one instance's** `control.commands_list`,
> derived (never hand-written — see `snapshot_commands.py`) so that `zene_commands`, and therefore
> `tools/list`, still answer with no instance running. The copy in this tree is **stale**. From the file's own
> `instance` block: binary `sha256 0a13ee76…`, lane head `059bf6bad`, version `0.1.0-alpha.15+a244564`, proto 1,
> **70 commands**, captured **2026-09-12T01:43:20Z**. The tree's command registry declares **74**, so four
> commands are missing from the snapshot — **`midi.learn_toggle`**, **`project.restore_revision`**,
> **`telemetry.consent`** and **`telemetry.status`**. Consequence, and the size of it: with no live instance and
> no cache, the bridge offers **70 generated tools + the 2 bridge tools**; against a live instance it generates
> one per registered command, so nothing is broken, only discovered later than it could be.
> The "23 generated tools" / "25 = 23 generated + 2 bridge tools" figures below (§"Status", §3.3, §9, and the
> 8.2 transcript) are **not** wrong as history: they are the verbatim 2026-09-11 verification run against the
> `post-alpha/agent-control-surface` lane build, which registered 23 commands. They are stale in the same
> direction as the snapshot, and one regeneration corrects both. This README is the note rather than the
> snapshot file because the snapshot is strict JSON (`json.load`, declared `"schema": 1`) that the bridge
> parses; the marker belongs in prose, not inside data a program reads.
> **Regenerating it needs a live instance** (see §2 for the headless recipe; there is no default binary and no
> offline mode for this). The real invocation, read from `snapshot_commands.py`'s own docstring and `argparse`
> block:
> `python3 snapshot_commands.py --socket /tmp/<an-instance>/zene.sock` — or, from a commands array
> you already have, `python3 snapshot_commands.py --from-file /tmp/commands.json`. It writes
> `zene_control/commands_snapshot.json` by default (`--out` to change that), records the binary, its sha256, the
> lane directory and its HEAD as provenance, and exits non-zero *without writing* if the source carries no
> commands.
> **Source:** the 2026-09-13 four-audit verification (`projects/lmms-fl-research/STATUS-CORRECTION-2026-09-13.md`
> §6, closing paragraph), which names the four missing commands; the invocation is read from the script, and the
> 70-command provenance from the JSON itself.

**Status:** built and verified 2026-09-11 against a live, headless Zene Studio
instance. The bridge answers `initialize` + `tools/list` + `tools/call` +
`resources/*` over stdio MCP; its tool list is **generated** from the instance's
`control.commands_list` and set-equals it (23 generated tools + 2 bridge tools);
the whole client/server contract is driven by a real MCP client in
`tests/test_mcp_e2e.py` (the live instance's contract) and
`tests/test_mcp_errors.py` (the bounded typed failures) on top of
`tests/test_units.py` (naming, generation, configuration, taxonomy, offline
surfaces) and `tests/test_wire_client.py` (real sockets, no DAW). The one real
instance, the shared state directory and the verbatim transcript live in
`tests/mcp_fixture.py`, imported by both end-to-end suites.

**Split 2026-09-12:** the two e2e/unit files were 610 and 513 lines with four
functions over CCN 10, so the freshly imported bridge missed the tools-scope bar
(`--scope tools`: 500 lines, CCN 10) that the rest of the fork's tooling meets.
They are split by concern, the four functions decomposed, and no assertion was
changed or dropped. Before/after counts, the split rationale and the measured
results of the re-run: `docs/TOOLS-SCOPE-AND-GATE6-FIX.md` (repo root).

| | |
|---|---|
| Entry point | `mcp-zene-control/server.py` (stdio MCP) |
| Package | `mcp-zene-control/zene_control/` (`config.py`, `protocol.py`, `registry.py`, `tools.py`, `server.py`) |
| Protocol | MCP stdio · negotiated `2025-11-25` · `serverInfo {name: zene-control, version: 1.0.0}` |
| Runtime | `/home/kruzzzzy/.hermes/hermes-agent/venv/bin/python3` (mcp 2.0 — constructor handlers `on_list_tools` / `on_call_tool` / `on_list_resources` / `on_read_resource`) |
| Socket resolution | `--socket` > `ZENE_CONTROL_SOCKET` > `<workdir>/zene-control.sock`, `workdir` = `--workdir` > `ZENE_CONTROL_WORKDIR` > cwd (absolute-ised) |
| Command list | live `control.commands_list` > `<state-dir>/commands.json` cache > committed `zene_control/commands_snapshot.json` |
| Tests | `python3 -m unittest tests.test_units` (39) · `tests.test_wire_client` (14) · `tests.test_mcp_e2e` (6) · `tests.test_mcp_errors` (5) — split by concern 2026-09-12, see `docs/TOOLS-SCOPE-AND-GATE6-FIX.md` |
| Tools | 25 = 23 generated from the DAW + `zene_commands` + `zene_status` |
| Resources | `zene://project/state`, `zene://instance/status` |

## 1. What it is

An MCP stdio server that exposes a **running** Zene Studio instance to an agent.
The DAW side is the opt-in in-app control surface (lane `zene-pa-agentctl`,
branch `post-alpha/agent-control-surface`, head `6b01b98eb`): started with
`--control-socket <path>` it serves line-delimited JSON-RPC over an `AF_UNIX`
socket, mode `0600`, never on the network.

The bridge holds **no command knowledge of its own**:

* at request time it reads `control.commands_list` from the instance and builds
  one MCP tool per command from that command's own `args_schema`, so the MCP
  surface cannot drift from the DAW — a command added to the DAW appears on the
  next `tools/list`, one removed disappears, a changed schema is reflected;
* with no instance reachable it serves the same list from the last-known copy
  (cache, then the committed snapshot), so an agent can *see what exists before
  launching anything*;
* the only bridge-owned additions to a generated tool are the **name** (§3), one
  optional `timeout_s` argument (§3.2) and prose in the description.

Design rules, all enforced by tests rather than convention:

| Rule | Where it is enforced |
|---|---|
| The tool list set-equals `control.commands_list` | `test_01_tools_list_set_equals_daw_commands_list` |
| Tool names are deterministic functions of the command id | `TestNaming` (unit) |
| No silent drop on a name collision — refuse loudly | `test_collision_is_refused_not_dropped` |
| Nothing engine-bound is sent before `engine_ready` | `test_10_readiness_rule_gates_engine_commands` (stub sees only pings) |
| Every call is bounded; a wedged instance answers `timeout` | `test_08_silent_instance_hits_the_bounded_timeout` |
| No instance / stale socket / mid-flight kill / proto mismatch are typed | `test_05`, `test_06`, `test_07`, `test_09` |
| One connection per call, handlers on a worker thread → a long render cannot wedge stdio | `zene_control/protocol.py`, `zene_control/server.py` (`asyncio.to_thread`) |
| Errors are never swallowed or faked | a failing tool returns `isError` + typed JSON; the DAW's own kinds pass through verbatim |

Two things the bridge does **not** do: it never writes to the DAW except by
forwarding a command, and it never invents a command, argument or tool.

## 2. How to start an instance it can attach to (the recipe)

An instance is useless until the engine is ready (AGENT-TOOLING.md §4): with no
opening audio device `engine_ready` never becomes true and every command —
including `project.open` — answers `busy` forever. Use the Dummy-audio recipe
(`tests/harness.py` `Instance` is a working implementation):

```bash
W=/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/lmms-fl-research
BIN=$W/zene-pa-agentctl/build/lmms      # the lane build that has --control-socket
TMP=$(mktemp -d /tmp/zene-XXXX)

cat > "$TMP/lmmsrc.xml" <<'XML'
<?xml version="1.0"?>
<!DOCTYPE lmms-config-file>
<lmmsconfig version="0.2.0-alpha" configversion="3">
  <app configured="1"/>
  <audioengine audiodev="Dummy (no sound output)"/>
  <paths workingdir="WORKSPACE"/>
</lmmsconfig>
XML
sed -i "s#WORKSPACE#$TMP/workspace#" "$TMP/lmmsrc.xml"; mkdir -p "$TMP/workspace"

QT_QPA_PLATFORM=offscreen \
HOME=$TMP XDG_CONFIG_HOME=$TMP/config XDG_DATA_HOME=$TMP/data \
  "$BIN" --config "$TMP/lmmsrc.xml" --control-socket "$TMP/zene.sock" &
```

Three details are load-bearing and each cost a measurement:

* `audiodev` must match `AudioDummy::name()` **exactly** — `Dummy (no sound output)`.
  Otherwise `MainWindow` puts up a modal "Audio device setup failed" dialog before
  the event loop starts and the engine never becomes ready.
* `HOME`/`XDG_CONFIG_HOME`/`XDG_DATA_HOME` must point into the temp dir, or the run
  disturbs the user's real settings.
* `QT_QPA_PLATFORM=offscreen` — there is no display.

Then poll `control.ping` until `engine_ready` is true. The bridge does this itself
(§4); measured on this box: socket connectable after **0.10 s**, `engine_ready`
after **2.6–3.2 s**, 23 commands registered.

Point the bridge at it (no configuration at all if you launch the bridge with that
directory as its working directory, because `zene-control.sock` is the default name):

```bash
PY=/home/kruzzzzy/.hermes/hermes-agent/venv/bin/python3
$PY $W/mcp-zene-control/server.py --socket "$TMP/zene.sock"          # stdio MCP
$PY $W/mcp-zene-control/call_tool.py zene_status --socket "$TMP/zene.sock"
```

## 3. The tool list and how it is generated

### 3.1 Names

```
tool_name = "zene_" + command_id.replace(".", "_")   # then any other unsafe char -> "_"
mixer.set_volume      -> zene_mixer_set_volume
control.commands_list -> zene_control_commands_list
render.render         -> zene_render_render
session.slot-fill     -> zene_session_slot_fill      (dashes map to underscores too)
```

The mapping is deterministic (same id → same name, always) and injective for
every id the registry uses; if two ids ever collided the bridge **refuses** with a
typed error naming both, rather than dropping one. Names in the reserved bridge
namespace (`zene_commands`, `zene_status`) are also collision-checked.

Everything else about the tool — description, `inputSchema` properties, `required`,
`maximum`/`minimum`, `enum`, `additionalProperties: false` — is the DAW's own data.
The DAW's *result* schema is carried in the tool's `_meta` (`zene/result_schema`)
instead of being declared as `outputSchema`, because the bridge wraps the result in
an envelope; declaring it as `outputSchema` would be a schema the response does not
satisfy.

### 3.2 The one bridge-owned argument

Every generated tool gains an optional `timeout_s` (0.1–3600, never `required`). It
is stripped before the command is forwarded, so the DAW's `additionalProperties:false`
validation is unaffected. Defaults: `--call-timeout` (30 s) for ordinary commands;
`--long-call-timeout` (900 s) for the commands in `zene_control/config.py`
`LONG_COMMANDS` — today only `render.render` ("renders the whole song by shelling out
to the CLI; minutes on a long project"), which is also called out in that tool's
description. A call is bounded by `min(ready_timeout, timeout_s)` while readiness is
polled, plus `timeout_s` for the command itself.

`render.render` does not block anything else: the bridge opens **one connection per
call** and runs each call on a worker thread, so a multi-minute render neither
head-of-line-blocks another tool call nor freezes the stdio server.

### 3.3 The generated tool names (`tools/list` = 25)

The two bridge-owned tools (§3.4) plus **23 generated tools** — that is the whole
`tools/list` of the verified run. Each generated name is `zene_ + <command id>` with
`.` → `_` (built from the live `control.commands_list`, then re-checked against the
DAW's own ids by `test_01`):

| generated tool | DAW command |
|---|---|
| `zene_control_commands_list` | `control.commands_list` |
| `zene_control_ping` | `control.ping` |
| `zene_control_quit` | `control.quit` |
| `zene_control_redo` | `control.redo` |
| `zene_control_transactions` | `control.transactions` |
| `zene_control_undo` | `control.undo` |
| `zene_control_version` | `control.version` |
| `zene_mixer_add_channel` | `mixer.add_channel` |
| `zene_mixer_get_state` | `mixer.get_state` |
| `zene_mixer_remove_channel` | `mixer.remove_channel` |
| `zene_mixer_set_pan` | `mixer.set_pan` |
| `zene_mixer_set_volume` | `mixer.set_volume` |
| `zene_project_get_state` | `project.get_state` |
| `zene_project_open` | `project.open` |
| `zene_project_save` | `project.save` |
| `zene_render_render` | `render.render` |
| `zene_track_get_state` | `track.get_state` |
| `zene_track_list` | `track.list` |
| `zene_transport_get_state` | `transport.get_state` |
| `zene_transport_play` | `transport.play` |
| `zene_transport_seek` | `transport.seek` |
| `zene_transport_set_tempo` | `transport.set_tempo` |
| `zene_transport_stop` | `transport.stop` |

### 3.4 Bridge-owned tools and resources

| Tool | What it does |
|---|---|
| `zene_commands` | The DAW's command list, with each command's schemas. Live when an instance answers, else the cache, else the committed snapshot; reports `source`, `live`, `captured_at`, `instance` provenance and `instance_error` when it fell back. Works with **no instance connected**. |
| `zene_status` | Socket path, whether an instance answers, `engine_ready`, the bridge's budgets, the snapshot provenance and the verbatim typed error when offline. Always answers. |

| Resource | What it does |
|---|---|
| `zene://project/state` | `project.get_state` — file, modified, tempo, track count, tracks, playing. |
| `zene://instance/status` | The same diagnostics as `zene_status`, as JSON. |

## 4. The readiness rule

The socket is available before the engine is initialised. The bridge **never sends
an engine command before `control.ping` reports `engine_ready: true`**:

1. connect (`no_instance` / `stale_socket` are typed here);
2. send `control.ping` **without** the `proto` member — the DAW accepts a proto-less
   request from any version (`ControlServer::protoMatches`) and reports the version it
   speaks, which is the only way to learn a version that would otherwise refuse you.
   A difference against `--proto` (default 1) is a typed `proto_mismatch`;
3. for a command whose group is engine-bound (`requires_engine`), poll `control.ping`
   every 150 ms until `engine_ready` or `min(--ready-timeout 60s, timeout_s)`, then
   send the command. If readiness never arrives: typed `not_ready`, and the command
   was never sent;
4. `control.*` commands are **engine-free**: they answer before readiness and are not
   gated. The registry does not put `requiresEngine` on the wire, so the bridge derives
   engine-freedom from the group and documents that inference (`config.ENGINE_FREE_GROUPS`).

## 5. Error taxonomy

Every failure is an `isError` tool result whose second content block and
`structuredContent` carry `{"ok": false, "error": {kind, message, retryable, origin,
hint, [command], [elapsed_s], [detail]}}`. Never a hang, never a silent no-op.

| kind | origin | retryable | raised when | hint it carries |
|---|---|---|---|---|
| `no_instance` | bridge | yes | nothing exists at the socket path | start an instance with `--control-socket`, or set `--socket`/`ZENE_CONTROL_SOCKET` |
| `stale_socket` | bridge | yes | the file exists but nothing is listening (killed instance, leftover socket, plain file) | remove the file or start the instance that owns it |
| `disconnected` | bridge | yes | the instance closed the connection mid-request | the instance exited or crashed; check it and re-issue |
| `timeout` | bridge | yes | the bounded per-call budget ran out | raise `timeout_s`/`--call-timeout`, check the instance is not wedged |
| `proto_mismatch` | bridge | no | `control.ping` reports a different control proto | restart the bridge with `--proto <the instance's version>` |
| `not_ready` | bridge | yes | `engine_ready` stayed false for the readiness budget | the Dummy-audio recipe, and poll `control.ping` |
| `bridge_error` | bridge | no | unparseable/desynced reply, unsafe path, internal failure (detail carries the raw line or traceback) | see `detail` |
| `not_found` | DAW | no | the instance has no such command/id | call `zene_commands` |
| `invalid_args` | DAW | no | args failed the command's `args_schema` | the tool's `inputSchema` is the DAW's own |
| `requires` | DAW | no | a declared precondition is unmet | the message says which |
| `busy` | DAW | yes | the instance is still starting up | retry after `engine_ready` |
| `refused` | DAW | no | the command exists but the state forbids it | the message says why |

The last five are the DAW's own kinds (AGENT-TOOLING.md §4) and arrive **verbatim**
(`origin: "daw"`, message included); an error kind the bridge does not recognise is
downgraded to `bridge_error` rather than trusted (unit-tested).

One documented interaction: the pre-call `control.commands_list` probe must absorb a
disconnect so that `tools/list` and `zene_commands` still work with no instance. When
an instance dies during that probe, the probe falls back to the cache and the
*same* tool call's own connection then reports the post-kill state (`stale_socket`).
Both outcomes are typed and bounded; neither is a hang. `test_07` covers both, and the
mid-call case is driven with `--offline` to isolate the command connection.

## 6. Registration block for `~/.hermes/config.yaml`

Registered 2026-09-11 through the sanctioned CLI (the agent cannot write this file
directly — `hermes mcp add`, which connects and lists tools before saving):

```bash
hermes mcp add zene-control \
  --command /home/kruzzzzy/.hermes/hermes-agent/venv/bin/python3 \
  --args /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/lmms-fl-research/mcp-zene-control/server.py \
         --workdir /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/lmms-fl-research/mcp-zene-control
```

which wrote exactly this entry under the existing `mcp_servers:` mapping (siblings:
`ai-kos`, `vscode-bridge`, `chrome-devtools`, `fff`, `lmms-lab` — none of them touched):

```yaml
mcp_servers:
  zene-control:
    command: /home/kruzzzzy/.hermes/hermes-agent/venv/bin/python3
    args:
      - /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/lmms-fl-research/mcp-zene-control/server.py
      - --workdir
      - /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/lmms-fl-research/mcp-zene-control
    enabled: true
```

Notes that matter:

* `--workdir` is in **args**, not a config key: the Hermes stdio client builds its
  `StdioServerParameters(..., cwd=config.get("cwd"))` (`tools/mcp_tool_transport.py`), so
  the honoured key for a working directory is `cwd:` (as the `fff` entry uses), and
  `--workdir` makes the bridge's default socket path deterministic
  (`<workdir>/zene-control.sock`) whichever directory Hermes itself happens to run from.
* Hermes passes a **filtered** environment to stdio servers (PATH/HOME/USER/LANG/TERM/
  SHELL/TMPDIR/XDG_* plus anything in the entry's `env:`). The bridge therefore needs no
  environment at all: the CLI argument carries everything, and `--socket`/`ZENE_CONTROL_*`
  remain available for a session that wants to point it at one specific instance.
* **A running Hermes session will only pick this up after a restart** — MCP servers are
  connected at session start, and the tool set is discovered then. A profile that should
  see the tools also needs the server name in its `toolsets` list.
* `hermes mcp add` re-serialised the whole file: it moved the existing top-level
  `fallback_providers:` block and appended its own commented-out template sections. A
  parsed comparison of before/after shows **zero** changed values and **zero** modified
  `mcp_servers` entries — only the new entry was added — but the diff is not a pure
  append, and that is stated here rather than glossed over.

## 7. Reproduce

```bash
cd <repo>/tools/mcp-zene-control         # in-tree since the bridge shipped in the product
PY=/home/kruzzzzy/.hermes/hermes-agent/venv/bin/python3

$PY -m unittest tests.test_units          # 39, no DAW binary needed
$PY -m unittest tests.test_wire_client    # 14, real sockets; no DAW binary
$PY -m unittest tests.test_mcp_e2e -v     # 6, the live instance's contract
$PY -m unittest tests.test_mcp_errors -v  # 5, the bounded typed failures
# both e2e modules start (or reuse) one real headless instance through tests/mcp_fixture.py
$PY snapshot_commands.py --socket /tmp/<an-instance>/zene.sock   # regenerate the snapshot
$PY call_tool.py zene_status --socket /tmp/<an-instance>/zene.sock
$PY call_tool.py zene_render_render '{"out":"/tmp/a.wav"}' --socket /tmp/<an-instance>/zene.sock
```

`ZENE_CONTROL_BINARY` overrides the binary the harness starts;
`ZENE_CONTROL_PYTHON` overrides the interpreter it launches the bridge with;
`ZENE_CONTROL_STATE` overrides the cache directory; `ZENE_CONTROL_OFFLINE=1` (or
`--offline`) makes the bridge never touch the socket.

## 8. Verification evidence (verbatim)

### 8.1 Fixture and suites

```
---- module fixture ----
{
  "socket_s": 0.103,
  "engine_ready_s": 3.038,
  "binary": "/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/lmms-fl-research/zene-pa-agentctl/build/lmms",
  "socket_path": "/tmp/zene-control-test-v5whm6r6/zene.sock",
  "daw_commands": 23,
  "command_list_source": "live",
  "instance_exit": 0,
  "transcript_path": "/tmp/zene-control-state-3d2x3ktl/mcp-transcript.json"
}

MCP E2E: 40 recorded exchanges; fixture socket 0.103s, engine ready 3.038s, 23 DAW commands

Ran 11 tests in 59.032s
OK
```

```
Ran 53 tests in 3.368s
OK
```

`python3 -m unittest tests.test_units` → `EXIT=0`; `python3 -m unittest tests.test_mcp_e2e -v`
→ `EXIT=0`. The per-query transcript for the run above is also kept in-tree at
`mcp-zene-control/.state/mcp-transcript.json`.

That block is the 2026-09-11 verification run against the agent-control lane's build, kept
verbatim — which is why it says "Ran 11 tests" and "Ran 53 tests": the suites were split by
concern on 2026-09-12 (`docs/TOOLS-SCOPE-AND-GATE6-FIX.md`), and the split re-run against a
build of the merged tree gives 39 (`test_units`) + 14 (`test_wire_client`) with no DAW binary
needed, and 6 (`test_mcp_e2e`) + 5 (`test_mcp_errors`) with one real headless instance. Every
assertion from the 11/53 run is still executed; none was changed, moved out of a suite or
dropped. In that merged-tree build one e2e test fails: `test_11` calls `control.undo`, and the
DAW SEGFAULTs inside `ProjectJournal::undo` → `PatternStore::updateComboBox` (null pattern
track), which the bridge honestly reports as the typed `disconnected`. It is reproduced with a
raw socket client and no bridge process at all, so it is a DAW-side defect, not a bridge one;
the probe, stack and exit codes are in `docs/TOOLS-SCOPE-AND-GATE6-FIX.md`.

### 8.2 `tools/list` — the generated set equals the DAW's commands

`test_01` reads `control.commands_list` from the instance **with its own client**, then
asserts the bridge's generated names are exactly `zene_ + id.replace(".", "_")` for
that list, that no DAW argument property was lost, that `timeout_s` was added and is
not required, and that each generated `inputSchema` carries no synthesised content.

```json
{
  "kind": "tools_list",
  "phase": "tool_generation",
  "server_info": {
    "name": "zene-control",
    "version": "1.0.0"
  },
  "protocol": "2025-11-25",
  "tool_count": 25,
  "generated_count": 23,
  "generated": [
    "zene_control_commands_list",
    "zene_control_ping",
    "zene_control_quit",
    "zene_control_redo",
    "zene_control_transactions",
    "zene_control_undo",
    "zene_control_version",
    "zene_mixer_add_channel",
    "zene_mixer_get_state",
    "zene_mixer_remove_channel",
    "zene_mixer_set_pan",
    "zene_mixer_set_volume",
    "zene_project_get_state",
    "zene_project_open",
    "zene_project_save",
    "zene_render_render",
    "zene_track_get_state",
    "zene_track_list",
    "zene_transport_get_state",
    "zene_transport_play",
    "zene_transport_seek",
    "zene_transport_set_tempo",
    "zene_transport_stop"
  ],
  "bridge_tools": [
    "zene_commands",
    "zene_status"
  ],
  "step": 1
}
```

### 8.3 The full flow through the bridge — raw MCP transcript

Real `mcp.client.stdio` + `ClientSession`; `request` is the tool call the client made,
`structured` is the response's `structuredContent` (the first content block is the
one-line `summary`). The fixture ships the master channel only, so the flow adds a
channel to have an addressable fader (`mixer.add_channel`, not reversible — the DAW
says so), then sets it, reads it back, renders and saves.

```json
{
  "kind": "tools_call",
  "phase": "full_flow",
  "tool": "zene_project_open",
  "arguments": {
    "path": "/tmp/zene-flow-939ylkro/agent-control-fixture.mmp"
  },
  "is_error": false,
  "elapsed_s": 0.133,
  "summary": "project.open: ok in 0.07s (file=/tmp/zene-flow-939ylkro/agent-control-fixture.mmp, tempo=140, track_count=1)",
  "structured": {
    "ok": true,
    "tool": "zene_project_open",
    "command": "project.open",
    "mutating": true,
    "engine_free": false,
    "timeout_s": 30.0,
    "elapsed_s": 0.075,
    "result": {
      "file": "/tmp/zene-flow-939ylkro/agent-control-fixture.mmp",
      "tempo": 140,
      "track_count": 1
    },
    "instance": {
      "version": "0.2.0-alpha",
      "proto": 1,
      "engine_ready": true,
      "socket_path": "/tmp/zene-control-test-v5whm6r6/zene.sock"
    },
    "summary": "project.open: ok in 0.07s (file=/tmp/zene-flow-939ylkro/agent-control-fixture.mmp, tempo=140, track_count=1)"
  },
  "step": 2
}
{
  "kind": "tools_call",
  "phase": "full_flow",
  "tool": "zene_mixer_get_state",
  "arguments": {},
  "is_error": false,
  "elapsed_s": 0.007,
  "summary": "mixer.get_state: ok in 0.00s (channels=[{'id': 'ch-0', 'index': 0, 'is_bus': False, 'is_master': True, 'muted': False, 'name': 'Master', 'pan': None, 'sends': [], 'soloed': False, 'volume': 1}], count=1)",
  "structured": {
    "ok": true,
    "tool": "zene_mixer_get_state",
    "command": "mixer.get_state",
    "mutating": false,
    "engine_free": false,
    "timeout_s": 30.0,
    "elapsed_s": 0.001,
    "result": {
      "channels": [
        {
          "id": "ch-0",
          "index": 0,
          "is_bus": false,
          "is_master": true,
          "muted": false,
          "name": "Master",
          "pan": null,
          "sends": [],
          "soloed": false,
          "volume": 1
        }
      ],
      "count": 1
    },
    "instance": {
      "version": "0.2.0-alpha",
      "proto": 1,
      "engine_ready": true,
      "socket_path": "/tmp/zene-control-test-v5whm6r6/zene.sock"
    },
    "summary": "mixer.get_state: ok in 0.00s (channels=[{'id': 'ch-0', 'index': 0, 'is_bus': False, 'is_master': True, 'muted': False, 'name': 'Master', 'pan': None, 'sends': [], 'soloed': False, 'volume': 1}], count=1)"
  },
  "step": 3
}
{
  "kind": "tools_call",
  "phase": "full_flow",
  "tool": "zene_mixer_add_channel",
  "arguments": {},
  "is_error": false,
  "elapsed_s": 0.007,
  "summary": "mixer.add_channel: ok in 0.00s (channel=ch-1, index=1)",
  "structured": {
    "ok": true,
    "tool": "zene_mixer_add_channel",
    "command": "mixer.add_channel",
    "mutating": true,
    "engine_free": false,
    "timeout_s": 30.0,
    "elapsed_s": 0.001,
    "result": {
      "channel": "ch-1",
      "index": 1
    },
    "instance": {
      "version": "0.2.0-alpha",
      "proto": 1,
      "engine_ready": true,
      "socket_path": "/tmp/zene-control-test-v5whm6r6/zene.sock"
    },
    "summary": "mixer.add_channel: ok in 0.00s (channel=ch-1, index=1)"
  },
  "step": 4
}
{
  "kind": "tools_call",
  "phase": "full_flow",
  "tool": "zene_mixer_set_volume",
  "arguments": {
    "channel": "ch-1",
    "volume": 0.5
  },
  "is_error": false,
  "elapsed_s": 0.007,
  "summary": "mixer.set_volume: ok in 0.00s (channel=ch-1, volume=0.5)",
  "structured": {
    "ok": true,
    "tool": "zene_mixer_set_volume",
    "command": "mixer.set_volume",
    "mutating": true,
    "engine_free": false,
    "timeout_s": 30.0,
    "elapsed_s": 0.001,
    "result": {
      "channel": "ch-1",
      "volume": 0.5
    },
    "instance": {
      "version": "0.2.0-alpha",
      "proto": 1,
      "engine_ready": true,
      "socket_path": "/tmp/zene-control-test-v5whm6r6/zene.sock"
    },
    "summary": "mixer.set_volume: ok in 0.00s (channel=ch-1, volume=0.5)"
  },
  "step": 5
}
{
  "kind": "tools_call",
  "phase": "full_flow",
  "tool": "zene_mixer_get_state",
  "arguments": {},
  "is_error": false,
  "elapsed_s": 0.012,
  "summary": "mixer.get_state: ok in 0.00s (channels=[{'id': 'ch-0', 'index': 0, 'is_bus': False, 'is_master': True, 'muted': False, 'name': 'Master', 'pan': None, 'sends': [], 'soloed': False, 'volume': 1}, {'id': 'ch-1', 'index': 1, 'is_bus': False, 'is_master': False, 'muted': False, 'name': 'Channel 1', 'pan': None, 'sends': [{'amount': 1, 'pre_fader': False, 'to': 'ch-0'}], 'soloed': False, 'volume': 0.5}], count=2)",
  "structured": {
    "ok": true,
    "tool": "zene_mixer_get_state",
    "command": "mixer.get_state",
    "mutating": false,
    "engine_free": false,
    "timeout_s": 30.0,
    "elapsed_s": 0.004,
    "result": {
      "channels": [
        {
          "id": "ch-0",
          "index": 0,
          "is_bus": false,
          "is_master": true,
          "muted": false,
          "name": "Master",
          "pan": null,
          "sends": [],
          "soloed": false,
          "volume": 1
        },
        {
          "id": "ch-1",
          "index": 1,
          "is_bus": false,
          "is_master": false,
          "muted": false,
          "name": "Channel 1",
          "pan": null,
          "sends": [
            {
              "amount": 1,
              "pre_fader": false,
              "to": "ch-0"
            }
          ],
          "soloed": false,
          "volume": 0.5
        }
      ],
      "count": 2
    },
    "instance": {
      "version": "0.2.0-alpha",
      "proto": 1,
      "engine_ready": true,
      "socket_path": "/tmp/zene-control-test-v5whm6r6/zene.sock"
    },
    "summary": "mixer.get_state: ok in 0.00s (channels=[{'id': 'ch-0', 'index': 0, 'is_bus': False, 'is_master': True, 'muted': False, 'name': 'Master', 'pan': None, 'sends': [], 'soloed': False, 'volume': 1}, {'id': 'ch-1', 'index': 1, 'is_bus': False, 'is_master': False, 'muted': False, 'name': 'Channel 1', 'pan': None, 'sends': [{'amount': 1, 'pre_fader': False, 'to': 'ch-0'}], 'soloed': False, 'volume': 0.5}], count=2)"
  },
  "step": 6
}
{
  "kind": "tools_call",
  "phase": "full_flow",
  "tool": "zene_render_render",
  "arguments": {
    "out": "/tmp/zene-flow-939ylkro/render.wav",
    "format": "wav"
  },
  "is_error": false,
  "elapsed_s": 6.178,
  "summary": "render.render: ok in 6.16s (bytes=2720856, format=wav, frames=680192, path=/tmp/zene-flow-939ylkro/render.wav)",
  "structured": {
    "ok": true,
    "tool": "zene_render_render",
    "command": "render.render",
    "mutating": false,
    "engine_free": false,
    "timeout_s": 900.0,
    "elapsed_s": 6.164,
    "result": {
      "bytes": 2720856,
      "format": "wav",
      "frames": 680192,
      "path": "/tmp/zene-flow-939ylkro/render.wav",
      "sample_rate": 44100,
      "sha256": "fb2fba5c61bddb8efb708406c41f57599f9393d095d7a6b321d4631d5366b423"
    },
    "instance": {
      "version": "0.2.0-alpha",
      "proto": 1,
      "engine_ready": true,
      "socket_path": "/tmp/zene-control-test-v5whm6r6/zene.sock"
    },
    "summary": "render.render: ok in 6.16s (bytes=2720856, format=wav, frames=680192, path=/tmp/zene-flow-939ylkro/render.wav)",
    "long_operation": "renders the whole song by shelling out to the CLI; minutes on a long project"
  },
  "step": 7
}
{
  "kind": "render_artifact",
  "phase": "full_flow",
  "path": "/tmp/zene-flow-939ylkro/render.wav",
  "bytes": 2720856,
  "seconds": 6.178,
  "step": 8
}
{
  "kind": "tools_call",
  "phase": "full_flow",
  "tool": "zene_project_save",
  "arguments": {
    "path": "/tmp/zene-flow-939ylkro/saved.mmp"
  },
  "is_error": false,
  "elapsed_s": 0.03,
  "summary": "project.save: ok in 0.02s (file=/tmp/zene-flow-939ylkro/saved.mmp, saved=True)",
  "structured": {
    "ok": true,
    "tool": "zene_project_save",
    "command": "project.save",
    "mutating": true,
    "engine_free": false,
    "timeout_s": 30.0,
    "elapsed_s": 0.023,
    "result": {
      "file": "/tmp/zene-flow-939ylkro/saved.mmp",
      "saved": true
    },
    "instance": {
      "version": "0.2.0-alpha",
      "proto": 1,
      "engine_ready": true,
      "socket_path": "/tmp/zene-control-test-v5whm6r6/zene.sock"
    },
    "summary": "project.save: ok in 0.02s (file=/tmp/zene-flow-939ylkro/saved.mmp, saved=True)"
  },
  "step": 9
}
{
  "kind": "tools_call",
  "phase": "full_flow",
  "tool": "zene_project_get_state",
  "arguments": {},
  "is_error": false,
  "elapsed_s": 0.014,
  "summary": "project.get_state: ok in 0.00s (file=/tmp/zene-flow-939ylkro/agent-control-fixture.mmp, modified=False, playing=False, tempo=140)",
  "structured": {
    "ok": true,
    "tool": "zene_project_get_state",
    "command": "project.get_state",
    "mutating": false,
    "engine_free": false,
    "timeout_s": 30.0,
    "elapsed_s": 0.004,
    "result": {
      "file": "/tmp/zene-flow-939ylkro/agent-control-fixture.mmp",
      "modified": false,
      "playing": false,
      "tempo": 140,
      "track_count": 1,
      "tracks": [
        "trk-0"
      ]
    },
    "instance": {
      "version": "0.2.0-alpha",
      "proto": 1,
      "engine_ready": true,
      "socket_path": "/tmp/zene-control-test-v5whm6r6/zene.sock"
    },
    "summary": "project.get_state: ok in 0.00s (file=/tmp/zene-flow-939ylkro/agent-control-fixture.mmp, modified=False, playing=False, tempo=140)"
  },
  "step": 10
}
{
  "kind": "full_flow_result",
  "phase": "full_flow",
  "steps": [
    "added",
    "mixer_after",
    "mixer_before",
    "opened",
    "project_state",
    "render",
    "saved",
    "set_volume"
  ],
  "out_wav": "/tmp/zene-flow-939ylkro/render.wav",
  "saved": "/tmp/zene-flow-939ylkro/saved.mmp",
  "step": 11
}
```

(10 exchanges recorded for this test; the complete run — 40 exchanges — is in `mcp-zene-control/.state/mcp-transcript.json`.)

### 8.4 The lifecycle/error matrix (each bounded, each typed)

| case | how it was produced | result | budget | measured |
|---|---|---|---|---|
| no instance | bridge pointed at a path that does not exist | `no_instance` | 5 s | 0.013 s wall (0.001 s in-bridge) |
| stale socket file | bind + listen + `close()` leaving the file, then call | `stale_socket` | 5 s | 0.017 s |
| killed mid-call | real instance `SIGSTOP`ped, call issued, `SIGKILL` 0.5 s later | `disconnected` | 20 s | 0.53 s in-bridge |
| killed while idle | `SIGKILL` a ready instance, then call (socket file is left behind) | `stale_socket` | 5 s | 0.010 s |
| wedged instance | stub that accepts and never replies | `timeout` | 1 s | 1.0 s in-bridge, bridge process not wedged |
| protocol mismatch | bridge `--proto 2` against the proto-1 instance | `proto_mismatch` | 5 s | immediate; message `the instance speaks control proto 1, the bridge requires proto 2` |
| engine never ready | stub answering ping with `engine_ready:false` | `not_ready` | 2 s | 2.134 s; the stub saw only `control.ping`/`control.commands_list` — **the engine command was never sent** |
| DAW `not_found` | `zene_mixer_set_volume {"channel":"ch-9999"}` | `not_found` | 30 s | 0.001 s |
| DAW `refused` | `zene_mixer_set_pan` (no pan on a `MixerChannel` in this tree) | `refused` | 30 s | 0.006 s |
| DAW `invalid_args` | `zene_transport_seek {"ticks":"not-a-number"}` | `invalid_args` | 30 s | 0.004 s |
| unknown bridge tool | `zene_no_such_tool` | `not_found` + the full tool list in the message | — | 0.013 s |

Verbatim excerpt (the raw records are in `.state/mcp-transcript.json`):

```json
{"kind": "no_instance_matrix", "call_kind": "no_instance", "call_elapsed_s": 0.0134,
 "commands_source": "cache", "generated_tools_offline": 23}
{"kind": "stale_socket", "error_kind": "stale_socket", "elapsed_s": 0.017}
{"kind": "killed_mid_call", "error_kind": "disconnected", "inner_elapsed_s": 0.53,
 "timeout_s": 20.0, "message": "the instance closed the connection before replying (it exited or crashed mid-request)"}
{"kind": "killed_idle", "error_kind": "stale_socket", "elapsed_s": 0.01}
{"kind": "timeout", "error_kind": "timeout", "bridge_wall_s": 5.406, "stub_saw": ["control.ping", "control.ping"]}
{"kind": "proto_mismatch", "error_kind": "proto_mismatch",
 "bridge_message": "the instance speaks control proto 1, the bridge requires proto 2",
 "daw_reply_to_proto99": {"ok": false, "error": {"kind": "refused",
   "message": "unsupported protocol version; this instance speaks proto 1"}}}
{"kind": "readiness_gate", "kind_reported": "not_ready", "elapsed_s": 2.134,
 "seen_after_engine_call": ["control.ping", "control.commands_list", "control.ping", "..." ]}
```

Note the last two lines together: the DAW answers a foreign-proto **request** with its
generic `refused`, while the bridge's proto-less probe reports the mismatch by kind
with both versions named.

### 8.5 Render artifact and undo (do not take "ok" on trust)

```
render: 2,720,856 bytes at /tmp/zene-flow-939ylkro/render.wav
        result: {"bytes": 2720856, "format": "wav", "frames": 680192, "sample_rate": 44100,
                 "path": "/tmp/zene-flow-939ylkro/render.wav",
                 "sha256": "fb2fba5c61bddb8efb708406c41f57599f9393d095d7a6b321d4631d5366b423"}
        680,192 frames at 44100 Hz = 15.42 s of 16-bit stereo; the file exists and is non-empty.
        The sha256 differs between runs of the same project (an earlier run of this same
        fixture produced 83ddebdf…), so the hash proves the render happened, not that it is
        bit-reproducible — the DAW's own determinism is a separate question (the `lmms-lab`
        server's `lmms_render_matrix` is the tool for that).
undo:   transactions recorded for mixer.add_channel, mixer.set_volume, project.open,
        project.save, transport.set_tempo; transport.set_tempo reversible=true;
        control.undo -> undone=true
```

### 8.6 `~/.hermes/config.yaml` diff

```diff
--- /tmp/config.yaml.before-zene-control	2026-09-12 00:00:16.197877127 +0100
+++ /home/kruzzzzy/.hermes/config.yaml	2026-09-12 00:01:21.060879388 +0100
@@ -22,6 +22,9 @@
       gpt-oss:20b:
         context_length: 65536
       nomic-embed-text:latest: {}
+fallback_providers:
+  - provider: deepseek
+    model: deepseek-flash
 toolsets:
   - hermes-cli
   - web
@@ -297,6 +300,48 @@
     args:
       - /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/lmms-fl-research/mcp-lmms-lab/server.py
     enabled: true
-fallback_providers:
-  - provider: deepseek
-    model: deepseek-flash
+  zene-control:
+    command: /home/kruzzzzy/.hermes/hermes-agent/venv/bin/python3
+    args:
+      - /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/lmms-fl-research/mcp-zene-control/server.py
+      - --workdir
+      - /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/lmms-fl-research/mcp-zene-control
+    enabled: true
+
+# ── Security ──────────────────────────────────────────────────────────
+# Secret redaction is ON by default — strings that look like API keys,
+# tokens, and passwords are masked in tool output, logs, and chat
+# responses before the model or user ever sees them. Set redact_secrets
+# to false to disable (e.g. when developing the redactor itself).
+# tirith pre-exec scanning is enabled by default when the tirith binary
+# is available. Configure via security.tirith_* keys or env vars
+# (TIRITH_ENABLED, TIRITH_BIN, TIRITH_TIMEOUT, TIRITH_FAIL_OPEN).
+#
+# security:
+#   redact_secrets: true
+#   tirith_enabled: true
+#   tirith_path: "tirith"
+#   tirith_timeout: 5
+#   tirith_fail_open: true
+
+# ── Fallback Model ────────────────────────────────────────────────────
+# Automatic provider failover when primary is unavailable.
+# Uncomment and configure to enable. Triggers on rate limits (429),
+# overload (529), service errors (503), or connection failures.
+#
+# Supported providers:
+#   openrouter   (OPENROUTER_API_KEY)  — routes to any model
+#   openai-codex (OAuth — hermes auth) — OpenAI Codex
+#   nous         (OAuth — hermes auth) — Nous Portal
+#   zai          (ZAI_API_KEY)         — Z.AI / GLM
+#   kimi-coding  (KIMI_API_KEY)        — Kimi / Moonshot
+#   kimi-coding-cn (KIMI_CN_API_KEY)   — Kimi / Moonshot (China)
+#   minimax      (MINIMAX_API_KEY)     — MiniMax
+#   minimax-cn   (MINIMAX_CN_API_KEY)  — MiniMax (China)
+#   bedrock      (AWS IAM / boto3)     — AWS Bedrock (Converse API)
+#
+# For custom OpenAI-compatible endpoints, add base_url and key_env.
+#
+# fallback_model:
+#   provider: openrouter
+#   model: anthropic/claude-sonnet-4
```

## 9. Known limits and untested things

* **A running Hermes session needs a restart** before the tools appear (the entry was
  appended to the config, nothing more).
* **Untested from inside a live Hermes session.** Everything here was verified over
  raw stdio MCP with the SDK's own client; the tool count an agent sees depends on
  that client re-listing after the restart.
* **`requiresEngine` is not on the wire.** The engine-free/engine-bound split is
  derived from the group (`control.*`), which matches the C++ registration today; if
  a future command is engine-free outside `control`, the bridge would gate it (safe
  direction) and that would need editing `ENGINE_FREE_GROUPS`.
* **`requires` is empty for all 23 commands** in this instance, so the bridge only
  reports it in the description; nothing is gated on it.
* **`render.render` is a long operation by bridge-side policy.** The registry declares
  no duration hint, so `LONG_COMMANDS` is hand-maintained in `config.py`; a new slow
  command gets the 30 s default until someone adds it (an explicit `timeout_s` always
  works). The DAW-side reason it is slow — the render path serialises then shells out
  to the CLI — is a DAW limit, not the bridge's.
* **The pre-call command-list probe absorbs a disconnect** (§5) so offline discovery
  keeps working; the following call reports `stale_socket` in that window. Typed, never
  a hang, but it is why `test_07` passes `--offline` for the mid-call case.
* **Only proto 1 has been spoken for real.** The `--proto` mismatch path is tested
  against a real proto-1 instance and against a DAW-like stub; a real proto-2 instance
  does not exist yet, so "restart the bridge with `--proto 2` and it works" is
  unverified.
* **Concurrency beyond two clients is untested.** The DAW listens with backlog 16 and
  the bridge opens one connection per call; two clients were exercised implicitly
  (`zene_commands` while other calls were open), more were not.
* **POSIX only.** `AF_UNIX` by construction; the DAW refuses a relative socket path
  and the bridge absolute-ises before connecting.
* **DAW-side limits that the bridge surfaces rather than fixes:**
  `mixer.set_pan` is a typed refusal (no pan on a `MixerChannel` in this tree);
  `mixer.add_channel` is honestly not reversible; `project.open` can hang on a modal
  error box (the bridge's `timeout_s` bounds the agent's wait, it cannot dismiss the
  dialog) — that specific hang was not reproduced here; `control.quit` needs the DAW's
  own watchdog.
* **The bridge has no `zene_call` escape hatch** by design: `tools/list` is regenerated
  per request, so a command that appears while the bridge is running is listed on the
  next `tools/list` — but an MCP client that caches the list per session will not see
  it without re-listing or restarting the session.
* **The snapshot is a committed copy** of one instance's command list. The provenance that used to be printed
  here (`binary sha256 94582147…`, lane head `6b01b98eb`, version `0.2.0-alpha`, proto 1, 23 commands,
  captured 2026-09-11T22:54:33Z) described an **earlier capture**; corrected 2026-09-13 against the committed
  file itself, `zene_control/commands_snapshot.json` records binary `sha256 0a13ee76…`, lane head `059bf6bad`,
  version `0.1.0-alpha.15+a244564`, proto 1, **70 commands**, captured **2026-09-12T01:43:20Z** — and it is
  stale against the registry's 74 (see the snapshot note at the top of this file). It is what makes
  `zene_commands` work before any instance exists, and it can go stale; `snapshot_commands.py` regenerates it
  from a live instance (`--socket`) or a saved commands array (`--from-file`), with `--out` defaulting to this
  file.
