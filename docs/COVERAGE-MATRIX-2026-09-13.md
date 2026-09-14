# Zene Studio — control-surface coverage matrix

> **SUPERSEDED — 2026-09-14.** This file is a **tip-pinned snapshot**: its own header pins `ddf5f171d`
> and its §1.2 computes **150 ids = 144 + 6** over **28 groups** (27 id prefixes + the helper-built
> `wasm`). It is kept as the record of that base and is **not** updated in place. The measurement of
> record is **`docs/COVERAGE-REMEASURE-2026-09-14.md`** (branch `030/audit`), taken at the release tip
> **`3956ef589`** — `33` command groups and `185` command ids, the registry source and a live
> `control.commands_list` agreeing exactly. Read §9 of that file for the four surfaces this one's own §9
> (*Re-measurement, 2026-09-13*) left disagreeing (28/150, 30/154, 31/170), and §6 for the 41 ids the `chain`,
> `clock`, folder-track, `midi.retro_capture_*`, `record`, `freeze`/`bounce`, `groove` and
> `transport.punch_*` merges added between the two tips. The tip some other documents attribute to this
> file, `0ee78abed`, appears nowhere in it and is **not verified**.

| | |
|---|---|
| Tree inventoried | `ddf5f171d` (`release/0.3.0`, the 0.3.0-alpha integration tip) |
| Worktree | `projects/lmms-fl-research/zene-030-audit` (branch `030/audit`, created from `ddf5f171d`) |
| Date | 2026-09-13 |
| Author | audit lane (inventory only — no build, no fixes) |
| Question answered | the owner's directive *"testing and mcp tooling for all features present in zene studio"*: **nobody had measured it. This is the measurement.** |

## Method, and the honesty rules this file follows

Every number below came from a command run against the tree or against the live MCP bridge. The command is
named next to the number. Unpiped exit codes only. Nothing here is inferred from another document.

- The **authoritative** id list is the register call sites (`src/core/ControlCommands*.cpp`), because the MCP
  bridge and the tests are both keyed by id.
- `tools/mcp-zene-control/zene_control/commands_snapshot.json` is a **derived** artifact. It is used here as a
  cross-check only, and every disagreement it has with the tree is reported (§1.3).
- `docs/specs/AGENT-SURFACE-INVENTORY.md` is a **pinned 0.1.0-alpha snapshot** (its own header: tree
  `zene-remote@2f0cd8689`, 2026-09-11). It is **stale** at this tip — it states "no in-app command registry
  exists" and "`mcp-zene-control` → not found in the tree", both false here. It is cited where it is the only
  prior record, and flagged where it disagrees.
- Three numbers in this file are *reachability* counts, not *correctness* counts. They are labelled as such
  and must not be read as "this feature works".

---

## 1. The command surface

### 1.1 Registration points

`src/core/ControlRegistry.cpp` `registerControlCommands()` makes **30** `register*Commands(...)` calls.
`grep -n 'register.*Commands' include/ControlRegistry.h include/ControlRegistryGroups.h` shows **43**
`LMMS_EXPORT` declarations: the 30 called directly, the top-level `registerControlCommands` itself, and **12**
sub-registrations their parent group calls — `registerUndoBoundsCommands`, `registerProjectFilesCommands`,
`registerArrangementStateCommands`, `registerPluginDeviceCommands`, `registerPluginParameterCommands`,
`registerPluginStateCommands`, `registerPluginPresetCommands`, `registerWarpEditCommands`,
`registerRackMacroCommands`, `registerRackZoneCommands`, `registerWasmEditCommands`,
`registerBrowserTagCommands`.

Three registration calls are compile-time gated (found with a guard-aware scan of
`registerControlCommands()`):

| gate | registration calls |
|---|---|
| `#ifdef ZENE_TELEMETRY_ENABLED` | `registerTelemetryCommands` |
| `#ifdef LMMS_HAVE_SESSION_VIEW` | `registerSessionCommands`, `registerSessionLaunchCommands` |
| `#ifdef LMMS_HAVE_WASM` | `registerWasmCommands` |

There are **41** `src/core/ControlCommands*.cpp` translation units plus the shared helper
`src/core/ControlWasmSupport.cpp`.

### 1.2 Every command id, counted

```
grep -c '\.id = QStringLiteral' src/core/ControlCommands*.cpp | ...     # 144 id assignments
# the wasm group builds its ids in a helper:
#   src/core/ControlWasmSupport.cpp:62   cmd.id = QStringLiteral("wasm.") + verb;
#   verb args: list, get_state, process (ControlCommandsWasm.cpp)
#              load, unload, set_param (ControlCommandsWasmEdit.cpp)
144 + 6 = 150 ids
```

**28 groups · 150 command ids.** `cmd.group` equals the id prefix for all 150 (0 mismatches — checked with a
regex over every `cmd.id`/`cmd.group` pair, 0 hits where `id` does not start with `group + "."`). *These are
the audit tip's own figures at `ddf5f171d`; the release tree's counts are re-measured in §9 below and are
**not** overwritten here.*

### 1.3 Cross-checks, and the disagreements

| cross-check | result |
|---|---|
| §1.2 register call sites | **150** ids |
| `grep '^\s*R(\|^\s*RC(' src/core/ControlReversibilityTable*.cpp`, deduped | **150** distinct ids — the same set (0 symmetric difference) |
| committed offline snapshot `commands_snapshot.json` | **144** ids. `tree − snapshot` = exactly the 6 `wasm.*` ids; `snapshot − tree` = **empty**. **Zero naming disagreements.** |

The 6-id difference is **explained, not drift**: `wasm.*` is behind `#ifdef LMMS_HAVE_WASM`, `WANT_WASM`
defaults ON but degrades to OFF when the wasmtime C API is absent, and none of the seven CI jobs provisions it
(`docs/KNOWN-LIMITATIONS.md`). The snapshot was therefore captured from a wasm-off build.

**Two real disagreements found, both inside the tree:**

1. **Snapshot provenance vs snapshot content.** The snapshot's `instance.version` is
   `0.2.0-alpha.126+12fc4ef` and its `source_socket` is `/tmp/zn-split.sock`, while its commit message reads
   *"regenerate the offline MCP snapshot at the 0.3.0-alpha tip"*. The **id content is correct for this tip**
   (144/144 match), but the provenance block names a 0.2.0-alpha binary and a `/tmp` socket. `instance.lane`
   also points at `zene-030/tools/zene-pa-agentctl`, a path that does not exist in the tree.
2. **The A16 row-count documented in three places, three numbers.** `grep -n '139\|127\|142'
   docs/RELEASE-NOTES-v0.3.0-alpha.md tests/src/core/ReversibilityContractTest.cpp`:
   - `docs/RELEASE-NOTES-v0.3.0-alpha.md:300` — "holds **139 rows** … 71 / 9 / 3 / 56", "**137** with
     `-DZENE_TELEMETRY=OFF`".
   - `tests/src/core/ReversibilityContractTest.cpp:87` — the asserted constant is `{142, 75, 9, 3, 55}`
     (telemetry-off, wasm-off base; `+2` telemetry and `+6` wasm are added by guards) — so **150** when both
     are on. 142 alone matches neither 139 nor 137.
   - `tests/src/core/ReversibilityContractTest.cpp:62-64` — the test's own docstring says the release notes
     quote "**127 rows** / 63 / 9 / 3 / 52".
   The 150 the test reduces to is the number **corroborated by the two independent tree sources** in §1.2. The
   prose figures are not. I could not settle whether the 139 is a stale count or a deliberate
   different-configuration figure without building (see §6).

---

## 2. Table A — one row per command group

**Legend for `tested?`** — the highest-strength registered proof that exists for that group:

- `behavioural` — a registered ctest invokes the group's ids and asserts on the results (state read back,
  inverse exercised, typed refusals).
- `partial` — a registered ctest exercises some of the group's ids; the rest have no registered-test
  reference (the uncovered ids are named).
- `reachability only` — no registered ctest asserts this group's behaviour; the group is only *swept* for a
  typed reply by the registered `agent_surface` ctest (see §3.2) and/or *named* by
  `ControlRegistryTest`/`ControlCommandsSnapshot`.

**Legend for `MCP tool?`** — measured against the MCP bridge actually registered in `~/.hermes/config.yaml`
(§4), not a hypothetical one:

- `tool (live)` — one generated tool per id is in the surface the agent sees today.
- `generated, not wired` — the 0.3.0 tree's own bridge generates a tool per id *when a live instance answers
  at the configured socket*; the registered bridge is pointed elsewhere (§4), so nothing is exposed today.
- `no tool today` — not reachable through the registered MCP surface at all right now.
- `no tool offline` — reachable only when a live WASM-enabled instance is running; absent from every offline
  copy.

| group | ids | test file(s) (registered ctests) | tested? | MCP tool? | notes |
|---|---|---|---|---|---|
| `app` | 1 | `ControlRegistryTest.cpp`, `control-socket-integration.py`, `control-session-m1.py` | reachability only | tool (live) | `app.version` — presence assertion + socket read; nothing asserts the build-option string |
| `arrangement` | 1 | `ControlEditCommandsTest.cpp`, `control-socket-integration.py` | behavioural | tool (live) | |
| `audio` | 2 | `ControlRegistryTest.cpp`, `control-socket-integration.py` | reachability only | tool (live) | `audio.device_set` writes the config key; no test asserts the written value survives |
| `automation` | 5 | `ControlAutomationScriptTest.cpp` (5/5), `control-socket-integration.py` (5/5) | behavioural | tool (live) | `automation.mode_set` is a **refusal stub** — the id is registered and always refuses ("this build has no automation modes") |
| `browser` | 6 | `ControlBrowserCommandsTest.cpp` (6/6) | behavioural | **no tool today** | strongest group: drives `browser.tag.*` through `control.undo` and reads the store file back |
| `clip` | 10 | `control-socket-integration.py` (10/10), `ControlEditCommandsTest.cpp` (7/10), `UndoBoundsTest.cpp` | behavioural | **no tool today** (7/10 ids would be) | |
| `comp` | 7 | `TakeLaneCompTest.cpp` (7/7) | behavioural | **no tool today** | |
| `control` | 11 | `control-socket-integration.py` (9/11), `UndoBoundsTest.cpp`, `ControlRegistryTest.cpp` | behavioural | tool (live) for 8/11 | `control.set_undo_depth`, `control.set_undo_coalescing`, `control.undo_depth` have **no tool in the live surface** |
| `dsp` | 1 | `ControlRegistryTest.cpp`, `control-socket-integration.py` | reachability only | tool (live) | |
| `export` | 3 | **none registered** | **none** | **no tool today** | the group's only exercising artefact is `tests/control-export-settings.py`, which is **not registered in `tests/CMakeLists.txt`** (§3.3). A committed transcript exists at `tests/evidence/export-src-dither/transcript.txt` — under `tests/`, **not** `docs/` |
| `link` | 5 | `ControlLinkCommandsTest.cpp` (5/5), `control-link-sync.py` | behavioural | **no tool today** | `ControlLinkSync` reports *Skipped* when the host cannot carry multicast |
| `midi` | 2 | `ControlRegistryTest.cpp` (1/2 only) | **partial** | tool (live) for 1/2 | `midi.learn_toggle` appears **only** in `tests/upstream-modifications.txt` (a manifest, not a test) — no test anywhere |
| `mixer` | 5 | `ControlRegistryTest.cpp` (5/5), `control-socket-integration.py` (5/5), `agent-surface-gate.py` | behavioural | tool (live) | `mixer.set_pan` is a **refusal stub** ("this tree has no pan on a mixer channel") |
| `modulator` | 7 | `ControlModulatorCommandsTest.cpp` (7/7) | behavioural | **no tool today** | |
| `note` | 9 | `ControlEditCommandsTest.cpp` (6/9), `control-socket-integration.py` (6/9), `ControlNoteExpressionCommandsTest.cpp` (3/9) | behavioural | **no tool today** (6/9 would be) | |
| `plugin` | 11 | `ControlRegistryTest.cpp` (11/11), `control-socket-integration.py` (11/11), `ReversibilityUndoTest.cpp` | behavioural | tool (live) | |
| `project` | 4 | `ControlRegistryTest.cpp` (3/4), `control-socket-integration.py` (3/4) | behavioural | tool (live) for 3/4 | `project.restore_revision` referenced but not behaviourally exercised |
| `rack` | 12 | `RackMacrosTest.cpp` (12/12), `RackZonesTest.cpp` (4/12) | behavioural | **no tool today** | `rack.zone_*` is a *model plus a lookup* — nothing routes notes through a zone (documented, `KNOWN-LIMITATIONS.md`) |
| `render` | 1 | `ControlRegistryTest.cpp`, `control-socket-integration.py`, `agent-surface-gate.py` | behavioural | tool (live) | |
| `roll` | 1 | `ControlEditCommandsTest.cpp`, `control-socket-integration.py` | behavioural | tool (live) | |
| `script` | 2 | `ControlAutomationScriptTest.cpp` (2/2), `control-socket-integration.py` (2/2) | behavioural | tool (live) | Lua API v0 only; it reaches no mixer, effect, plugin, send or automation-clip object |
| `session` | 11 | `control-session-m1.py` (6/11) | **partial** | **no tool today** | 5 ids — `session.clear`, `session.clear_slot`, `session.launch_slot`, `session.stop_all`, `session.stop_slot` — occur **nowhere outside `src/`+`include/`**: `grep -rlE 'session\.(clear\|clear_slot\|launch_slot\|stop_all\|stop_slot)' src include tests` returns only the 5 implementation files and the registry header. No test, no doc |
| `settings` | 2 | `ControlRegistryTest.cpp`, `control-socket-integration.py` | behavioural | tool (live) | |
| `telemetry` | 2 | `TelemetryTest.cpp` (2/2), `ControlRegistryTest.cpp` (2/2) | behavioural | **no tool today** | `telemetry.consent` is the **only** `agent-surface-allowlist.txt` entry (`requires: display, human`) — the one id the whole-tree sweep does not exercise |
| `track` | 8 | `control-socket-integration.py` (8/8), `ControlEditCommandsTest.cpp` (7/8), `ReversibilityUndoTest.cpp` | behavioural | tool (live) | `track.set_arm` is a **refusal stub** — yet `src/core/audio/MultiTrackRecorder.cpp` (a real 2-track recorder) is in the tree; see Table B |
| `transport` | 10 | `ControlTempoMapCommandsTest.cpp` (7/10), `ControlRegistryTest.cpp` (5/10), `ReversibilityUndoTest.cpp` | behavioural | tool (live) for 5/10 | the 5 `transport.tempo_map_*` ids have **no tool in the live surface** |
| `warp` | 5 | `ControlWarpCommandsTest.cpp` (5/5) | behavioural | **no tool today** | |
| `wasm` | 6 | `control-wasm-sandbox.py` (6/6), registered only when `WANT_WASM` | behavioural (WASM builds only) | **no tool offline** | absent from every offline snapshot because no CI job provisions the wasmtime C API; and `wasm.load` hosts a module **in the host sandbox, not in any device chain** — nothing is heard |

**Totals from the table:** 28 groups · 150 ids · 141 ids referenced by ≥1 registered test artefact ·
**9 ids with no registered-test reference** · **10 groups with no MCP tool in the surface the agent sees
today**.

---

## 3. Testing — what the tests actually prove

### 3.1 The registry of record

`tests/CMakeLists.txt` registers **99** C++ test sources (`LMMS_TESTS`, some behind `if()` guards) and **27**
`add_test(NAME …)` entries that bypass the list (the Python transcripts, wait-for-engine harnesses and the
plugin-host suites). `ctest` from the top-level build directory reports 0 tests; the suite is run from
`<build>/tests`.

`tests/unregistered-tests-gate.sh` (Gate 10) scans **only** `git ls-files 'tests/src/*.cpp' 'tests/src/**/*.cpp'`
— the extension filter is `*.cpp` under `tests/src/`. It says nothing about `tests/*.py`. That is the
brief's point, and it is load-bearing here (§3.3).

### 3.2 The whole-tree sweep (reachability, not correctness)

The registered `agent_surface` ctest (`tests/agent-surface-gate.py`) starts the binary headless, reads
`control.commands_list` from the **live registry**, and calls **every non-allowlisted command**, requiring a
**typed** result (`ok` or a typed error). Its reverse-completeness check (`agent_surface_lib.check_sweep`,
`tests/agent-surface-allowlist.txt`) names any declared command that is neither swept nor allowlisted.
The allowlist has exactly one entry: `telemetry.consent`.

So: **149 of the 150 ids are exercised to a typed reply** in a build where all groups are compiled in, and
`telemetry.consent` is the single documented exception. This proves *reachable and does not crash*. It proves
**nothing** about whether a command does what it says. Table A's `tested?` column is deliberately distinct
from this.

### 3.3 The 9 ids with no registered-test reference

Computed as `150 tree ids − {ids literal-referenced by any registered test artefact}`
(`tests/src/**/*.cpp` plus the `tests/*.py` that appear in `tests/CMakeLists.txt` — 134 artefacts):

| group | ids | why |
|---|---|---|
| `export` | `export.get_settings`, `export.set_dither`, `export.set_src_quality` | the only artefact that exercises them, `tests/control-export-settings.py`, is **not registered**; its committed transcript is at `tests/evidence/export-src-dither/transcript.txt` |
| `session` | `session.clear`, `session.clear_slot`, `session.launch_slot`, `session.stop_all`, `session.stop_slot` | nothing outside `src/`+`include/` mentions them |
| `midi` | `midi.learn_toggle` | only referenced from `tests/upstream-modifications.txt`, which is a manifest |

### 3.4 Test files that exist but are registered as no ctest

`tests/CMakeLists.txt` does not mention these **10** `tests/*.py`:

```
agent_surface_lib.py              (library imported by agent-surface-gate.py)
brand-resource-sweep.py
control-export-settings.py        <-- the export group's ONLY proof
control-reversibility-transcript.py
control-stable-ids.py
control-warp-commands-transcript.py
control_instance_diagnosis.py
control_socket_flows.py
control_socket_harness.py         (library imported by other transcripts)
link_sync_evidence.py
```

(Gate 10 cannot see any of them: it filters on `*.cpp` under `tests/src/`.) For the charter's criterion 3 —
*"a proof: a registered ctest, **or** a committed control-surface transcript under `docs/`"* — the export group
is the one group that satisfies **neither** half.

---

## 4. MCP exposure — measured, not inferred

### 4.1 How the bridge works

`tools/mcp-zene-control/zene_control/registry.py` generates **one MCP tool per command id at request time**
from `control.commands_list`, tool name = `zene_` + id with `.` → `_` (injective; a collision is refused, never
dropped). Two bridge-owned tools always exist: **`zene_commands`** and **`zene_status`**. There is **no generic
passthrough tool** — an unregistered id has no tool. With no live instance the bridge serves
`<state-dir>/commands.json`, then the committed `commands_snapshot.json`.

### 4.2 Which bridge is registered

`grep -n -A6 'zene-control:' ~/.hermes/config.yaml`:

```yaml
  zene-control:
    command: /home/kruzzzzy/.hermes/hermes-agent/venv/bin/python3
    args:
      - /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/lmms-fl-research/mcp-zene-control/server.py
      - --workdir
      - /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/lmms-fl-research/mcp-zene-control
```

The registered bridge is **a different copy** from the 0.3.0 tree's `tools/mcp-zene-control/`.
`diff -rq` of the two shows `config.py`, `protocol.py`, `tools.py`, `commands_snapshot.json` and two test files
**differ**; the 0.3.0 tree additionally has `README.md`, `tests/test_mcp_errors.py` and
`tests/test_wire_client.py` that the registered copy lacks. **The commands actually reachable by an agent in
this session come from the registered copy, not from the audited tip.**

### 4.3 The measured surface

Called the bridge's own tool, `zene_commands(source="auto")`:

> `70 commands from the cache (captured 2026-09-12T03:16:31Z) — no live instance (no_instance: no control
> socket at …/mcp-zene-control/zene-control.sock)`, `"source": "cache"`, `"live": false`,
> `instance.version = "0.1.0-alpha.21+65d657e"`.

**70 ids · 18 groups.** The registered copy's committed snapshot is likewise **70** ids (captured from
`0.1.0-alpha.15+a244564`). No instance is listening at the configured socket path
(`<workdir>/zene-control.sock`, which does not exist); the only live `zene` processes on the box
(`pgrep -a -f zene`) are other lanes' instances on `/tmp/snap.sock`, which this bridge is not pointed at.

### 4.4 The gap

| | tree @ `ddf5f171d` | agent's MCP surface today |
|---|---|---|
| groups | **28** | **18** |
| ids | **150** (144 + 6 wasm) | **70** |

**10 groups have no MCP tool at all**: `browser`, `comp`, `export`, `link`, `modulator`, `rack`, `session`,
`telemetry`, `warp`, `wasm`.

**74 ids in the tree have no MCP tool** (the exact list is the 144-id tree snapshot minus the 70-id cache —
reproducible by diffing `tools/mcp-zene-control/zene_control/commands_snapshot.json` against
`mcp-zene-control/.state/commands.json`).

Two further ids are only *partly* exposed: `control` is present for 8 of its 11 ids (`control.set_undo_depth`,
`control.set_undo_coalescing`, `control.undo_depth` absent), and `transport` for 5 of 10 (the five
`transport.tempo_map_*` absent).

**The mechanism is not a per-feature bridge gap.** A command added to the DAW is automatically a tool *when a
live instance answers*. The failure mode is that no instance is running at the socket the registered bridge
watches, so the 0.3.0 surface is invisible and a 0.1.0-alpha cache is served instead. Regenerating the
registered copy's cache/snapshot from a live 0.3.0 instance, or starting the instance at that socket, closes
the whole 74-id gap at once.

---

## 5. Table B — in the tree, NOT drivable through the socket

Every row is a feature with real code and often registered tests, and **no command group at all**. A single
grep over the whole registry —

```
grep -rhoE 'QStringLiteral\("(stem|mastering|lufs|record|meter|vca|freeze|scan)\.[a-z_]+' src/core/ControlCommands*.cpp
```

— returns **nothing**, so no `stem.`, `mastering.`, `lufs.`, `record.`, `meter.`, `vca.`, `freeze.` or `scan.`
id is registered anywhere.

| # | feature (tree anchor) | its tests | why it is not drivable |
|---|---|---|---|
| 1 | **Stem separation** — offline HTDemucs via ONNX Runtime (`CMakeLists.txt:120` `WANT_STEM_SPLIT`, `stem_split_cli.py`) | `OnnxRuntimeStemSeparatorTest`, `StemExportTest`, `StemJobManagerTest`, `StemModelStoreTest`, `StemSplitPipelineTest` | no `stem.*` group; only route is the `stem_split_cli.py` CLI, outside the socket. The release honesty row is `WANT_STEM_SPLIT OFF` |
| 2 | **Mastering chain / auto-mastering** (`src/core/MasteringChain.cpp`, `MasteringJob.cpp`) | `MasteringTest` | no `mastering.*` group |
| 3 | **LUFS / loudness metering** (`src/core/LufsMeter.cpp`) | `LufsMeterTest`, `LoudnessReportTest`, `MasteringTest` | no `lufs.`/`meter.` group; `export.get_settings` does not expose it |
| 4 | **VCA / edit groups** (`src/core/VcaGroup.cpp`, `include/VcaGroup.h`, `Mixer::createVcaGroup`) | `VcaGroupTest` | no `vca.*` group. `KNOWN-LIMITATIONS.md` records the UI absence: a group can only be created by editing the project file |
| 5 | **PDC + sidechain** (`AudioBusHandle.cpp`, `Rack.cpp`, `EffectChain.cpp`, `Mixer.cpp`) | `PdcMixerTest`, `PhaseDSidechainTest`, `MixerRoutingBackwardCompatTest` | no command group; latency compensation is not readable or settable through the socket |
| 6 | **Routing graph** (`src/core/RoutingGraph.cpp`, `RoutingChainNodes.cpp`) | `RoutingGraphTest`, `RoutingGraphLiveTest`, `RackTest` | no command group |
| 7 | **Audio ports / `AudioBus`** (`AudioPortsModel.cpp`, `AudioBus.cpp`) | `AudioPortsTest`, `AudioPortsModelTest`, `AudioBusTest`, `AudioBusHandleTest`, `PluginAudioPortsTest` | no command group; pin/bus topology is reachable only from C++ |
| 8 | **Multi-track recorder** (`src/core/audio/MultiTrackRecorder.cpp`, registered in `src/core/CMakeLists.txt:312`) | `MultiTrackRecorderTest`, `TwoTrackRecordingHarness`, `TwoTrackAlsaCaptureProbe` | no `record.*` group — and the id that should cover it, **`track.set_arm`, is a registered refusal stub** ("this tree has no record-arm on a song track"). The feature and the command contradict each other |
| 9 | **Plugin scan cache + quarantine** (`PluginScanCache.cpp`) | `PluginScanCacheTest` | no command group; the documented quarantine route is hand-editing a JSON file |
| 10 | **Crash reporter** (`CrashReporter.cpp`) | `CrashReporterTest` | no command group |
| 11 | **Note random / transform / slide notes** (`NoteRandom.cpp`, `NoteTransform.cpp`) | `NoteRandomTest`, `NoteTransformTest`, `SlideNotesTest`, `MidiProbabilityPersistenceTest` | no command group; `note.*` covers add/move/remove/resize/select/velocity_set/expression_\* only |
| 12 | **MPE pressure + timbre** — stored on the `Note` and editable via `note.expression_set` | `MpeExpressionTest`, `MpeNoteStorageTest`, `MpeInputPathTest` | **drivable but inert**: `docs/MPE.md` §4 and `KNOWN-LIMITATIONS.md` state only the pitch axis reaches playback; pressure and timbre "reach no instrument" |
| 13 | **Lua API surface beyond the bound objects** | `ScriptBindingsTest`, `ScriptEngineTest`, `ScriptStabilisationTest` | `script.run`/`script.list` are drivable, but the binding deliberately reaches no mixer channel, effect chain, plugin, send, PDC, automation clip, controller or settings object |
| 14 | **Autosave / project recovery** — `project.restore_revision` exists | `ProjectRecoveryTest`, `ProjectOpenIntegrityTest` | drivable (the one in-tree *recovery* feature that is), but **referenced by no behavioural test** (Table A: `project` 3/4) |
| 15 | **Real-time-safety whole-tree verification programme** | none — no test, gate or tool implements it | *not in the tree at all*; see Table C |
| 16 | **Golden-audio integration programme** | none | *not in the tree at all*; see Table C |

Three more ids are **registered but always refuse**, so their features are absent while the id suggests
otherwise: `track.set_arm` (Table B #8), `mixer.set_pan` ("this tree has no pan on a mixer channel"),
`automation.mode_set` ("this build has no automation modes"). A caller sees a declared, schema'd, reversibility-
classified command that cannot ever succeed.

---

## 6. Table C — the 0.3.0 engine set: in the tree, or not

Source of the item list: `WAVE-1-BRIEFS.md` §"The release ladder" and `V0.3-V0.5-RELEASE-LADDER.md`.
Each row was checked with a literal `grep -rniI '<pattern>' src include tests` at `ddf5f171d`.

### 6.1 In the ladder's 0.3.0 set and **not in the tree at all** (0 hits outside pinned/stale prose)

| ladder item | evidence at this tip |
|---|---|
| **freeze / bounce-in-place** | `grep -rniI 'freeze'` → 3 hits, all prose in `ControlServerSocket.cpp`, `Mixer.cpp`, `RenderManager.cpp`. `grep -rlniI 'bounce'` → 0 hits. **Absent.** |
| **groove pool + quantise** | `grep -rlIw 'groove'` → **0 hits**. (Swim/quantise hits are session-view launch quantisation, a different feature.) **Absent.** |
| **punch in / out** | `grep -rniI 'punch'` → only "Sustain Punch", a Sfxr plugin parameter in `tests/reference/Sfxr/`. **Absent.** |
| **sample-accurate automation** | `grep -rnI 'sample-accurate'` → 3 prose hits (`Song.h`, `SessionScheduler.h`, `ControlCommandsSessionLaunch.cpp`) and `include/AudioEngine.h:304` which describes the **current** behaviour as "non-sample-accurate automation". **Absent.** (The panner that `AudioEngine.h` names is a separate, earlier fix.) |
| **real-time-safety whole-tree verification programme** | no test, gate or tool. `grep -rliI 'real-time safety' tests docs tools` → prose in `tests/CMakeLists.txt`, `docs/CONVENTIONS.md`, `docs/PLUGIN-SCAN-CACHE.md` only. **Absent.** |
| **golden-audio verification programme** | `grep -rliI 'golden'` across the whole tree → **one file**, the pinned 0.1.0 `docs/specs/AGENT-SURFACE-INVENTORY.md`. **Absent.** |
| **`ARCH-2`** — the control registry as a `zene::api` boundary | `grep -rniI 'zene::api\|namespace zene' src include` → **0 hits**. The registry is `lmms::ControlRegistry`. **Absent.** |
| **plugin chains as reusable presets** | `grep -rliIE 'chain.?preset\|EffectChainPreset'` → 0 hits. (`rack.add_chain` adds a *rack* chain, which is not this item.) **Absent.** |
| **tempo-map export / SMF cross-DAW interchange** | `grep -rniI 'smf\b\|standard midi file' src include` → **0 hits**. (`transport.tempo_map_*` reads and edits the map in-session only.) **Absent.** |
| **MIDI controller auto-reconnection** | `grep -rniI 'auto-reconnect\|autoreconnect' src include docs` → **0 hits**. **Absent.** |
| **pitch-preserving time-stretch** | no `src/plugins/GranularPitchShifter`; `ls src/plugins \| grep -i 'pitch\|stretch\|granular'` → nothing. The name appears only as a `tests/reference/` render directory. **Absent.** |
| **transient / BPM / key detection on import** | `grep -rniI 'bpm.*detect\|key.*detect\|detectBpm\|detectKey'` → 0 hits (`bpm` hits are tempo entry and the ALSA queue). **Absent.** |
| **retrospective MIDI capture** | `grep -rniI 'retrospective'` across `src include tests docs` → **0 hits**. **Absent.** |
| **retrospective audio capture** | same grep → **Absent.** |
| **project collection / archive, hashing, relink** | `grep -rniI 'relink\|project archive\|projectarchive'` → only the word "relinks" in the mutation gate's prose. **Absent.** |
| **DAWproject import / export** | `grep -rniI 'dawproject'` across `src include tests tools docs` → **0 hits**. **Absent.** |
| **controller soft-takeover, LED feedback, mapping templates** | `grep -rniI 'soft.takeover\|softTakeover\|LED feedback\|mapping template'` → **0 hits**. Note the ladder says "learn path landed": `midi.learn_toggle` **is** registered and MIDI-learn tests exist — so this row is **half in**: learn yes, soft-takeover/LED/templates no |
| **chord track / chord detection / progression tools** | no `ChordTrack` in `src/core` or `include`; "chord" hits are `InstrumentFunctions`. **Absent.** |
| **folder tracks** | `ls src/core include \| grep -i folder` → nothing. **Absent.** (The ladder's second half, layout/workspace presets, is 0.5.0 by its own record.) |

### 6.2 In the ladder's 0.3.0 set and **in the tree, drivable through the socket**

These items are closed on the evidence of Table A, not on prose:

| ladder item | command group | proof |
|---|---|---|
| tempo map, tempo/time-signature changes | `transport.tempo_map_*` (5 ids) | `ControlTempoMapCommandsTest` (7/10 of the group) |
| recording crash recovery | `project.restore_revision` + `ProjectRecovery.cpp` | `ProjectRecoveryTest`; the id itself has no behavioural test |
| racks, rack macros, key/velocity zones | `rack.*` (12 ids) | `RackMacrosTest` (12/12), `RackZonesTest` (4/12) |
| clip/object effects — fades, crossfades, clip gain | `clip.set_fade`, `clip.set_gain`, `clip.crossfade` | `ClipEditsTest`, `ClipFadesRenderTest` |
| phase-locked multitrack edit groups (comping half) | `comp.*` (7 ids) | `TakeLaneCompTest` (7/7); `TakeLaneTest` |
| modulation layer + per-note expression | `modulator.*` (7) + `note.expression_*` (3) | `ControlModulatorCommandsTest`, `ControlNoteExpressionCommandsTest`, `ModulationLayerTest` |
| Ableton-Link session sync | `link.*` (5) | `ControlLinkCommandsTest`, `ControlLinkSync` |
| browser tags / search / peak cache | `browser.*` (6) | `ControlBrowserCommandsTest` (6/6) |
| bounded, coalescing undo | `control.undo_depth` etc. | `UndoBoundsTest` |
| export dither + SRC quality | `export.*` (3) | **no registered test** — see §3.3 |
| warp markers | `warp.*` (5) | `ControlWarpCommandsTest` |
| Session View | `session.*` (11) | `control-session-m1.py` 6/11 only; 5 ids untested |
| WASM DSP sandbox | `wasm.*` (6, `#ifdef LMMS_HAVE_WASM`) | `ControlWasmSandbox`; compiled out of every release build |

**Deliberately excluded, unchanged by the ladder** (`V0.3-V0.5-RELEASE-LADDER.md`): item 17 (project preview
renders) and **OSC**. Neither is in the tree and neither is a gap.

---

## 7. What I could not determine

Stated plainly, because the next wave is scoped from this list.

1. **Whether the tree's test suite is currently green.** I did not build (no build slot; §WAVE-1-BRIEFS disk
   rule). Every `tested?` verdict in Table A is a statement about **which registered test names a command and
   asserts on it**, not about whether that test passes at this tip. `ControlWarpCommandsTest`,
   `ControlTempoMapCommandsTest` and `ControlModulatorCommandsTest` are each registered *and* cited by the
   release notes' A16 section as the detector that caught a dropped row — so I have indirect evidence that
   they run, but I did not execute them.
2. **The true A16 row count.** Three figures exist in the tree (139 / 142 / 127 — §1.3). The 150 my counting
   reduces the test constant to matches the 150 distinct ids from two independent sources, but I cannot say
   which of 139/142/127 the release notes are supposed to quote without building.
3. **Whether the duplicated reversibility rows are harmless.** Rows for the same id exist in **both**
   `ControlReversibilityTable.cpp` and `…Passive.cpp` (**24** ids: the read-only inspectors `app.version`
   through `warp.list`), and again for the four `transport.tempo_map_*` ids in
   `ControlReversibilityTable.cpp` and `…Action.cpp` — **28** duplicated ids out of 150. `insertRows` writes
   into a `QHash` keyed by command, so the behaviour is last-write-wins and looks fine; but **178** row
   invocations is not the entry count, and I could not reconcile that arithmetic with the asserted constant
   (142) or the documented one (139) without running `ReversibilityContractTest`.
4. **What a live 0.3.0 instance would expose through the MCP bridge.** The mechanism (one tool per
   `commands_list` id, at request time) is read from `registry.py`, and no per-feature bridge work is needed.
   I did not start a 0.3.0 instance against the registered bridge's socket — other lanes' instances are
   running on `/tmp/snap.sock`, and the brief forbids disturbing them. So "all 144 non-wasm ids become tools"
   is a **mechanism** claim, not a measured one. What *is* measured is §4.3: **70 ids today**.
5. **Whether the registered MCP bridge copy is meant to be the 0.3.0 one.** The two copies differ. I did not
   find a record stating which is canonical, and the registered one is not in the audited worktree.
6. **`tests/evidence/export-src-dither/transcript.txt`** may be the export group's intended proof under the
   charter's criterion 3, but criterion 3 names *`docs/`*, and this file is under `tests/`. I record the fact
   and do not adjudicate it.
7. **`docs/specs/AGENT-SURFACE-INVENTORY.md`** is the only consolidated prior inventory and is pinned at
   0.1.0-alpha. Roughly ten of its rows (no command registry, `mcp-zene-control` absent from the tree,
   `WarpMarker` 0 hits, `TakeLane`/`comping` 0 hits, no modulation layer, no racks, no Link, no browser tags)
   are now false. It was not re-derived for this matrix; it is cited only as history.

---

## 8. The gap list, in one place

- **28 groups, 150 ids** registered. **9 ids with no registered-test reference**: `export` ×3,
  `session` ×5, `midi.learn_toggle` ×1.
- **1 group with no test at all**: `export` — its only proof is an unregistered transcript.
- **10 groups with no MCP tool today**: `browser`, `comp`, `export`, `link`, `modulator`, `rack`, `session`,
  `telemetry`, `warp`, `wasm`; **74 ids** in total.
- **3 registered-but-always-refusing stubs**: `track.set_arm`, `mixer.set_pan`, `automation.mode_set`.
- **16 features in the tree that are not drivable through the socket** (Table B).
- **19 ladder items not in the tree at all** (Table C §6.1) — 18 entirely absent, plus
  controller soft-takeover/LED/mapping-templates which is *half in* (the MIDI-learn path landed,
  `midi.learn_toggle` is registered). Two of the 19 — the real-time-safety and golden-audio verification
  programmes — are named in `WAVE-1-BRIEFS.md` as *already-open 0.3.0 work*.

---

## 9. Re-measurement, 2026-09-13 — the release tree's own counts, both figures dated

The counts above are the **audit tip's own** and stay as written. A later lane re-measured the release tree by
the same method — counting the registry source, not a document — because `docs/FEATURE-LIST-0.3.0.md`,
`V0.3-SCOPE-CORRECTIONS.md` and the coverage-matrix copies were quoting 28/150, 30/154 and 31/170 against each
other. Read-only (`git grep` against the ref); the `zene-030` worktree was not touched.

| base | ids | groups, as defined |
|---|---|---|
| `ddf5f171d` (this matrix's tip, `030/audit`) | **150** = 144 `.id =` assignments + 6 helper-built `wasm.*` | **28** = 27 id prefixes **+** the helper-built `wasm` group |
| `334790219` (`release/0.3.0`) | **170** = 164 + 6 | **31** id prefixes **+** the helper-built `wasm` group = 32 group names |
| `01b99753a` (`release/0.3.0`, 2026-09-13) | **170** = 164 + 6 | as above |

```
git grep -h -o '\.id = QStringLiteral(' <ref> -- 'src/core/ControlCommands*.cpp' | wc -l          # 164
git grep -h -o '\.id = QStringLiteral("[^"]*\.' <ref> -- 'src/core/ControlCommands*.cpp' \
  | sed 's/.*QStringLiteral("//; s/\.[^.]*$//' | sort -u                                          # 31 prefixes
```

The groups added against this matrix's 28 are **`bounce` (1 id), `freeze` (3), `groove` (7), `record` (6)** and
three more `transport` ids (`transport.punch_*`) — 27 + 4 = 31 prefixes, with the id total up by 20 (17 new
ids in those four groups plus the 3 punch ids).

**One base difference is left unreconciled rather than smoothed:** this matrix's **28 includes** the
helper-built `wasm` group, while the release-tree figure of **31 excludes** it — so the two are one apart as
*group* counts, not three. Whether the compile-gated `wasm` group counts as a group of the surface is a
definition no document settles, and neither figure is changed here because of it. Stated in both directions so
a reader does not read one as an error: the ids (150 → 170) agree on one base; the groups (28 vs 31/32) do not.
