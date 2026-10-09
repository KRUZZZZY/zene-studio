# M0 automation lock-free design

**Branch:** `design/m0-automation-lockfree`, based on `c405233f8`
**Scope:** clusters 2647, 5528, and 5388
**Status:** 5528's clip-owned edits and immutable readers are implemented. This round closes compound-publication and idle-reclamation gaps and implements the 5388 recording handoff. Cluster 2647 remains separate and blocked.

## 2647 — `AutomatableModel::valueBuffer()`

`AutomatableModel::valueBuffer()` takes `m_valueBufferMutex` at `src/core/AutomatableModel.cpp:570-573`. Mixer channel work calls it from parallel jobs for faders and sends (`src/core/Mixer.cpp:527,625,785`), and effect and instrument plug-ins use it for parameter buffers. The lock currently protects the model's mutable per-period cache when two jobs reach the same model. Removing it at the call site would also expose controller-cache races: `Controller::valueBuffer()` has a mutable period cache, and an LFO advances its phase and amount model while updating it. Linked models call another model's locking getter (`AutomatableModel.cpp:653-662`); meanwhile `setValueInternal()` writes `m_oldValue` outside that getter's lock (`AutomatableModel.cpp:354-358`). The sole-reader sidechain optimization (`valueBufferForSoleReader()`, `Mixer.cpp:474`) only covers one receiver-owned model and does not establish general ownership.

### Chosen design

Build a complete, render-owned list of every `AutomatableModel` whose value buffer can be requested during a period. The list is prepared and grown on the control thread while `AudioEngine::m_changeMutex` excludes `renderNextPeriod()`. In each period, after `Song::processNextBuffer()` has applied that period's automation and before any instrument, effect, or mixer worker job starts, the render thread evaluates each registered model's cache once. Evaluation uses the existing equations and fixed `ValueBuffer`; linked models are visited through a render-only evaluator, and shared controllers are reached and updated only from this serial prepass. Worker callers become read-only period-buffer lookups and never compute a cache or call a controller.

The model registry is a control-thread-owned immutable pointer array, atomically published as a whole. It has no fixed model-count cap: control-side growth allocates a larger array before publication, and the old array is retained until the render period that could have loaded it has quiesced. If control-side allocation fails, the model or plug-in activation that requires the new registration is refused and reported; the render thread never skips an unregistered cache or grows the array. The registry includes built-in, LADSPA, and other plug-in models; adding a new audio-thread `valueBuffer` caller without registering its model is an invariant violation caught by the realtime regression probe.

The prepass alone does not solve the unsynchronized GUI-to-render value handoff. Model input state must be published as a coherent old/new pair with a target-verified lock-free atomic representation. The render thread consumes one pair per model at prepass time and owns `m_oldValue` and the period cache thereafter. The representation must preserve the current “immediately previous `setValueInternal()` value” interpolation start, including multiple control writes between periods; substituting the last rendered value would change the ramp. `m_nextLink`, controller connections, and registry membership remain stable for the whole render period under the existing change lock. `Controller::valueBuffer()` and every stateful controller update must execute in that same prepass, once per period, before workers run.

### Invariants and realtime argument

- The control thread owns model creation, destruction, registry replacement, and allocation. `AudioEngine::m_changeMutex` keeps the registry and model graph alive and stable through the period. Old registry storage is freed on the control thread after a quiescent period.
- The render thread owns cache computation and `m_oldValue` after consuming the coherent input pair. Mixer and plug-in jobs own no model cache writes; they only read the prepared model-owned buffers.
- The model list can grow without a render-time limit or silent truncation. Failure to grow is reported on the control thread and activation is rejected. Every render cache remains the existing fixed-size, period-sized `ValueBuffer`.
- The audio path uses fixed iteration over the already-published registry and existing fixed buffers: no allocation, mutex, Qt signal, syscall, or wait. Only lock-free atomics may cross the control/render boundary; the implementation must `static_assert` that its chosen atomic representation is always lock-free on supported Qt 5 and Qt 6 targets.
- Null-buffer behavior, sample count, model fitting, automation transforms, interpolation endpoints, and controller evaluation frequency stay unchanged. No arithmetic is moved into the mixer consumers. The gate is an identical rendered buffer comparison, including a shared LFO driving controls on two parallel tracks, plus the existing `valueBuffer()` callers' tests.

### Considered and rejected

- Removing `QMutexLocker` from `valueBuffer()` and calling `valueBufferForSoleReader()` everywhere is rejected: the model cache, linked ring, and shared controller are not sole-reader objects.
- A per-mixer-channel cache is rejected: the same model can be read by a plug-in, a send, and multiple jobs, so channel-local caches duplicate evaluation and do not define one owner.
- Keeping a lock only around `Controller::valueBuffer()` is rejected: it leaves the per-model cache serialized and `setValueInternal()`/`m_oldValue` unpaired; putting that lock on the controller path also retains an audio-thread lock.

### Why implementation is BLOCKED in this lane

This branch has no complete render-model registry or coherent model-value publication primitive. The proposed prepass cannot safely replace the mutex until both exist and every out-of-tree and in-tree plug-in consumer is covered. A partial change would make one of the current shared-controller or linked-model cases race, while an ordinary test would not prove registry completeness. The coherent old/new handoff also needs a lock-free representation validated on both shipped Qt configurations. No product change is made until those invariants can be implemented and the parallel-LFO render comparison passes bit-for-bit.

## 5528 — `AutomationClip` readers and mutable time map

The render path takes `m_clipMutex` in `AutomationClip::writeBlockRamp()` (`src/core/AutomationClip.cpp:760-764`) and in the nested `valueAt()` interpolation path (`:568-612`). The header returns the live `QMap` by both const and mutable reference (`include/AutomationClip.h:144-151`). Editors and control tools mutate that reference in more than a dozen places; `AutomationNode::setInValue()` and `setOutValue()` edit it through the mutable accessor without first taking the clip mutex (`src/core/AutomationNode.cpp:80-107`). A snapshot published only by existing clip methods would therefore become stale as soon as any such writer changed a node.

### Mutation audit (source lines at `ce24bc183`)

| File:line | Mutation found | `AutomationClip` edit method that owns it |
|---|---|---|
| `include/AutomationClip.h:144-147` | Exposed the live map for writes; the mutable overload is now removed after caller migration. | No replacement accessor; use the edit methods below. |
| `src/core/AutomationNode.cpp:75-89,95-109` | Legacy node value setters reached the map through a stored clip pointer, then regenerated adjacent tangents. | The node-to-clip mutation path is removed; `setNodeInValue()` / `setNodeOutValue()` own edits. |
| `src/core/AutomationClip.cpp:90-91` | Copy constructor inserts copied nodes and rebinds each node to the new clip. | Copy constructor, followed by initial snapshot publication. Node values now carry no clip pointer. |
| `src/core/AutomationClip.cpp:248,304` | Insert or replace a point, remove quantized neighbors when requested, recalculate tangents. | `putValue()` / `putValues()` |
| `src/core/AutomationClip.cpp:340-384` | Remove one point or an inclusive range and regenerate neighboring tangents. | `removeNode()` / `removeNodes()` |
| `src/core/AutomationClip.cpp:396-438` | Reset out values or unlock and regenerate tangents over a range. | `resetNodes()` / `resetTangents()` |
| `src/core/AutomationClip.cpp:589-617` | Recording inserts or removes points based on the last recorded value. | `recordValue()` runs on the control thread after queue drain; `recordQueuedValue()` publishes the completed map edit and its applied-sequence watermark once. |
| `src/core/Song.cpp:708-790` | Render thread records a controlled-model value into its automation clip. | `enqueueAutomationRecord()` writes the stable clip ID, time, and value to the caller's preallocated SPSC queue. |
| `src/core/NotePlayHandle.cpp:666-677` | Per-note worker records detuning automation into the note's clip. | `enqueueAutomationRecord()` writes to that worker's assigned queue slot. |
| `src/core/AutomationClip.cpp:478-548` | Drag removes/restores a point, inserts its current position/value and restores any locked tangents. | `setDragValue()` |
| `src/core/AutomationClip.cpp:918-989` | Undo/load clears and repopulates nodes, restores curve/tangent state, and may regenerate tangents. | `loadSettings()` |
| `src/core/AutomationClip.cpp:1180-1184` | Clear removes the complete map. | `clear()` |
| `src/core/AutomationClip.cpp:1249-1307` | Internal tangent generation changes in/out tangent fields on live nodes. | Called only inside the owning edit operation; that operation publishes once after the complete edit. |
| `src/core/AutomationClipTransforms.cpp:44-68` | Flip vertically changes node values and recalculates tangents. | `flipY()` |
| `src/core/AutomationClipTransforms.cpp:179-206` | Flip horizontally builds and installs a mirrored map. | `flipX()` |
| `src/core/AutomationClipTransforms.cpp:258-275` | Shape draw erases the selected range and inserts generated nodes. | `drawShape()` |
| `src/core/AutomationClipTransforms.cpp:337-377` | Range transform moves/rescales nodes and installs the result if destinations do not collide. | `transformRange()` |
| `src/core/AutomationClipTransforms.cpp:415-445` | Thinning removes redundant nodes and recalculates tangents. | `thin()` |
| `src/gui/clips/AutomationClipView.cpp:478-495` | Rescales point values when the controlled model's range changes, then regenerates tangents. | `rescaleValuesToRange()` |
| `src/gui/editors/AutomationEditor.cpp:333-334,368,376-378,398-423` | Replace a point; fine tune values; erase/reset tools remove points, reset out values, or unlock tangents. | `removeNode()` / `putValue()` / `setNodeInValue()` / `setNodeOutValue()` / `resetNodes()` / `setNodeLockedTangents()` |
| `src/gui/editors/AutomationEditor.cpp:478-491,568-629,761-899` | Drag or draw points, draw-out edits values, and tangent editing changes or resets tangents. | `setDragValue()` / `removeNodes()` / `resetNodes()` / `resetTangents()` / `setNodeOutValue()` / `setNodeLockedTangents()` / `setNodeTangent()` |
| `src/gui/editors/AutomationPointEdit.cpp:65-80` | Point move removes and reinserts a node, then restores its curve/bend. | `moveNode()` |
| `src/core/ControlCommandsAutomationTools.cpp:65-80,124-181` | Command point move removes and reinserts a node, then restores its curve/bend. | `moveNode()` |
| `src/core/ControlCommandsAutomationTools.cpp:199-244` | Control commands remove points after a trim boundary and insert trim endpoints. | `removeNodes()` / `putValues()` / `putValue()` |
| `src/core/ControlCommandsAutomationEdit.cpp:92,125,155` | Add a point, remove a point, or clear a parameter's automation lane. | `putValue()` / `removeNode()` / `clear()` |
| `src/core/ControlCommandsAutomationLanes.cpp:84,158,173,189,215` | Initialize a lane point, draw or transform a shape, thin redundant points, or change a point curve. | `putValue()` / `drawShape()` / `transformRange()` / `thin()` / `setNodeCurve()` |
| `src/core/ControlCommandsNoteCurves.cpp:141` | Set per-note detuning curve points. | `putValue()` on the note's `AutomationClip` |
| `src/gui/editors/PianoRoll.cpp:2900-2932` | Drag or erase points in a note parameter curve. | `setDragValue()` / `removeNodes()` / `removeNode()` |
| `tests/src/core/AutomationLanesTest.cpp:279-281,299-304,378-380,395-397,424-426,663-669` | Test fixtures directly change tangents, tangent locks, or curves through the mutable accessor. | `setNodeTangents()` / `setNodeLockedTangents()` / `setNodeCurve()` |

`Song::processAutomations()` and `NotePlayHandle::processTimePos()` are both audio-side producers; the latter records per-note detuning. They no longer call `recordValue()` or touch the clip map. `PianoRoll.cpp:1122` binds a non-const map reference but only iterates it; the other `getTimeMap()` uses in `PianoRoll.cpp`, `AutomationEditor.cpp`, `AutomationClipView.cpp`, `AutomationEditorKeys.cpp`, `InlineAutomation.h`, `ItemListAutomationSource.cpp`, control helpers, and tests are read-only. These become const views. `loadSettings()` is also the undo/redo restoration path through `Clip::restoreState()`; no separate mutable undo writer exists.

### Chosen design

Keep the editable `QMap` as control-thread state and publish an immutable, sorted, contiguous `AutomationClipReadSnapshot` after each successful edit. The snapshot contains POD node fields (tick, in/out value, in/out tangent, tangent-lock flag, curve and bend) plus every clip-level field the audio evaluator reads (progression, tension, start position, offset, length, mute, and sample-accurate setting). It contains no `AutomationNode`, `AutomationClip`, or GUI object pointer. `writeBlockRamp()` and audio-side `valueAt()` use only the published snapshot and the current arithmetic/order of the existing evaluator; the public map API is read-only, and all mutation goes through clip edit methods that publish before returning.

Publish with an atomic raw pointer. An audio reader increments a lock-free global automation-snapshot reader count before loading any snapshot pointer and decrements it after its last read. A writer atomically exchanges the current pointer, appends the old snapshot to control-thread retirement storage, and frees retired snapshots only after observing zero readers. The increment-before-load ordering makes a reader that can hold the old pointer visible to reclamation; a reader that starts after exchange can only load the new pointer. Clip/project object destruction remains under `m_changeMutex`, so no reader can outlive the clip shell. All snapshot allocation and destruction happen on the control thread.

Before publishing, the writer holds the clip's edit lock, applies the edit to the editable map, copies the full map and clip metadata, then releases the lock. The mutable `getTimeMap()` overload is removed. Direct node changes become `AutomationClip` operations (including value, tangent, curve, move, and transform edits); the GUI and control command callers are migrated. Read callers either use a const map view under control-thread ownership or a value snapshot. Loading, undo, copy/transform, reset, recording-drain, and clear paths all publish too. Compound edits suppress nested publications and publish once after the complete map change. There is no fixed node or retired-snapshot capacity. If building a new snapshot fails, the edit is refused and surfaced on the control thread; the last valid snapshot stays published. Retired storage is reclaimed on control-side publication and drain; the existing 60 Hz main-window timer retries collection after readers quiesce even when no later edit occurs.

### Invariants and realtime argument

- The control thread owns `QMap`, edit locks, snapshot construction, publication, retired storage, and freeing. One atomic pointer identifies the immutable snapshot visible to render readers. Nested drag/map operations publish one completed edit.
- Audio readers do not touch `m_timeMap`, `m_clipMutex`, a Qt container, or a ref-counted pointer. They use a scoped lock-free reader count and binary search over immutable POD nodes. The fixed-capacity `AutomationRamp` remains the output buffer and retains its existing counted refusal behavior.
- Snapshot storage is dynamically sized off audio, so curves have no silent node limit. The reader counter must be a lock-free atomic on supported targets. A retired snapshot is never deleted by the audio thread.
- The interpolation formulas, float operation order, node boundary selection, block knot positions, clip length/offset, and null/empty behavior remain identical. Validation compares `valueAt()` for every relevant tick and generated ramp samples for existing discrete, linear, cubic, bend, and sample-accurate cases, then bit-compares rendered output.

### Considered and rejected

- `std::shared_ptr`/atomic shared ownership is rejected unless its implementation proves that final release cannot free or take an internal lock on the audio thread.
- Publishing from existing mutator methods while leaving `getTimeMap()` mutable is rejected: those writes bypass publication and the node setters currently have no clip-lock boundary.
- Copying the `QMap` on each render period is rejected: it allocates, performs tree traversal and copying on audio, and creates an unbounded render cost.

### Implementation boundary

The live mutable accessor and node-to-clip setter backdoor are removed. Editor, point-edit, control-command, piano-roll, transform, load/undo, reset, clear, copy and range-rescale writes now go through `AutomationClip` ownership. `valueAt()` and `writeBlockRamp()` read a sorted POD snapshot under a lock-free reader count; old snapshots are retired and freed from control-side publication/destruction. The regression suite compares the legacy evaluator and ramps bit-for-bit, probes allocation and the clip edit mutex, and races complete curve edits against rendering.

`recordValue()` was the exception: `Song::processAutomations()` and `NotePlayHandle` called it from render/worker paths, where it took `m_clipMutex`, edited the map, and suppressed publication. The 5388 handoff below removes those audio-side map writes and keeps pending values visible until the control thread publishes them.

### 5528 validation evidence

- `AutomationClipSnapshotTest` compares the QMap evaluator and snapshot `valueAt()` at each tick from -2 through 48, and compares every ramp knot and generated frame for discrete, linear, cubic, bend, hold, smooth, and sample-accurate settings. Its edit-mutex probe fails when a lock is deliberately reintroduced into either audio reader, then passes without those locks; the concurrent renderer accepts only complete A or B curves. Compound-drag publication is counted and raced against a reader that rejects the intermediate missing point. A reader held across retirement proves that a later idle control poll frees the old snapshot.
- The automated-project render comparison used `tests/data/automation-audio-fixture.mmp` with a master-volume automation track linked to model ID `9000001`, plus flat and no-automation controls. The legacy QMap evaluator matching `e0cef61f6` and the snapshot evaluator produced identical WAV data-chunk hashes: automated `231247afe6ef389ae7aa0d1bcd75d82648a137a7e089e3136af351be7dc1689f`, flat `cc77f1f1664dd54e053e5682ff7cb05fe48e10fcf0061c1a50a379fbb098e58a`, and no automation `46cabeacc7caf8a880240165b0035a968ad94a3563bd7ffa3c50e8079cb8944d`. Each control render had a distinct hash.
- `ctest --output-on-failure -R 'Automation|ControlAutomation|AudioAutomation|NoteCurvesAcross'` from `build/tests`: 12/12 passed. The four required repository gates passed after the final source split.

## 5388 — recording automation from render and worker threads

`Song::processAutomations()` inserts each recording model into a `QSet` and calls `AutomationClip::recordValue()` on the render thread (`src/core/Song.cpp:573-650`). `NotePlayHandle::processTimePos()` is a second writer for per-note detuning (`src/core/NotePlayHandle.cpp:672`). `recordValue()` takes `m_clipMutex` and edits the map, while suppressing snapshot publication; later readers therefore miss points that the legacy map evaluator saw.

### Chosen design

`Song` allocates one 4096-event SPSC queue for the render caller and each fixed audio worker when the engine and song are constructed. The render caller owns slot zero; each worker has a fixed thread-local slot assigned before it starts. Song and NotePlayHandle producers running on the same thread share that thread's queue, so every queue remains genuinely single-producer. Before enqueue, `recordingMapTime()` captures the clip offset and the current quantization into a map-relative tick, matching the point position the old `recordValue()` call inserted. An event contains the clip's stable journal ID, that map-relative `TimePos`, value, and its sequence in that producer's queue; it contains no clip or model pointer. The UI's existing 60 Hz timer drains each queue in FIFO order, resolves IDs through the live project journal under the control-side change guard, calls `recordQueuedValue()` on the resolved clip, and publishes a snapshot carrying that producer's applied-sequence watermark. A missing ID is counted as stale and discarded without dereference.

The queues also provide the bounded audio-visible recording overlay. `valueAt()` and `writeBlockRamp()` scan the fixed set of queues and choose the closest pending recorded point on either side of the requested clip time. The overlay uses the clip's existing progression and the adjacent base/recorded points; queue events retain the map positions and values that the old synchronous map edit used. A per-clip snapshot watermark separates events already represented in the immutable snapshot from newer queued events. Draining publishes the edited map and its watermark before the event slots become reusable; slots are reclaimed only after the snapshot reader count is quiescent. A reader holding an older snapshot therefore still sees unpublished recording events, while a reader holding the new snapshot does not apply them twice. If a queue reaches 4096 pending entries, it drops the newest event and increments its overflow counter; the UI status bar surfaces losses. The overlay scans at most 4096 entries per producer queue and does not allocate or lock.

The audio-side `hasAutomation()` guard uses the same bounded queue query, so an empty published map cannot make a caller skip points that are waiting for the control-thread drain. When the newest pending event shares a tick with the last published node, the event wins in the held-last-value path as well as in normal point lookup. These rules preserve recording visibility at the first queued point and when a recording replaces an existing node.

Replace `QSet<const AutomatableModel*> recordedModels` with a fixed 4096-entry open-addressed membership table in an 8192-slot array owned by `Song`, keyed by stable model ID and tagged with a per-tick generation. It drives the existing `setUseControllerValue()` decisions without allocating. A full table is counted and surfaced separately. Preserve the original write gates, inverse scaling, captured musical time, and `recordValue()` equal-value collapse on the main-thread replay.

### Invariants and realtime argument

- Each audio thread writes only to its own preallocated SPSC queue. No audio writer takes a mutex, allocates, emits a Qt signal, performs I/O, waits, or retries. Queue, overlay scan, and membership-table probe counts have fixed upper bounds.
- The main thread alone advances the apply cursor, resolves stable IDs, edits `m_timeMap`, publishes snapshots, reports overflow/stale IDs, and reclaims queue slots after readers quiesce. Retired snapshots also receive a recurring control-thread collection poll so an idle clip cannot strand retired storage.
- Stable clip IDs are resolved under the existing model-change guard. A deleted clip is never dereferenced from an event; its event is counted as stale. Each queue holds 4096 pending events, and the Song membership table holds 4096 model IDs. Both reject new entries at capacity and expose counters.
- `recordValue()` remains the single owner of map insertion/removal and equal-value collapse. Snapshot reads incorporate pending events across producer queues before the drain, so a render cannot observe a stale curve just because the UI timer has not run yet. Song and per-note recording tests check both producer paths, FIFO ordering, queue capacity, stale IDs, table overflow, and equal-value collapse.
- `AutomationRecordingTest` checks both per-tick values and ramp knots against synchronous `recordValue()` during and after the sweep, then renders a deterministic 24-period sample-buffer sweep through normal discrete and linear sample-accurate clips. It compares every stereo frame bit-for-bit against the synchronous pre-change replay. Separate assertions cover an empty published map with pending events and a pending event replacing an existing node at the same tick. Other curve shapes retain the existing evaluator tests but are not claimed as part of this recording-sweep render comparison.
- Queue insertion and overlay reads have no mutex or allocation. The allocation/held-edit-mutex probe exercises queue insertion; the RT-safety sweep covers enqueue and both snapshot readers.

### Considered and rejected

- A mutex around `recordValue()` is rejected: it keeps an unbounded audio wait and mutable-map access.
- A dynamically growing queue or membership set is rejected: both can allocate or grow on audio.
- Storing clip pointers in events is rejected because the clip can be removed before the UI drain.
- Draining from the audio period is rejected because it would restore map mutation and snapshot allocation to audio.

## Work order and acceptance

1. Define and test the coherent model-value publication primitive and exhaustive registry, then install the prepass and remove render-job cache locks (2647; still blocked).
2. Remove the mutable map accessor, publish/reclaim immutable snapshots, and prove compound edits publish once (5528; implemented, including the stage 1 follow-up fixes).
3. Install the bounded SPSC recording handoff, pending overlay, fixed model membership table, and main-thread drain for both recording producers (5388; implemented).
4. For each product change, reverse only the relevant path to prove a red regression where feasible, restore and prove green, run the targeted render comparison, and record gate exits. Never claim FIXED from a lock probe alone when its ownership or audio-output invariant remains unproven.
