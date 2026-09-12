# `control.undo` dropped the client's connection: the DAW-side SIGSEGV, its mechanism, the fix, and the proof

**Verdict.** Reproduced on `post-alpha/foreign-merge` (worktree `zene-pa-foreign`), diagnosed to the
instruction, and fixed. `control.undo` did not "close a connection and carry on": it **SIGSEGV'd the
DAW** (`returncode -11`), and every client socket died with the process. The fault was a null
dereference in `PatternStore::updateComboBox()` reached from `ProjectJournal::undo()` — i.e. *below*
the control surface, on the very call the GUI's Ctrl+Z makes. Its cause is a **read of
`PatternTrack`'s static registry that registered the caller as a side effect** (`QMap::operator[]`
inserts a zero for an absent key); during a checkpoint restore the GUI's own
`PatternTrackView::close()` performed that read on a track whose entry had just been erased, and the
ghost entry it left made the *replacement* track derive the wrong pattern number. The fix is five
one-line read changes (plus one null guard): **no registered-track behaviour changes**, and the
render proves it byte-for-byte.

Nothing was pushed, no tag exists, only this worktree was touched.

---

## 1. What the client saw, and what the client did not see

`tests/integration-logs-3f-undo/probe-undo.py` (the raw `RawDawClient` probe, no MCP, no
`server.py`), against `build/zene` before the fix — `probe-before.log`:

```
project.open               ok=True
transport.set_tempo        ok=True (tempo 128)
control.transactions       ok=True (2 transactions; top: transport.set_tempo, reversible,
                                    inverse {op: transport.set_tempo, args {bpm: 140}})
control.undo               ConnectionError: the instance closed the connection
process_alive_after_undo   True          <-- a race, not survival: see below
same_connection_ping       BrokenPipeError: [Errno 32] Broken pipe
socket_file_exists         True
process_alive_after_1s     False
fresh_connection           ConnectionRefusedError: [Errno 111] Connection refused
process_returncode         -11           <-- SIGSEGV
```

**The framing "the process survives" does not survive contact with the probe.** `poll()` is `None`
only in the instant between the client's `recv` returning EOF and the crash becoming observable;
one second later the process is gone with `-11`, the socket file is a stale inode, and a fresh
connection is refused. So the connection drop is a *symptom* of process death, and "make the failing
command answer with a typed error" was never reachable by editing the command handler: there was no
handler left running to answer. The instance's own `app.log` (`instance-app-before.log`) ends at the
plugin-scan lines — the server printed **nothing** before dying, which rules out a Qt fatal, an
exception, or a log-and-close error path and leaves the signal.

## 2. The server half: the stack and the state at the fault

`tests/integration-logs-3f-undo/gdb-wrapper.sh` (the instance runs under `gdb`, the probe unchanged)
— `gdb-undo-bt.log`:

```
Last stopped for thread 1 (Thread 0x7ffff15b8c80 (LWP 2738611)).
Program stopped at 0x55555583ac1a.
It stopped with signal SIGSEGV, Segmentation fault.

#0  lmms::PatternStore::updateComboBox (this=0x5555563a4be0) at src/core/PatternStore.cpp:203
203          m_patternComboBoxModel.addItem(pt->name());
pt = 0x0
i = 0
curPattern = 0
$3 = 1                            <-- numOfPatterns()
...
#9   ProjectJournal::undo          src/core/ProjectJournal.cpp:126
#10  undoThroughJournal            src/core/ControlCommandsControl.cpp:252
#11  undoLastCommand               src/core/ControlCommandsControl.cpp:308
#12  the control.undo lambda       src/core/ControlCommandsControl.cpp:333
...
#24  ControlServer::dispatchLine   src/core/ControlServer.cpp:392
```

Not a dangling pointer, not a teardown ordering accident: on the **first** iteration (`i = 0`), with
`numOfPatterns() == 1` (the Song really does hold one pattern track), `PatternTrack::findPatternTrack(0)`
returned `nullptr` and the result was dereferenced.

Where the null came from is not inferable from the stack, so it was measured. A temporary
instrumented build (`tests/integration-logs-3f-undo/instance-app-instrumented4.log`; every temporary
`fprintf` was reverted before the fix was written) dumps the registry at each event — `size`, and
every `{key: value}` pair. The undo window, verbatim:

```
131  loadSettings this=0x…34a0 journalRestore=1 tracks=1 parent=journaldata   <-- the Song restore
132  clearAllTracks this=0x…34a0 tracks=1
133  dtor ENTER this=0x…ca080 tc=0x…34a0 pattern=0 s_infoMap.size()=1        <-- the old PatternTrack
141  map[~Track-entry]                       size=0:                          <-- its entry is erased
142  map[~Track-after-emit-destroyedTrack]   size=1: {0x56c6652ca080:0}       <-- THE GHOST APPEARS HERE
145  Track::create type=1 tc=0x…34a0
146  Track::Track body entry this=0x56c6652ca080 tc=0x…34a0                  <-- same address recycled
152  ctor ENTER this=0x56c6652ca080 s_infoMap.size()=1                        <-- wrong number derived
153  map[ctor-after-insert]                  size=1: {0x56c6652ca080:1}       <-- 0 is now vacant
155  updateComboBox numOfPatterns()=1
157    i=0 pt=(nil)                                                           <-- SIGSEGV
```

The insert happens **inside `emit destroyedTrack()`** (line 141 → 142), which is emitted by
`Track::~Track` (`src/core/Track.cpp:112`) after `PatternTrack::~PatternTrack` has already erased its
own entry. The reader is a *friend* access that the first pass of instrumentation missed because it
is neither `patternIndex()` nor one of `PatternTrack.cpp`'s own reads — it is the GUI:

```cpp
// src/gui/tracks/PatternTrackView.cpp  (both sites: the destructor and close())
getGUI()->patternEditor()->m_editor->removeViewsForPattern(PatternTrack::s_infoMap[m_patternTrack]);
```

`gui::PatternTrackView` is a friend of `PatternTrack` (`include/PatternTrack.h:96`), so it reads the
static `QMap<PatternTrack*, int>` with `operator[]`, which **inserts `{m_patternTrack: 0}` when the
key is absent** — and it is absent precisely because the track is being destroyed. That is the ghost.

## 3. The mechanism, in order

1. `control.undo` → `undoLastCommand` (`ControlCommandsControl.cpp:308`) → `undoThroughJournal` →
   `ProjectJournal::undo()` unwinds the top checkpoint, the `transport.set_tempo` **Song checkpoint**
   (`control.transactions` reports its mechanism as "ProjectJournal (Song checkpoint)").
2. `ProjectJournal::restoreState` → the Song's `loadSettings`. `TrackContainer::loadSettings`
   (`src/core/TrackContainer.cpp:87-91`) sees `parentNode().nodeName() == "journaldata"` and calls
   `clearAllTracks()`, which `delete`s the Song's tracks.
3. `~PatternTrack` erases its registry entry, **then** `~Track` emits `destroyedTrack()`.
4. The GUI's `PatternTrackView::close()` (direct connection; the `TrackView` connect is at
   `src/gui/tracks/TrackView.cpp:93/200`) reads `s_infoMap[m_patternTrack]` with `operator[]` on the
   just-erased key → **QMap inserts `{dyingTrackAddress: 0}`**. The entry now outlives the object.
5. The restore continues: `Track::create(element, tc)` constructs the replacement `PatternTrack`, and
   the allocator hands it **the same address** the dying track had — so the ghost belongs to it now.
6. `PatternTrack::PatternTrack` derives its number from the registry: `int patternNum =
   s_infoMap.size();` → `1` instead of `0`, and stores it, **overwriting the ghost's 0**. The registry
   now holds `{addr: 1}` and **no entry has index 0**, while the Song holds one pattern track.
7. `updateComboBox()` loops `for (i = 0; i < numOfPatterns(); ++i)`: index 0 has no track,
   `findPatternTrack(0)` returns `nullptr`, `pt->name()` faults. UI thread, `SIGSEGV`, process gone,
   every connection gone.

The mechanism needs a *live* `PatternTrackView` for the dying track — i.e. a GUI-created view of a
pattern track that a project load created. That is why this was invisible to the socket-only flows
(`tests/control-reversibility-transcript.py` undoes `track.add`, an *action* checkpoint that destroys
a track instead of restoring a container, and never walks this path) and why it needed the bridge's
end-to-end undo test, which loads the fixture (its single track is `type="1"`, a pattern track), to
surface at all.

## 4. The control: the GUI's Ctrl+Z is the same call

The surface's contract says undo unwinds "the same stack the GUI's Ctrl+Z unwinds". That is literally
true, and it is the whole answer:

```cpp
// src/gui/MainWindow.cpp:1418-1421        (Edit ▸ Undo / Ctrl+Z)
void MainWindow::undo() { Engine::projectJournal()->undo(); }

// src/core/ControlCommandsControl.cpp:246-252    (undoThroughJournal)
if (journal != nullptr && journal->canUndo()) { journal->undo(); undone = true; }
```

Both entry points call `Engine::projectJournal()->undo()`. The faulting frame (#9) is *inside* that
shared function, below both of them, and the ghost that causes it is manufactured by GUI code that
runs identically for a socket-driven undo and a keyboard-driven one (both are the same process with
the same live view; `control.undo` is dispatched on the UI thread, `ControlRegistry::runOnUiThread`).
**So the GUI's Ctrl+Z would crash identically.** The difference between the two paths is not the
defect and there is no difference to point at: our surface merely *reaches* an upstream defect by
using the engine's own undo stack, exactly as documented.

What I could **not** do, and do not claim: click Ctrl+Z. This is a headless (`QT_QPA_PLATFORM=offscreen`)
instance, there is no display to drive, and I did not find a second non-socket trigger for
`ProjectJournal::undo()` in the tree (the Lua bindings expose no undo). The equivalence above is by
**code identity of the entry point plus the position of the fault below it**, not by a GUI click — and
because the two paths share both the entry point and the ghost-producing view, a GUI run is not
expected to differ. A lane with a display could confirm it in one keystroke.

## 5. Whose defect, and why the fix is still ours to make

Everything on the fault path is inherited upstream code: `PatternStore::updateComboBox`'s unguarded
`pt->name()`, `PatternTrack`'s registry and the `s_infoMap[this]` reads, `PatternTrackView`'s friend
reads, and `TrackContainer::loadSettings`' teardown-and-rebuild on a journal restore. We did not
introduce it and we did not modify those files before this change.

It is nevertheless a **product** defect, not an upstream curiosity: `control.undo` is a shipped
command of this product's agent surface, and it kills the process. That is why it is fixed here
rather than documented as a limitation — with the divergence declared in
`tests/upstream-modifications.txt` in the same commit (four entries, one per file), as Gate 6
requires.

## 6. The fix

Four files, five behavioural lines. The rule the change restores, stated once so a future reader can
check it: **only `PatternTrack`'s constructor creates a registry entry, and only its destructor
erases one. Every other access is a read.**

| file | change | why |
| --- | --- | --- |
| `include/PatternTrack.h:79` | `patternIndex()`: `s_infoMap[this]` → `s_infoMap.value(this)` | the public read was the same insert-on-read; `value()` returns the same `int` for a registered track |
| `src/gui/tracks/PatternTrackView.cpp:64,73` | the destructor's and `close()`'s friend reads → `value(...)` | **the** ghost source: it runs from `destroyedTrack()` after the entry was erased |
| `src/tracks/PatternTrack.cpp:68,99,131,162,171,190` | the five remaining non-constructor reads → `value(this)` | same class of defect (`play()` can run while a track is mid-teardown); `swapPatternTracks`' `qSwap(s_infoMap[t1], s_infoMap[t2])` is deliberately left as a **write** — it swaps two live tracks' numbers |
| `src/core/PatternStore.cpp:203-215` | skip an index whose `findPatternTrack(i)` is `nullptr` | defence in depth: `numOfPatterns()` counts the Song's tracks while `findPatternTrack()` searches the registry, so the two *can* disagree; a disagreement must cost a combobox row, never the process |

`QMap::value(key)` and `QMap::operator[](key)` return the **same int** for a key that is present
(`value()`'s default is `int()` = 0, which is what `operator[]` would have inserted and returned for
an absent one). So for every registered track — every track that exists — the observable behaviour is
identical, and the only behaviour that disappears is the fabricated entry. With the entries no longer
fabricated, step 6 of §3 cannot happen: the replacement track derives `0`, index 0 has a track, and
`pt` is never null.

The guard in `updateComboBox` is what makes the *next* disagreement a reply rather than a corpse. It
is not a typed error *reply* at the surface, because it never needs to be: with the registry
consistent the undo succeeds, which is what the client asks for (and gets: §7).

## 7. Proof

**The probe now replies, on a live connection** — `probe-after.log`:

```
project.open               ok=True
transport.set_tempo        ok=True (tempo 128)
control.transactions       ok=True
control.undo               ok=True {"can_redo": true, "can_undo": false,
                                    "mechanism": "lmms::ProjectJournal",
                                    "undone": true, "undone_command": "transport.set_tempo"}
process_alive_after_undo   True
same_connection_ping       ok=True
socket_file_exists         True
process_alive_after_1s     True          <-- before: False with returncode -11
fresh_connection           control.ping ok, control.transactions ok
process_returncode         None
```

**And the undo did its job, not merely "did not crash"** — `verify-undo-replies.py` (new; asserts over
one connection, exit 0) `verify-after.log`, 8/8:

```
ok  control.undo answered a reply (no dropped connection)
ok  the instance survived the undo
ok  the SAME connection still answers control.ping
ok  the undo restored the recorded inverse tempo (140)      (140)   <-- 128 -> 140, the recorded inverse
ok  control.transactions still answers
ok  control.redo still answers
ok  a second control.undo still answers
ok  the connection is still usable after redo+undo again
```

**The bridge suite** — `cd tools/mcp-zene-control && ZENE_CONTROL_BINARY=<worktree>/build/zene python3 -m pytest tests/ -q`,
`pytest-after.log`:

```
64 passed in 21.42s          (measured against this lane's fixed build; the "before" count,
                             63 passed / 1 failed, is the baseline this lane was handed and is
                             recorded in tests/integration-logs-3f-bridge/ — I re-measured the
                             before *probe* myself, §1, rather than re-running the whole suite
                             against a stale binary)
```

`test_11_mutating_command_undo_and_transactions`, by name, `pytest-test11-after.log`:

```
tests/test_mcp_e2e.py::ZeneControlBridgeE2E::test_11_mutating_command_undo_and_transactions PASSED
1 passed, 5 deselected in 3.54s
```

No test was weakened, skipped or deleted, and `control.undo` was not removed.

**ctest** — `ctest --test-dir build/tests --output-on-failure` (exit 0 unpiped), `ctest-after.log`:

```
100% tests passed, 0 tests failed out of 86
```

Both counts were re-measured on the **committed** tree after the two commits landed
(`pytest-committed-tree.log` 64 passed / exit 0, `ctest-committed-tree.log` 86/86 / exit 0); the
summary of every run, its command and its unpiped exit code is in `final-verification.txt`.

**The render did not move** — `QT_QPA_PLATFORM=offscreen build/zene render data/projects/shorties/sv-DnB-Startup.mmpz -o … -f wav`,
twice, against the fixed binary, `render-after-sha256.txt` / `render-after-chunks.log`:

```
943e323864cb5c07bb8da264736978be9294513b93c1b6b81421cb6d8a890526   (2177120 bytes)  x2
  chunk fmt   payload sha256=3b9b8f3ae2f92cd966db8fa02134f6f6b7ac1db3a8a7734c053c17e45d14d12b
  chunk LIST  payload sha256=9fdb1a38dd4d3f13b577d93fb83e54e74413a50c8ba3b0dc3e4e17398767940f
  chunk data  payload sha256=b37cefc5a97e2d4664bbb0087a187935cdb9421030492f3b21972c3a7e3e59ca
```

Identical to `tests/integration-logs-3f/final/render-sha256.txt`: same file hash, same `data`
payload, same header. A behaviour-preserving change, measured on the audio path and not argued.

**Gate 6** (no undeclared divergence in inherited code) passes on the committed tree with the four new
ledger entries (`gate6-after.log`: `PASS … 439 changed path(s) declared`).

**The scope manifests** (`regen.py --head`, `manifest-verify-head.log`): `fork-sources.txt` and
`all-sources.txt` **REPRODUCE** (+0/−0) — the new evidence files under
`tests/integration-logs-3f-undo/` are not admitted by either pathspec, so no manifest needed an entry.
`tools-sources.txt` **DOES NOT REPRODUCE: +0/−21**, and it is a pre-existing finding, not this lane's:

* the 21 missing lines are exactly the hand-written `# SPLIT 2026-09-12 (tools-scope bar)` comment
  block that `8131b1ac9` (the tools-scope lane's bridge-split commit, which *predates this lane's
  base*) added to the body of that manifest; the documented regeneration command emits the header and
  the entries, not a hand-written narrative, so the block can never be reproduced;
* the verifier reports **+0 additions**, i.e. no entry is missing — nothing my change added is
  unregistered;
* neither of my commits touches any manifest (`git show --name-only`), and nothing has changed
  `regen.py` or `tools-sources.txt` since `8131b1ac9`, so the mismatch is deterministic and was
  already there at my base.

I deliberately did **not** run `regen.py --write` to silence it: that would delete another lane's
21-line rationale from a manifest I have no business rewriting (an unrelated change in this lane's
commits). It is reported here as a second finding for whoever owns that lane.

## 8. What I could not establish

* **The GUI's Ctrl+Z was not clicked.** See §4 — the equivalence is by code identity of the entry
  point and the position of the fault below it, not by a GUI run. I found no non-socket, non-GUI
  trigger for `ProjectJournal::undo()` in this tree to use instead.
* **Why the journal step was a whole-Song checkpoint.** The checkpoint that crashes was a Song
  checkpoint (the `transport.set_tempo` transaction reports mechanism "ProjectJournal (Song
  checkpoint)"), so undoing a tempo change rebuilds the entire track container. That is the shape
  that makes the ghost reachable, and it is inherited behaviour (`AutomatableModel`/`JournallingObject`
  checkpointing), not something this lane changed or fully mapped. It is worth a separate look: a
  checkpoint that restores less would be cheaper *and* avoid the GUI teardown entirely.
* **Whether any other control command restores a Song checkpoint.** I fixed the mechanism, not the
  set of commands that reach it; the guard in `updateComboBox` bounds the blast radius of any
  remaining registry/container disagreement, but I did not enumerate which commands push Song
  checkpoints.

## 9. Process, hygiene, artefacts

* **Only this worktree.** `git worktree list` was not disturbed; `zene-pa-integration` was never
  touched. Nothing pushed, no tag, `origin` (LMMS/lmms) and `messmerd` unwritten, no rebase, no force,
  no `git add -A`.
* **Dirty C++ I did not write.** At the start of this lane the worktree showed
  `M src/core/RoutingGraph.cpp` with `id <= m_nodes.size()` — a live **mutant** of the tools-scope
  lane's mutation gate (`build/mutation-gate/`, 88.5% kill score, summary at 12:21). By the time I
  built (12:23) the gate had reverted it and `git status` was clean. I neither built on it nor
  reverted it myself, and no commit of mine contains it.
* **My own edits.** The temporary diagnostics (`ZENEDBG` `fprintf`s and a debug dump in
  `PatternTrack.{h,cpp}`, `PatternStore.cpp`, `TrackContainer.cpp`, `Track.cpp`, `Song.cpp`) were
  reverted with `git checkout --` before the fix was written; the fix commit contains the four files
  above and the ledger only. The instrumented runs survive as evidence logs, and the instrumented
  build is not the build that was tested.
* **Artefacts** (all under `tests/integration-logs-3f-undo/`): `probe-undo.py` (the raw probe, copied
  from the bridge lane's own), `capture-undo.py` (keeps the instance's `app.log`), `verify-undo-replies.py`
  (the behavioural assertions), `gdb-wrapper.sh`; before/after probe reports, the gdb stack, the
  instrumented server logs, `pytest-after.log`, `pytest-test11-after.log`, `ctest-after.log`,
  `render-after-*`, and each build's log with its unpiped exit code.
