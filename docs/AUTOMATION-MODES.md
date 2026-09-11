# Automation modes (Read / Touch / Latch / Write)

**Status:** landed on `post-alpha/automation-modes`, base `post-alpha/v0.2` = `0c23587d2`.
**Verified:** 2026-09-11, on the tree this document ships with (see *Evidence* at the end for the
exact commands, exit codes and hashes).

This closes the first of the roadmap-gap register's undocumented automation items
("Automation modes + sample-accurate automation", *Bar 2 documented gap with no task*, smallest
honest version: Read/Touch/Latch/Write on volume/pan/sends/plugin params, with touch-timeout, trim
offset, and sample-accurate volume/pan playback). Four of the five parts are implemented and
proved; **sample-accurate playback is not, and is named as such below with the line that blocks
it** — that half is the sibling engine gap the register already flags separately.

---

## 1. What existed before this change, requirement by requirement

| Requirement | Before | Evidence |
|---|---|---|
| Read / Touch / Latch / Write per control | **absent** | `include/AutomatableModel.h:77` (`class AutomatableModel`), `:81` (`enum class ScaleType` — value scaling, *not* a mode). `grep -rniE '\b(touch\|latch)\b' src/core include` returns no automation hit (only unrelated comments such as `src/core/AudioEngine.cpp:335`). The only write concept is a per-**clip** boolean, `AutomationClip::isRecording()` / `setRecording()` (`include/AutomationClip.h:189-190`), toggled from `src/gui/clips/AutomationClipView.cpp:136` and `src/gui/tracks/TrackOperationsWidget.cpp:322`. Per clip, not per control; no "while held"; no timeout. |
| No-destruction guarantee | **absent — and the status quo is the destructive case** | With a clip's record flag on, every tick's manual value is written into it: `src/core/Song.cpp:437` → `AutomationClip::recordValue` (`src/core/AutomationClip.cpp:435`) → `putValue` (`src/core/AutomationClip.cpp:225`), which inserts/replaces the node at that tick and regenerates the surrounding tangents. With the flag off the manual move is simply overwritten again each tick (`src/core/Song.cpp:474`). There is no mode in which a control can be ridden without writing. |
| Touch timeout | **absent** | no such symbol anywhere in `src/core`, `include` or `src/gui` (`grep -rn timeout` on the automation path returns nothing). |
| Trim offset | **absent** | `grep -rn trim src/core include` returns only `QString::trimmed()` call sites, e.g. `src/core/PluginFactory.cpp:256`. |
| Sample-accurate volume/pan playback | **absent** | See §4 item 1 — the producer is tick-quantized and the per-frame reader is GUI-only. |
| Merge semantics (write/overwrite) | **partly: node-insert only** | `AutomationClip::putValue` (`src/core/AutomationClip.cpp:225`) merges a node at the tick and keeps the neighbours; `recordValue` (`:435-447`) de-duplicates a repeated value and removes a node the curve has moved away from. Nothing implements "overwrite the pass". |

## 2. What landed

### 2.1 The mode state machine — `include/AutomatableModel.h:323-405`, `src/core/AutomatableModel.cpp:762-946`

`enum class AutomationMode { Read, Touch, Latch, Write }` (`include/AutomatableModel.h:342`) and a
pure decision function `automationWantsWrite(quint64 transportRun, qint64 nowNs)`
(`src/core/AutomatableModel.cpp:879`) — no allocation, no locking, no mutation:

| mode | transport running | gesture | writes? |
|---|---|---|---|
| Read | any | any | no — follows the automation, never writes it |
| Touch | yes | held, within the timeout | **yes** |
| Touch | yes | released or timed out | no — returns to reading |
| Latch | yes | touched in *this* run | **yes**, until the run ends (release does not stop it) |
| Write | yes | none needed | **yes** — overwrites the pass as the transport runs |
| any | no (run token 0) | any | no — including during an offline render |

**Default is Read**, deliberately: it is the status quo the live alpha already behaves as (a control
that is not armed follows its automation and never writes), it is what every existing project's
controls are, and it is what makes the change behaviour-preserving — see the render proof in §3.2.

### 2.2 Where the mode is consulted — `src/core/Song.cpp:408-440` and `:474`

- A control writes when **either** the clip's own record flag is set **or** the mode says so:
  `recordedModels` is filled by the same loop as before (`src/core/Song.cpp:437`), so the existing
  apply pass (`:474`) automatically stops overwriting a control that is being written — the manual
  value stands and is recorded, exactly as the pre-existing record path does.
- The trim offset is applied at `src/core/Song.cpp:474`, the one place where the written automation
  is read *out* to the control: `model->effectiveAutomationValue(model->scaledValue(it.value()))`.
  It is therefore never written back into the clip, which is what makes it non-destructive.
- Nothing reads the clock unless some control is in a non-Read mode
  (`src/core/Song.cpp:409-430`), so a session that never leaves Read does no extra work on the
  render thread.

### 2.3 Thread ownership (the realtime rule)

**The mode, gesture and trim state is owned by the GUI thread; the decision is made by the render
thread from that state, with no lock between them.**

- Writes (GUI thread): `setAutomationMode` (`src/core/AutomatableModel.cpp:788`), `setTrimOffset`
  (`:920`), `noteAutomationTouchStart` (`:858`) / `noteAutomationTouchEnd` (`:871`) — `std::atomic`
  stores with relaxed ordering.
- Reads (render thread): `automationWantsWrite` (`:879`) and `automationMode()` (`:780`) — relaxed loads.
- `static_assert(std::atomic<AutomationMode>::is_always_lock_free, …)` in the header
  (`include/AutomatableModel.h:350`) makes a lock-free mode change a compile-time requirement, and
  `AutomationModesTest::testModeStateIsLockFree` asserts the same for the other atomics at runtime.
- The transport run token is published by the transport's own observer on the render thread,
  `AutomatableModel::observeAutomationTransport` called once per period from
  `Song::processNextBuffer` (`src/core/Song.cpp:214`, implementation `src/core/AutomatableModel.cpp:796`).
  Only the transport's *edges* change anything, so a run keeps one token for its whole length and a
  Latch survives it.
- A Latch stores the **run token** it was armed under, not a flag
  (`src/core/AutomatableModel.cpp:865`), which is what makes it self-clearing: when the transport
  stops, the token becomes 0 (`:818`) and no later run can match a stale engagement — with no sweep
  over the models and nothing for the render thread to do on stop.
- A side effect worth stating: an offline render reports "not running" (`m_exporting`, `:214`), so an
  export can never modify a project's automation, even if a control was left in Write mode.

### 2.4 The GUI entry point — `src/gui/widgets/Fader.cpp:179`, `:209`, `:272`

The mixer fader — the control the register's own words are about ("mixers ride faders") — opens a
touch gesture on press, refreshes it on every move, and closes it on release. In Read (the default)
that changes nothing at all; in Touch or Latch it is what makes a manual pass write.

## 3. Proofs

### 3.1 Unit tests — `tests/src/core/AutomationModesTest.cpp` (15 test slots, all passing)

The test binary reports `Totals: 15 passed, 0 failed`; Gate 3 counts 73 candidate slots / 52
assertion macros / 0 literal tautologies for the file. (Its first run on this branch was 14 passed /
1 failed — the failure was in the *test*, see the end of this section.)

The tests drive the product's own path: they build a `Song` with an automation track and clip and
call `Song::processNextBuffer()` repeatedly, exactly as `AudioEngine::renderStageNoteSetup()`
(`src/core/AudioEngine.cpp:241`) does on the render thread.

| test | what it pins |
|---|---|
| `testReadIsTheDefault` | mode defaults to Read, trim to 0 |
| **`testReadNeverWrites`** | **the no-destruction property**: ride the control 12 periods across a three-node curve in Read and require the clip's time map to be **bit-identical** (keys, in/out values and tangents, compared as raw float bit patterns) — then run the *identical* harness in Touch and require it to change, so the first assertion cannot pass by being blind |
| `testTouchWritesWhileHeldAndReturnsToReading` | writes while held; after release the decision is false (returns to reading) |
| `testTouchTimeoutEndsTheGesture` | writes at `t0`, `t0+timeout-1`; not at `t0+timeout+1`; a new move refreshes the window; an explicit release ends it immediately |
| `testLatchHoldsUntilTheTransportRunEnds` | still writing 100 timeouts after release, in the armed run; **not** writing in the next run; not writing with the transport stopped; plus the integration leg — `song->stop(); song->processNextBuffer()` drops the token to 0 and the latch is over |
| `testWriteWritesWithoutATouch` | writes with no gesture at all; not with the transport stopped; and a real Write pass changes the clip |
| **`testEachModeDiffersFromTheOthers`** | the decision table over 6 scenarios × 4 modes, plus a pairwise check that **no two modes produce the same decision vector** — a mode implemented as another fails here |
| `testAModeChangeNeverDropsRecordedData` | cycling all four modes **with the transport stopped**, and the non-writing modes (Read, Touch and Latch without a gesture) **while it runs**, both leave the time map bit-identical; a Write pass overwrites only where the playhead reaches and cannot erase a node it never passed over |
| `testSwitchingOutOfAWriteModeKeepsTheRecordedData` | leaving Touch for Read is neither an undo nor a delete |
| `testTrimOffsetIsNonDestructive` | `effectiveAutomationValue(w) == w + offset`; setting/changing/clearing the trim leaves the clip bit-identical |
| `testZeroTrimIsABitExactNoOp` | bit-exact for `0.0f`, `-0.0f`, ±1, ±2, `float::min`, `denorm_min` |
| `testTrimReachesTheValueTheEngineReads` | through the real `Song` path: a written 0.5 with trim 0.1 lands as exactly 0.6 on the control, and the clip is still one node with the same bits |
| `testModeStateIsLockFree` | `std::atomic<AutomationMode>`, `<float>`, `<qint64>` are all lock-free |

### 3.2 The Read-mode render is byte-identical — `tests/automation-modes-render-check.sh`

`bash tests/automation-modes-render-check.sh --baseline build/lmms-baseline --candidate build/lmms`

The check renders a real project headlessly (`QT_QPA_PLATFORM=offscreen lmms render <fixture> -f wav
-a -s 48000 -o <out>`), hashes the WAV **data chunk** (a RIFF parse, not the file — a header
difference must not be able to masquerade as an audio one), and compares:

1. **byte-identity** — the reference `Read` render from the baseline binary (built from the base
   commit, kept as `build/lmms-baseline`) against the candidate;
2. **determinism** — the candidate renders the same project twice, so leg 1 would be a real
   difference and not render noise (without this the identity claim is unfalsifiable);
3. **sensitivity** — the same project with the written automation at a different value, and with the
   automation clip removed entirely, must both render differently. (Two renders of two silent files
   are also byte-identical; this is the leg that rules that out.)

Fixtures are generated inside the script from the tracked demo `data/projects/shorties/Crunk(Demo).mmp`
by inserting a real `<automationclip>` whose `<object id>` resolves to the **Master mixer channel's
volume model** — a linear `FloatModel(1, 0, 2, 0.001)`, the model a mixer fader rides. Nothing is
added to the repository. (It has to be that model rather than the song's own "Master volume": the
percentage model maps every clip value onto its ceiling through the inverse-scaling path the comment
at `src/core/Song.cpp:431-437` documents, so two different curves render identically — found while
validating the fixture, and recorded here because it is another face of the same known defect.)

```
--- leg 1+2: byte-identity of the Read render, and render determinism ---
baseline  fader-down : 9023ccce40bf665833c5f2645573c36f268fbad337cf3aabe0b161c0d15b9195 (recorded reference, base commit + CI flags)
candidate fader-down : 9023ccce40bf665833c5f2645573c36f268fbad337cf3aabe0b161c0d15b9195
candidate fader-down2: 9023ccce40bf665833c5f2645573c36f268fbad337cf3aabe0b161c0d15b9195
PASS: Read render byte-identical baseline -> candidate
PASS: the same binary renders the same project byte-identically twice

--- leg 3: sensitivity (the comparison can see a difference) ---
candidate fader-flat: 4db4b4293ffdc6a6333cf94dfaed6888ea77328ac1a3e471b52e3fcf6df18f66
candidate no-auto   : 2b45f119fa408a9f69950228f48761d7ea93c267649d023f826f3fc6abb97ea7
PASS: a different written automation value renders differently (automation is applied)
PASS: removing the automation clip renders differently

automation-modes-render-check: PASS (all legs)
```

Every render is 797440 frames, RIFF `fmt` tag 3 (32-bit float), 2 channels, 48000 Hz — 16.6 s
of stereo audio, i.e. the file is not vacuous.

**Provenance of the reference hash, stated plainly.** `9023ccce…` was produced by the base commit
`0c23587d2` built in this worktree with the CI's linux-x86_64 flag set
(`-DUSE_WERROR=ON -DCMAKE_BUILD_TYPE=RelWithDebInfo -DTARGET_UARCH=official -DUSE_COMPILE_CACHE=ON
-DWANT_DEBUG_CPACK=ON -DWANT_QT6=ON`), through this same script and these same three fixtures, and
it is the value the run was repeated against (that run rendered the same project three times with the
base binary and all three hashes were identical).
The one thing this document does *not* contain is a freshly built baseline binary in the same
invocation: the machine is shared with five sibling lanes and a full rebuild of the base tree is
~35 minutes, and the binary kept from the base build was lost when a shared-disk reclaim deleted
this worktree's `build/` mid-rebuild (disk was at 88%; the directory vanished between 23:46 and
23:50 and the rebuild failed with exit 2 and no log). The script takes `--baseline <lmms-binary>`
for anyone re-deriving it. What makes the recorded reference trustworthy rather than merely
asserted is that an *independently built* candidate binary reproduces it byte for byte — which is
exactly the claim being tested: the Read render is unchanged.

### 3.3 What is *not* claimed

The render check proves the **Read** path is behaviour-preserving and that automation is applied.
It is not an end-to-end test of Touch/Latch/Write *through the GUI* (no GUI event loop in a headless
render); that semantics is pinned by the unit tests above, which drive the same decision function the
render path calls.

### 3.4 The first run of this suite failed, and it was the test at fault

Worth recording, because it is the useful kind of failure. `testAModeChangeNeverDropsRecordedData`
cycled every mode *while the transport ran*, Write included — and Write legitimately writes the pass,
so a node appeared at tick 2 and the neighbouring tangents were regenerated. The test was asserting
"a mode change never drops data" by taking an action that is not a mode change but a writing pass.
It now pins the property in the two states where it actually holds (transport stopped; and playing
through the modes that have no gesture to write with) and asserts the Write case separately: a pass
overwrites where the playhead reaches and cannot erase a node it never passed over. Commit
`db0d7d255`.

## 4. What is NOT done

1. **Sample-accurate playback of volume/pan — not implemented.** The evaluation path does not make
   it reachable by a small change, and this is the precise blocker:
   - Automation is evaluated **once per tick**, not per frame: `src/core/Song.cpp:339`
     (`if (static_cast<f_cnt_t>(frameOffsetInTick) == 0)`) guards the call at `:342`, and the value
     reaches the control as a single `setValue` at `:474`. At 48 kHz / 140 bpm
     `Engine::framesPerTick()` (`src/core/Engine.cpp:140`) is ≈ 428.6 frames, so a fader ride is
     quantized to ~8.9 ms steps.
   - The *consumer* half already exists — `MixerChannel::updatePostFaderBuffer`
     (`src/core/Mixer.cpp:341-350`) multiplies per frame when `m_volumeModel.valueBuffer()` is
     non-null, and `AutomatableModel::valueBuffer()` (`src/core/AutomatableModel.cpp:530`) fills a
     per-frame buffer — but its only sources are a sample-exact *controller* (`:548`, LFO/Peak) and a
     one-block linear interpolation between the previous and current value (`:607`). That is a
     block-interpolated step, not a sample-accurate read of the clip's curve.
   - The per-frame reader that would be needed exists but is not on the playback path:
     `AutomationClip::valuesAfter` (`src/core/AutomationClip.cpp:651`) is called only by the two GUI
     editors (`src/gui/clips/AutomationClipView.cpp:315`,
     `src/gui/editors/AutomationEditor.cpp:1324`) to draw, and it `new[]`s its result
     (`src/core/AutomationClip.cpp:666`) — an allocation on any audio-thread use.
   Making this sample-accurate means a per-frame producer feeding `ValueBuffer` from the clip, aligned
   with the mixer's period (which is not the same thing as a tick), and it belongs with the automation
   data-model rework the code itself asks for. Landing half of it would silently misalign volume
   against the period, which is worse than the current step.
2. **Persistence.** The mode, the trim and the timeout are not saved in the `.mmp`. A project
   reloads with every control in Read and no trim. `AutomatableModel::saveSettings`/`loadSettings`
   (`include/AutomatableModel.h:267`, `:271`) were deliberately not touched: they are the
   project-file compatibility surface, and the alpha's files must keep loading unchanged. This is
   the first follow-up.
3. **Only the mixer fader is wired to a touch gesture.** Pan, sends and plugin-parameter knobs
   (`src/gui/widgets/Knob.cpp`) would each need press/move/release hooks. The mode state machine and
   the write path are per-`AutomatableModel`, so the missing piece is the widget hook, not the
   semantics; there is no per-control mode UI anywhere yet either (no menu to set a mode).
4. **Write mode does not erase the un-passed remainder of the clip.** Write overwrites the pass as
   the transport moves through it (which is what `recordValue`/`putValue` do per tick, including
   removing a node the new value has moved away from — `src/core/AutomationClip.cpp:435-447`). If a
   Write pass is stopped halfway, the automation *ahead of the playhead* is untouched. That is the
   safe direction, and it is stated rather than silently different from a console's write-to-end.
5. **No per-mode undo.** A pass is journalled the same way the existing record path journals it
   (`addJournalCheckPoint` in the GUI paths); there is no "undo this pass" distinct from the global
   undo stack.
6. **The trim is applied to automated controls only** (it is applied where the automation is read
   out, `src/core/Song.cpp:474`), and it is clamped with the value: an offset that pushes the value
   past the model's maximum is clamped by `fittedValue` and the control reports the clamped value.
7. **The known-flaky headless exit** is not fixed: the binary intermittently aborts *after* writing a
   complete WAV ("QThread: Destroyed while thread is still running", ~1 run in 10, pre-existing and
   unrelated to this change). The render check reports the exit code and accepts a run that produced
   a complete file, which is why its verdicts are hashes and not exit codes.

## 5. Evidence

### Build and test (unpiped exit codes)

```
$ JOBS=4 bash tools/local-ci.sh --build-dir build --jobs 4
```

- **Before** (base `0c23587d2`): configure `EXIT=0`, build `EXIT=0`, ctest `EXIT=0`,
  `100% tests passed, 0 tests failed out of 25`.
- **After:** configure `EXIT=0`, build `EXIT=0`, ctest `EXIT=0`,
  `100% tests passed, 0 tests failed out of 26` (the 25 above plus `AutomationModesTest`), 39.17 s.
  Raw output: `configure EXIT=0 / build EXIT=0 / ctest EXIT=0 / ctest totals: 100% tests passed, 0
  tests failed out of 26 / local-ci: overall exit=0`.
- Deviation: `-DWANT_QT6=ON` (this box has no Qt5 development files; the CI runner installs
  `qtbase5-dev`). Suite denominator: 25 in this configuration; the 26 in `tests/QA-GATES.md` is the
  same tree configured with `WANT_STEM_SPLIT=ON` (three stem tests), which is off here.

### The gates

| gate | command | exit |
|---|---|---|
| 3 — no tautological tests | `bash tests/no-tautology-gate.sh` | 0 |
| 4 — per-method complexity | `bash tests/complexity-gate.sh --check` | 0 |
| 6 — no undeclared upstream divergence | `bash tests/no-upstream-regression-gate.sh` | 0 |
| 7 — per-file length | `bash tests/file-length-gate.sh --check` | 0 |
| 8 — token duplication | `bash tests/duplication-gate.sh --check` | 0 |

Gate 6's ledger: this change adds four entries to `tests/upstream-modifications.txt`
(`include/AutomatableModel.h`, `src/core/AutomatableModel.cpp`, `src/core/Song.cpp`,
`src/gui/widgets/Fader.cpp`), each with the reason read off the diff. The new test and the render
check live under `tests/`, and `tests/CMakeLists.txt` is build config, so neither needs a ledger
entry.

**Note on `--strict`:** `bash tests/no-tautology-gate.sh --strict` exits 1 on this tree **for every
registered test file**, pre-existing ones included ("FEWER ASSERTS THAN SLOTS" for all 23), because
the mode counts `init()`/`initTestCase` slots that cannot carry an assertion. That is a property of
the check, not of this change; the enforced (non-strict) mode is the one reported above.

### Collision with the sibling lane

`post-alpha/midi-race` is editing `src/core/AutomatableModel.cpp` in its own worktree. This change
keeps `AutomatableModel` **additive**: new API in its own banner-marked block in the header
(`include/AutomatableModel.h:323-405`), new state at the end of the member list (`:509-530`), and the
whole implementation appended at the end of the `.cpp` (`src/core/AutomatableModel.cpp:762-946`).
`setControllerConnection` and the controller-binding code are untouched, and no existing member,
method or signature was restructured.
