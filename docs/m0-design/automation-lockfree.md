# M0 automation lock-free design

**Branch:** `design/m0-automation-lockfree`, based on `c405233f8`
**Scope:** clusters 2647, 5528, and 5388
**Status:** 5528 has the edit ownership and immutable reader snapshot implementation in this lane. Its audio-side recording writers remain blocked on the 5388 handoff; clusters 2647 and 5388 remain separate.

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
| `src/core/AutomationClip.cpp:443-455` | Recording inserts or removes points based on the last recorded value. | `recordValue()`; its audio-thread callers require the separate recording handoff in cluster 5388 before publication can be moved off audio. |
| `src/core/Song.cpp:648` | Render thread records a controlled-model value into its automation clip. | `recordValue()`; publication still awaits the cluster 5388 recording handoff. |
| `src/core/NotePlayHandle.cpp:672` | Per-note worker records detuning automation into the note's clip. | `recordValue()`; publication still awaits the cluster 5388 recording handoff. |
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

`Song.cpp:648` and `NotePlayHandle.cpp:672` call `recordValue()` on render/worker audio paths; the latter is per-note detuning recording. `PianoRoll.cpp:1122` binds a non-const map reference but only iterates it; the other `getTimeMap()` uses in `PianoRoll.cpp`, `AutomationEditor.cpp`, `AutomationClipView.cpp`, `AutomationEditorKeys.cpp`, `InlineAutomation.h`, `ItemListAutomationSource.cpp`, control helpers, and tests are read-only. These become const views. `loadSettings()` is also the undo/redo restoration path through `Clip::restoreState()`; no separate mutable undo writer exists.

### Chosen design

Keep the editable `QMap` as control-thread state and publish an immutable, sorted, contiguous `AutomationClipReadSnapshot` after each successful edit. The snapshot contains POD node fields (tick, in/out value, in/out tangent, tangent-lock flag, curve and bend) plus every clip-level field the audio evaluator reads (progression, tension, start position, offset, length, mute, and sample-accurate setting). It contains no `AutomationNode`, `AutomationClip`, or GUI object pointer. `writeBlockRamp()` and audio-side `valueAt()` use only the published snapshot and the current arithmetic/order of the existing evaluator; the public map API is read-only, and all mutation goes through clip edit methods that publish before returning.

Publish with an atomic raw pointer. An audio reader increments a lock-free global automation-snapshot reader count before loading any snapshot pointer and decrements it after its last read. A writer atomically exchanges the current pointer, appends the old snapshot to control-thread retirement storage, and frees retired snapshots only after observing zero readers. The increment-before-load ordering makes a reader that can hold the old pointer visible to reclamation; a reader that starts after exchange can only load the new pointer. Clip/project object destruction remains under `m_changeMutex`, so no reader can outlive the clip shell. All snapshot allocation and destruction happen on the control thread.

Before publishing, the writer holds the clip's edit lock, applies the edit to the editable map, copies the full map and clip metadata, then releases the lock. The mutable `getTimeMap()` overload is removed. Direct node changes become `AutomationClip` operations (including value, tangent, curve, move, and transform edits); the GUI and control command callers are migrated. Read callers either use a const map view under control-thread ownership or a value snapshot. Loading, undo, copy/transform, reset, recording-drain, and clear paths all publish too. There is no fixed node or retired-snapshot capacity. If building a new snapshot fails, the edit is refused and surfaced on the control thread; the last valid snapshot stays published. Retired storage is reclaimed on each control-side publication and main-thread drain after a quiescent reader count.

### Invariants and realtime argument

- The control thread owns `QMap`, edit locks, snapshot construction, publication, retired storage, and freeing. One atomic pointer identifies the immutable snapshot visible to render readers.
- Audio readers do not touch `m_timeMap`, `m_clipMutex`, a Qt container, or a ref-counted pointer. They use a scoped lock-free reader count and binary search over immutable POD nodes. The fixed-capacity `AutomationRamp` remains the output buffer and retains its existing counted refusal behavior.
- Snapshot storage is dynamically sized off audio, so curves have no silent node limit. The reader counter must be a lock-free atomic on supported targets. A retired snapshot is never deleted by the audio thread.
- The interpolation formulas, float operation order, node boundary selection, block knot positions, clip length/offset, and null/empty behavior remain identical. Validation compares `valueAt()` for every relevant tick and generated ramp samples for existing discrete, linear, cubic, bend, and sample-accurate cases, then bit-compares rendered output.

### Considered and rejected

- `std::shared_ptr`/atomic shared ownership is rejected unless its implementation proves that final release cannot free or take an internal lock on the audio thread.
- Publishing from existing mutator methods while leaving `getTimeMap()` mutable is rejected: those writes bypass publication and the node setters currently have no clip-lock boundary.
- Copying the `QMap` on each render period is rejected: it allocates, performs tree traversal and copying on audio, and creates an unbounded render cost.

### Implementation boundary

The live mutable accessor and node-to-clip setter backdoor are removed. Editor, point-edit, control-command, piano-roll, transform, load/undo, reset, clear, copy and range-rescale writes now go through `AutomationClip` ownership. `valueAt()` and `writeBlockRamp()` read a sorted POD snapshot under a lock-free reader count; old snapshots are retired and freed from control-side publication/destruction. The regression suite compares the legacy evaluator and ramps bit-for-bit, probes allocation and the clip edit mutex, and races complete curve edits against rendering.

`recordValue()` remains the exception. `Song::processAutomations()` and `NotePlayHandle` call it from render/worker audio paths. It still takes `m_clipMutex` and changes the editable map; this implementation suppresses snapshot publication during that call so it does not allocate on audio. Consequently, automation being recorded is not visible in the read snapshot until a safe recording handoff publishes it. Removing this last audio-side clip lock while preserving same-period write/read behavior requires the audio-visible recording overlay or equivalent ordering proof described in cluster 5388. That work is outside 5528 and blocks the unqualified claim that no audio thread takes `m_clipMutex`.

### 5528 validation evidence

- `AutomationClipSnapshotTest` compares the QMap evaluator and snapshot `valueAt()` at each tick from -2 through 48, and compares every ramp knot and generated frame for discrete, linear, cubic, bend, hold, smooth, and sample-accurate settings. Its edit-mutex probe fails when a lock is deliberately reintroduced into either audio reader, then passes without those locks; the concurrent renderer accepts only complete A or B curves.
- The automated-project render comparison used `tests/data/automation-audio-fixture.mmp` with a master-volume automation track linked to model ID `9000001`, plus flat and no-automation controls. The legacy QMap evaluator matching `e0cef61f6` and the snapshot evaluator produced identical WAV data-chunk hashes: automated `231247afe6ef389ae7aa0d1bcd75d82648a137a7e089e3136af351be7dc1689f`, flat `cc77f1f1664dd54e053e5682ff7cb05fe48e10fcf0061c1a50a379fbb098e58a`, and no automation `46cabeacc7caf8a880240165b0035a968ad94a3563bd7ffa3c50e8079cb8944d`. Each control render had a distinct hash.
- `ctest --output-on-failure -R 'Automation|ControlAutomation|AudioAutomation|NoteCurvesAcross'` from `build/tests`: 12/12 passed. The four required repository gates passed after the final source split.

## 5388 — recording automation from the render thread

`Song::processAutomations()` inserts each recording model into a `QSet` and calls `AutomationClip::recordValue()` on the render thread (`src/core/Song.cpp:573-650`); `recordValue()` takes `m_clipMutex` and edits the map (`src/core/AutomationClip.cpp:443-453`). `NotePlayHandle::process` has a second audio-side `recordValue()` caller (`src/core/NotePlayHandle.cpp:672`), so changing only the Song call would not make recording lock-free.

### Chosen design

Use a preallocated, bounded SPSC queue for each audio producer, drained together by the main thread. The fixed producer set is the render thread plus the engine's fixed worker threads; each queue has exactly 4096 pending events, so total storage is `4096 * (workerCount + 1)` events allocated when the worker pool is created. Each event stores the clip's stable project ID, relative musical `TimePos`, inverse-scaled float value, and period token; it stores no raw clip/model pointer beyond the render period. A main-thread poll resolves the ID against the open project and replays every event from each producer queue in order through the same `recordValue()` edit logic, which in turn publishes the immutable clip snapshot from the 5528 design. The two current sources write separate target clips (Song automation clips and per-note detuning clips), so order within each target is producer-local. Preserve the original musical timestamp rather than substituting the drain time.

Replace `QSet<const AutomatableModel*> recordedModels` with a preallocated fixed-capacity membership table owned by `Song`, keyed by stable model ID and reset with a generation counter each automation tick. The render thread applies the same write gates and write decision, captures the same inverse-scaled value, and records the model in the membership table. The main thread replays those values through `recordValue()` in order, which preserves the per-clip `m_lastRecordedValue` equal-value collapse. The table continues to drive `setUseControllerValue()` for the current tick; clip mutation itself is deferred.

Each producer queue has 4096 pending events, with no overwrite and no waiting: a full push increments an atomic dropped-event counter and keeps rendering. The membership table has 4096 entries and reports overflow separately. The main thread periodically drains every queue in producer order and replays each event without coalescing, then raises one user-visible warning with the dropped counts. If the active clip ID no longer exists at drain time, count it as a stale-target drop and surface it; do not dereference a freed clip. The queues and table are fixed at construction and never grow on audio.

### Invariants and realtime argument

- The render thread(s) own event production, musical timestamp/value capture, and the fixed current-tick membership table. The main thread owns dequeue, ID resolution, map mutation, snapshot publication, warnings, and all event/clip-memory reclamation.
- Each bounded SPSC push has no mutex, allocation, signal, I/O, wait, or retry loop on audio. Capacity is exactly 4096 queued events per producer and 4096 current-tick model entries. Overflow drops the newest event for that producer, increments a visible counter, and never overwrites an unread event.
- Events carry only stable IDs and values. Draining takes `m_changeMutex` on the main thread before resolving target IDs or publishing clip state. Musical positions use the captured relative tick and remain independent of drain delay.
- The current tick's model write-mode/controller behavior and every accepted event's `recordValue()` result must match exactly. Song+NotePlayHandle recording tests exercise multiple producers, FIFO behavior per clip, full-queue counts, stale IDs, and equal-value collapse. Before/after audio renders must be bit-identical while recording is active.

### Considered and rejected

- A mutex around `recordValue()` is rejected: it only moves the same unbounded wait to the render thread.
- A dynamically growing `QVector`/`QSet` queue is rejected: it allocates and can grow without bound in `processAutomations()`.
- Storing clip pointers in deferred events is rejected: a clip can be removed after the render period and before the main-thread drain.
- Draining on the next audio period is rejected: it would reintroduce clip mutation and snapshot publication on audio.

### Why implementation is BLOCKED in this lane

Moving `recordValue()` to a later main-thread drain changes when the edited curve becomes visible to `buildAutomationRamps()` and `automatedValuesAt()`. In particular, a sample-accurate writing clip can have its next period's read snapshot built before the GUI drains the previous period's event, which can change rendered samples. Preserving only the captured musical timestamp does not prove bit-identical output. The queue design therefore needs a bounded audio-visible recording overlay or a proven ordering contract that makes deferred publication observationally equivalent; neither exists in this branch. The second `NotePlayHandle` producer and the current `QSet` semantics also need regression coverage. Until that bridge is designed and render-compared, moving only the Song write would be an unsafe partial fix.

## Work order and acceptance

1. Define and test the coherent model-value publication primitive and exhaustive registry, then install the prepass and remove render-job cache locks (2647).
2. Add snapshot value-evaluation tests and a complete writer API audit; then remove the mutable map accessor and publish/reclaim snapshots (5528).
3. Design the render-visible recording overlay, then install the MPSC queue and main-thread drain for both recording producers (5388).
4. For each product commit, reverse only that change to prove a red regression, restore and prove green, run the targeted render comparison, and record gate exits. Never claim FIXED from a lock probe alone when its ownership or audio-output invariant remains unproven.
