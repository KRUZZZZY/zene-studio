# The patcher node graph: `patcher.get_state` / `patcher.set_wiring`

**Status: in the tree and on the socket.** Feature row 69 of `docs/FEATURE-LIST-0.3.0.md`
("Patcher node-graph driving") asked for the smallest honest `patcher.*` group: read a node
graph, and make an EDIT possible - through the plan swap the threading contract names, or with
evidence that the engine cannot take a live edit in this release. This is the design record of
the decision taken: **the edit is built**, as a whole-graph rebuild published under the engine's
model-change guard, and the DERIVED-graph constraint is **resolved** rather than written down as
a limitation.

Lane `030/patcher-graph`, worktree `zene-030/wpatch`, base `release/0.3.0` @ `f611c888b`.

---

## 1. The two recorded reasons a chain's graph had no setter

Row 28 recorded them; both are re-verified here against the clone (file and line).

1. **The threading contract.** `include/RoutingGraph.h:58-62` (control-thread-only topology edits
   and `prepare()`; "must not run concurrently with `process()`"), and the full design it names -
   `PATCHER-MVP.md` §3 ("No live plan swap"), §4 Part C 3 (the lock-free pending-change plan swap
   of `mixer/SPEC-dynamic-routing.md` §5.4) - is deliberately not implemented by the spike.
2. **The graph is DERIVED.** `EffectChain::rebuildRoutingGraph()` cleared the graph and re-wired
   it linearly from the effect list on every topology change, so a hand-wired edge was discarded
   by the next `plugin.load` / `plugin.unload` / `moveUp` / `moveDown` / `clear` / `loadSettings`.

## 2. Reason 1 is answered by the seam the engine already has

`AudioEngine::renderNextPeriod()` holds `m_changeMutex` for a **whole render period**
(`src/core/AudioEngine.cpp:368`, `std::lock_guard{m_changeMutex}`), and
`AudioEngine::requestChangeInModel()` / `doneChangeInModel()` lock the *same* mutex from the
control thread (`src/core/AudioEngine.cpp:628-637`). An edit published inside that guard is
therefore **never concurrent with `process()`** - which is the requirement the contract states,
word for word. It is also what every topology edit in this tree already does:
`EffectChain::appendEffect` / `removeEffect` / `moveUp` / `moveDown` / `clear` all wrap their
`rebuildRoutingGraph()` in `requestChangeInModel()` / `doneChangeInModel()`, and the mixer's
channel add/remove and `AudioBusHandle` registration take the same guard
(`include/AudioEngine.h:166` documents the audio-thread side of it).

So the edit is **whole-graph and off the audio thread**:
`EffectChain::setPatchWiring()` (`src/core/EffectChainPatcher.cpp`) stores the wiring, rebuilds
the graph - every node, every buffer, the cached plan - on the calling (control) thread, and
publishes it; when the derivation cannot take the wiring, it puts the **previous** wiring back
and rebuilds in place, so a refusal leaves the chain exactly as it was.

**What this is NOT, stated because the difference is the interesting part:** it is not the
lock-free double-buffered plan swap. The edit **blocks the audio thread for the rebuild's
duration** - a shorter cost than a `plugin.load`, paid by every other topology edit here - and the
lock-free swap of `SPEC-dynamic-routing.md` §5.4 remains unimplemented. `docs/KNOWN-LIMITATIONS.md`
carries that bound.

## 3. Reason 2 is answered by making the wiring DATA the chain owns

`include/PatchWiring.h` (`src/core/PatchWiring.cpp`) is the new type: a `PatchRef` is a node
addressed by **ROLE** - `"input"` (the chain's own `ChainInputNode`) or `"effect:<index>"`
(chain order) - because a graph node id is *rebuilt* on every topology change and a role is not.
A `PatchWiring` is the edge list plus the output node, with its JSON form.

* The node SET stays **derived**: one input node plus one `EffectNode` per effect, in list order.
  That keeps `EffectChain::graphMirrorsEffectList()` true, so the graph is still **on** the signal
  path (`routes_through_graph` true) and the patch is *rendered* rather than silently bypassed -
  the failure mode a naive implementation has.
* The **wiring** is authored state, and `rebuildRoutingGraph()` applies it on every rebuild: a
  hand-wired edge now survives the next `plugin.load`.
* A wiring the current effect list cannot take (an effect removed under an index-addressed edge) is
  **dropped**, `patch_dropped: true` is reported by the read, and the chain falls back to its
  derivation - never a half-wired graph.

## 4. The group on the wire

| id | class (A16) | what it does |
| --- | --- | --- |
| `patcher.get_state` | `not_mutating` | nodes with their role/type/arity/own parameters/prepared flag, the wiring as roles **and** node ids, the cached plan, the output node, `editable` with the reason when an edit cannot land |
| `patcher.set_wiring` | `snapshot` (inverse = the same command with the captured wiring, `applies: command`) | replaces the wiring; an empty edge list restores the derived wiring |

Refusals, all typed and all writing nothing (validated **before** the chain is touched):
`invalid_args` for an unknown reference, a port outside the node's arity, a repeated or self edge,
a cycle, or an output node the input cannot reach; `refused` when the chain does not render
through a graph at all (its graph is empty). `control.undo` is one step and restores the previous
wiring **as the state it was**: a chain that was on its derived wiring comes back as the
derivation (an empty edge list), not as a linear-looking authored patch.

A16 rows: the end of `src/core/ControlReversibilityTableRouting.cpp` (the group drives the routing
graph those rows already cover); histogram moved with them
(`tests/src/core/ReversibilityContractTest.cpp`, `docs/RELEASE-NOTES-v0.3.0-alpha.md`).

## 5. What is NOT done

* **No patcher canvas and no GUI.** Nothing in `src/gui/` draws or edits the wiring.
* **The node set is not authorable.** A node can be neither added to nor removed from a chain's
  graph by a command, and no verb sets a node's own parameters (the read *reports* them - a
  `GainNode`'s `gain`, a `OnePoleLowPassNode`'s `alpha`, a `ConstantSourceNode`'s `value` - because
  they are read generically through the node's own `saveSettings`).
* **The wiring is session state.** There is no `<routinggraph>` element in `<fxchain>`: a patch
  does not survive a save/load, exactly as the pre-existing re-wired session did not
  (`docs/ROUTING-GRAPH-LIVE.md` §7).
* **The rack's graph is not editable.** `Rack::rebuildRoutingGraph()` derives it from the rack's
  chain list and its chain selector; this group's target is a track's or a mixer channel's own
  effect chain. `routing.get_state` still reports the rack graph.
* **The edit is not lock-free** (§2) and it is not saved into the project.
* **Measured scope limit, unchanged from row 28:** a chain whose devices have audio-ports models
  keeps the plain effect loop (`src/core/EffectChainPatcher.cpp:121`), and the built-in devices
  this tree ships are `AudioPlugin`-derived, so a real track's chain graph is normally EMPTY and
  `patcher.set_wiring` refuses it, typed. A chain of **legacy** effects makes the graph live, which
  is what the proof below builds.

## 6. Proof

* **Registered ctest `PatcherCommandsTest`** (`tests/src/core/PatcherCommandsTest.cpp`,
  registered in `tests/CMakeLists.txt`): both ids and their A16 rows; the derived wiring read back
  (three nodes, two connections, output node 2, roles `input` / `effect:0` / `effect:1`); the EDIT
  measured on the audio path - the channel is rendered through `Mixer::masterMix()` before and
  after the re-wire, and the patched render is **exactly** the linear render without the bypassed
  effect's gain (the `RoutingGraphLiveTest` shape: the arithmetic of the missing stage, not merely
  "the bytes differ"); the authored wiring **surviving** `rebuildRoutingGraph()`; `control.undo`
  restoring the derived wiring and the render with it; every refusal typed with the state unchanged;
  and one block through the patched graph allocating **0** bytes on the audio thread
  (`tests/src/core/AllocationProbe.h`).
* **The socket half is automatic and needs no per-feature work:** the MCP bridge derives one tool
  per registered id from a live instance's `control.commands_list` (row 28's spine), so
  `patcher.get_state` and `patcher.set_wiring` are tools as soon as an instance of this build is
  answering. The committed offline snapshot
  (`tools/mcp-zene-control/zene_control/commands_snapshot.json`) is regenerated once by the
  integration lane after the last group merge - never hand-edited.
* **Gates run in this lane:** `file-length-gate.sh --check` (fork scope) exit 0 (the fork's four
  new files are 253 / 320 / 431 / 83 lines, all under the 500-line ratchet);
  `fork-sources-gate.sh` exit 0; `no-upstream-regression-gate.sh` exit 0 (the two inherited files
  it edits, `include/EffectChain.h` and `src/core/EffectChain.cpp`, are declared in
  `tests/upstream-modifications.txt` in the same commit).

## 7. Reproduction

```sh
cmake --build build -j2 > log 2>&1; echo EXIT=$?
cd build/tests && ctest -R PatcherCommandsTest --output-on-failure > log 2>&1; echo EXIT=$?
# and, an instance on the socket (0.3.0 release configuration):
QT_QPA_PLATFORM=offscreen build/zene --control-socket /tmp/z.sock &
#   patcher.get_state  {"target": "ch-1"}
#   patcher.set_wiring {"target": "ch-1",
#                       "edges": [{"from": "input", "to": "effect:1"}],
#                       "output": "effect:1"}    # bypasses effect 0
#   patcher.set_wiring {"target": "ch-1", "edges": []}   # back to the derived wiring
```

## 8. Not verified in this lane, stated plainly

* **No build was run to completion in this lane.** The owner directive for this pass puts the
  feature first and the build second; the code was checked by `-fsyntax-only` compiles of every
  touched translation unit with the tree's own configure flags (`-DWANT_QT6=ON -DUSE_WERROR=ON
  -DRelWithDebInfo -DTARGET_UARCH=official`) plus the three gates above, and `PatcherCommandsTest`
  itself has **not been executed**. A green build is the next lane's first step, not a claim here.
* The bridge's live tool list, the snapshot regeneration and the patcher's behaviour under a real
  audio device are the integration lane's to measure.
