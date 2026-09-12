# Racks: parallel chains and a Chain Selector on a mixer channel

**Status: the first honest slice of #599 is done.** A mixer channel can now hold a
rack: two or more effect chains processing the same input block and summing into
the channel's output, with a single control that routes the channel to one chain
at a time, both persisted in the project. Macros and key/velocity zones are
**not** in this slice and are named in §5.

Worktree: `projects/lmms-fl-research/zene-pa-racks`, branch `post-alpha/racks`,
based on `post-alpha/router-live` (`d09ebad52`).

| commit | what it is |
| --- | --- |
| `af27e193f` | the model: `Rack.{h,cpp}`, `RackNodes.{h,cpp}`, the mixer channel's `m_rack`, save/load |
| `24dd527b3` | `RackTest.cpp` (parallel sum, selector, allocation, persistence, old projects) and the render-proof harness in `tools/` |
| this commit | this report |

---

## 1. What `router-live` gave this slice

`docs/ROUTING-GRAPH-LIVE.md` (the sibling lane) put a `RoutingGraph` under a
mixer channel's effect chain: `EffectChain::rebuildRoutingGraph()` builds
`ChainInputNode -> EffectNode per effect` from the chain's own list on every
topology edit, `EffectChain::processAudioBuffer(AudioBus&)` renders the block
through it when `canProcessThroughGraph()` says the graph is usable and through
the unchanged plain loop otherwise, and `EffectChain::routingGraph()` exposes the
graph to a control-thread caller — explicitly as "the seed #599 needs".

What that left missing for racks was not the graph but the *shape*:
a chain's graph is one chain, `ChainInputNode -> ... -> last effect`, with no way
to express "two chains on the same input, summed". This slice adds the second
chain, the summing output, the selector, and persistence — and nothing else.

## 2. The model

**Where it lives.** `MixerChannel::m_rack` (`include/Mixer.h`), an
upstream-inherited file: chain **0 is the channel's own `m_fxChain`** — the
object the effect rack GUI (`EffectRackView`), the channel's `<fxchain>` element
and the PDC accounting already own. Chains 1..n belong to the rack and are saved
as `<chain index="k">` children of a `<rack selected=…>` element inside the
channel's `<mixerchannel>`:

```xml
<mixerchannel num="1" …>
  <send channel="0" amount="1"/>
  <fxchain numofeffects="1" enabled="1"> … the channel's own chain … </fxchain>
  <rack version="1" selected="-1">
    <chain index="1">
      <fxchain numofeffects="1" enabled="1"> … the parallel chain … </fxchain>
    </chain>
  </rack>
</mixerchannel>
```

Each chain carries its **own** `<fxchain>` element, written by the existing
`EffectChain` machinery: the rack never re-implements what a chain is.

**The rack's node set** (`include/RackNodes.h`) is three classes, one of which
already existed:

| node | role |
| --- | --- |
| `ChainInputNode` (reused from `RoutingChainNodes.h`) | emits the rack's planar mirror of the channel's block |
| `RackChainNode` (new) | hands one chain's block to `EffectChain::processAudioBuffer(AudioBus&)` |
| `RackSumNode` (new) | sums every chain's output into the rack's output |

Wiring, `Rack::rebuildRoutingGraph()` (control thread):

* parallel (`selected="-1"`): `input -> chain 0`, `input -> chain 1`, … each
  chain `-> sum`; output node = sum. A `RoutingGraph` node's input port takes any
  number of connections, so the sum node needs no per-chain state.
  `nodeCount() == 1 + chains + 1` (4 for a two-chain rack), 4 connections.
* selected (`k`): `input -> chain k -> sum`; output node = sum, and the chain
  nodes that are not selected are **not added to the graph at all**, so they are
  not in the cached plan (`nodeCount() == 3`, `processingOrder().size() == 3`).

**What renders a chain: no new DSP.** `RackChainNode` bridges the graph's planar
block to the interleaved one-stereo-pair `AudioBus` that
`EffectChain::processAudioBuffer(AudioBus&)` takes — the *same call the mixer
makes for the channel's own chain*. A chain in a rack therefore keeps the whole
existing machinery and its own fallbacks: it renders through its own
`RoutingGraph` when it can route, and through its plain effect loop when it
cannot (e.g. a chain containing an effect with an active audio ports model).
That is why the rack does not need the chain-level restrictions the graph has,
and why it works with built-in effects as well as legacy ones.

**The audio path** (`MixerChannel::doProcessing`, `src/core/Mixer.cpp`):

```
if (m_rack.canProcessThroughRack(m_bus))  m_stillRunning = m_rack.processAudioBuffer(m_bus[, sidechain]);
else                                      m_stillRunning = m_fxChain.processAudioBuffer(m_bus[, sidechain]);
```

The `else` branch is the pre-existing expression, unchanged. `Rack::
processAudioBuffer()` mirrors the bus into the graph's input buffer, gives every
chain node the channel's sidechain input for the block, runs
`RoutingGraph::process()`, mirrors the result back onto the bus (silence flags
included) and returns the chains' aggregated "still processing" answer.

**Realtime.** The node set, the planar buffers and every chain's `AudioBus` are
built in `rebuildRoutingGraph()`, called from `addChain()`, `removeChain()`,
`setSelectedChain()`, `clear()` and `loadSettings()` — all control-thread, all
inside the audio engine's model-change guard except `loadSettings()` (which
inherits `EffectChain::loadSettings()`'s existing, documented hazard rather than
adding one). `canProcessThroughRack()` and `processAudioBuffer()` read scalars,
vector sizes and preallocated buffers: no allocation, no lock, no growth
(§4.3).

**When the rack is not on the path** — the same philosophy as the router lane's:
fewer than two chains, no wiring, a block size the graph was not prepared for,
a bus that is not one stereo pair, or a graph that no longer mirrors the chain
list. In every case the channel renders its own chain, which is exactly what it
did before racks existed.

**A disabled chain passes its input through**, unchanged: that is what the
chain's existing enabled flag means on the channel's own path, and the node's
output already holds the summed input before the chain is asked to process. A
chain with no effects is such a chain.

## 3. The Chain Selector: the semantics, stated

`selectedChain()` is `-1` (`Rack::Parallel`) or a chain index.

* **`-1` — parallel.** Every chain is fed the same input and the channel's
  output is the sum of every chain's output.
* **`k` — selected.** The input feeds chain `k` alone and chain `k` alone is the
  channel's output. The chains that are not selected are **not in the graph's
  node set**: they are not fed, their effects are not called, and they
  contribute nothing — not even their own input.
* **Idle behaviour of a deselected chain.** It does not process and its DSP
  state is *frozen*, not flushed: a delay or reverb in a chain that is switched
  away resumes where it stopped when the chain is selected again, rather than
  having had its tail rendered while it was idle. Its `EffectChain` object (and
  so its effects and their settings) survives the switch.
* **Switching.** A control-thread operation that rebuilds the node set under
  `Engine::audioEngine()->requestChangeInModel()` / `doneChangeInModel()`, like
  every other topology edit in this tree. It takes effect at a block boundary.
* **Clicks, not crossfades.** There is **no crossfade and no fade**: the block
  after the switch is already exactly the newly selected chain's output (asserted
  in the test), so where the two chains' outputs differ the switch is a step
  discontinuity and may click. An equal-power crossfade is not implemented (§5).
* Removing the selected chain drops the rack back to parallel; a selection that
  is not a chain wires nothing at all, rather than guessing (a bad selection can
  never route a chain nobody asked for).

## 4. Proofs

### 4.1 Unit level — `tests/src/core/RackTest.cpp`

Through `Mixer::masterMix()`, the path `lmms render` takes; 16 periods × 256
frames = **8192 output samples**; chain A's effect is a gain of 2.0 both
channels, chain B's a gain of 0.5 / 0.25 (powers of two, so the arithmetic is
exact in float and can be compared with `==`). Every number below is printed by
the test (`RACK_EVIDENCE …`, stdout, flushed).

| evidence (label from the test output) | samples | differing | max │Δ│ | reading |
| --- | --- | --- | --- | --- |
| `no-rack-run-to-run-floor` | 8192 | 0 | **0 LSB** | no rack: two renders in a row are byte-identical; the floor of this harness is 0 |
| `no-rack-vs-chain-a-arithmetic` | 8192 | 0 | **0 LSB** | and it *is* the channel's own chain, exactly |
| `parallel-chain-a-blocks` / `-b-blocks` | — | — | — | **16 / 16** blocks: both chains processed |
| `parallel-vs-sum-of-both-chains` | 8192 | 0 | **0 LSB** | the parallel render is `input*2.0 + input*0.5` (L) / `+ input*0.25` (R), sample for sample |
| `parallel-vs-no-rack-sensitivity-control` | 8192 | 8189 | 2 097 152 LSB | configuring a rack **does** change the render (the 3 equal samples are the input's own zeros) |
| `parallel-vs-chain-a-alone` / `-b-alone` | 8192 | 8189 | — | the sum is neither chain alone |
| `selector-b-chain-a-blocks` | — | — | — | **0** blocks: the unselected chain's effect was never called |
| `selector-b-chain-b-blocks` | — | — | — | **16** blocks: the selected one processed every block |
| `selector-b-vs-chain-b-arithmetic` | 8192 | 0 | **0 LSB** | selecting B is exactly B's output |
| `selector-a-vs-selector-b-sensitivity-control` | 8192 | 8189 | 7 340 032 LSB | A and B are measurably different |
| `selector-a-vs-no-rack-transparency` | 8192 | 0 | **0 LSB** | selecting the channel's own chain is byte-identical to no rack: the rack's wiring adds no arithmetic |
| `rack-process-allocations` | — | — | — | **0 allocations** across the measured block (`AllocationProbe.h`) |
| `reloaded-rack-passthrough` | 8192 | 0 | **0 LSB** | after a reload the rack is on the path, and an empty (disabled) chain passes its input through |
| `reloaded-rack-selector-routes` | 8192 | 0 | **0 LSB** | the reloaded rack routes real DSP: the effect added to the reloaded chain is the whole output |
| `persistence-round-trip` | — | — | — | `<rack selected="1">` saved, loaded, re-saved with its chain and that chain's `<effect>` |
| `old-project-no-rack` | — | — | — | a project with no `<rack>` element loads with `chains=1, routed=0` and re-saves with no `<rack>` element |

`ctest` runs it as one of 27 tests; all 8 slots pass, exit 0 (see §6 for how
often that whole run is green on this box). `tests/src/core/RackTest.cpp` is
registered in `tests/all-sources.txt`; Gate 3 measures it as 103 test slots with
75 assertions and no tautologies.

### 4.2 A real headless render — the rack path is reachable end to end

Fixture: `tools/rack-render-fixture.py make /tmp/rackproof` writes four one-bar
projects — a TripleOscillator through **mixer channel 1**, whose own `<fxchain>`
holds one Amplifier at 100%, plus (in three of them) a `<rack>` whose chain 1
holds an Amplifier at 50%. Rendered 32-bit float at 44100 Hz with the built
`lmms` **in place in its build tree**, 302 336 frames / 604 672 samples each:

```
QT_QPA_PLATFORM=offscreen build/lmms render /tmp/rackproof/rack-parallel.mmp \
    -o /tmp/rackproof/final/rack-parallel-1.wav -f wav -a -s 44100
```

| fixture | RMS | dBFS | peak | sha256 (payload) |
| --- | --- | --- | --- | --- |
| `no-rack` | 0.200271 | −13.968 | 0.645387 | `9284d3189e417704654ed669407916ee…` |
| `rack-select-a` (`selected="0"`) | 0.200271 | −13.968 | 0.645387 | **`9284d318…` — the same bytes** |
| `rack-select-b` (`selected="1"`) | 0.100135 | −19.988 | 0.322694 | `741c604a3c546817caec2a2ee1b39c7d…` |
| `rack-parallel` (`selected="-1"`) | 0.300406 | −10.446 | 0.968081 | `b56cfa32efa77c9f71aa210d8f9521c2…` |

| comparison | differing / samples | max │Δ│ | level |
| --- | --- | --- | --- |
| **floor**: `no-rack` run 1 vs 2, and 1 vs 3 (same binary, same project, 3 runs) | **0** / 604 672 | **0 LSB** | ±0.00000 dB |
| **no-rack behaviour**: the **base** binary (`d09ebad52`, rack code absent) vs this branch, same project, 3 + 3 runs | **0** / 604 672 | **0 LSB** | +0.00000 dB |
| **transparency**: `no-rack` vs `rack-select-a` (the rack routing the channel's own chain) | **0** / 604 672 | **0 LSB** | +0.00000 dB |
| **selector (sensitivity control)**: `rack-select-a` vs `rack-select-b` | 501 042 / 604 672 | 2 706 951 LSB | **−6.02060 dB** = exactly the 50% Amplifier, `20·log10(0.5)` |
| **parallel sum (sensitivity control)**: `rack-parallel` vs `rack-select-a` | 501 042 / 604 672 | 2 706 951 LSB | **+3.52183 dB** = `20·log10(1.0+0.5)` |
| **parallel sum identity**: `rack-parallel` vs (`rack-select-a` **+** `rack-select-b`), sample-wise | 318 340 samples differ, all by **less than one 24-bit LSB** (max │Δ│ = **2.98·10⁻⁸**, −150.5 dB below full scale) | **0 LSB** | RMS 0.300406 vs 0.300406 = **+0.00000 dB** |

Two more things this measurement shows:

* **A pre-rack build degrades forward-compatibly.** The base binary renders
  `rack-parallel.mmp` byte-identically to `no-rack.mmp` (`9284d318…`): an LMMS
  that does not know `<rack>` ignores it and renders the channel's own chain,
  the same way it already ignores `<bus>`.
* **The fixture is bit-reproducible**, so this floor is 0 and every claim above is
  an exact statement rather than an envelope. That is a property of *this*
  project (a synthesiser, no sample playback); the tree's renderer is generally
  **not** bit-reproducible — see §6 of `docs/STEM-EXPORT.md` and the note below.

### 4.3 Allocation on the audio path

`processingThroughTheRackAllocatesNothing`: one 256-frame block through
`Rack::processAudioBuffer(AudioBus&)` — the exact call `MixerChannel::
doProcessing()` makes — with `tests/src/core/AllocationProbe.h` counting
`operator new` on the calling thread: **0 allocations** (after one warm-up
block). The rack's node set and all of its buffers exist before the block
arrives; the chains' own paths allocate nothing either (their lane's proof).

### 4.4 Persistence, and the project that has no rack

* The saved `<rack>` element carries the selector and, per chain, the chain's own
  `<fxchain>` with its `<effect>` elements and their settings — asserted on the
  save side, and the re-save after a reload writes the same configuration.
* A reloaded rack is on the path: `chains=2`, `selected=1`, 3 nodes, and its
  audio is the selected chain's (§4.1, two `0 LSB` rows).
* A project with **no** `<rack>` element — and no rack code has ever seen it —
  loads with one chain, nothing routed, and re-saves with no `<rack>` element.
* **The reload fixture's chains carry no effects, deliberately.** Instantiating a
  chain from XML instantiates its effects, which starts the plugin loader's
  threads; a headless test binary that then tears the engine down aborts with
  `QThread: Destroyed while thread is still running` — the teardown race
  `docs/ROUTING-GRAPH-LIVE.md` §7 documents and removed its own case for. So the
  effect half of this claim is asserted where it is deterministic (the save
  side), and the *load* path with real effects is exercised where it is safe: the
  real render in §4.2 loads a project whose rack chains contain real Amplifiers.

### 4.5 Gates

```
bash tests/fork-sources-gate.sh            # Gate 9 -> exit 0
  PASS: every tracked source in scope is registered (108 fork-NEW, 994 inherited, 0 stale)
bash tests/no-upstream-regression-gate.sh  # Gate 6 -> exit 0
  PASS: every change to upstream-inherited code since 01148947e is declared (31 file(s) in the ledger)
bash tests/run-all-gates.sh                # -> exit 3 = PASS-WITH-SKIPS
  gate 1 ctest PASS | 2 coverage SKIP | 3 no-tautology PASS | 4 complexity PASS
  5 mutation PASS (kill score 88.5% >= 80%) | 6 upstream-regression PASS
  7 file-length PASS | 8 duplication PASS (1.03% < 5%) | 9 fork-sources PASS
  only gate 2 skipped (no --with-coverage), so the run is incomplete, not green
```

Run once more at this commit, with the §6 teardown flake landing on gate 1 in
that run, `run-all-gates.sh` exits **1** with every other gate green and the
failing test named as `RoutingGraphLiveTest`. Both runs are reported in §8; the
rack is not involved in either outcome.

Gate 9's fork-NEW count went 102 → 108: the four new rack sources (two headers,
two translation units) plus the two `tools/` proof scripts, which Gate 6 would
otherwise have called an undeclared change to upstream-inherited code (Gate 6's
mechanical rule has no `tools/**` exemption; `tools/local-ci.sh` is registered
the same way). Gate 4 refused the first version of `tools/rack-render-proof.py`
for two functions over CCN 10; the WAV reader was split until it passed rather
than being added to the baseline.

## 5. What is NOT done

**Deferred by the task, not started:** macros and key/velocity zones. They are
UI-and-mapping work on top of this model: macros need the selector and per-chain
levels to be automatable models (the rack has no `Model` surface yet, so nothing
can be assigned or automated), and key/velocity zones need *note-level* input at
the rack, which this model does not carry — the graph moves one stereo block, so a
zone would be a different data path (per-note routing), not a control on this one.

**What a user still cannot do.** There is no UI and no scripting binding for the
rack: a user cannot create a rack, add a chain, choose a chain in the selector,
see a rack in the effect rack view, or reach any of this from the app. The only
way to have a rack today is a project file that already contains a `<rack>`
element (the fixtures in `tools/` show the shape). The cheapest next step to
reachability is a `ScriptBindings` (Lua) or agent-tool binding for
`Rack::addChain()` / `setSelectedChain()`, or a button in `EffectRackView`;
neither is in this slice.

**Other known limits, named:**

* **Latency/PDC is not per-chain.** The channel's published latency is still
  `m_fxChain.latencyFrames()` (`src/core/Mixer.cpp`), so a rack whose *other*
  chains report latency is not aligned at the summing point, and switching the
  selector changes the channel's latency without the PDC graph being told.
* **Each chain is one stereo pair** (its `AudioBus` has one pair, like the
  channel's), so a chain cannot host an effect whose extra output ports need more
  track channel pairs.
* **Sidechain**: every chain in a block receives the channel's sidechain input;
  there is no per-chain sidechain routing and no per-chain send.
* **No undo/Journal entry** for rack edits, and no automation of the selector.
* **The rack's wiring is not serialized as a graph.** It is derived from the chain
  list and the selection and rebuilt on load, which is why a re-wired graph
  (`RoutingGraph::save()`, the Patcher path) is still not persisted.
* **A rack on the master channel** works by construction (master is a
  `MixerChannel`) but was not exercised.
* **Two chains in parallel double the CPU**, and a deselected chain contributes
  nothing but also cannot be pre-rolled to avoid a resume transient.
* **`.mmp` files written by this build gain a `<rack>` element only when a rack is
  configured.** A channel with no rack writes no element, so the claim "a project
  with no rack is unchanged" holds at the byte level for the mixer section too.

## 6. A defect found on the way, not fixed here (not the rack's)

The reason `ctest` and the renderer are intermittently red on this box is
**`AudioEngine::~AudioEngine()`** (`src/core/AudioEngine.cpp:122`): it calls
`quit()` on every worker thread and then `wait(500)` — a 500 ms budget per
worker, with `m_numWorkers = QThread::idealThreadCount() - 1` (19 on this
20-thread machine). When the box is loaded (sibling lanes compile here), a worker
can miss that budget and the `QThread` object is then destroyed while still
running: `QFATAL: QThread: Destroyed while thread is still running`, SIGABRT.

Measured, 8 direct runs each, same build, same session:

| binary | exit 0 | aborted (134) |
| --- | --- | --- |
| `RackTest` (this slice) | 7 | 1 |
| `PdcMixerTest` (untouched by this slice) | 6 | 2 |
| `RoutingGraphLiveTest` (untouched by this slice) | 6 | 2 |

In the aborted `RackTest` run **every test slot passed**; only `cleanupTestCase`
failed at `Engine::destroy()`. The same abort hits the `lmms` binary at exit
after a **complete, correct** render: 5 of the 85 render invocations in this
session aborted at exit, and in every aborted log the render's own
`PERFLOG | Project Render` line precedes the abort and the WAV is complete on
disk. It is pre-existing, tree-wide, and unrelated to racks; the fix (a wait
budget that cannot expire, or a join loop) belongs to whoever owns engine
teardown, and is deliberately not attempted here.

## 7. How to reproduce

```sh
cd /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/lmms-fl-research/zene-pa-racks
JOBS=4 bash tools/local-ci.sh --build-dir build --jobs 4     # -DWANT_QT6=ON printed as a deviation
cd build/tests && ctest                                       # 27 tests; RackTest prints the §4.1 table
QT_QPA_PLATFORM=offscreen ./RackTest                          # the same evidence on its own

python3 tools/rack-render-fixture.py make /tmp/rackproof
for i in 1 2 3; do for p in no-rack rack-select-a rack-select-b rack-parallel; do
  QT_QPA_PLATFORM=offscreen build/lmms render /tmp/rackproof/$p.mmp \
      -o /tmp/rackproof/$p-$i.wav -f wav -a -s 44100; done; done
python3 tools/rack-render-proof.py measure    /tmp/rackproof/no-rack-1.wav
python3 tools/rack-render-proof.py compare    /tmp/rackproof/no-rack-1.wav \
                                              /tmp/rackproof/rack-select-b-1.wav
python3 tools/rack-render-proof.py sumcompare /tmp/rackproof/rack-parallel-1.wav \
                                              /tmp/rackproof/rack-select-a-1.wav \
                                              /tmp/rackproof/rack-select-b-1.wav
```

The base-binary comparison in §4.2 was taken by checking the three files this
change touches out of `d09ebad52`, building `lmms`, rendering, and restoring
`git checkout HEAD -- include/Mixer.h src/core/Mixer.cpp src/core/CMakeLists.txt`.

## 8. Run log — unpiped exit codes

```
$ JOBS=4 bash tools/local-ci.sh --build-dir build --jobs 4
configure EXIT=0            (build/configure.log; deviation: -DWANT_QT6=ON, no Qt5 on this box)
build EXIT=2                (build/build.log: the OOM killer took cc1plus in plugins/Eq,
                             plugins/Flanger and plugins/DynamicsProcessor — 4 "Terminated
                             signal terminated program cc1plus" lines, no compiler error)
$ cmake --build build -j2   # same build dir, resumed at 55% with half the jobs
build EXIT=0                (build/build-retry.log)
$ cmake --build build -j2   # after restoring the branch sources
build EXIT=0
$ cd build/tests && ctest
ctest EXIT=0                (100% tests passed, 0 tests failed out of 27; 29.56 s)
$ bash tests/fork-sources-gate.sh           -> exit 0
$ bash tests/no-upstream-regression-gate.sh -> exit 0
$ bash tests/run-all-gates.sh               -> exit 3 (PASS-WITH-SKIPS; only coverage skipped)
```

A later `run-all-gates.sh` at the same commit gave:

```
$ bash tests/fork-sources-gate.sh           -> exit 0
$ bash tests/no-upstream-regression-gate.sh -> exit 0
$ bash tests/run-all-gates.sh               -> exit 1
  gate 1 ctest FAIL (26/27: RoutingGraphLiveTest aborted in cleanupTestCase)
  gates 2 SKIP, 3-9 PASS
```

The `-j4` build dying was memory pressure from concurrent lanes on this box, not
a code failure: the retry at `-j2` finished the same build tree with no compiler
errors, and `-DWERROR=ON` is in force throughout. `ctest` was run 10 times on
this branch in this session: **7 green (27/27, exit 0), 3 red**, and the three
reds were three *different* tests — `PdcMixerTest`, `RackTest`,
`RoutingGraphLiveTest` — each aborting in `cleanupTestCase` with the same
`QThread: Destroyed while thread is still running` (§6). The green run quoted
above is one of the seven.

## 9. Proposed wording for `docs/KNOWN-LIMITATIONS.md` / the release notes

Not edited here (another lane owns those files). Suggested, in the register those
files use:

> **Racks are a model without a UI (#599, first slice).** A mixer channel can
> hold parallel effect chains summed into the channel, with a chain selector
> that routes the channel to one chain at a time, and both survive save/reload.
> There is no way to build a rack in the application yet — no button, no menu, no
> script binding — so the feature is reachable only from a project file that
> already contains a `<rack>` element. Macros and key/velocity zones are not
> implemented. Loading a rack project in an older build is safe: the unknown
> element is ignored and the channel renders its own chain.
>
> Per-chain latency compensation is not implemented: a rack whose chains report
> different latencies is not sample-aligned at the summing point, and switching
> the selector changes the channel's latency without telling the PDC graph.

## 10. Numbering note

The legacy project-upgrade path in `src/core/DataFile.cpp` (`upgrade_0_4_0_20080118`)
rewrites a `rack` child of an old `<fx>` element. This slice's `<rack>` is a child
of a `<mixerchannel>` in projects written long after that upgrade's version
window; no upgrade touches it, and no new element name is reused.
