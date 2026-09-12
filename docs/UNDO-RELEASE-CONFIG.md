# `control.undo` kills the process after it has replied: the deferred view destructor

**Verdict.** Reproduced deterministically, diagnosed, fixed, and verified. `control.undo` replies
`ok {"undone": true, "undone_command": "transport.set_tempo"}` and the process then dies with
`SIGSEGV` (exit status `-11`) one event-loop iteration later. The fault is in
`~InstrumentTrackView()`, which runs from a **posted event** (`Qt::WA_DeleteOnClose`) *after* the track
it views has already been destroyed, and which reads its model to reach the MIDI port menus it owns:

```cpp
InstrumentTrackView::~InstrumentTrackView()
{
	delete m_window;
	m_window = nullptr;

	delete model()->m_midiPort.m_readablePortsMenu;      // src/gui/tracks/InstrumentTrackView.cpp:178
	delete model()->m_midiPort.m_writablePortsMenu;      // :179
}
```

`ProjectJournal::undo()` restores the `transport.set_tempo` **Song checkpoint**;
`TrackContainer::loadSettings()` tears the container down and rebuilds it (`clearAllTracks()`), and it
deletes the container's **tracks** while their **views** are still up. A `TrackView` closes itself
from its track's `destroyedTrack()` signal and — because it sets `Qt::WA_DeleteOnClose` — is only
*scheduled* for deletion, so its destructor runs with a dead model. The order the rest of the tree
keeps is **view-then-track** (`TrackContainerView::deleteTrackView()`, and `Song::clearProject()`
clearing the editor views first); the serialization-restore path was the one that did not announce
itself. The fix is that announcement (`TrackContainer::aboutToClearTracks()`) plus the slot that
answers it (`TrackContainerView::removeAllTrackViews()`).

## Two corrections to the framing this lane was handed

**1. It is not a release-configuration defect.** The handover said "release configuration kills it,
Debug is fine". Measured: **both configurations die, on exactly the same cells** (§5). The
discriminating variable is the **session**, not the build:

> The undo kills the instance when the Song container holds an **instrument track** at the moment of
> the restore. `~InstrumentTrackView()` is the only track view in this tree that dereferences its
> model in its destructor; `~PatternTrackView()` uses its own `m_patternTrack` member and a
> pointer-keyed registry lookup, so a session whose Song holds only pattern tracks survives the same
> undo on the same binary.

The "Debug passes" observation is explained by the **probe**, not the build: the only thing ever run
against a Debug build was `verify-undo-replies.py`, which **opens the bridge fixture** — one pattern
track, no instrument track — while the reproduction that fails **opens no project at all**, so its
session is the default one, which this tree builds *with* an instrument track. Same operation, two
different sessions.

**2. A bug of my own, on the record.** My first 4-cell matrix reported "4 of 4 crash, so the project
is not the variable" — the exact opposite of the truth. It parsed the cell name as `<shape>-<source>`
and the cells are named `<source>-<shape>`, so `project.open` was **never called in any cell** and all
four ran the default session. The tell was in the machine-readable record, not the summary line: the
`project` field was empty and `project_open` was absent from every row. `matrix-release.log`'s
first block is that invalid run, kept deliberately; §1 is the corrected one.

Nothing was pushed, no tag exists, `origin` (LMMS/lmms) and `messmerd` were never written.

> **Base note.** This branch was created from `post-alpha/integration` at `3fd5a4f3c`; everything below
> is measured against that commit. `post-alpha/integration` has since moved to `6212c9c01` (another
> session merged the telemetry kill-switch), and `git diff --stat 3fd5a4f3c..6212c9c01` over the four
> files this lane touches is **empty**, so the merge is trivial.

---

## 1. The reproduction, and what actually discriminates

### 1a. The reported repro, and the corrected matrix

`repro-undo-release.py` is the repro as reported: start, poll `control.ping` until `engine_ready`,
`transport.set_tempo`, `control.undo`, then one more request on the same connection. No project is
opened. `matrix-undo.py` separates the two variables that had been conflated — **{default project,
project opened} × {the earlier lane's probe shape, the minimal probe}** — two runs per cell, and
records the session's own `track.list` per cell (`matrix-release.log`, `matrix-release.jsonl`):

| cell | session | shape | run 1 | run 2 |
| --- | --- | --- | --- | --- |
| `empty-minimal` | default project: 4 tracks `[instrument, sample, pattern, automation]` | minimal | **DEAD `rc=-11`** | **DEAD `rc=-11`** |
| `empty-probe` | default project | earlier lane's probe | **DEAD `rc=-11`** | **DEAD `rc=-11`** |
| `fixture-minimal` | bridge fixture opened: 1 track `[pattern]` | minimal | PASS | PASS |
| `fixture-probe` | bridge fixture opened | earlier lane's probe | PASS | PASS |

### 1b. The discriminator, isolated

`probe-tracktypes.py` opens each bundled project, records the Song container's track types, performs
the same one tempo change and one undo, and reports whether the instance survived. Same binary, six
sessions (`tracktypes-release.log`):

| session | Song tracks | result |
| --- | --- | --- |
| default project | 4 — `[instrument, sample, pattern, automation]` | **DEAD `rc=-11`** |
| `demos/Alf42red-Mauiwowi.mmpz` | 17 — 16 × instrument, 1 × automation | **DEAD `rc=-11`** |
| `demos/AngryLlama-NewFangled.mmpz` | 19 — automation + pattern + instrument mix | **DEAD `rc=-11`** |
| `demos/Ashore.mmpz` | 26 — instrument/pattern/automation mix | **DEAD `rc=-11`** |
| `demos/CapDan/CapDan-ReggaeTry.mmpz` | 9 — 4 × pattern, then 4 × instrument | **DEAD `rc=-11`** |
| bridge fixture `agent-control-fixture.mmp` | 1 — `[pattern]` | **PASS** |

**Five sessions with an instrument track in the Song: dead. The one without: alive.** That is the
variable — not the configuration, not the project as such, not the probe shape. Note also what the
fixture is: the session the earlier lane's whole verification used, which is why that lane's fix
looked complete and why its probe stays green here.

### 1c. Which step kills it

`probe-variants.py` (`variants-release.log`), default session, one step at a time:

| variant | what it does | result |
| --- | --- | --- |
| `ping-only` | ping, wait 1 s, ping | PASS |
| `tempo-only` | `transport.set_tempo`, wait, ping | PASS |
| `undo-only` | `control.undo` with an **empty journal**, wait, ping | PASS |
| `tempo-then-undo` | `transport.set_tempo`, `control.undo`, wait, ping | **DEAD `rc=-11`** |
| `undo-slow` | the same, waiting 3 s before the last ping | **DEAD `rc=-11`** |

The socket layer, the dispatch, the reply path and the no-op undo are clean: the kill needs
`journal->undo()` to actually **restore a checkpoint**.

## 2. The stack, and how it died

`gdb-wrapper.sh` runs the instance under gdb with the probe unchanged (`gdb-undo-release-bt.log`,
`under-gdb.log`, `instance-app.log`):

```
Thread 1 "zene" received signal SIGSEGV, Segmentation fault.
#0  lmms::gui::InstrumentTrackView::~InstrumentTrackView (this=0x55555768f400)
      at src/gui/tracks/InstrumentTrackView.cpp:178
178         delete model()->m_midiPort.m_readablePortsMenu;
#1  lmms::gui::InstrumentTrackView::~InstrumentTrackView (...) at .../InstrumentTrackView.cpp:180
#2  QObject::event(QEvent*)                                   libQt6Core
#3  QApplicationPrivate::notify_helper                        libQt6Widgets
#4  QCoreApplication::notifyInternal2                         libQt6Core
#5  QCoreApplicationPrivate::sendPostedEvents                 libQt6Core
...
#12 QCoreApplication::exec()                                  libQt6Core
#13 main                                                      src/core/main.cpp:1318
```

**Frame #2 is the whole of "after the reply":** the destructor is not on the undo's stack at all — it
runs from `QCoreApplicationPrivate::sendPostedEvents`, i.e. from a **posted event**, one event-loop
iteration after `control.undo` answered. An agent sees a successful call and then a corpse, and no
handler is left running that could have returned an error.

### The faulting memory access

```
$1 = (void *) 0x308                  <- $_siginfo._sifields._sigfault.si_addr
$3 = 0x0                             <- $rdi at the fault
```

`si_addr 0x308` with `rdi == 0` is `model()` yielding **null** and the member access compiled as a load
at `null + 0x308` — `m_midiPort.m_readablePortsMenu`'s offset inside `InstrumentTrack`. State at the
crash (`gdb-trackview-state.log`, `gdb-model.log`):

```
this          = 0x555557566310          (the view, being deleted from a posted event)
m_track       = 0x555557a26410          (the track it still points at)
m_window      = 0x0                     (no instrument window was ever opened)
m_readablePortsMenu = 0x0, m_writablePortsMenu = 0x0   (never created: the audio engine's
                                                       MIDI client is the raw dummy one)
```

and the first word of the object at `m_track` — the vptr a `dynamic_cast` needs — reads **`0x0`**
(`gdb-vptr.log`): the memory the view calls its model is freed, not a live `InstrumentTrack`. The two
`delete`s would each be a harmless `delete nullptr`; it is `model()`'s **reaching for the model** that
faults.

### The same defect has more than one consumer

With `MALLOC_PERTURB_=170` (which fills freed memory, changing what the dangling pointer reads) one
run faulted **in a different method of the same class** — quoted from that run, whose gdb log the next
run overwrote:

```
#0  lmms::gui::InstrumentTrackView::corruptStateUpdate (this=0x55555768c0a0)
      at src/gui/tracks/InstrumentTrackView.cpp:433
433         if (model()->audioBusHandle()->isCorrupted())
$_siginfo._sifields._sigfault.si_addr = 0x1c18
```

`corruptStateUpdate` is reached from `MainWindow::periodicUpdate` — a timer, not the destructor. Both
are consumers of the same dangling model; which one wins is a race. Nine further perturbed runs
(`gdb-release-perturb-{1,2,3,b1..b5}.log`, `gdb-release-perturb-slow.log`) all landed on line 178 at
`si_addr 0x308`, so line 178 is the reproducible site and `corruptStateUpdate` is the same defect by a
second route.

## 3. The mechanism, in order

1. `control.undo` → `undoLastCommand` → `undoThroughJournal` → `ProjectJournal::undo()` unwinds the
   top checkpoint. `transport.set_tempo`'s checkpoint captures the **Song**
   (`ControlCommandsTransport.cpp` calls `song->addJournalCheckPoint()` before `setTempo()`, and the
   registry merges the model's own checkpoint into the same step), so the restore rebuilds the whole
   track container.
2. `ProjectJournal::restoreState()` → the Song's `loadSettings()` → `TrackContainer::loadSettings()`
   sees `parentNode().nodeName() == "journaldata"` and calls `clearAllTracks()`
   (`src/core/TrackContainer.cpp:87-91`).
3. `clearAllTracks()` is `while (!m_tracks.empty()) delete m_tracks.front();` — it deletes the
   **tracks**. Their views are not touched.
4. `~Track` emits `destroyedTrack()`, which `TrackView` connected to `TrackView::close()`
   (`src/gui/tracks/TrackView.cpp:93`, `:200`). `close()` removes the view from
   `TrackContainerView::m_trackViews` and calls `QWidget::close()`.
5. The view was constructed with `setAttribute(Qt::WA_DeleteOnClose, true)`
   (`src/gui/tracks/TrackView.cpp:91`), so `QWidget::close()` only **posts a deferred delete**. The
   view survives, its model pointer still naming the track just destroyed.
6. The restore continues; `Track::create()` builds the replacement tracks.
7. `control.undo` replies `ok` and the event loop returns to `sendPostedEvents`.
8. The posted event runs `~InstrumentTrackView()`, which asks its model for the MIDI port menus. The
   model is gone (vptr `0x0`), `model()` yields null, the member access faults at `0x308`. Process
   gone, every client connection with it.

`~PatternTrackView()` does not do this — `PatternTrack::s_infoMap.value(m_patternTrack)` uses a
pointer *value* as a key, not a dereference — which is why the fixture session survives step 8.

### The trail, measured

`gdb-reuse-trail.gdb` (breakpoints only, no code change) prints every `InstrumentTrack` construction
and destruction and every deferred view destructor, in order (`gdb-reuse-trail.log`):

```
CTOR InstrumentTrack 0x555557a29730
DTOR InstrumentTrack 0x555557a29730        <- step 3: the container deletes the track
DTOR InstrumentTrack 0x555557a29730
CTOR InstrumentTrack 0x555557a29320        <- step 6: the restore's replacement tracks
CTOR InstrumentTrack 0x555557f324d0
>>> DEFERRED-VIEW-DTOR this=0x55555768d070 track=0x555557a29730 window=(nil)   <- step 8
Thread 1 "zene" received signal SIGSEGV, Segmentation fault.
```

The view's destructor names **the address of the track whose destructor already ran**.

## 4. Why this is not the defect the earlier lane fixed

`docs/CONTROL-UNDO-CONNECTION-DROP.md` fixed a different fault on a neighbouring path: a
`QMap::operator[]` read of `PatternTrack`'s static registry that registered the caller, reached from
`PatternStore::updateComboBox()` *inside* `ProjectJournal::undo()` — **before** the reply, on the
undo's own stack. Its four-file fix is on this branch and is not disturbed. This defect has a
different stack (`~InstrumentTrackView` from a posted event, after the reply), a different object (an
`InstrumentTrack`, not a pattern track), and it fires on sessions with no pattern-track machinery in
play at all. It is also why that lane's probe stayed green: its fixture has no instrument track.

## 5. Debug vs release: the difference does not exist

The framing this lane was handed was `-O2`/`NDEBUG` vs `-O0`. A Debug build of the same commit
(`-DCMAKE_BUILD_TYPE=Debug`, `-DUSE_WERROR=ON`, same Qt6/VST3/CLAP flags) was built and run on both
sessions, and on the same six-session sweep:

| session | release configuration (`build-release`) | Debug (`build-debug`) |
| --- | --- | --- |
| `empty-minimal` (default project) | DEAD `rc=-11` ×2 | **DEAD `rc=-11`** |
| `empty-probe` (default project) | DEAD `rc=-11` ×2 | **DEAD `rc=-11`** |
| `fixture-minimal` (fixture) | PASS ×2 | **PASS** |
| `fixture-probe` (fixture) | PASS ×2 | **PASS** |
| default project + 4 bundled demo projects (`tracktypes`) | 5 DEAD of 5 | **5 DEAD of 5** |
| bridge fixture (`tracktypes`) | PASS | **PASS** |

`matrix-debug.log`, `tracktypes-debug.log`. And the Debug crash is not merely the same symptom — it is
the same frame and the same call path (`gdb-debug-bt.log`):

```
Thread 1 "zene" received signal SIGSEGV, Segmentation fault.
#0  lmms::gui::InstrumentTrackView::~InstrumentTrackView (...) at .../InstrumentTrackView.cpp:178
#1  lmms::gui::InstrumentTrackView::~InstrumentTrackView (...) at .../InstrumentTrackView.cpp:180
#2  QObject::event(QEvent*)
#5  QCoreApplicationPrivate::sendPostedEvents
```

**One defect, both configurations.** No `-O2`/`NDEBUG` explanation is needed or available: the
use-after-free is deterministic in both, and the only thing that ever made Debug look safe was a probe
whose fixture has no instrument track in it.

## 6. The fix

One idea, four files: **a container must take its views down before it deletes its tracks** — the order
`TrackContainerView::deleteTrackView()` and `Song::clearProject()` already keep — and the
serialization-restore path has to say when that moment is.

| file | change |
| --- | --- |
| `include/TrackContainer.h` | new signal `aboutToClearTracks()`, emitted immediately before a restore clears this container's tracks |
| `src/core/TrackContainer.cpp` | `loadSettings()` emits it before `clearAllTracks()` in the journal-restore branch |
| `include/TrackContainerView.h` | new slot `removeAllTrackViews()` |
| `src/gui/editors/TrackContainerView.cpp` | the view connects the signal to that slot and implements it (delete every view, leave the tracks) |

Why this is the root cause and not a guard:

* it removes the **cause** — on this path the view no longer outlives its model, so
  `~InstrumentTrackView()`'s two `delete`s run while the track is alive, exactly as they do on every
  path that always worked (`deleteTrackView`, `Song::clearProject`);
* it is the order the tree already relies on, so it adds no new invariant;
* it fixes the whole class for that container rather than one statement: the same deferred window is
  what lets `corruptStateUpdate` reach a dead model (§2), and the same mechanism manufactures the
  registry ghost the earlier lane patched around in `PatternTrack`, so that guard now has nothing to
  guard;
* a null guard would have been the wrong fix on principle, not just in style: **the failure mode is a
  *dangling* pointer as often as a null one**, and no `if (model() != nullptr)` can see the difference.
  `ModelView::~ModelView()` is the tree's own precedent for that mistaken guard (§7).
* nothing else changes: `~TrackContainer`'s own `clearAllTracks()` and `project.open` are untouched,
  and a plain load has already cleared the views in `Song::clearProject()` before it reaches here, so
  the signal finds none.

Divergence from upstream is declared in `tests/upstream-modifications.txt` in the same commit
(`src/core/TrackContainer.cpp`'s existing `#625` entry carries this lane's reason appended to it; one
entry per path, no duplicate key), as Gate 6 requires.

## 7. The class sweep, answered

Four destructors in `src/gui/**` dereference a model. Verdict on each, explicitly:

| site | reachable after its model is destroyed? | why |
| --- | --- | --- |
| `~InstrumentTrackView` (InstrumentTrackView.cpp:178-179) | **YES — measured** | the deferred-delete path described above: the track dies in `clearAllTracks()`, the view is destroyed one event-loop iteration later. Both statements are reached, and by the time the first one runs the base class's guard has not executed yet — `~InstrumentTrackView` runs **before** `~ModelView`. |
| `~ModelView` (ModelView.cpp:42-48) | **YES, latently — not measured to crash** | its `m_model != nullptr && m_model->isDefaultConstructed()` is exactly the guard that cannot detect a dangling model, and it runs *after* every derived destructor. In the measured crash it is never reached (line 178 faults first); with the two menus null on this box it would be the next reader of the dead track. It is reachable in a *normal* path too: `~AudioAlsaSetupWidget` deletes the model and Qt then destroys `m_channels`, whose `~ModelView` reads the deleted model (see below). |
| `~MainWindow` (MainWindow.cpp:258-262) `delete view->model(); delete view;` | **NO — same shape, not reachable** | the window owns both the `m_tools` `PluginView`s and the plugin models they expose, destroys them in one synchronous pass on the GUI thread, and deletes the model **first** while it is alive. Nothing else frees a `Plugin` model, and there is no deferred deletion in the loop. The shape — a view deleting its model — is the same one this defect is about, so it is the natural next place a lifetime rule would be tested; today it is sound because both sides are owned by one object. |
| `~AudioAlsaSetupWidget` (AudioAlsaSetupWidget.cpp:88-91) `delete m_channels->model();` | **NO — the model is live; the widget's own teardown leaves `~ModelView` a dangling read** | `auto m = new LcdSpinBoxModel();` (no parent, `isDefaultConstructed=false`) is created by this widget for this widget, handed to its own `LcdSpinBox` child, and freed exactly once, here. The child outlives this destructor body (Qt destroys it later), so its `~ModelView` then reads a freed `m_model` — harmless-looking, and the same latent class as the row above. |

### Who should own the two MIDI-port menus

Ownership today is split, and that split is *why* this crash is possible:

* the pointers are **members of `lmms::MidiPort`** (`include/MidiPort.h:141-142`), initialised to
  `nullptr` by `MidiPort::MidiPort` (`src/core/midi/MidiPort.cpp:51-52`);
* they are **created by the view** — `InstrumentTrackView`'s constructor
  (`src/gui/tracks/InstrumentTrackView.cpp:103-109`) — and only when the MIDI client is not the raw
  dummy one;
* they are **freed only by `~InstrumentTrackView`**, which therefore has to reach through `model()` to
  find them;
* `MidiPort::~MidiPort()` (`src/core/midi/MidiPort.cpp:95-103`) does **not** free them: it
  unsubscribes and unregisters the port. That is deliberate — `deleteTrackView()` deletes the view
  *before* the track, so a `~MidiPort` that also freed them would double-free on that path.

**Verdict: yes, the model should free what the model stores, and `~MidiPort` is the right place** — it
runs as part of `~InstrumentTrack`, i.e. at a moment when the object is unambiguously alive and
correctly typed, which is exactly what `~InstrumentTrackView` cannot guarantee. That change would make
the view's destructor model-free and would remove this statement as a fault site.

It is **not** in this commit, deliberately, for two reasons I would rather state than paper over:
(1) it does not fix the *class* — `corruptStateUpdate` (§2) and `~ModelView` (§7) would still touch a
dead model, so the ordering fix is the one that closes the defect; (2) on this box
`midiClient()->isRaw()` is true, so **the menus are never created and never freed** — the ownership
change cannot be exercised here, and an unmeasurable change should not ride along with a verified one.
Recommendation: land it as its own slice, with a non-dummy MIDI client so it can be tested, and with
`~MidiPort` freeing the two pointers while the view stops touching them.

**Residual, stated plainly.** Between `destroyedTrack()` and the deferred deletion a view still holds a
raw pointer to its model; this fix removes the reachable dereferences on the restore path, but a
`TrackView` that outlives its model by any other route is still a use-after-free. Owning views *by the
container* — so no path can separate them — is the architectural version. It is a larger change than a
release fix should carry, it is not needed to close this defect, and it is recommended for the next
slice rather than smuggled into this one.

## 8. Proof, on the release configuration

All of the following ran against `zene-undo-rel-fix/build-release/zene` — the release configuration
(`RelWithDebInfo`, `-DUSE_WERROR=ON`, `-DTARGET_UARCH=official`, `-DUSE_COMPILE_CACHE=ON`,
`-DWANT_QT6=ON -DWANT_VST3=ON -DWANT_CLAP=ON`), built from the tree of this branch's commit,
`BUILD_EXIT=0`. Artefacts in `tests/integration-logs-undo-rel/`.

| proof | result | log |
| --- | --- | --- |
| the exact repro, `bpm 120`, **3 runs** | `EXIT=0` ×3; undo replies `ok`; the **next call on the same connection answers**; process alive | `repro-release-run{1,2,3}.log` |
| the 4-cell matrix, 2 runs | **4 of 4 PASS, twice** | `matrix-release-after.log` |
| the discriminator re-measured | **default project + 4 bundled projects: 5 of 5 PASS** (all were DEAD before) | `tracktypes-release-after.log` |
| the tree's own probe | `verify-undo-replies.py` (unchanged) **8/8 ok, `VERIFY_EXIT=0`** | `verify-undo-replies-after.log` |
| `ctest` from `build-release/tests` | **100% tests passed, 0 failed out of 86, `CTEST_EXIT=0`** | `ctest-after.log` |
| the render | `data/projects/shorties/sv-DnB-Startup.mmpz` — **the same project the lane before used** — twice: `943e323864cb5c07bb8da264736978be9294513b93c1b6b81421cb6d8a890526` (2 177 120 bytes), `data` payload `b37cefc5a97e2d4664bbb0087a187935cdb9421030492f3b21972c3a7e3e59ca`, `fmt`/`LIST` payloads identical | `render-after-chunks.log` |
| the repro under gdb | **no `SIGSEGV`, no `~InstrumentTrackView` frame**; undo `ok`, next call `ok`, alive | `gdb-after.log`, `under-gdb-after.log` |
| Gate 6 (inherited code) | see the note below | `gate6-after.log` |
| `fork-sources-gate.sh` | **PASS** — "every tracked source in scope is registered (242 fork-NEW, 1036 inherited, 35 tooling)" | `gate-fork-sources.log` |

The render is compared by **chunk payload**, not by file hash alone, and against the value recorded by
the lane before (`tests/integration-logs-3f-undo/render-after-chunks.log`) and by the release session
(`b37cefc5…`) — same project, same values, no cross-project comparison.

**Gate 6 note (a pre-existing red, not this lane's).** On the committed tree the gate reports **22
violations, every one of them a `docs/release-verification-0.2.0-alpha/*.log`** — another session's
committed verification logs, which have no ledger entry. None of the four files this lane touches is
among them (each is a `declared divergence`). I did not add 22 ledger entries for another session's
files: that is an unrelated change, and the same reasoning the earlier lane used about
`tools-sources.txt`. It needs an owner.

## 9. What I could not establish

* **Whether the GUI's Ctrl+Z hits the same path.** It calls the same `Engine::projectJournal()->undo()`
  and the fault is below that entry point, so it is expected to — but this is a headless
  (`QT_QPA_PLATFORM=offscreen`) instance and no click was made.
* **`m_window != nullptr`.** With an instrument window open, `delete m_window` hands a dead `MidiPort`
  to `InstrumentMidiIOView`'s `ModelView` base. The ordering fix removes the deferred window teardown
  on this path too, but no repro that opens the window first was built, so that site is reasoned
  about, not measured.
* **The exact reason `model()` yields null rather than faulting** on a zero vptr. Measured: the first
  word of the object at `m_track` is `0x0`, and the fault is at `0x308` with `rdi == 0`.
* **Which allocation owns the freed block.** In the trail run the replacement `InstrumentTrack`s
  received different addresses, so the block was taken by another object of the same size class; its
  identity was not established.
* **The ownership change in §7** — `~MidiPort` freeing the two menus — is reasoned, not measured: on
  this box the menus are never created (`isRaw()` MIDI client), so it cannot be exercised here.
* **Whether any other command restores a whole-container checkpoint.** Song checkpoints are what make
  the view-teardown reachable; the set of commands that push them was not enumerated.

## 10. Process, hygiene, artefacts

* Two worktrees of the product clone, one per job: `zene-undo-rel` (reproduction, diagnosis, the Debug
  build, the Debug verification) and `zene-undo-rel-fix` (the fix, the release build, the release
  verification). `git worktree list` otherwise untouched; `zene-pa-integration` was never read, built
  in, or named; the sibling lanes' worktrees (`zene-next-telemetry-off`, `zene-next-*`) were never
  touched. Both builds ran at `-j2`.
* The Debug build was restarted once, after a transient **link** failure this lane caused by editing
  sources while the build was running (objects compiled from two different headers cannot link). Its
  consistency control is the fixture session and the default-project session, which behave on Debug
  exactly as they do on release — the expected thing for an unfixed tree, and the reason the Debug
  result is usable.
* Nothing pushed, no tag, no rebase, no force, no `git add -A`; `origin` and `messmerd` unwritten.
* Every exit code quoted here was measured **unpiped** (`cmd > log 2>&1; echo EXIT=$?`), and every log
  lives under `tests/integration-logs-undo-rel/` in the repository — no `/tmp`. The two 2.1 MB render
  WAVs are not committed; their chunk hashes are the durable record, and are in
  `render-after-chunks.log`.
