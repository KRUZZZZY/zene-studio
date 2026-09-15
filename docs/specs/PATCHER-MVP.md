<!--
  In-repo copy of the specification this repository's code and tests cite (DOC-5, 2026-09-15).
  This is a PINNED SNAPSHOT, not a live document:
    source   : the program workspace, PATCHER-MVP.md
    sha256   : 91fb1d5014ae7f3af2a8f7bf15690a85bbf1e6f69cdb6c305cbc83966379b28f
    bytes    : 13786
    why this file: the patcher/graph MVP scope; cited by include/RoutingGraph.h and include/EffectChain.h
  The workspace copy remains the working copy and is where the document is edited; refreshing
  this snapshot is a deliberate act, recorded in the commit that does it. If a citation in this
  repository and this file ever disagree, reconcile them in one commit - the citations name the
  file by bare name (e.g. `SPEC-zene-studio.md`), and this directory is where that name resolves.
  See docs/specs/README.md for the convention and for the cited documents NOT yet committed.
-->
# PATCHER-MVP.md — task #562 spike: internal-node DAG in ONE instrument channel

**Worktree:** `lmms-patcher/` · **branch:** `feat/patcher-mvp` (off upstream `4e677cb6c`)
**Commits:** `fadd04a83` (engine), `f92231c04` (tests) — local only, no push.
**Scope guard:** no file under `src/core/Mixer.*`, `include/Mixer.h`, `src/core/EffectChain.cpp`
or `include/EffectChain.h` was touched. `git diff --stat 4e677cb6c HEAD` = 9 files, +1371/−0.

---

## 1. What was built

| File | Role |
|---|---|
| `include/RoutingNode.h` | Node base: ports, `prepare()`/`process()`, `saveSettings()`/`loadSettings()`, private output buffers + input wiring (`friend RoutingGraph`) |
| `include/RoutingGraph.h` | DAG: `addNode`/`removeNode`/`connect`/`disconnect`, cached plan, `setOutputNode`, `process(AudioBuffer&)`, XML `save`/`load`, node factory |
| `include/RoutingNodes.h`, `src/core/RoutingNodes.cpp` | `ConstantSourceNode`, `OnePoleLowPassNode` (`y[n] = y[n-1] + alpha*(x[n]-y[n-1])`), `GainNode`, `SinkNode` |
| `src/core/RoutingGraph.cpp` | Kahn topological sort, cycle rejection at `connect()` time, plan caching, serialization |
| `src/core/RoutingNode.cpp` | Buffer allocation (`prepare`), zeroing, input summing |
| `tests/src/core/RoutingGraphTest.cpp` | QtTest suite (7 test functions + data rows) |
| `src/core/CMakeLists.txt`, `tests/CMakeLists.txt` | 3 sources + 1 test registered (additive, +4 lines total) |

Design decisions:

- **Per-block evaluation.** `RoutingGraph::process(AudioBuffer&)` walks the cached
  `std::vector<int> m_plan` and then copies the output node's first output into the
  channel buffer. All buffers are allocated in `prepare()` (control thread); the audio
  path only reads the plan and the pre-wired input pointer vectors.
- **Cycle rejection at connect time, never at audio time.** `connect()` pushes the edge,
  runs `rebuildPlan()` (Kahn); on failure it pops the edge and returns `false` with an
  error string. `rebuildPlan()` is the only place that allocates, and it runs on the
  calling (control) thread.
- **Serialization.** `RoutingGraph::save(QDomElement&)` appends
  `<routinggraph version frames channels output>` containing `<node id type>` elements
  (each node writes its own `<param name value>` children) and
  `<connection from fromport to toport>` elements. `load()` rebuilds through the
  `createNode()` factory, remaps file ids and replays `connect()`, so a corrupted file
  containing a cycle is rejected too. Round-trip is exercised through a real `DataFile`
  (`.xpf`) file.

---

## 2. What the MVP proves (real test output)

`cd build/tests && ./RoutingGraphTest` → **exit 0**:

```
********* Start testing of RoutingGraphTest *********
Config: Using QtTest library 6.4.2, Qt 6.4.2 (x86_64-little_endian-lp64 shared (dynamic) release build; by GCC 13.2.0), ubuntu 24.04
PASS   : RoutingGraphTest::initTestCase()
PASS   : RoutingGraphTest::ProcessesKnownBlock(dc-1.0-alpha-0.5-gain-0.25)
PASS   : RoutingGraphTest::ProcessesKnownBlock(dc-0.5-alpha-0.3-gain-2.0)
PASS   : RoutingGraphTest::ProcessesKnownBlock(dc-0.25-alpha-0.9-gain-0.5)
PASS   : RoutingGraphTest::ContinuesAcrossBlocks()
PASS   : RoutingGraphTest::RejectsCycle()
PASS   : RoutingGraphTest::RoundTripsSaveLoad()
PASS   : RoutingGraphTest::ProcessingDoesNotAllocate()
PASS   : RoutingGraphTest::RejectsUnknownNodeType()
PASS   : RoutingGraphTest::cleanupTestCase()
Totals: 10 passed, 0 failed, 0 skipped, 0 blacklisted, 3ms
********* Finished testing of RoutingGraphTest *********
TEST_EXIT=0
```

- **(a) Known block, exact output** — `source -> one-pole low-pass -> gain` evaluated into
  one 48-frame stereo `AudioBuffer`, compared sample-by-sample against an independently
  computed scalar reference for three parameter sets (tolerance 1e-5). `ContinuesAcrossBlocks`
  proves the filter state carries over between consecutive blocks (96 frames).
- **(b) Cycles rejected** — three-node `c -> a`, self-connection `a -> a`, two-node `c -> b`
  and duplicate connections all return `false` with a non-empty error; the edge count and
  processing order stay unchanged and the graph still processes afterwards.
- **(c) Round-trip** — save to a real `DataFile` `.xpf` file, load into a fresh graph:
  node count, connection count, output node, and per-node parameters (`value=0.75`,
  `alpha=0.25`, `gain=0.5`) survive, and the restored graph's output is **bit-identical**
  (`memcmp`) to the original's.
- **(d) No audio-thread allocation** — the test replaces global `operator new` with a
  counting allocator; after a warm-up, one `process()` call performs **0 allocations**.
- **(e) No regressions** — `ctest` from `build/tests`: **9/9 passed** (8 pre-existing +
  the new test; the clean branch has 8 registered tests, so "the pre-existing 9" in the
  task brief is off by one — see §5 for the exact `ctest -N` listing).

---

## 3. What it explicitly does NOT do

- **No Mixer/EffectChain changes.** No multi-channel/bus routing, no sends/receives, no
  sidechain, no `MixerRoute` involvement.
- **No real instrument or effect nodes.** Only constant source, one-pole low-pass, gain
  and sink; no TripleOscillator, no plugin wrapping, no LV2/VST hosting.
- **Not wired into `InstrumentTrack`.** The "instrument channel" is represented by a
  standalone `AudioBuffer`; `InstrumentTrack::processAudioBuffer`
  (`include/InstrumentTrack.h:70`) is untouched. There is no audible/WAV proof.
- **No live plan swap.** `rebuildPlan()` allocates and runs on the control thread; there
  is no double-buffered plan with an atomic swap for editing while audio plays.
- **No GUI, no parameter pins, no MIDI nodes.** No `NodeGraphView`, no
  `AutomatableModel` pin connections.
- **No project-file integration.** The graph round-trips through its own `DataFile`, not
  as part of an `.mmpz` song/project save.

---

## 4. What the full Patcher needs (Part B/C), with clone evidence

### Part B — multi-channel + mixer-as-graph (in flight elsewhere, task #572)

1. **The audio path is stereo-fixed.** `DEFAULT_CHANNELS = 2`
   (`include/lmms_constants.h:38`), `SampleFrame` stores
   `std::array<sample_t, DEFAULT_CHANNELS> m_samples` (`include/SampleFrame.h:189`).
   `AudioBuffer` already supports up to `MaxChannelsPerAudioBuffer = 128`
   (`include/lmms_constants.h:39`, `include/AudioBuffer.h:223`) and the planar
   multi-channel view exists (`include/AudioBufferView.h`, 626 LOC, included from
   `include/AudioBuffer.h:31`) — but nothing in the path drives more than 2 channels.
   The full Patcher needs the AudioPorts core that activates it:
   `mixer/SPEC-dynamic-routing.md` §6 Phase B (line 383) and Phase A pin connectors
   (line 369).
2. **Mixer channels must become graph nodes.** `MixerChannel` is a `ThreadableJob`
   (`include/Mixer.h:45`) with fixed send/receive vectors (`include/Mixer.h:69`,
   `include/Mixer.h:72`) and a per-channel atomic dependency counter
   (`include/Mixer.h:87`); `masterMix()` is at `src/core/Mixer.cpp:668` and
   `createChannelSend()` at `src/core/Mixer.cpp:492`. The extension is specified in
   `mixer/SPEC-dynamic-routing.md`: §5.1 current model (line 228), §5.2 PipeWire atomic
   pending-counter scheduler (line 255), §5.3 topology-sort integration (line 317),
   §5.4 thread-safety for live graph edits (line 334), §6 Phase C mixer integration +
   dynamic channels (line 397).
3. **Sidechain and parallel buses** need the channel-pair model and pre/post-fader taps
   (`mixer/SPEC-dynamic-routing.md` §4, lines 136–197; §6 Phase D, line 413).
4. **Collision avoidance.** Part B is an active workstream on branch
   `part-b-engine-integration` (`PART-B-INTEGRATION.md`, task #572), stacked on
   `part-a-core-abstractions`; it builds on upstream's
   `Effect::processAudioBuffer(AudioBuffer&)` (`include/EffectChain.h:66`; chain is a
   linear `EffectList m_effects`, `include/EffectChain.h:73`). This spike deliberately
   shares no file with it.

### Part C — canvas, pins, live edit (P4 Phase 2)

1. **Node canvas does not exist.** `REPORT.md` P4 proposes `include/gui/NodeGraphView.h`
   with QGraphicsScene (`REPORT.md:325`, `REPORT.md:339`); `grep -rn "NodeGraph"` over
   `src/ include/ plugins/` finds nothing. Existing QGraphicsScene use is limited to the
   mixer channel rename line-edit (`src/gui/MixerChannelView.cpp:30`, `:109`) and the Eq
   plugin curve. Integration point is the existing `src/gui/MixerView.cpp`
   (`REPORT.md:343`).
2. **Parameter pins.** `AutomatableModel` parameters connectable as graph pins are
   required by the playbook (`TASK-PLAYBOOKS.md:383`) and the P4 `AutomationNode`
   (`REPORT.md:336`); not implemented here.
3. **Live graph edit during playback** (`TASK-PLAYBOOKS.md:384`; risk at
   `REPORT.md:368`) requires the lock-free pending-change plan swap of
   `mixer/SPEC-dynamic-routing.md` §5.4 (line 334) — my `rebuildPlan()` is explicitly
   not that.
4. **Full P4 Phase 2 GUI scope**: drag-to-connect, node selection, parameter display,
   `.mmpz` serialization via `DataFile` (`REPORT.md:351-355`).

---

## 5. Verification log (verbatim)

### Build — `cmake -B build -S . -DWANT_QT6=ON && cmake --build build -j4`

Configure tail (`/tmp/patcher-build.log` lines 156–169, verbatim; the `IMPORTANT`
box is CMake's stock reminder — configure and build both succeeded):

```
* Experimental Qt6 support          : Enabled


-----------------------------------------------------------------
IMPORTANT:
after installing missing packages, remove CMakeCache.txt before
running cmake again!
-----------------------------------------------------------------



-- Configuring done (1.4s)
-- Generating done (0.5s)
-- Build files have been written to: /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/lmms-fl-research/lmms-patcher/build
```

Compile/link of the new code (same log; `[...]` marks omitted unrelated lines):

```
[...]
[ 39%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/RoutingGraph.cpp.o
[ 39%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/RoutingNodes.cpp.o
[ 39%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/RoutingNode.cpp.o
[...]
[ 59%] Built target lmmsobjs
[...]
[ 61%] Building CXX object tests/CMakeFiles/RoutingGraphTest.dir/RoutingGraphTest_autogen/mocs_compilation.cpp.o
[ 61%] Building CXX object tests/CMakeFiles/RoutingGraphTest.dir/src/core/RoutingGraphTest.cpp.o
[...]
[100%] Linking CXX executable RoutingGraphTest
[100%] Built target RoutingGraphTest
BUILD_EXIT=0
```

### Tests — `cd build/tests && ctest --output-on-failure`

```
Test project /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/lmms-fl-research/lmms-patcher/build/tests
    Start 1: ArrayVectorTest
1/9 Test #1: ArrayVectorTest ..................   Passed    0.03 sec
    Start 2: AudioBufferTest
2/9 Test #2: AudioBufferTest ..................   Passed    0.02 sec
    Start 3: AutomatableModelTest
3/9 Test #3: AutomatableModelTest .............   Passed    1.29 sec
    Start 4: MathTest
4/9 Test #4: MathTest .........................   Passed    0.02 sec
    Start 5: ProjectVersionTest
5/9 Test #5: ProjectVersionTest ...............   Passed    0.02 sec
    Start 6: RelativePathsTest
6/9 Test #6: RelativePathsTest ................   Passed    0.02 sec
    Start 7: RoutingGraphTest
7/9 Test #7: RoutingGraphTest .................   Passed    0.03 sec
    Start 8: TimelineTest
8/9 Test #8: TimelineTest .....................   Passed    1.25 sec
    Start 9: AutomationTrackTest
9/9 Test #9: AutomationTrackTest ..............   Passed    1.29 sec

100% tests passed, 0 tests failed out of 9

Total Test time (real) =   3.98 sec
CTEST_EXIT=0
```

`ctest -N` from `build/tests` lists exactly: ArrayVectorTest, AudioBufferTest,
AutomatableModelTest, MathTest, ProjectVersionTest, RelativePathsTest,
**RoutingGraphTest**, TimelineTest, AutomationTrackTest — `Total Tests: 9`.

### Commits / diffstat

```
f92231c04 Tests: Add RoutingGraphTest coverage for the DAG engine
fadd04a83 Core: Add RoutingNode/RoutingGraph DAG engine
4e677cb6c Don't compress man page in during build (#8494)

 include/RoutingGraph.h              | 142 ++++++++++++++
 include/RoutingNode.h               | 113 +++++++++++++
 include/RoutingNodes.h              | 119 ++++++++++++
 src/core/CMakeLists.txt             |   3 +
 src/core/RoutingGraph.cpp           | 364 ++++++++++++++++++++++++++++++++++++
 src/core/RoutingNode.cpp            | 117 ++++++++++++++
 src/core/RoutingNodes.cpp           | 157 ++++++++++++++
 tests/CMakeLists.txt                |   1 +
 tests/src/core/RoutingGraphTest.cpp | 355 +++++++++++++++++++++++++++++++++++++
 9 files changed, 1371 insertions(+)
```

---

## 6. Remaining unverified / caveats

- **The §562 DoD is not met by this spike** and was not meant to be: no TripleOscillator,
  no audible render, no `InstrumentTrack` wiring (`TASK-PLAYBOOKS.md:387-393` items 2, 4,
  5 remain open). This MVP proves only the graph engine inside one channel buffer.
- **Live edit during playback is untested** — there is no audio engine in the loop, and
  `rebuildPlan()` is not lock-free.
- **The zero-allocation proof is per-process-call on the test binary only.** It does not
  cover future node types or the eventual integration path.
- **One output port per node**; fan-in is summing only. Multi-output nodes, latency
  compensation, and node bypass/mute semantics are unproven.
- **Performance at 50+ nodes is unmeasured** (`REPORT.md:369` risk), as is behaviour when
  the block size changes mid-stream (today `prepare()` reallocates on the control thread).
- **No `.mmpz`/project integration**; the round-trip uses a standalone `DataFile`.
- **No Mixer/Part B interaction was tested**, by design — Part B is in flight on
  `part-b-engine-integration` (`PART-B-INTEGRATION.md`).
