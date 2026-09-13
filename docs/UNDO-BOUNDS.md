# Bounded, coalescing undo — the two decisions, and where they live

**Status:** implemented in `030/w11-undo-depth`, branched from the integration tip `8effd96ae`.
This is the DECISION RECORD for the two calls the change makes, written so the next reader does not
have to infer them from the code: **how deep the undo stack is allowed to be, and when two commands
are one undo step.** The obligation is SPEC A16 / task #623 ("undo depth and drag coalescing", owner's
item 4); the contract table's columns are in `docs/A16-REVERSIBILITY.md`.

Read this with `include/ProjectJournal.h` (the mechanism, and the same two decisions in short form) and
`src/core/ControlReversibilityTable.cpp` (the per-command data).

---

## 0. What was actually wrong (measured, not assumed)

The record said "undo depth is unbounded-or-arbitrary and drags are not coalesced". Re-measured on the
tip this lane was based on:

| claim | measured |
|---|---|
| **the depth cap exists but is arbitrary and unobservable** | `ProjectJournal::MAX_UNDO_STATES = 100`, a `static const int` with a `TODO: make this configurable in settings`, enforced by `trimUndoStack()`. It counted steps and nothing else: **no byte bound**, and no command reported it (`grep` for a depth accessor on the control surface: none). |
| **drags were not coalesced on the command path** | `ControlRegistry::invoke()` takes a checkpoint-per-command mark and merges *within* one command, so a 200-call `clip.move` run left **200 steps** — one per call. There was no rule that two consecutive same-target calls are one gesture. |
| **the GUI was already fine, and that is why this is a command-surface rule** | A canvas drag takes ONE checkpoint at mouse-press and then turns journalling off for the rest of the gesture: `ClipView::mousePressEvent` (`m_clip->addJournalCheckPoint()` then `setJournalling(false)` for Move/Resize), `Fader::mousePressEvent` and `AutomatableSlider::mousePressEvent` (`addJournalCheckPoint()` then `saveJournallingState(false)`). So a human's drag was one Ctrl+Z before this change and is one Ctrl+Z after it. **The defect was the agent's drag**, which had no grouping at all. |
| **structural operations** | were already journalled by the A16 lane (`track.add`/`track.remove` are `true_inverse` action checkpoints, `docs/A16-REVERSIBILITY.md` §1.1). Owner's-31 item 4's "deleting a track destroys its state" is fixed there, not here. |

---

## Decision 1 — the depth is bounded TWO ways, and every eviction is reported

| | value | where |
|---|---|---|
| count cap (default) | **100** steps — `ProjectJournal::MAX_UNDO_STATES`, the historical number, kept | `include/ProjectJournal.h` |
| count cap (ceiling) | **10000** — `MaxUndoStateLimit`; a request above it is REFUSED, not clamped | `include/ProjectJournal.h` |
| byte budget (default) | **16 MiB** — `DefaultMaxUndoBytes`, the serialised size of the retained steps | `include/ProjectJournal.h` |
| byte budget (ceiling) | **512 MiB** — `MaxUndoByteLimit` | `include/ProjectJournal.h` |
| eviction | **FIFO** on both caps; the **newest step is never dropped** (`size() > 1` in the byte loop) | `src/core/ProjectJournalBounds.cpp` |
| accounting | **exact**: every step measures its own serialised size at capture (`DataFile::toByteArray().size()`), so the budget means one thing for every command | `ProjectJournal::serialisedBytes` |
| reporting | `control.undo_depth` returns `depth`, `redo_depth`, `cap_steps`, `cap_bytes`, `retained_bytes`, `evicted`, `bounded`, `coalesced_steps` | `src/core/ControlCommandsUndo.cpp` |

**Why two caps and not one.** A count cap alone is not a memory bound: a `transport.set_tempo`
checkpoint carries the whole Song, so 100 of them is a different order of memory in a 20-track project
than in a 1-track one, and nothing in the tree bounded that. A byte cap alone is not a history at all
(one large checkpoint could evict everything). Both together are what "bounded" has to mean, and the
result is that the stack's memory is bounded by `min(100 steps, 16 MiB)` by default.

**Why the newest step is exempt from the byte cap.** An undo stack that has silently dropped the edit
the user just made is worse than one that is momentarily over budget; the overage is visible in
`retained_bytes > cap_bytes` rather than hidden. Same shape as the transaction record's cap (SPEC A16
deliverable 2: "the newest record is never the one dropped").

**Why the caps are settable and the ceilings are refusals.** `control.set_undo_depth {steps?, bytes?}`
sets either cap or both. A client that needs a long history asks for more; a client that asks for a
billion is refused, typed, with the ceiling named (`capRefused()`), because a bound that a caller can
silently remove is not a bound. Lowering a cap **evicts immediately** and the result says how many
steps that cost (`dropped`) — see the honesty note below.

**The record names the step it describes, by serial.** Every record carries the journal **serial**
(`step`, on the wire) of the step it was written for - and, since 0.3.0, the number of commands it
covers (`commands`). The serial is what makes the refusal below possible, and also what makes the
registry's "did this command produce a step?" test reliable: a bound can evict steps *while a command
runs*, so the stack DEPTH can be exactly what it was before the call even though a step was pushed.
The first version of this lane's code compared depths, and the eviction test caught it (`step: 0` on a
record for a command whose step was on the stack: `UndoBoundsTest::anEvictedStepIsNeverSilentlyUndone`).

**The record/step correspondence, which the byte cap made necessary.** The transaction record list
(`control.transactions`) and the journal stack are separate, and before this change they evicted at the
same *count* (100), which is why `docs/A16-REVERSIBILITY.md` §3 could say "an agent never sees a record
for a step it can no longer undo". A **byte** cap breaks that: the journal can evict steps the record
list still holds. Every record therefore carries the **serial** of the journal step it describes
(`Transaction::step`), and `control.undo` refuses, typed, when

```
record.step != 0 && record.step < journal->oldestStepSerial()
```

— the step has been evicted, so unwinding now would take back a *later* edit than the one asked about,
which is exactly the pretending SPEC A16 exists to remove. The refusal names the caps and points at
`control.undo_depth`'s `evicted`/`bounded` fields. Proof: `UndoBoundsTest::anEvictedStepIsNeverSilentlyUndone`.

**Also fixed here, found while doing this:** `trimUndoStack()` was called only from the three push
paths, so a cap lowered while steps sat on the *redo* stack could be exceeded when they came back
(`redo()` pushed without trimming). `redo()` now trims, and the byte accounting is maintained across
push, pop, merge and eviction.

---

## Decision 2 — a run of the same command on the same target is ONE undo step

**The rule, in one sentence:** *two consecutive calls of the same command on the same target, with no
other undo step pushed in between and less than the coalescing window apart, are ONE undo step;
anything else opens a new one.*

The rule is enforced by `control::UndoCoalescer` (`src/core/ControlUndoCoalescing.cpp`) and the test
for each condition is:

| condition | test | breaks the run when |
|---|---|---|
| same command + same target | `key == m_key`, where the key is `<command>:<target arg values>` | another command, or the same command on another clip/channel/parameter |
| directly above | `mark == m_topIndex + 1` (the journal depth the call opened at is one past the previous run's step) | any other step was pushed in between — a GUI edit, another command, a `control.undo` |
| inside the window | `sinceLastMs() <= window_ms` | a pause longer than the window (default **400 ms**) |
| the command declares it | the table's `RC()` rows | a command with no coalescing declaration never groups |

**Which commands coalesce** — DECLARED as data in the contract table (`RC(id, cls, rev, reason,
mechanism, fallback, target)`), one row per drag-shaped command, and read from there by both the
registry and `control.undo_depth`:

| command | target arguments | the gesture it is |
|---|---|---|
| `clip.move` | `clip` | dragging a clip along the timeline |
| `clip.resize` | `clip` | dragging a clip's edge |
| `mixer.set_volume` | `channel` | dragging a fader |
| `plugin.param_set` | `target,plugin,name,index` | dragging a device knob |
| `rack.macro_set` | `channel,macro` | dragging a macro |

Every one of them is `true_inverse` (a live `ProjectJournal` checkpoint), which is a requirement and not
a coincidence: a step can only be merged into another step if both restore live state. The anti-drift
test can assert exactly that (`entry.coalesces() ⇒ cls == true_inverse`).

**Deliberately NOT declared, with the reason** (so the next reader does not read the absence as an
oversight):

* `note.move` / `note.resize` — the note the call names is **re-derived by the move itself**
  (`note.move` re-sorts the clip's note list and reports the note's new id), so two consecutive calls
  of "the same note" cannot be recognised from their declared arguments. Grouping them would risk
  merging a drag of note A with a drag of note B.
* `warp.move` — a warp marker's key **is** the value being edited (`source_frame`), so the target
  changes on every call by construction.
* Everything that is not a gesture (`track.rename`, `clip.split`, `track.add`, …): one call, one step,
  and a second call is a second edit by definition.

**The window (400 ms, `control::UndoCoalesceWindowMs`).** Longer than a drag's frame interval at any
plausible call rate, shorter than a human's "I have stopped and started again" pause. It is a DECLARED
constant rather than a per-client default so undo granularity is reproducible between agents; it is
settable with `control.set_undo_coalescing {window_ms}` (0 = grouping off), and **changing the window
ends the gesture in flight**, so a client cannot retroactively group two gestures into one step.

**Why 0 is legal and why that matters.** `window_ms: 0` reproduces the pre-0.3.0 behaviour exactly —
one step per call — which makes the rule's effect measurable in the same process rather than argued:
`UndoBoundsTest::coalescingOffReproducesTheOldBehaviour` asserts 20 steps with the window off and
1 step with it on, for the same 20 calls. The value is also the honest escape hatch for a client that
needs per-call granularity.

**Semantics of a merged step are unchanged.** The journal's merge keeps the **earliest capture** of
each object, which is the state before the gesture: one `control.undo` (or one Ctrl+Z — the same stack,
the same call) returns the clip to where the drag started, not to the second-to-last frame of it. The
primitive is `ProjectJournal::coalesceTopStepIntoPrevious()`, and the existing merge rule
(`mergeCheckpointsFrom`) is unchanged.

**The record side: one record per undo step, not per call.** A coalesced run extends the record it
started (`ControlRegistry::extendTopTransaction()`): `before` and `inverse` stay what they were — the
state before the gesture, which still reverts the whole of it — and the count grows in
`control.transactions`' new per-record `commands` field. So a 200-call drag is ONE journal step and ONE
record with `commands: 200`, and the record list still evicts at the same depth as the stack.

---

## Where a reader finds each decision

| decision | written down in |
|---|---|
| the two caps, their values and the eviction rule | `include/ProjectJournal.h` (class comment), this file §Decision 1 |
| why two caps, why the newest step survives, the ceilings | this file §Decision 1 |
| the coalescing rule and its four conditions | `include/ControlUndoCoalescing.h`, this file §Decision 2 |
| which commands coalesce, on which arguments | `src/core/ControlReversibilityTable.cpp` (the `RC()` rows — the same file that classifies every command) |
| the window's value and its meaning | `include/ControlReversibility.h` (`UndoCoalesceWindowMs`) |
| the commands that expose and control all of it | `src/core/ControlCommandsUndo.cpp`, `docs/A16-REVERSIBILITY.md` §1 |
| the proof | `tests/src/core/UndoBoundsTest.cpp` |

## Defects found while doing this (both measured, both fixed here)

1. **The integration tip did not compile.** `src/core/ControlReversibilityTable.cpp` carried a stray
   `=======` merge marker (line 606), so the file that holds the whole A16 contract was a syntax error
   at `8effd96ae` (`g++ -fsyntax-only` → `error: version control conflict marker in file`). The same
   mangled merge had duplicated 36 `not_mutating` rows into that file and left it 120 lines over the
   file-length ratchet (Gate 7 red at the base). Fixed by deleting the duplicate block, moving the 7 rows
   that existed only there into the passive block and the 6 `snapshot` rows into a new third block — with
   the assembled table verified identical row-for-row before and after.
2. **The coalescing window's default was uninitialised.** `UndoCoalescer::m_windowMs` lost its in-class
   initialiser while this lane was being written, so the window read as indeterminate (`control.undo_depth`
   reported `window_ms: -1, enabled: false` in one process and behaved as "coalesce nothing" in another).
   The socket flow caught it, as a typed refusal: its own restore call sent `window_ms: -1` back and the
   schema refused it. Fixed (`int m_windowMs = UndoCoalesceWindowMs;`), and the flow now clamps the value
   it restores so an impossible window can never be echoed back. Worth recording because the two unit
   tests that pass with coalescing ON did not catch it: they set the window explicitly, and a garbage
   value that happened to be positive looked like the default.

## Honest limits (what this does NOT do)

1. **There is no undo-history UI.** The depth, the caps and the window are drivable through
   `--control-socket` and the MCP bridge, not from the interface; `docs/KNOWN-LIMITATIONS.md` and the
   0.3.0 release notes carry the one-line statement.
2. **The history is not persisted.** Close the app and the undo history is gone, as it was before.
3. **The caps are session state, not project state.** A reopened project starts at the default caps.
4. **The rule applies to the command path.** The GUI's own gestures already group themselves (a
   checkpoint at press, journalling off during the drag — §0), and nothing about that changed; a GUI
   drag was one step before this change and is one step after it.
5. **Two gestures of the same target closer together than the window are one step.** That is the rule
   working, not a defect: a client that needs a boundary sets the window down (or to 0), or issues any
   other command between the two.
6. **`control.set_undo_depth` cannot restore what it evicted.** The inverse restores the CAP; the steps
   a lower cap dropped are gone, and the transaction's mechanism says so rather than implying
   otherwise.
7. **The byte budget measures the checkpoint XML, not the transaction closures.** An action step
   (a deleted track's inverse) serialises no state of its own and counts as 0 bytes against the budget;
   its closure is one recorded operation, not a growing buffer.
