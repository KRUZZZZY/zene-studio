<!--
  In-repo copy of the specification this repository's code and tests cite (DOC-5, 2026-09-15).
  This is a PINNED SNAPSHOT, not a live document:
    source   : the program workspace, mixer/SPEC-dynamic-routing.md
    sha256   : 9fa32752d146f4fa25f034424a98aeff1fe8d42682e172f585a88e141b3fc748
    bytes    : 44148
    why this file: the routing/PDC design spec; the mixer/ working copy the workspace cites
  The workspace copy remains the working copy and is where the document is edited; refreshing
  this snapshot is a deliberate act, recorded in the commit that does it. If a citation in this
  repository and this file ever disagree, reconcile them in one commit - the citations name the
  file by bare name (e.g. `SPEC-zene-studio.md`), and this directory is where that name resolves.
  See docs/specs/README.md for the convention and for the cited documents NOT yet committed.
-->
# Dynamic Multi-Channel Routing for LMMS: Design Specification

> **Task:** AI-KOS task 555 (program: `lmms-fl-replacement-program`, mission: `lmms-mixer-routing-mission`)
> **Status:** Ready for implementation (design decisions resolved)
> **Version:** 1.2
> **Changelog:** v1.2 — All 3 open design questions resolved by project owner (D1: reorder doProcessing() for pre-fader sends + A-B regression test; D2: C1-freeze-then-C2 layering, split C2 out if it fails; D3: per-send performance gate, <5% vs regular-send baseline). v1.1 — Addresses all 8 required changes from SPEC-REVIEW.md: corrected MAX_MIXER_CHANNELS claim, fixed processBuffer→processAudioBuffer signatures, fixed SampleFrame type characterization, fixed XML serialization examples, added audio-thread no-allocation constraint, replaced impossible-to-race sidechain claim with per-sender buffering, added Effect::processImpl sidechain delivery, fixed spin-wait polling description.
> **Sources:** `findings-pr7459-deepdive.md`, `findings-daw-routing-models.md`, `findings-lmms-mixer-internals.md`, `findings-graph-runtimes.md`

---

## 1. Scope & Goals

### 1.1 In Scope

This specification defines the replacement of LMMS's fixed-64 point-to-point `MixerRoute` system with **dynamic multi-channel routing** and **native sidechain sends**. Specifically:

| Feature | Scope |
|---------|-------|
| Dynamic channel allocation | Replace fixed-64 `MixerChannel` vector with node-based graph; channels created/destroyed on demand. The `m_mixerChannels` vector (`std::vector<MixerChannel*>`) has no explicit ceiling — `mix_ch_t` is `uint16_t` (implying max 65535). The true limitation is the fixed stereo `SampleFrame`, not the container size (verified `lmms_constants.h` has only `DEFAULT_CHANNELS=2` and `MaxChannelsPerAudioBuffer=128`; no `MAX_MIXER_CHANNELS` constant exists) |
| Multi-channel audio transport | Adopt `AudioBus` (non-owning span of channel-pairs) so plugin I/O can carry >2 channels |
| Pin-connector routing matrix | Per-plugin matrix widget modeled on Reaper's Plug-in Pin Connector — rows = track channels, cols = plugin channels |
| Native sidechain sends | Channel-pair sidechain (Reaper pattern): sidechain taps carry audio to channels 3/4 of the destination, mapped via plugin pin assignment |
| Pre/post-fader send modes | Send type per route: post-fader (default), pre-fx, pre-fader (post-FX), post-fader (no gain) — matching Reaper's `I_SENDMODE` 0–3 |
| Parallel effect buses | Dedicated bus channel type (not nested in `MixerChannel::m_fxChain`) that sums multiple source sends and processes through its own FX chain |
| Graph scheduling | Extend `masterMix()` dependency-queue to handle sidechain taps and parallel bus nodes without deadlock |
| XML backward compatibility | New `<sidechain-send>` and `<bus>` elements coexist with legacy `<send channel="N"><amount value="F"/></send>` |

### 1.2 Out of Scope

| Feature | Reason |
|---------|--------|
| Native CLAP plugin hosting | Explicitly deferred — PR #7459 description; CLAP `audio-ports-config` needs runtime NTTP rebuilds (see §6 Phase E) |
| Native LV2 multi-channel | Out of scope — LV2 port enumeration differs; deferred to Phase E |
| Native LADSPA multi-channel | LADSPA has no port enumeration API; deferred |
| Carla multi-channel bridge | Messmerd's follow-up branch; not part of this spec |
| UI theming / Qt style changes | Visual appearance of PinConnector follows existing LMMS theme; no theming changes |
| SampleFrame removal | sakertooth's `SampleFrame`-is-root-problem debate (Apr 2025) is acknowledged but full `SampleFrame` replacement is a separate concern; this spec adds multi-channel alongside, not instead of, `SampleFrame` |
| Automation clip recording | Handled by existing automation system; not modified by this spec |
| Plugin delay compensation (PDC) | Out of scope for initial delivery — existing per-plugin latency reporting remains unchanged |

---

## 2. Design Decision: Adopt PR #7459 as Foundation

### 2.1 Explicit Adoption

This design **adopts the architecture proposed in PR #7459** (messmerd, `pin-connector` branch) as the foundation for dynamic multi-channel routing in LMMS. Three independent findings support this decision:

1. **Working pin-connector model already exists.** PR #7459 introduces `PinConnector` (`include/PinConnector.h`, `src/gui/PinConnectorView.cpp`) — a Qt matrix widget modeled on Reaper's Plug-in Pin Connector. Each cell is an automatable `BoolModel*` checkbox. Routing rules are specified: reading from a track channel does not consume the data, writing overwrites, multiple inputs sum, disconnected outputs bypass. *(Source: findings-pr7459-deepdive.md §1.2)*

2. **VST2 multi-channel works today.** The PR's `Vestige` bridge (`plugins/Vestige/Vestige.cpp`) and `VstPlugin` base (`plugins/VstBase/VstPlugin.cpp`) enumerate VST2/VST3 buses at init, creating appropriate `AudioPorts` ports. The `RemotePluginAudioPorts` specialization (`include/RemotePluginAudioPorts.h`) bridges the shared-memory `RemotePlugin` (non-template) with the new templated `AudioPorts` system. Sidechain *detection* for VSTs that advertise sidechain buses works. *(Source: findings-pr7459-deepdive.md §5, §8)*

3. **Reaper pattern proven in production.** Reaper's channel-pair routing — where sidechain is simply a send to channels 3/4 of a N-channel track with plugin pin mapping — matches PR #7459's `AudioBus` model (span of channel-pairs) and `AudioPortsModel` pin matrix. LMMS already has `AudioBuffer` with internal channel-group support (`BufferContainer::groupBuffers`) but exposes only the stereo interleaved view; PR #7459 provides the missing abstraction layer. *(Source: findings-daw-routing-models.md §3, findings-lmms-mixer-internals.md §9)*

### 2.2 The Split Needed: Phased PR Sequence

PR #7459 is **too large to review** — sakertooth (Oct 2025): "I wasn't able to review this properly because of its size." JohannesLorenz (Aug 2024) made the same observation. The diff covers 15+ new files and 9+ modified files across core, GUI, and plugin bridges simultaneously.

**Mitigation:** Split into **5 mergeable PR phases** (detailed in §6). Each phase:
- Is self-contained and reviewable (< 800 new lines)
- Has a clear verification criterion (existing tests pass + new unit test)
- Does not depend on later phases for correctness
- Lands independently in LMMS master

The phases are: PinConnector model extraction → AudioPorts core → Mixer integration + dynamic channels → Sidechain + parallel buses → Deferred backends. This sequence directly answers the "too big to review" blocker.

---

## 3. Core Data Model

### 3.1 Class Hierarchy (from PR #7459)

```
AudioPortsSettings (NTTP POD)          include/AudioPortsSettings.h
  ├── channelCount         — runtime channel count
  ├── dataType             — float/double
  ├── inPlace              — can host reuse buffer for input+output?
  └── ...

AudioBus (non-owning span)             include/AudioBus.h
  └── std::span<const SampleFrame>   — collection of track-channel pairs

AudioPorts (interface)                  include/AudioPorts.h
  ├── AudioPortsModel      — pin connection matrix (runtime)
  ├── AudioPorts::Buffer   — input/output buffer accessor
  ├── AudioPorts::Router   — routing logic via AudioPortsModel
  ├── active/inactive      — plugin-not-loaded state
  └── ...

AudioPortsModel (pin matrix)           include/AudioPortsModel.h
  ├── channel counts (runtime)
  ├── channel names
  └── std::vector<std::vector<BoolModel*>>  — rows=track ch, cols=plugin ch

PinConnector (GUI)                      src/gui/PinConnectorView.cpp
  └── Qt matrix widget (BoolModel checkboxes)

ConfigurableAudioPorts                 include/ConfigurableAudioPorts.h
  └── runtime switch: RemotePlugin shared-mem vs local buffer

CustomAudioPorts (CRTP convenience)   include/CustomAudioPorts.h

PluginAudioPorts                       include/PluginAudioPorts.h
  └── general-purpose AudioPorts for native plugins

RemotePluginAudioPorts                 include/RemotePluginAudioPorts.h
  └── AudioPorts specialization for out-process VSTs
```

*(Class list verified against findings-pr7459-deepdive.md §1.1)*

### 3.2 The 5 Hard Constraints & Mitigations

Each constraint from `findings-lmms-mixer-internals.md §10` is mapped to a specific design response:

| # | Constraint | Source (Verified) | Mitigation in This Design |
|---|-----------|-------------------|---------------------------|
| 1 | **In-place FX processing** — `EffectChain::processAudioBuffer(AudioBuffer&)` and every `Effect::processAudioBuffer(AudioBuffer&)` modify the channel's `AudioBuffer` in-place. There is no wet/dry split. | `src/core/EffectChain.cpp:187`, `include/Effect.h:69` (`processAudioBuffer(AudioBuffer&)` → `processImpl(SampleFrame*, f_cnt_t)` at line 164) | Sidechain taps are **read-only snapshots** taken **before** the FX chain runs (pre-fx) or **after** but before fader (post-fader). The tap copies into a separate `AudioBus` buffer — never shares the in-place working buffer. The channel's own processing remains in-place and unchanged. |
| 2 | **Single `AudioBuffer` per channel** — `MixerChannel::m_buffer` accumulates all sources (instruments, sends) additively via `MixHelpers::add()`. No origin tracking. | `include/Mixer.h:58`, `src/core/Mixer.cpp:642` (`mixToChannel`) | Sidechain channels get a **second dedicated `AudioBuffer`** (`m_sidechainBuffer`) for tapped audio. This buffer is populated by sidechain sends (not regular sends) and is exposed as a *read-only* extra input to the channel's FX chain. Regular sends still accumulate into `m_buffer` as before. |
| 3 | **Threaded dependency scheduling** — `masterMix()` uses a dynamic parallel job queue with per-channel `m_dependenciesMet` atomic counter. Channels start when all senders delivered. | `include/Mixer.h:87` (`m_dependenciesMet std::atomic_size_t`), `src/core/Mixer.cpp:668` (`masterMix`) | Sidechain sends are **excluded from dependency counting** — they tap audio without accumulating into the destination's primary buffer. A sidechain send from A→B does NOT add to B's `m_dependenciesMet`. This prevents the deadlock scenario where B waits for A while A waits for B (or more complex cycles). Parallel buses register as normal graph nodes with their own dependency count. (§5 details the scheduling extension.) |
| 4 | **Fixed stereo `SampleFrame`** — `SampleFrame` is a class with two `sample_t` (float) samples, left/right, internally stored as `std::array<sample_t, DEFAULT_CHANNELS>` with `DEFAULT_CHANNELS = 2`. Every buffer operation, every Effect, every MixHelpers function operates on stereo-frames. | `include/SampleFrame.h:40` (class), `include/SampleFrame.h:189` (`std::array<sample_t, DEFAULT_CHANNELS> m_samples`), `include/lmms_constants.h:38` (`DEFAULT_CHANNELS = 2`), `include/AudioBufferView.h:626` (multi-channel capability exists but unused) | PR #7459's `AudioBus` wraps `std::span<const SampleFrame>` — each SampleFrame remains a stereo-pair, but multiple pairs are carried as an array. The channel-count extension happens at the **bus level**, not the frame level. Native plugin overloads for deinterleaved (split) processing are added; interleaved overloads for existing native plugins are deferred pending the `SampleFrame` redesign debate. The `AudioBufferView` (626 LOC) already provides the multi-channel planar abstraction — this design activates it. |
| 5 | **XML serialization backward compatibility** — `Mixer::saveSettings` writes `<send channel="N"><amount value="F"/></send>` — `FloatModel::saveSettings` serializes `amount` as a child element, not a flat attribute. Loader uses `attribute("channel").toInt()`. | `src/core/Mixer.cpp:817` (save: `send->amount()->saveSettings(_doc, sendsDom, "amount")`), `src/core/Mixer.cpp:836-883` (load) | New elements (`<sidechain-send>`, `<bus>`) use **different node names** from the legacy `<send>`. The `JournallingObject` framework dispatches by `nodeName()`, so old `<send>` elements load unchanged → channels default to stereo. New `<sidechain-send>` elements have `channel`, `amount`, `mode` (pre/post), `dstChan` (3/4) attributes. A `<bus>` element wraps a `<mixerchannel>` element with an additional `<bus-sources>` list. File version flag in `<mixer version="2">` enables format migration. |

### 3.3 Pin Connector Routing Rules

From PR #7459 description, preserved verbatim:

1. **Reading** from a track channel (LMMS → plugin input) does **not** affect the track channel's audio data.
2. **Writing** to a track channel (plugin output → LMMS) **overwrites** whatever was in that track channel.
3. If a plugin output does **not** write to a track channel, the channel is **unmodified** (bypass).
4. If **multiple inputs** route to a single output channel, they are **summed**.
5. Default: only the 2 main L/R track channels are connected; additional user-defined channels require manual connection.

---

## 4. Sidechain Design

### 4.1 Channel-Pair Model (Reaper Pattern)

Sidechain is implemented as **channel-pair routing**, not a dedicated data type. The design follows Reaper's proven approach (*findings-daw-routing-models.md §3*):

```
MixerChannel A (source, e.g. kick drum)
  │
  ├── Regular send → B[ch 1/2]  (normal audio path, post-fader, with gain)
  │
  └── Sidechain send → B[ch 3/4] (sidechain tap, no gain, excluded from dep graph)
```

Implementation:

- **Destination channel must have ≥4 channels.** `AudioPortsSettings::channelCount` is set to ≥4 when any sidechain send targets this channel.
- **Sidechain send properties:**
  - `srcChan`: source channel pair (default 0 = channels 1/2)
  - `dstChan`: destination channel pair (default 2 = channels 3/4, 0-indexed)
  - `mode`: 0=post-fader, 1=pre-fx, 2=pre-fader (post-FX), 3=post-fader no gain
  - `amount`: send level (ignored for mode 3)
- **Plugin pin mapping:** The compressor/gate/expander on the destination channel maps its sidechain input to channels 3/4 via the `PinConnector` matrix widget (BoolModel checkbox row for plugin-input ← track-ch-3/4).

### 4.2 Resolving the Single-AudioBuffer Constraint

As documented in *findings-lmms-mixer-internals.md §10(2)*, each `MixerChannel` has exactly one `m_buffer`. When sidechain audio arrives, it must not be merged into the main buffer before FX processing, or the effect cannot distinguish "main signal" from "sidechain signal."

**Resolution — Split-Buffer Approach:**

1. Each `MixerChannel` gains a new member: `AudioBuffer m_sidechainBuffer`.
2. Sidechain sends accumulate into `m_sidechainBuffer` (not `m_buffer`).
3. The `EffectChain::processAudioBuffer()` signature is extended to accept an **optional second buffer**:
   ```cpp
   bool processAudioBuffer(AudioBuffer& buffer, AudioBuffer* sidechainBuffer = nullptr);
   ```
4. Effects that support sidechain (compressors, gates, expanders) read from `sidechainBuffer` as an additional input alongside `buffer`. Effects without sidechain support ignore the second argument (the nullptr default).
5. After the FX chain processes, `m_sidechainBuffer` is cleared for the next period.
6. Regular sends still accumulate into `m_buffer` via `MixHelpers::add()` — unchanged.

This preserves the in-place processing model (Constraint 1) while providing origin-tracked sidechain audio.

**Sidechain reachability for Effect subclasses:** The `Effect::processImpl(SampleFrame*, const f_cnt_t)` signature is the pure-virtual method every effect subclass overrides — it takes raw `SampleFrame*`, not `AudioBuffer&`. To avoid modifying every subclass, the sidechain buffer is delivered via an `EffectChain`-owned reference that effects query through a protected method. Two compatible approaches:
- **Thread-local `currentSidechainBuffer`** set by `EffectChain::processAudioBuffer` before iterating effects, accessible via `Effect::sidechainBuffer()`.
- **`EffectChain` member pointer** exposed as `const AudioBuffer* EffectChain::sidechainBuffer() const`, with `Effect` holding a back-pointer to its chain.

Both avoid touching the `processImpl` signature. Effects that do not support sidechain (the vast majority) simply ignore the query — the default returns `nullptr`.

### 4.3 Pre/Post-Fader Tap Points

Sidechain sends can tap audio at these points in the channel signal flow (*mapping Reaper's `I_SENDMODE` 0–3*):

| Mode | Tap Point | Signal Value | LMMS Equivalent |
|------|-----------|-------------|-----------------|
| Pre-fx | Before `EffectChain::processAudioBuffer` | Raw channel buffer (pre-effects) | `m_buffer` as populated by instruments/sends |
| Pre-fader | After `EffectChain::processAudioBuffer`, **before** `m_volumeModel` scaling | Processed signal, pre-fader | Channel buffer after FX, before volume multiply |
| Post-fader | After `m_volumeModel` scaling, **same** as regular send | Fader-scaled signal | Same as current `MixerChannel` output to sends |
| Post-fader (no gain) | After fader, sent at unity | Identical to post-fader but `amount` forced to 1.0 | Reaper's `I_SENDMODE=3` |

The tap point determines which `AudioBuffer` snapshot is copied into the sidechain send's buffer — always a **copy**, never a reference, to avoid modifying the source channel's output.

### 4.4 XML Schema Extension

`Mixer::saveSettings` extends its XML output. Backward-compatible parsing:

```xml
<!-- Legacy format (unchanged): -->
<mixer>
  <mixerchannel num="0" name="Master">
    <effectchain>...</effectchain>
    <volume/><muted/><soloed/>
  </mixerchannel>
  <mixerchannel num="1" name="Kick">
    <send channel="0"><amount value="1.0"/></send>
    <sidechain-send channel="2" amount="1.0" mode="0" srcChan="0" dstChan="2"/>
  </mixerchannel>
  <mixerchannel num="2" name="Bass" channels="4">
    <!-- has 4 channels; channels 3/4 receive sidechain -->
    ...
  </mixerchannel>
</mixer>
```

- **Old files:** `<send>` loads via existing path → works without changes.
- **New files without sidechain:** Identical to old format (`channels` attribute defaults to 2).
- **New files with sidechain:** `<sidechain-send>` nodes are skipped by old loaders (unknown `nodeName()`), so old LMMS simply doesn't load sidechain routing — the project opens but sidechain is silent.
- The `channels` attribute on `<mixerchannel>` is new. Old loaders ignore unknown attributes, so channels default to 2.

---

## 5. Graph Scheduling

### 5.1 Current `masterMix()` Dependency Model

*Source: findings-lmms-mixer-internals.md §2, src/core/Mixer.cpp:668*

```
masterMix(outputBuf):
  1. Build job list: muted channels → processed() immediately; leaf channels (no sends) → AudioEngineWorkerThread::addJob()
  2. Spin-wait polling loop:
     while master channel not Done:
       scan all channels:
         if any channel is Queued or InProgress:
           AudioEngineWorkerThread::startAndWaitForJobs()
           continue scanning
       if none found → break
  3. For each non-master channel in parallel (via doProcessing):
     a. EffectChain::processAudioBuffer(buf)
     b. For each m_sends: MixHelpers::addMultiplied(recv_buf, src, amount)
     c. Increment deps on receivers
     d. processed() → signal deps met
  4. Apply master volume → outputBuf
  5. Clear all channel buffers, reset process state, clear m_dependenciesMet
```

(The actual code (Mixer.cpp:694-712) does NOT use a concurrent ready queue — it polls `ThreadableJob::ProcessingState` on every channel in a busy-loop. The proposed extension in §5.2 replaces this polling with a lock-free ready queue, which requires explicit pre-allocation as noted below.)

A channel starts when `m_dependenciesMet == 0` (all senders delivered). This works for a directed acyclic graph but breaks if any edge participates in a cycle, is deferred, or carries sidechain metadata that shouldn't gate the destination.

### 5.2 Extension: PipeWire Atomic Pending-Counter Scheduler

The PipeWire pattern from *findings-graph-runtimes.md §2.2* applies directly:

> PipeWire uses an atomic pending-counter: each node's activation record has `required` (total deps) and `pending` (unsatisfied deps). The driver starts a cycle by setting all followers' `pending = required`. When a node's `pending` reaches 0 via atomic decrement, its `eventfd` signals → node processes. After processing, the node decrements `pending` on all its targets.

**Applied to LMMS `masterMix()`:**

```
New masterMix() pseudocode:

  // --- CONTROL THREAD (between periods) ---
  rebuildGraph():
    for each MixerChannel:
      node.required = count of incoming regular sends (NOT sidechain sends)
      node.pending  = node.required

  // --- AUDIO THREAD (each period) ---
  masterMixCycle():
    // 1. Seed: channels with required==0 start immediately
    for each channel where required==0:
      push to ready queue

    // 2. Process loop
    while ready queue not empty:
      channel = pop(ready queue)

      // 2a. Sidechain tap (pre-fx if mode=pre-fx)
      for each outgoing sidechain-send:
        snapshot = copy(channel.m_buffer, mode)  // select tap point
        write snapshot to target.m_sidechainBuffer

      // 2b. FX chain
      channel.effectChain.processAudioBuffer(channel.m_buffer,
                                             &channel.m_sidechainBuffer)

      // 2c. Regular sends (post-fx)
      for each regular send:
        MixHelpers::addMultiplied(target.m_buffer, channel.m_buffer, send.amount)

      // 2d. Sidechain sends (post-fx, if mode != pre-fx)
      for each outgoing sidechain-send that is post-fx:
        snapshot = copy(channel.m_buffer, mode)
        write to target.m_sidechainBuffer

      // 2e. Decrement targets' pending counters
      for each target in regular_sends:
        if atomically(--target.m_pending) == 0:
          push target to ready queue

      // Sidechain sends do NOT touch target.m_pending

    // 3. Wait for master Done (master has m_pending going to 0)
```

> **Audio-thread allocation constraint:** The `push`/`pop` operations on the ready queue in `masterMixCycle()` run on the audio thread and must never allocate memory. The ready queue must be a pre-allocated, fixed-capacity, lock-free SPSC ringbuffer (sized to `numChannels`). If a lock-free ringbuffer is not available, the current spin-wait polling pattern (looping over `ThreadableJob::ProcessingState` on all channels — see §5.1 current-code description) can be extended to handle sidechain sends instead, at the cost of CPU burn under contention.

This design guarantees:
- **No deadlock from sidechain edges:** sidechain sends never increment `pending` on the target, so they cannot create circular wait conditions.
- **Natural parallelism:** channels with independent sources process concurrently.
- **Deterministic completion:** the graph always terminates because only DAG-edges gate execution; sidechain edges are observation-only.

### 5.3 Topology Sort Integration (JUCE Pattern)

For graph rebuilds (channel add/delete, route create/delete), a JUCE-style topology sort verifies graph validity:

```
topologySort():
  // Collect all regular-send edges (NOT sidechain edges)
  // Run Kahn's algorithm (BFS, in-degree counting)
  // If cycle detected: roll back graph edit, report error
  // Else: assign processing order hint for work-stealing
```

The topology sort result is stored as a `std::vector<MixerChannel*>` processing hint. It is *not* the execution order — execution uses the atomic pending-counter — but it provides:
- Cycle detection (existing `isInfiniteLoop` is DFS-based; this extends to include parallel buses)
- Cache-friendly work-stealing hints for the worker thread pool
- A fallback sequential order for single-threaded fallback mode

### 5.4 Thread-Safety Model for Live Graph Edits

Adopting the **deferred-graph-edit pattern** from JACK (*findings-graph-runtimes.md §1.4*) and JUCE's `processLock` (*findings-graph-runtimes.md §4.3*) with improvements from PipeWire's two-thread architecture:

| Aspect | Mechanism | Source |
|--------|-----------|--------|
| Audio thread | Reads snapshot of graph topology set up before cycle; never allocates, never locks on graph data | JACK "deferred graph reconfiguration" |
| Control thread | Holds `processLock` (JUCE `CriticalSection`) for graph mutations; atomically swaps a **pending-change list** that the audio thread consumes before the next cycle | JUCE `processLock` + PipeWire shared-mem activation records |
| Adding channels | Append to pending-add list; next cycle's `rebuildGraph()` incorporates new node | JACK port registration pattern (deactivate → change → reactivate) |
| Removing channels | Mark tombstone in pending-remove list; next cycle stops processing, then deallocates | PipeWire main-thread-only topology changes |
| Sidechain send create | Edits `m_sidechainSends` vector (new member per channel) under lock; next cycle reads updated list | Same as regular send create but excludes from dep counting |
| Concurrent read/write | Two source channels A and B may each hold a sidechain send to target C and be processed **concurrently** by different worker threads — both would write `C.m_sidechainBuffer` simultaneously. This is a data race unless explicitly serialized. **Chosen strategy (a): per-sender intermediate buffers + post-hoc sum.** Each sender owns a private sidechain accumulation buffer; after all senders finish, the target sums the per-sender buffers into `m_sidechainBuffer` before its FX chain runs. This keeps sidechain sends parallel and avoids atomics on the audio path. | Design invariant — writers to `m_sidechainBuffer` are always accumulated/serialized via per-sender intermediates; never direct concurrent writes from multiple workers. |

**Live edit safety proof:** All graph mutations happen in the control thread between periods. The audio thread runs a pre-built snapshot. No mutex is taken on the audio path. This is the same model PipeWire uses for its main-thread / data-thread split.

### 5.5 Parallel Bus Nodes

A parallel bus is a `MixerChannel` with the `isBus` flag set. Key differences from regular channels:

- A bus does NOT receive instrument output directly (no `mixToChannel` from `InstrumentTrack`).
- A bus has its own `m_fxChain` (value-embedded, like regular channels).
- Sends to a bus are **pre-fader by default** (the sending channel's fader does not affect the bus's input).
- The bus's processed output is accumulated to its regular-send targets (master or other channels).
- Buses participate fully in dependency scheduling — they are graph nodes with `required`/`pending` counters.

*Source for bus semantics: Reaper (no fixed track types — any track is a potential bus), Cubase Group Channels, Ardour Bus routes (findings-daw-routing-models.md §3–§5).*

> **Decision D1 (resolved 2026-09-08, project owner): pre-fader sends require reordering `doProcessing()`.** Volume multiplication moves AFTER the send loop in `MixerChannel::doProcessing()` (Mixer.cpp:164), making pre-fader bus sends the native default with no per-send toggle needed in v1. Accept consequence: the reorder touches every channel's processing path, so Phase D must include a before/after render A-B regression test on a multi-channel project to prove byte-identical output for projects that use no bus sends. Phase D effort estimate revised upward accordingly.

---

## 6. Phased Implementation Plan

Each phase is an independent, mergeable PR. This phasing directly addresses the **"too big to review" blocker** that stalled PR #7459 for 2+ years.

### Phase A: Extract PinConnector Model (~400 LOC)

| Aspect | Detail |
|--------|--------|
| **Goal** | Land `AudioPortsSettings`, `AudioPortsModel`, and `PinConnector` as standalone data types — no audio pipeline changes, no Mixer integration. |
| **Files created** | `include/AudioPortsSettings.h` (POD struct, NTTP), `include/AudioPortsModel.h` (BoolModel matrix) |
| **Files modified** | None — new files only |
| **Dependencies** | None on other phases; builds on existing `BoolModel`/`JournallingObject` |
| **Verification** | Unit test: create an `AudioPortsModel` with 4×4 BoolModel matrix, set/reset entries, verify serialization round-trip. Existing mixer tests continue to pass. |
| **Effort** | 1–2 days |
| **Review size** | ~400 lines across 2 new headers — trivially reviewable |

The PinConnector GUI widget (`src/gui/PinConnectorView.cpp`) is deferred to Phase C so Phase A has no Qt dependency beyond `BoolModel`.

### Phase B: AudioPorts Core (~900 LOC)

| Aspect | Detail |
|--------|--------|
| **Goal** | Land `AudioBus`, `AudioPorts` interface, `AudioPorts::Buffer`, `AudioPorts::Router`, `ConfigurableAudioPorts`, `CustomAudioPorts`, `PluginAudioPorts` — the full audio transport layer. |
| **Files created** | `include/AudioBus.h`, `include/AudioPorts.h`, `include/ConfigurableAudioPorts.h`, `include/CustomAudioPorts.h`, `include/PluginAudioPorts.h`, `include/PluginAudioPortsBuffer.h` |
| **Files modified** | None (still no integration with Mixer or plugins) |
| **Dependencies** | Phase A (`AudioPortsModel`, `AudioPortsSettings`) |
| **Verification** | Unit test: connect two `PluginAudioPorts` via `AudioPorts::Router`, process 48-sample block of multi-channel audio, verify channel-count preservation and buffer routing. |
| **Effort** | 3–5 days |
| **Review size** | ~900 lines across 6 new headers (no .cpp beyond trivial constructors) — manageable |

PinConnector GUI still deferred to keep Phase B purely about audio transport.

### Phase C: Mixer Integration + Dynamic Channels (~1500 LOC)

| Aspect | Detail |
|--------|--------|
| **Goal** | Connect `AudioPorts` to `MixerChannel` — replace `MixerRoute` with dynamic channel-pair routes; add PinConnector GUI; enable VST2 multi-channel via `RemotePluginAudioPorts`. |
| **Files created** | `include/RemotePluginAudioPorts.h`, `include/RemotePluginAudioPortsController.h`, `src/gui/PinConnectorView.cpp` |
| **Files modified** | `include/Mixer.h` (replace `MixerRoute` with `AudioBus`-based routing), `src/core/Mixer.cpp` (buffer management for multi-channel), `include/RemotePlugin.h` (AudioPorts integration), `src/core/RemotePlugin.cpp` (shared-memory buffer routing), `include/RemotePluginClient.h` (float* buffers), `plugins/Vestige/Vestige.cpp` (VST2 multi-channel init, pin connector button), `plugins/VstBase/VstPlugin.cpp` (VST3 bus enumeration), `include/Plugin.h` (PinConnector ownership), `include/SampleTrack.h` (SampleTrack pin connector), `include/TrackOperationsWidget.h` (pin connector button in context menu), `include/AudioPlugin.h` |
| **Dependencies** | Phase A + Phase B |
| **Verification** | (1) Load a multi-out VST2 instrument (e.g. Kontakt, EZDrummer) → verify each output channel pair appears in PinConnector matrix. (2) Route outputs to separate mixer channels. (3) Verify mono input plugin (e.g. simple synth) still works with default stereo routing. |
| **Effort** | 2–3 weeks |
| **Review size** | Largest phase, but split into 3 sub-commits: (C1) MixerChannel AudioBus migration, (C2) RemotePlugin rework, (C3) GUI. Each sub-commit < 600 lines. |

> **Decision D2 (resolved 2026-09-08, project owner): Phase C sub-commits are NOT assumed independent — split C2 out if layering fails.** Plan remains: merge C1 (MixerChannel AudioBus migration) first, freeze its API, then build C2 (RemotePlugin rework) against the frozen API. If C2 cannot layer cleanly on C1's final API, C2 is split into its own standalone phase with separate review — do NOT bundle interdependent sub-commits. This matches the upstream lesson from PR #7459 (168 files, unreviewable).

This is the phase that **unblocks** VST3/CLAP hosting, multi-out instruments, and sidechain detection.

### Phase D: Sidechain + Parallel Buses (~1000 LOC)

| Aspect | Detail |
|--------|--------|
| **Goal** | Native sidechain sends (channel-pair mode), pre/post-fader tap points, parallel bus channels, split-buffer for sidechain audio, XML schema extension. |
| **Files created** | None new — additions to existing files |
| **Files modified** | `include/Mixer.h` (add `m_sidechainBuffer`, `m_sidechainSends`, `isBus` flag, send mode enum), `src/core/Mixer.cpp` (sidechain send processing, bus dispatch, split-buffer management), `include/EffectChain.h` (extend `processAudioBuffer` with optional sidechain buffer parameter), `src/core/EffectChain.cpp` (pass sidechain buffer to effects), `include/Effect.h` (sidechain query accessor so effects can reach it without changing every subclass), `src/core/Mixer.cpp` (XML save/load for `<sidechain-send>` and `<bus>`), `include/AudioPortsModel.h` (runtime channel count changes for sidechain destinations) |
| **Dependencies** | Phase C (Mixer integration must be complete) |
| **Verification** | (1) Create sidechain send from kick channel (A) to compressor channel (B) — use channel-pair mapping to channels 3/4. (2) Load compressor that reads sidechain input → verify ducking. (3) Verify pre-fx vs pre-fader vs post-fader tap points produce different sidechain signals. (4) Create parallel bus channel, route 3 source channels to it, verify summed FX output. (5) Open old project file (no sidechain) → verify it loads identically. (6) Open new project file with sidechain in old LMMS → verify it loads (sidechain silently dropped). |
| **Effort** | 2–3 weeks |
| **Review size** | ~1000 LOC across 7 modified files — modular by design |

### Phase E: Deferred Backends (LV2/CLAP) — Tentative

| Aspect | Detail |
|--------|--------|
| **Goal** | Native LV2 multi-channel port enumeration; CLAP `audio-ports-config` extension support; LADSPA bridging (fixed stereo). |
| **Files created** | Per-backend `AudioPorts` specializations |
| **Files modified** | Existing LV2/CLAP host files in `plugins/` |
| **Dependencies** | Phase C (AudioPorts core required for any backend) |
| **Verification** | Load LV2 multi-out instrument → verify channel enumeration matches `lv2-info` output. Load CLAP plugin with configurable port layout → verify `AudioPortsSettings` rebuilds with new config. |
| **Effort** | 1–2 weeks per backend |
| **Review size** | Small — backend bridges are isolated per file |

This phase is **explicitly tentative** pending upstream CLAP PR #7199 and LV2 multi-channel specification maturity.

---

## 7. Risks & Mitigations

| # | Risk | Likelihood | Impact | Mitigation |
|---|------|-----------|--------|------------|
| 1 | **Phase C diff still too large** even after splitting — reviewer fatigue repeats PR #7459's stall. | Medium | Critical (blocks entire roadmap) | Sub-commit within Phase C (C1/C2/C3 below 600 LOC each); pre-announce the phased strategy in the PR description; mention sakertooth/JohannesLorenz concerns explicitly; offer to walk through sub-commits in review calls. |
| 2 | **`SampleFrame` redesign debate reignites** — sakertooth's Apr 2025 concern (SampleFrame is the root problem) blocks Phase A acceptance. | Medium | High (delays Phase A) | Explicitly state in Phase A that `AudioPortsModel`/`AudioPortsSettings` are **not** a `SampleFrame` replacement — they work with `SampleFrame` as-is. Open a separate issue for the `SampleFrame` redesign so it's tracked separately from this PR sequence. |
| 3 | **Sidechain performance overhead** — split-buffer approach doubles memory bandwidth for sidechain-tapped channels. | Low | Medium | Benchmark before/after: measure L1/L2 cache misses and buffer copy time. The sidechain tap is a `memcpy` of `frames × 2 × sizeof(float)` bytes per channel per period — ~4 KB at 48 kHz/256 frames — negligible. If measurable, use lazy copy (sidechain buffer as shared_ptr with COW). |
| 4 | **Thread-safety regression** — live graph edit during audio processing causes use-after-free on sidechain buffers. | Low | Critical (audio crash) | Enforced by architecture: the control thread never touches buffers that the audio thread reads in the same cycle. Sidechain buffer writes happen *only* during sidechain send processing (audio thread), and the control thread only modifies the topology between cycles via pending-change list. |
| 5 | **LV2/CLAP backends never merge** — upstream CLAP PR #7199 stagnates or conflicts with this architecture. | Medium | High (Phase E incomplete) | Acceptable: Phase C gives VST2 multi-channel today. Phase D gives sidechain usable with VST2 compressors. CLAP/LV2 are additive, not blocking. Document the `AudioPortsSettings` ↔ CLAP `audio-ports-config` mapping (findings-pr7459-deepdive.md §4) so future implementors have a blueprint. |
| 6 | **Project file migration** — old LMMS loads new sidechain project and silently drops sidechain data → users lose routing on downgrade. | Medium | Low (user-facing, recoverable) | Document in release notes that sidechain projects require LMMS ≥ version X. The `version` attribute on `<mixer>` triggers a warning dialog in old LMMS: "This project uses sidechain routing not supported by this version." |

---

## 8. Verification & Test Plan

### 8.1 Per-Phase Verification

| Phase | Verification Criterion | Type | Success Condition |
|-------|----------------------|------|-------------------|
| **A** | `AudioPortsModel` unit test | Automated (Catch2) | Create 4×4 matrix → set entries → serialize/deserialize → entries match |
| **A** | Existing mixer tests pass | Automated | `test/mixer/` (wherever mixer tests live) — 0 failures |
| **B** | `AudioBus` channel-pair routing test | Automated | Connect 2 `PluginAudioPorts` via `Router` → process 48 frames of 4-channel audio → output preserves per-channel data |
| **B** | `AudioPorts` active/inactive state | Automated | Deactivate port → verify buffer reads return silence |
| **C** | Multi-out VST2 instrument test | Manual (smoke) | Load Kontakt/EZDrummer → verify 8+ output channels appear in PinConnector → route each output to separate mixer channel → verify distinct audio per channel |
| **C** | Mono VST2 regression test | Manual | Load simple mono synth → verify standard stereo routing works (signal identical on L/R) |
| **C** | PinConnector GUI test | Manual | Open pin connector → verify BoolModel checkboxes toggle → routing changes reflected in output |
| **D** | Sidechain ducking test | Manual + Automated (visual waveform check) | Create kick→compressor sidechain route → verify compressor gain reduction synchronized with kick transient |
| **D** | Pre/post tap test | Automated | Route sidechain pre-fx vs post-fader → verify signal level difference equals fader gain |
| **D** | Parallel bus test | Automated | Create bus channel, send 3 channels to it → verify bus output equals summed inputs processed through bus FX chain |
| **D** | XML backward compat test | Automated | Load legacy `.mmp` project → verify all routes, volumes, FX chains match original |
| **D** | XML forward compat test | Manual | Save project with sidechain → open in old LMMS → verify no crash, sidechain routing absent (expected) |
| **E** | LV2 port enumeration | Manual | Load LV2 multi-out plugin → verify channel count matches `lv2-info` |
| **E** | CLAP port config switch | Manual | Load CLAP plugin → switch port config → verify `AudioPortsSettings` rebuilds correctly |

### 8.2 Mission Success Criteria (from `lmms-mixer-routing-mission`)

| Criterion | How Verified | Phase |
|-----------|-------------|-------|
| Dynamic channel allocation (not fixed 64) | Create 100+ mixer channels via API → verify no 64-artifact limit | C |
| Multi-channel plugin I/O (VST2) | Multi-out VST2 load test (above) | C |
| Native sidechain sends | Sidechain ducking test (above) | D |
| Parallel effect buses | Parallel bus test (above) | D |
| Backward-compatible project files | XML backward compat test (above) | D |
| PR mergable (not "too large to review") | Confirmed by reviewers (sakertooth, JohannesLorenz) | A→E (each phase) |

### 8.3 Performance Regression Gate

Before merging Phase D, a performance regression test must pass:

```
Test: 64-channels, each with 3 sidechain sends + 2 regular sends
      48 kHz, 256 frames/period, 10 seconds runtime
Threshold: <5% increase in CPU time vs Phase C baseline (no sidechain)
```

If the split-buffer `memcpy` overhead exceeds 5%, implement lazy-copy (allocate sidechain buffer only for channels with active sidechain sends).

> **Decision D3 (resolved 2026-09-08, project owner): performance gate is per-send.** Metric locked: "<5% single-core CPU per ACTIVE sidechain send, measured against the baseline cost of a regular send, at 48 kHz / 256-frame buffer." The per-channel 5% figure is dropped. The §8.3 gate table and any test harness must implement this per-send comparison.

---

## 9. Sources

### Findings Files

| File | Content |
|------|---------|
| `findings-daw-routing-models.md` | FL Studio, Ableton, Reaper, Cubase, Ardour, Bitwig, Pro Tools routing architectures |
| `findings-pr7459-deepdive.md` | Full PR #7459 class hierarchy, CLI mapping, VST3 bus mapping, review history, blockers |
| `findings-lmms-mixer-internals.md` | Verified data-structure inventory on commit 4e677cb, 5 hard constraints, processing path |
| `findings-graph-runtimes.md` | JACK, PipeWire, Carla, JUCE graph models; thread-safety analysis; license compatibility |

### Consolidated URLs

**PR #7459 & LMMS internals:**
- PR #7459: https://github.com/LMMS/lmms/pull/7459
- LMMS source clone: commit 4e677cb at `/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/lmms-fl-research/lmms`
- Messmerd branch: `messmerd/lmms:pin-connector`

**DAW architecture references:**
- FL Studio Mixer/Sends: https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/mixer_sendsidechain.htm
- FL Studio Plugin Wrapper: https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/plugins/wrapper.htm
- FL Studio Mixer Overview: https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/mixer.htm
- Ableton Live Routing: https://www.ableton.com/en/manual/routing-and-i-o/
- Ableton Live Sidechain: https://www.ableton.com/en/manual/live-audio-effect-reference/
- Reaper ReaScript API: https://www.reaper.fm/sdk/reascript/reascripthelp.html
- Cockos SDK: https://www.reaper.fm/sdk/sdk.php
- Cubase Signal Chain: https://skippystudio.nl/2025/08/cubase-signal-chain/
- Ardour Route Doxygen: https://community.ardour.org/files/doxygen/classARDOUR_1_1Route.html
- Bitwig Sidechaining: https://www.bitwig.com/learnings/sidechaining-tutorial-49/
- Bitwig Routing Guide: https://www.bitwig.com/userguide/latest/routing/
- Pro Tools Reference: https://avid.com
- Pro Tools Latency: https://www.production-expert.com/production-expert-1/a-to-z-of-pro-tools-l-is-for-latency

**Graph runtime references:**
- JACK API: https://jackaudio.org/api/
- JACK Design: https://jackaudio.org/files/design.pdf
- JACK Ringbuffer: https://jackaudio.org/api/ringbuffer_8h_source.html
- PipeWire Overview: https://docs.pipewire.org/page_overview.html
- PipeWire Scheduling: https://docs.pipewire.org/page_scheduling.html
- Carla Source: https://github.com/falkTX/Carla (CarlaEngineGraph.hpp / CarlaEngineGraph.cpp)
- Carla Manual: https://kx.studio/Documentation:Manual:Carla
- JUCE AudioProcessorGraph API: https://docs.juce.com/master/classjuce_1_1AudioProcessorGraph.html
- JUCE Source: https://github.com/juce-framework/JUCE (modules/juce_audio_processors_headless/)
- JUCE Thread-Safety: https://forum.juce.com/t/audioprocessorgraph-thread-safety/24419
- Reason Rack Manual: https://archive.org/details/manuals_202401
- Reason Developer Docs: https://developer.reasonstudios.com/documentation/

---

> **End of Specification — Dynamic Multi-Channel Routing for LMMS**
>
> Prepared for AI-KOS task 555 under program `lmms-fl-replacement-program`,
> mission `lmms-mixer-routing-mission`. All file paths and class claims are
> verified against the LMMS clone at commit 4e677cb and PR #7459 branch source.