# LANE STATE — 030/routing-surface (feature rows 27, 28, 29)

- **Worktree:** `/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/lmms-fl-research/zene-030/wroute`
- **Branch:** `030/routing-surface`, branched from **3956ef589** (`release/0.3.0` tip when this lane started)
- **Tip sha:** `a3dfbdcd5` — three commits: `c20eca2a1` (the feature), `8d51242de` (the gate fixes),
  `a3dfbdcd5` (the measured corrections the transcripts found)
- **Logs:** `/home/kruzzzzy/zene-030-wroute-3956ef589/` (never /tmp)
- **Not done, deliberately:** no merge, no push, no `commands_snapshot.json` regeneration (the parent's
  MERGE-time step), no edit to another lane's `docs/FEATURE-LIST-0.3.0.md`.

## What is done — with the command that shows it

### 1. Eleven command ids in five groups

| id | A16 class | reversibility |
|---|---|---|
| `pdc.report` | `not_mutating` | writes nothing |
| `routing.get_state` | `not_mutating` | writes nothing |
| `bus.list` | `not_mutating` | writes nothing |
| `bus.create` | `true_inverse` | recorded action: one step deletes the bus it created |
| `bus.remove` | `snapshot` | **`reversible: false`** — `control.undo` FAILS, typed `irreversible` |
| `port.get_state` | `not_mutating` | writes nothing |
| `port.set_pin` | `snapshot` | recorded inverse IS the command with the previous pin (`applies: command`) |
| `mixer.route_to` | `true_inverse` | recorded action (delete the route, or write the old amount back) |
| `mixer.send_to` | `true_inverse` | same |
| `mixer.sidechain_to` | `true_inverse` | same (recorded amount + tap point) |
| `mixer.route_remove` | `true_inverse` | recorded action: re-creates the route with its amount + tap |

### 2. Verification — every number below is a real run on this branch

**ctest (`bash tools/local-ci.sh --build-dir build --jobs 2`, exit code 8):**

| test | result |
|---|---|
| `ControlPdcCommands` (124) | **Passed** 2.09 s |
| `ControlRoutingCommands` (125) | **Passed** 2.13 s |
| `ControlBusCommands` (126) | **Passed** 2.09 s |
| `ControlPortsCommands` (127) | **Passed** 2.12 s (exit 77 skip path NOT taken: the pin write is measured) |
| `agent_surface` (137) | **Passed** 3.20 s — the sweep exercises every non-allowlisted id headless, so all 11 survive junk arguments |
| totals | **142 of 144 passed (99%)** |

The two failures, exactly:

1. **`ControlCommandsSnapshot` (144) — Failed, EXPECTED.** The binary declares 11 ids the committed
   `tools/mcp-zene-control/zene_control/commands_snapshot.json` does not carry. Regenerating it is the
   parent's MERGE-time step (`python3 tools/mcp-zene-control/snapshot_commands.py --socket <sock>` against a
   live instance of the merge tip, then commit the JSON). Doing it here would ship a snapshot that is already
   wrong. This red is a collected red, not a stop.
2. **`ControlLinkSync` (132) — Failed under `ctest -j2`, "A still lists 2 peers after B left"; NOT this lane's
   and NOT reproducible.** Re-run in isolation twice:
   `cd build/tests && ctest -R ControlLinkSync --output-on-failure > <log> 2>&1; echo EXIT=$?` → **EXIT=0 both
   times, "100% tests passed, 0 tests failed out of 1"**. It is a peer-counting flake in a multicast test that
   starts two instances while a sibling lane was building on the same box; no file of this lane touches
   `link.*`, and it did not fail on re-run.

**Gates, unpiped:**

| gate | command | exit |
|---|---|---|
| 4 complexity | `bash tests/complexity-gate.sh --check` | **0** (`PASS (check mode: no regressions)`) |
| 7 file length | `bash tests/file-length-gate.sh --check` | **0** |
| 8 duplication | `bash tests/duplication-gate.sh` | **0** — `duplicated lines 2.12% (budget 5%)`, 403 fork sources scanned |
| 9 fork sources | `bash tests/fork-sources-gate.sh` | **0** — `scanned 1456 …, 403 fork-NEW, 1057 inherited, 34 tooling, 0 stale` |
| 6 upstream regression | `bash tests/no-upstream-regression-gate.sh` | **0** — `409 changed path(s) declared; the ledger holds 424 entries` |
| unregistered tests | `bash tests/unregistered-tests-gate.sh` | **0** — `126 scanned (124 registered, 2 declared-not-built, 4 helpers)` |
| evidence | `bash tests/evidence-gate.sh` | **0** — `6268 file(s) scanned, 0 refused` |
| release honesty | `bash tests/release-honesty-gate.sh --header build/lmmsversion.h --artifacts build` | **0** — all 6 documented features match the build |
| manifests | each file's own `Regenerate with` recipe, diffed against its entry list | **0, empty diff** (both REPRODUCE) |
| whole-scoreboard | `bash tests/run-all-gates.sh` | see *run-all-gates* below |

### 3. What the transcripts measure (all four green against the real binary)

- **`pdc.*` (15 checks).** `pdc.report` publishes the mixer's own graph: `total_latency_frames` 0,
  `delay_line_capacity_frames` 16384 (`LatencyCompensation::MaxFrames`), the master first with its
  `is_master`/`is_bus` flags, `sidechain.supported` true with the four `SidechainTapPoint` names, a real
  `mixer.send_to` appearing as a route with its amount and pre-fader flag, the sender's send count growing by
  exactly one, a sidechain send reported with `tap_point: pre_fx` / `amount: 0.25` / `deferred: false` and its
  receiver's `sidechain_receive_count` 1, one `control.undo` taking it back out, and eight typed refusals
  that change nothing.
- **`routing.*` (30 checks).** The engine's own rules, measured: a track with no effects reports an empty
  chain graph; a **built-in effect keeps the plain effect loop** (`routes_through_graph: false`, graph empty)
  because `EffectChain::rebuildRoutingGraph` returns early for a chain whose devices have audio-ports models
  (`src/core/EffectChain.cpp:89`) — and the SAME device answers `port.get_state` with a real matrix, which is
  the evidence for why; the **rack's graph** is the live non-empty one: two added chains build five nodes
  (`chain_input`, `rack_sum`, three `rack_chain`), six connections, output node 1, `prepared: true` at
  `frames: 256`, processing order `[0, 2, 3, 4, 1]` (sum node last); removing one chain re-wires it to four
  nodes / four connections and removing the other leaves it unwired; plus all four mixer routing verbs, the
  bus pre-fader default, `route_remove` + undo restoring the amount, and the cycle/self-route refusals.
- **`bus.*` (15 checks).** A fresh mixer has no bus; `bus.create` makes one the engine calls a bus and
  `bus.list` finds it; the mixer's own fader reaches it and undoes; `bus.create`'s undo removes it; the A16
  record is `true_inverse`/`reversible: true`; `bus.remove` records `snapshot`/`reversible: false` and
  `control.undo` **fails typed `irreversible`**; `bus.remove` refuses ch-0 before writing and is `not_found`
  for a channel that does not exist.
- **`port.*` (11 checks).** A built-in effect reports an initialized `AudioPortsModel` with both matrices
  (shape, names, pins); `port.set_pin` flips a real pin (`previous: true` → `enabled: false`, the pin gone
  from the matrix), records the `snapshot` row whose inverse is the command, and `control.undo` puts the pin
  back; a `processor_channel` past the matrix is refused typed and no pin moves; every malformed request is
  typed.

### 4. The nine touch-points

1. New TUs: `ControlCommandsPdc.cpp`, `ControlCommandsRouting.cpp`, `ControlCommandsBus.cpp`,
   `ControlCommandsPorts.cpp`, `ControlCommandsMixerRoutes.cpp`, `ControlMixerSupport.cpp` +
   `include/ControlMixerSupport.h` (the shared engine half of the routing verbs)
2. `include/ControlRegistryGroups.h` — 6 declarations
3. `src/core/ControlRegistry.cpp:494` — ONE call, `registerRoutingSurfaceCommands(registry)`
4. `src/core/ControlReversibilityTableRouting.cpp` — **11 rows**, this lane's rows as one group whatever
   their class, joined by `reversibilityRowTable()` like the folder-tracks group's; histogram moved
   185→196 rows / 99→104 / 14→16 / 68→72 and the test's base constant 183/99/14/4/66 → 194/104/16/4/70
5. `src/core/CMakeLists.txt` — 7 new `core/*.cpp`
6. `tests/CMakeLists.txt` — 4 new ctests, `QT_QPA_PLATFORM=offscreen`, `ControlPortsCommands` with
   `SKIP_RETURN_CODE 77`
7. `tests/fork-sources.txt` (8 C++ paths + 4 `.py` names, and the 4 names added to every python pathspec
   line) and `tests/all-sources.txt` (8 paths) — both verified REPRODUCES
8. Four transcripts + `docs/KNOWN-LIMITATIONS.md` (4 bullets) + `docs/RELEASE-NOTES-v0.3.0-alpha.md`
   (a section, one UI-absence line per group, the stated bounds, the histogram)
9. **The offline snapshot is NOT regenerated** — merge-time step (see the red above)

### 5. Limits stated rather than hidden

- No `pdc` setter: `Mixer::updateLatencyCompensation()` recomputes every edge's delay per period.
- No `RoutingGraph` editor: the class's threading contract forbids live topology edits and a chain's graph is
  derived; the settable topology is the mixer's.
- The chain graph is normally EMPTY in this tree (every built-in device is `AudioPlugin`-derived); the live
  graph is the rack's. Measured, and stated in all three places.
- `bus.remove` is not reversible; `port.set_pin` needs a device that HAS a matrix (in this build they all do;
  a build where none does reports ctest *Skipped*, never *Passed*).

## Per-row replacement text for rows 27 / 28 / 29

`docs/FEATURE-LIST-0.3.0.md` is owned by another lane this fire and was NOT edited. Replace lines
**184, 185, 186** (the §3 table) with these three rows verbatim:

**Row 27 — line 184:**

```
| 27 | PDC and sidechain | none yet | **drivable** — in the tree with registered tests (`PdcMixerTest`, `PhaseDSidechainTest`, `MixerRoutingBackwardCompatTest`); **readable** through `--control-socket` with `pdc.report` (the mixer's published total latency, every channel's alignment point (`Mixer::channelInputLatency`) and chain latency, the compensation applied at every send, the direct track inputs, and whether sidechain routing exists with each route's tap point and deferred flag), and the sidechain half is settable with `mixer.sidechain_to` / `mixer.route_remove`; **not settable as a value** by decision — `Mixer::updateLatencyCompensation()` recomputes every edge's delay once per period, so a command that wrote one would be overwritten by the next period. Proof: `tests/control-pdc-commands.py` (ctest `ControlPdcCommands`, 15 checks). **Bound:** the published number is 0 unless a device reports latency (`Effect::latencyFrames()` defaults to 0; only the WASM effect overrides it), so the arithmetic of a nonzero delay is proven by `PdcMixerTest` and the surface by the transcript | audit Table B #5, and this row's own "neither readable nor settable" sentence |
```

**Row 28 — line 185:**

```
| 28 | Routing graph | none yet | **drivable (read)** — in the tree with registered tests (`RoutingGraphTest`, `RoutingGraphLiveTest`, `RackTest`) and live in the audio path; the graph is **readable** through `--control-socket` with `routing.get_state` (nodes with the engine's own type names, connections, the cached topological processing order, the output node, `EffectChain::routesThroughGraph`, and a mixer channel's `Rack` graph). **No command EDITS a graph, by decision**: `include/RoutingGraph.h`'s threading contract forbids live topology edits and names the atomic plan swap it deliberately does not implement, and a chain's graph is DERIVED (`EffectChain::rebuildRoutingGraph` re-wires it from the effect list, so a hand-wired edge would be discarded by the next `plugin.load`). **Measured scope:** a chain whose devices HAVE audio-ports models keeps the plain loop (`EffectChain::rebuildRoutingGraph` returns early, `src/core/EffectChain.cpp:89`), and every built-in device is `AudioPlugin`-derived (`DefaultEffect`), so the chain graph is normally EMPTY and the graph with live prepared nodes is the **rack's** — `tests/control-routing-commands.py` (ctest `ControlRoutingCommands`, 30 checks) measures it: two added chains = five nodes, six connections, output node 1, prepared at the engine's block size. The settable topology in this release is the MIXER's: `mixer.route_to` / `mixer.send_to` / `mixer.sidechain_to` / `mixer.route_remove`. The patcher GUI is missing and is out of scope (§ *Out of scope*) | audit Table B #6 |
```

**Row 29 — line 186:**

```
| 29 | Audio ports / `AudioBus` | none yet | **drivable** — in the tree with five registered tests (`AudioPortsTest`, `AudioPortsModelTest`, `AudioBusTest`, `AudioBusHandleTest`, `PluginAudioPortsTest`); the **bus topology** is drivable with `bus.list` / `bus.create` / `bus.remove` (a bus is a `MixerChannel` with `is_bus`, so its fader and routing are `mixer.set_volume` and the routing verbs — there is no `bus.set_*`), and the **pin matrix** is drivable with `port.get_state` / `port.set_pin` (`AudioPortsModel::Matrix::setPin`, the PinConnector view's own write, validated before it writes). Proof: `tests/control-bus-commands.py` (ctest `ControlBusCommands`, 15 checks, which measures BOTH A16 answers: `bus.create` is one undoable step, `bus.remove` makes `control.undo` fail typed `irreversible`) and `tests/control-ports-commands.py` (ctest `ControlPortsCommands`, 11 checks — the pin write is MEASURED, not skipped). **Bounds:** `bus.remove` is not reversible (nothing recreates a channel With state); `port.set_pin` needs a device that HAS an `AudioPortsModel` — in this tree every built-in effect is `AudioPlugin`-derived (`DefaultEffect`) and so has one, and a build where none does reports the transcript's ctest **Skipped** (never Passed) | audit Table B #7 |
```

**And the three §"gap" rows at lines 399, 400, 401:**

- line 399 → `| 5 | PDC + sidechain | `PdcMixerTest`, `PhaseDSidechainTest`, `MixerRoutingBackwardCompatTest` | readable with `pdc.report` (sidechain drivable with `mixer.sidechain_to`); the compensation is not settable as a value by decision (recomputed per period) — `tests/control-pdc-commands.py` | 27 |`
- line 400 → `| 6 | Routing graph | `RoutingGraphTest`, `RoutingGraphLiveTest`, `RackTest` | readable with `routing.get_state`; no graph EDIT by decision (threading contract + the graph is derived); the live graph is the rack's — `tests/control-routing-commands.py` | 28 |`
- line 401 → `| 7 | Audio ports / `AudioBus` | `AudioPortsTest`, `AudioPortsModelTest`, `AudioBusTest`, `AudioBusHandleTest`, `PluginAudioPortsTest` | drivable: `bus.list` / `bus.create` / `bus.remove`, `port.get_state` / `port.set_pin` — `tests/control-bus-commands.py`, `tests/control-ports-commands.py` | 29 |`

If the audit lane's numbering has moved, replace by ROW TITLE ("PDC and sidechain", "Routing graph",
"Audio ports / `AudioBus`"), not by line number, and touch nothing else in that file.

## Files created / modified

Created: `include/ControlMixerSupport.h`, `src/core/ControlMixerSupport.cpp`,
`src/core/ControlCommandsPdc.cpp`, `src/core/ControlCommandsRouting.cpp`, `src/core/ControlCommandsBus.cpp`,
`src/core/ControlCommandsPorts.cpp`, `src/core/ControlCommandsMixerRoutes.cpp`,
`src/core/ControlReversibilityTableRouting.cpp`, `tests/control-pdc-commands.py`,
`tests/control-routing-commands.py`, `tests/control-bus-commands.py`, `tests/control-ports-commands.py`,
this file.

Modified: `include/ControlRegistryGroups.h`, `include/ControlReversibility.h`, `src/core/CMakeLists.txt`,
`src/core/ControlRegistry.cpp`, `src/core/ControlReversibilityTable.cpp`, `tests/CMakeLists.txt`,
`tests/fork-sources.txt`, `tests/all-sources.txt`, `tests/src/core/ReversibilityContractTest.cpp`,
`docs/KNOWN-LIMITATIONS.md`, `docs/RELEASE-NOTES-v0.3.0-alpha.md`.

Not touched: `docs/FEATURE-LIST-0.3.0.md`, `tools/mcp-zene-control/zene_control/commands_snapshot.json`,
any other lane's worktree, `release/0.3.0`.

## Next exact command for the parent

```bash
# after merging this branch into release/0.3.0 and rebuilding:
cd <merge-tip-build>/tests && ctest -R ControlCommandsSnapshot --output-on-failure   # now red: 11 new ids
QT_QPA_PLATFORM=offscreen <merge-tip-build>/zene --control-socket /tmp/z.sock &
python3 tools/mcp-zene-control/snapshot_commands.py --socket /tmp/z.sock
git add tools/mcp-zene-control/zene_control/commands_snapshot.json
```
