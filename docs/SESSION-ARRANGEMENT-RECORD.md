# Arrangement Record and Follow Actions — the Session View's engine halves

*Board task #641; the engine halves of #596. Zene Studio 0.3.0-alpha. Referenced by
`include/SessionFollow.h`, `include/SessionArrangementRecorder.h`,
`src/core/ControlCommandsSessionFollow.cpp` and
`src/core/ControlCommandsSessionRecord*.cpp`.*

This document records the design decisions and the honest bounds of the two features the
0.3.0 release owed on top of the Session View data layer (#594) and its launch scheduler
(#595). It is written to be read beside the code, not instead of it.

## 1. What was missing

`FollowAction` has been part of the session model since #594 and is persisted per slot —
the `followactions` element of a `<clip>` inside the `<session>` project block
(`src/core/SessionClip.cpp`). **Nothing evaluated it.** A chain round-tripped through save
and reload, and no launch ever consulted it: `grep -rn 'FollowAction' src` returned the
model, the serialiser and the state emitters, and no engine.

There was also no Arrangement Record path at all. SPEC §4.1 asks for "session performance
(launches, moves) recorded into the Arrangement as clips/automation" and a
"Back-to-Arrangement switch"; before this work, `grep -rniI 'BackToArrangement\|arrangement
record' src include` returned nothing outside the design documents.

## 2. Follow Actions

### Where the decision runs, and why

SPEC A3 asks for evaluation "by a Scheduler on the UI-side engine clock … delivered to the
audio path via the existing lock-free command-queue pattern". This engine takes the other
half of that pattern, and says so plainly:

* the **chain** travels from the model thread through the **existing** SPSC command queue
  (`LaunchCommandType::Follow`, a POD payload — no second queue, no allocation on either
  side), and
* the **evaluation** happens where the launch state already lives: on the audio thread,
  against the same `SessionClockContext` the launches use.

The reason is that a Follow Action's whole content is *this slot's own playback state, one
clock step later*. The state it reads and writes is the audio thread's; moving the
evaluation to the model thread would require publishing that state and arguing about a
second thread's ordering for a decision that is quantise-and-fire, not sample-critical.

### The decision is a pure function

`include/SessionFollow.h` holds the decision as an inline function over a fixed-size POD
plan (`FollowPlan`, `MaxFollowChainEntries = 8`) and an evaluation window (`FollowEval`).
No globals, no threads, no audio state, no allocation — so `SessionFollowTest` drives every
one of the ten action types exhaustively, and the one place that calls it adds nothing but
the clock and an RNG.

The rules, each pinned by a test:

| Action | Where it goes |
| --- | --- |
| `none` | nowhere: the action time passes and nothing happens (a measurement, not a retry) |
| `stop` | playback of the cell ends at the action time |
| `play_again` | the same cell starts over at the action time |
| `previous` / `next` | the column's scene row, **wrapping** |
| `first` / `last` | the ends of the scene list |
| `any` | uniform over the whole list |
| `other` | uniform over the list **minus** the current scene (no answer on a one-scene grid) |
| `jump` | the entry's own `jumpTo`; **out of grid is refused, never clamped** |

**Timing.** The chain's timing is its **first entry's**, and that is deliberate: the fire
time has to be known *before* an entry is selected (the selection is what the fire time
schedules), so a chain whose entries disagree on timing uses the first entry's — the only
rule under which "Linked" means one thing. Linked means the clip's own length, falling back
to **one bar** when the cell has no model length (never to 0, which would fire on every
audio period). Unlinked means the first entry's `timeBars`.

**Chance A/B weighting.** The weights are the entries' own `chance` fields, normalised over
the chain — so a single entry at 1.0 is the plain "always" action, and two entries at
0.25/0.75 behave as Live's A/B pair. A chain whose weights sum to nothing selects the
**first** entry: an action chain that can never fire is indistinguishable from a broken one.

**A refused action fires once per action time.** The scheduler advances the slot's next
action tick by exactly one step per evaluation, so an action that does nothing (or is
refused) consumes its action time instead of being re-decided every period. That is what
keeps a chain that can do nothing from spinning on the audio path.

### What is observable from the model thread

The installed plans are audio-thread storage, so a model-thread reader may not walk them.
Four relaxed atomics are published instead: the **count** of armed cells, the same set as a
**bitmask** (bit `track * 8 + scene`, for `track < 8` and `scene < 8` — the count covers the
wider grid), the number of **fires**, and the **newest fire packed into one 64-bit word**
(outcome, the chain entry that produced it, the scene it addressed, and the action time it
was scheduled for). One word, so a reader can never pair a new outcome with the previous
tick — the same argument `publishStart()` makes for the start line.

## 3. Arrangement Record

### The ring

`include/SessionArrangementRecorder.h` is a second SPSC ring in the session engine, and it
points the other way: the **audio** thread is the producer (one relaxed load and one release
store — no allocation, no lock, no syscall) and the **model** thread is the consumer. It is
deliberately *not* the launch command queue with the roles swapped: that queue is
single-producer by construction, and a second producer on it would break the one invariant
`commandsCrossThreadsWithoutLoss` exists to prove.

64 events between two command calls. A launch is two events, so a full 32-cell performance
fits; beyond that the ring **drops and counts** rather than growing — the same bounded
failure the launch queue follows.

### What is recorded

The launch engine already detects, on the audio thread, the two transitions that are a
performance: a slot **started** (`Started` / `Retriggered`) and a slot **stopped**
(`Stopped`). Those are the events the ring carries, at the tick the transition actually
fired on — not the period that noticed it.

* "Launches, moves": a **launch** is a start/stop pair; a **move** is what a `SwitchScene`
  Follow Action does (it stops the cell that was playing and starts the next), so it records
  as its own pair and lands as its own clip. A **restart** (`play_again`) records nothing:
  it is the same playback continuing, so its arrangement clip is the one span from the
  launch to the stop that ends it.
* **A reset records the stop of every slot it ends** (`SessionScheduler::consumeResetRequest`).
  Without that, `session.stop_all` / `session.back_to_arrangement` would leave starts whose
  stop never came — and the pass that lands them would have to lose them or invent their ends.
* The ring is **not** cleared by a reset. The performance that just ended is the thing the
  feature exists to keep, so a reset leaves it for the land pass (and the disarm path).

### The pass

`session.arrangement_record_land` pairs every recorded launch with the stop that ended it
and creates **one arrangement clip per pair** on that column's song track, at the recorded
ticks, with the length the performance had.

* It **refuses, consuming nothing**, while any recorded start is still open. A half-landed
  performance would either lose the starts it could not pair or invent their ends; the
  refusal names the cells that are still playing, which is what the client has to stop.
* The intended protocol, therefore: **arm, perform, stop the session
  (`session.stop_all` or `session.back_to_arrangement`), land.**
* The inverse is **one Track journal checkpoint per touched track**, taken before that
  track's first clip is created — the `clip.add` and `midi.retro_capture_to_clip` mechanism —
  so ONE `control.undo` takes the whole pass back. The recorded transaction's
  `before`/`inverse` describe the **first** clip of the pass (a complete, exact inverse for
  that clip: `clip.delete`); the mechanism names the checkpoints that carry the rest,
  because a transaction carries one inverse and a pass creates one clip per completed launch.

### Back to Arrangement

`session.back_to_arrangement` is one atomic reset request on the scheduler plus the
recorder's disarm. It is the same engine operation `session.stop_all` makes, and it exists
as its own id because it is the **arrangement's** verb: the reply reports the recorded
performance, which it does **not** drop. Nothing in `src/gui/` draws it.

## 4. Bounds, stated rather than implied

1. **A landed clip carries its position and length, not the session clip's notes.** The
   reply's `pattern` field reports the PatternStore reference the session slot names; the
   notes are not copied into the clip.
2. **The "rendered audio matches the session playback" half of #596's acceptance is UNMET
   in this tree.** A launched session slot does not render audio at all — there is no
   session-clip playback path (#597) — so there is no session audio for a render comparison
   to be made against. The position-and-length proof is what this release can hold.
3. The plan table is a fixed `MaxFollowPlans` (16 cells); a cell armed beyond it is refused
   and counted. A chain longer than `MaxFollowChainEntries` (8) is refused, never truncated.
4. A `jump` whose target is outside the grid is refused at **set** time (the command's typed
   `InvalidArgs`) and, if the grid shrinks afterwards, fires **once** as `Refused` at fire
   time — visible in `session.follow_get_state`'s `last_fire`, never silent.
5. The grid UI (#598) is **out of 0.3.0**. Everything here is reachable through
   `--control-socket` and through nothing else.
