# RoutingGraph on the live audio path

**Status: done.** `RoutingGraph` is no longer a tested class that nothing
instantiates: a mixer channel's effect chain is now rendered *through* a
`RoutingGraph`, proved byte-identical to the pre-change render for a project
with a chain, and proved live by a re-directed connection that changes the
render in exactly the way the re-wiring says it should.

Worktree: `projects/lmms-fl-research/zene-pa-router`, branch
`post-alpha/router-live` (base `post-alpha/v0.2` = `0c23587d2`).

| commit | what it is |
| --- | --- |
| `fcec2ebe3` | the render harness + the **pre-change** reference render (`tests/reference/routing-graph-live-render.raw`), captured with the pre-existing chain loop still in place |
| the commits above `fcec2ebe3` (the top one adds this report) | the graph on the path: `RoutingChainNodes.{h,cpp}`, the `EffectChain` wiring, the tests for both proofs, and the imported gate scripts |

The defect this fixes is the repository's own status of record: "the Patcher
engine is present but uninstantiated, so patching is not available in the
build". Verified at the base commit: `grep -rn RoutingGraph src/ include/`
outside `RoutingGraph.{h,cpp}` returned **no call sites**.

---

## 1. The path before the change

A mixer channel's effect chain lives on the audio thread like this:

| where | what |
| --- | --- |
| `src/core/Mixer.cpp:411` | `MixerChannel::doProcessing()` — the channel's per-period work |
| `src/core/Mixer.cpp:502-504` | `m_stillRunning = m_sidechainReceives.empty() ? m_fxChain.processAudioBuffer(m_bus) : m_fxChain.processAudioBuffer(m_bus, &m_sidechainBuffer);` |
| `src/core/EffectChain.cpp:260` (base) | `EffectChain::processAudioBuffer(AudioBus&, const AudioBuffer*)` |
| `src/core/EffectChain.cpp:274` (base) | `for (Effect* effect : m_effects) { moreEffects \|= effect->processAudioBuffer(bus); }` — the chain **was** this range-for |
| `src/core/Effect.cpp:177` | `Effect::processAudioBuffer(AudioBus&)` — the bus entry point |
| `src/core/Effect.cpp:218` | `processImpl(inOut.bus()[0], inOut.frames())` — the plugin's DSP, in place on the channel memory |

`src/core/AudioBusHandle.cpp:119` is the second caller on a real signal path
(instrument/track effect chains); it reaches the same `EffectChain` code.

So the chain's order was a `std::vector<Effect*>` walked by a range-for, and
"routing" was whatever that vector said. There was no graph anywhere on the
path.

## 2. How the graph is built from that path

New files (registered in `tests/fork-sources.txt`):

* `include/RoutingChainNodes.h`, `src/core/RoutingChainNodes.cpp`
  * `ChainInputNode` — a `RoutingNode` with no inputs that emits the host's own
    block. A graph that processes an existing path needs exactly one such node,
    because every other node reads its inputs.
  * `EffectNode` — a `RoutingNode` that owns one block and hands it to
    `Effect::processAudioBuffer(AudioBuffer&)` in place.
* `include/EffectChain.h` / `src/core/EffectChain.cpp` (upstream-inherited, so
  both are declared in `tests/upstream-modifications.txt`):
  * `EffectChain::rebuildRoutingGraph()` (**control thread**) clears the graph
    and rebuilds it from `m_effects`: one `ChainInputNode` sourced from the
    chain's planar mirror of the incoming block, then one `EffectNode` per
    effect **in list order**, connected linearly
    (`input -> effect 0 -> ... -> effect n-1`), output node = the last effect.
    The graph is then `prepare()`d for the engine's block size.
  * It is called from `appendEffect()`, `removeEffect()`, `moveUp()`,
    `moveDown()`, `clear()` and `loadSettings()` — every place the effect list
    or its order changes. `moveUp`/`moveDown` now take the same
    `requestChangeInModel()`/`doneChangeInModel()` guard the other topology
    edits take (they previously swapped the list under a running audio thread);
    that is the second reason both files are in the divergence ledger.
  * `processAudioBuffer(AudioBus&, …)` renders through the graph when
    `canProcessThroughGraph()` says the graph is safely usable, and otherwise
    runs the **unchanged** range-for.
  * `EffectChain::routingGraph()` exposes the graph so a control-thread caller
    can re-wire it (this is the seed #599 needs; there is no UI yet).

The equivalence is **by construction**: the graph is built from the same
`m_effects` vector, in the same order, with one connection per adjacent pair
that the range-for used to walk. Nothing is hand-wired.

### Realtime contract

`processAudioBuffer()` allocates nothing: the graph, its nodes and both planar
blocks are built in `rebuildRoutingGraph()` on the control thread, and the
audio path only mirrors the block in (`std::memcpy`-equivalent loops), runs
`RoutingGraph::process()` and mirrors it back. `canProcessThroughGraph()` is a
cheap read of scalars and two vector sizes.

### The trap that cost the first iteration (worth recording)

`Effect::processAudioBuffer(AudioBuffer&)` is the *legacy* entry point: it
renders `processImpl()` on the buffer's **interleaved scratch**
(`Effect.cpp:148`) and converts back to planar afterwards. The graph carries
planar blocks. Feeding the effect a planar block it had just summed therefore
produced **silence** on the first run — the effect read a stale zeroed scratch.
`EffectNode::process()` now syncs planar → interleaved (`toInterleaved()`)
immediately before the call. The pre-change bus path never had this problem
because it handed the plugin the channel's own memory (`Effect.cpp:218`).

## 3. Proof (a): the render is byte-identical to the pre-change render

`tests/src/core/RoutingGraphLiveTest.cpp` renders one mixer channel's chain
(two deterministic gain effects) through `Mixer::masterMix()` for 16 periods of
256 frames and prints the SHA-256 of the interleaved float render, 32768 bytes.

The reference was captured **before** any of this code existed
(commit `fcec2ebe3`, built from the unmodified base):

```
ROUTING_GRAPH_EVIDENCE reference  bytes=32768 sha256=323109a4fef596076154f5fa2be851ab5349182693fb48519cfb2ce1b26d9cb7
```

With the graph on the path:

```
ROUTING_GRAPH_EVIDENCE render     bytes=32768 sha256=323109a4fef596076154f5fa2be851ab5349182693fb48519cfb2ce1b26d9cb7
```

**Byte-identical — sha256 `323109a4fef596076154f5fa2be851ab5349182693fb48519cfb2ce1b26d9cb7`, 32768/32768 bytes**, no dB delta (there is no float difference to
report: the payload is compared byte-for-byte, not by a tolerance).

The same test slot asserts `chain->routesThroughGraph()` **before** it renders,
so the equivalence claim cannot be vacuous: the graph was demonstrably on the
path while those bytes were produced. The test renders twice and compares, so a
non-deterministic harness would fail here too.

`ctest` from `build/tests`: **26/26 pass**, 0 failed (25 before this change; the
new one is `RoutingGraphLiveTest`).

### Why there is no `.mmp`-level WAV A/B

The task asked for a sha256 of a rendered WAV. I tried it and it cannot support
any conclusion here, so I am not presenting it as one:

* the only in-tree projects that carry an effect chain
  (`data/projects/demos/StrictProduction-DearJonDoe.mmp`, 3 `ladspaeffect`
  chains) **do not render deterministically**. Two renders with the *same*,
  unmodified base binary differ in 48,524,197 of 72,017,920 payload bytes
  (payload sha256 `e1344391…` vs `98dbf52e…`, first divergence at payload
  offset 2,035,768). A before/after WAV comparison on that project would
  distinguish nothing.
* `plugins/RnnoiseDenoiser/testdata/*.mmp` (4 `rnnoisedenoiser` chains) is not a
  usable substitute either: repeated renders of `rnnoise_test_A.mmp` do not
  agree, and one run aborted with SIGABRT (exit 134).

The unit-level reference render is the *stronger* oracle anyway: it is a
byte-for-byte comparison of the float payload (no WAV header, no bit-depth
quantisation, no dither to hide a difference), it is committed and reproducible,
and the `Mixer::masterMix()` path it drives is the same one `lmms render` uses.

## 4. Proof (b): the graph is doing the work, not bypassed

`redirectingAConnectionChangesTheRender` re-directs the graph's input straight
to the second effect, taking the first one out of the path — **through the
public graph API** (`EffectChain::routingGraph()`), with no change to the
effect list, no rebuild, and no new node:

```
ROUTING_GRAPH_EVIDENCE render-linear      bytes=32768 sha256=323109a4fef596076154f5fa2be851ab5349182693fb48519cfb2ce1b26d9cb7
ROUTING_GRAPH_EVIDENCE render-redirected  bytes=32768 sha256=2af2c7f60ecf41aeef6e1b95a50473036cd9a359de26381da2f5ce70e1c521d4
```

The control differs, and the test pins *how* it differs:

* `graph.nodeCount()` is still 3 (input + 2 effects) with the same output node,
  so the difference cannot come from a different node set;
* every sample of the linear render is exactly **2.0 ×** the corresponding
  sample of the re-directed render — the gain of the effect the re-wiring took
  out of the path (`kFirstLeft`), asserted sample-by-sample, not with a
  tolerance;
* restoring the original connection restores the original bytes exactly
  (`render() == linear`).

That last point is what makes the graph genuinely *routing* rather than merely
calling the effects in plan order: a node that is simply not connected still
runs (it is in the cached plan), so if the graph were not carrying audio between
nodes the re-directed render would have been unchanged. It changed, and it
changed by exactly the arithmetic of the missing stage.

## 5. Allocation on the audio path

`processingThroughTheGraphAllocatesNothing` runs one 256-frame block through
`EffectChain::processAudioBuffer(AudioBus&)` — the exact block path
`MixerChannel::doProcessing()` calls — with `tests/src/core/AllocationProbe.h`
counting `operator new` on the calling thread:

**0 allocations** across the measured block (after one warm-up block).
`RoutingGraphTest::ProcessingDoesNotAllocate` covers the graph itself; this
covers the integration around it (mirroring, silence-flag publication, node
result aggregation).

## 6. Behaviour-preserving by default

`canProcessThroughGraph()` returns false — and the chain then runs the
pre-existing range-for, unchanged — whenever any of these holds:

* the chain has no effects (nothing to route): `chainWithoutEffectsIsNotRouted`
  asserts `routesThroughGraph() == false` and an empty, unprepared graph;
* the block size is not the size the graph was prepared for (a harness block, or
  a device reconfiguration);
* the bus is not a single channel pair (the graph's planar blocks mirror one
  stereo pair);
* the graph no longer mirrors the effect list (a topology edit that somehow
  slipped past `rebuildRoutingGraph()`), so the worst case of any missed hook is
  today's behaviour, never a wrong render;
* any effect in the chain has an **audio ports model** (`AudioPlugin`, i.e.
  CLAP/VST3-style hosting): those route their own ports on the bus and override
  the bus entry point, and the graph's planar blocks cannot carry that port map.

## 7. What is NOT done

* **Re-wired connections are not persisted.** The graph is rebuilt linearly on
  every topology change and on project load; a custom wiring lives for the
  session only. No `<routinggraph>` element is written into `<fxchain>`.
* **No UI, no racks, no macros.** `routingGraph()` is a control-thread API and
  the seed for #599; nothing in the GUI calls it.
* **Live re-wiring during playback is not safe** and is not attempted:
  `RoutingGraph.h`'s threading contract (topology edits and `prepare()` are
  control-thread operations; the atomic plan swap is still to come) still
  applies. The liveness proof re-wires between renders.
* **The project-load path is not covered by a test.** `loadSettings()` calls
  `rebuildRoutingGraph()` (visible in the diff, and `EffectChain::loadSettings`
  is reached by every `.mmp` load), but a unit test that instantiates a chain
  from XML in the headless test binary races at teardown: the plugin-loader
  threads it starts can still be running when `Engine::destroy()` runs, and the
  binary then aborts with `QThread: Destroyed while thread is still running`
  (the same test passes 8/8 under `gdb`, which is slower). The case was removed
  rather than left flaky; it needs the loader shut down cleanly first, which is
  not this task.
* **Multi-pair buses, instruments/sample-playback chains as graph nodes, and
  graph-level serialization** are untouched.

## 8. How to reproduce

```sh
cd /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/lmms-fl-research/zene-pa-router
JOBS=4 bash tools/local-ci.sh --build-dir build --jobs 4    # configure/build/ctest
cd build/tests
QT_QPA_PLATFORM=offscreen ./RoutingGraphLiveTest            # both proofs + the allocation probe
```

The test prints the four `ROUTING_GRAPH_EVIDENCE` lines quoted above.

Gates, run at this branch's HEAD:

```
bash tests/fork-sources-gate.sh            # Gate 9 -> exit 0
  PASS: every tracked source in scope is registered (102 fork-NEW, 993 inherited).
bash tests/no-upstream-regression-gate.sh  # Gate 6 -> exit 0
  PASS: every change to upstream-inherited code since 01148947e is declared (31 file(s) in the ledger)
bash tests/run-all-gates.sh                # -> exit 3 = PASS-WITH-SKIPS
  gate 1 ctest PASS | 2 coverage SKIP | 3 no-tautology PASS | 4 complexity PASS
  5 mutation PASS (kill score 88.5% >= 80%) | 6 upstream-regression PASS
  7 file-length PASS | 8 duplication PASS (1.07% < 5%) | 9 fork-sources PASS
  only gate 2 was skipped (no --with-coverage), so the run is incomplete, not green
```

`tests/fork-sources-gate.sh` and the version of `tests/run-all-gates.sh` that
runs Gate 9 and exits 3 on a skipped gate were **not present** at this branch's
base (`0c23587d2`); they are committed here byte-identical to the copies on
`post-alpha/gate-debt` (`84388107e`) so the gates the program's QA suite expects
can actually be run from this branch.
