# Session-clip playback path — Slice 0 (card #597)

**Branch:** `040/feat-597` (base `0.4.0/train-w1`).
**Worktree:** `zene-597`.
**Depends on:** the data layer (#594), the launch scheduler (#595) and the Arrangement Record /
Follow halves (#596/#641) — all merged. This slice closes the one gap they left open.

---

## 1. The gap, read off the tree

`docs/KNOWN-LIMITATIONS.md` states it twice, in the product's own words:

> a launched session slot **does not render audio**, because this tree has no session-clip playback
> path (`src/core/SessionClip.cpp` is serialisation only)

Verified rather than quoted, on the base commit `042abe489`:

| Claim | Where it is true |
| --- | --- |
| The launch engine works | `include/SessionScheduler.h` — the pure decision (`launchTickAt`, `applyLaunchCommand`, `advanceLaunchState`) plus the engine, driven from `src/core/Song.cpp:288` |
| A launched slot takes its track over | `src/core/Song.cpp:467` — `if (m_sessionScheduler.trackIsSessionActive(trackIndex)) { continue; }` |
| …and renders nothing in its place | there is no branch between that `continue` and the next track: the track is *silenced*, not *replaced* |
| The slot's content is never published to the audio thread | `SessionScheduler`'s command payload carries `(track, scene, type, mode, quantisation, plan)`; no `patternId` crosses over |
| `SessionClip.cpp` is serialisation only | 210 lines: `setPatternReference`, `setAudioReference`, `clear`, `saveState`, `restoreState` — no `play`, no `process` |

So the engine knows *that* a cell is playing and *since when*, and nothing knows *what it holds*.
The take-over and the content are two different halves of A1, and only the first is built.

**The claim this slice makes:** a launched MIDI session slot renders audio — the pattern it
references is played, looped, at the slot's own position, from the launch grid line.

## 2. The content problem, and the shape chosen

The audio thread may not read `SessionModel` (model thread, `QVector` storage, rule 4 — realtime).
The scheduler's own file comment states the contract: *"The launch state belongs to the audio
thread. Nothing else ever reads or writes it. The model/GUI thread crosses over with
`requestLaunch()` … which do nothing but an atomic push … no allocation, no lock, no syscall."*

So the content crosses over the same way the Follow Actions plans already do
(`installFollowPlan` → `m_followPlans`, read by `evaluateFollow`): a **fixed table written through
the command queue**. Three additions, all following an in-tree precedent:

1. **`publishSlotContent(track, scene, patternId, loopLengthTicks)`** — model thread, one queue
   push, no allocation (the `installFollowPlan` shape, `SessionScheduler.h:439`). False when the
   fixed table is full: dropped and counted, never grown.
2. **`playbackForColumn(track)`** — audio thread. Returns whether a slot on that column is
   rendering, the pattern to render, and the position to render it at:
   `positionTicks = m_positionTicks - state.startedTick`, wrapped by the clip's loop length.
   This is the whole of the new audio-thread read surface: a bounded loop over the existing
   `m_active` storage.
3. **The `Song::processAudio` hook** — at the existing take-over site, render the slot's pattern
   for this period instead of falling through to `continue`:

   ```cpp
   Engine::patternStore()->play( TimePos( view.positionTicks ), framesToPlay,
                                 frameOffsetInPeriod, view.patternId );
   ```

   `PatternStore::play` (`src/core/PatternStore.cpp:47`) already wraps the position modulo the
   pattern length and already routes each PatternTrack's pass — it is the same call pattern mode
   makes (`src/core/Song.cpp:340`). No new playback machinery is invented.

## 3. The semantic decision this slice had to make, stated plainly

**A MIDI session slot references a `PatternStore` id — `session.set_slot`'s own refusal names it:
"type 'midi' needs 'pattern': the PatternStore id the slot references"
(`src/core/ControlCommandsSession.cpp:144`).** A `ClipSlot` carries a `patternId` and nothing else:
no notes of its own (`include/SessionModel.h:118-192`). A pattern's notes live on the
**pattern-store** tracks, each carrying its own instrument; the song's tracks are a different set.

Therefore the only content a launched MIDI slot can render is **its pattern, through the pattern
store's tracks**. That is what this slice implements. The two consequences, so they are not
mistaken for bugs later:

- **The column's take-over and the pattern's instruments are different track sets.** The grid
  column decides *which cell was launched* and suppresses that column's **song** track (A1, already
  built, asserted by `SessionSchedulerRenderTest::launchedClipChangesTheRender`). The notes come
  from the pattern the slot names. A design that routed a pattern through the launching column's
  own instrument would need a track↔pattern mapping the model does not have.
- **An empty slot still takes its track over and renders silence.** That is today's behaviour, it
  is what the existing render test asserts, and this slice does not change it. It is the control
  that keeps "the launch fired" separable from "there was content to play".

## 4. What this slice does NOT do

Named, because each is a real half of #597's eventual scope:

- **Audio slots** (`type: 'audio'`, `audioSource`) still render nothing. Slice 1: a session audio
  clip needs a playback handle on the launching track's sample path, which is a different
  mechanism from the pattern store's.
- **Legato mode** does not yet inherit the outgoing clip's position; a fresh launch starts at 0.
  The launch state machine tracks `startedTick`, so the position is correct *for a start*; carrying
  the outgoing position across a launch pair is its own piece of the state machine.
- **Free-running session playback with the transport stopped.** The render pass sits inside the
  `if (!m_playing) return;` gate, so a slot renders while the transport runs. The session *clock*
  already free-runs (SPEC A2); making its audio free-run with it is a mixer-level change.
- **Per-clip `gain` / `transpose` / `detune` / RAM mode** are persisted but not applied on this
  path; they are clip-level gains on top of the pattern, not part of "does it render at all".
- **Scene tempo / time-signature overrides** are not applied on launch.
- **The grid UI (#598)** — still out of scope, and still the reason every proof here is driven
  through `--control-socket`.

## 5. Acceptance, and how it will be proven

The unmet half of #596's acceptance is exactly *"rendered audio matches the session playback"*
(`docs/SESSION-ARRANGEMENT-RECORD.md:155`). The evidence for this slice is therefore a **render
comparison**, not a state assertion:

1. **Unit (pure, no audio device).** `SessionSchedulerTest`: content publication round-trips
   through the queue; `playbackForColumn` reports the launched cell's pattern and a position that
   advances with the session clock and wraps on the loop length; an unlaunched column reports
   nothing; the existing allocation probe still passes with a publication in flight.
2. **Render (the real export path, `ProjectRenderer`).** `SessionSchedulerRenderTest` gains:
   - a song with a pattern whose notes reach an instrument, launched → the render is **non-silent**
     and differs from the same song with nothing launched;
   - the same song with an **empty** slot launched → still the take-over take (silence), which is
     the control that separates "content played" from "the track was taken over".
3. **End-to-end (the socket, headless).** The M1 driver (`tests/control-session-m1.py`) launches
   4 clips across 2 scenes through `--control-socket`; its docstring currently states that no
   assertion is about sound. It gains the render assertion: a launched slot's export is non-silent
   where the un-launched one is silent.

Behaviour preservation (rule 5): with **nothing launched**, `playbackForColumn` reports nothing,
the new branch is not taken, and the render must stay byte-identical. That is asserted, not
assumed — it is the existing `renderWithNothingLaunchedIsByteIdenticalAcrossRuns` case.

## 6. Realtime analysis (rule 4)

- The new **model-thread** call (`publishSlotContent`) is one bounded queue push: no allocation, no
  lock, no syscall — the same shape as `requestLaunch` / `installFollowPlan`.
- The new **audio-thread** calls (`playbackForColumn`) are a bounded loop over
  `std::array<ActiveSlot, 64>` and its fixed content table: no allocation, no lock.
- `PatternStore::play` is the pass pattern mode already makes from this same thread (its
  `clipVector` growth inside `InstrumentTrack::play` is pre-existing upstream behaviour, not
  introduced here).
- The allocation-counter pattern the program's realtime tests use is extended to cover the
  publication path, so the claim is measured rather than argued.

## 7. Registration (rule 12)

New files: `include/SessionPlayback.h` and `src/core/SessionPlayback.cpp` (the feature), plus
`tests/src/core/SessionPlaybackTest.cpp` (its proof). All three are registered in
`tests/fork-sources.txt` in this lane, and the sources are in `src/core/CMakeLists.txt` /
`tests/CMakeLists.txt`.

**The ratchet shaped the implementation twice, and both times it was obeyed rather than worked
around.** `include/SessionScheduler.h` sat at *exactly* 500 lines, so `publishStart`'s body moved to
`SessionFollow.cpp` beside its only caller, and the new bodies live in `SessionPlayback.cpp`. And
`tests/src/core/SessionSchedulerTest.cpp` is grandfathered at 549 lines in the whole-tree ratchet, so
the new tests are their own file — a new test source is free, a grown grandfathered one is a
regression.

`src/core/Song.cpp` is already declared in `tests/upstream-modifications.txt`; this lane extends that
entry rather than replacing it.

## 8. Verification (2026-09-28)

**What is proved, and how.** The half that was missing is the *hand-off*, so that is what
`SessionPlaybackTest` measures — five cases, all passing (`7 passed, 0 failed`):

| Case | Claim |
| --- | --- |
| `publishedContentReachesTheRenderPath` | Content published on the model thread arrives on the audio thread on the same queue the launch does; `playbackForColumn()` then reports the pattern and a position that advances with the session clock; an unlaunched column is never reported as playing |
| `anEmptyCellStillTakesItsColumnOverAndHoldsNothing` | The control: an empty cell still takes its column over with `patternId < 0` |
| `republishingACellReplacesItsContent` | A cell's content is replaced in place, not accumulated |
| `resetDropsPublishedContentWithTheLaunchState` | A project change drops the content too — no cell can render the previous project's pattern |
| `aFullTableRefusesAndLeavesTheCellSilent` | The fixed table refuses at its bound rather than growing |

**Regression evidence.** The whole session suite is **10/10 passed** (`ctest -R Session`, from
`build/tests`, `QT_QPA_PLATFORM=offscreen`, `LMMS_PLUGIN_DIR=<build>/plugins`) — including
`SessionSchedulerRenderTest`, whose behaviour-preservation case asserts a headless render is
byte-identical with nothing launched. The build is clean (0 errors, the `zene` binary links).

**Gate evidence.** `bash tests/file-length-gate.sh --check` (the scope CI runs):
**PASS, no regressions.** In the wider `--scope all`, this lane adds **zero** regressions: 27, the
same count the untouched live line `zene-040` reports. (Those 27 are pre-existing drift in files this
lane never touches.)

**The render half is now PROVEN (2026-09-28).** `SessionPlaybackRenderTest` builds the decisive
fixture — a silent arrangement plus a pattern store track holding TripleOscillator and a one-bar note,
cell (0,0) referencing that pattern — and measures the export:

```
PASS : aLaunchedSlotWithContentRendersAudio()
PASS : aColumnTakenOverByAnEmptyCellStaysSilent()
AB_EVIDENCE session-slot-silent   pcm_bytes=302080
AB_EVIDENCE session-slot-launched pcm_bytes=302080
Totals: 4 passed, 0 failed, 0 skipped
```

The proof is a **silence comparison on the PCM payload**, not on the file: the fixture's arrangement
clip carries an all-zero buffer, so with nothing launched the export is digital silence, and it becomes
audible only because the launched cell's pattern sounded. The control pins the other direction: a
column taken over by an EMPTY cell stays silent. Whole session suite **11/11**, file-length gate
**PASS**, build clean.

### The detour that produced this, kept because it is the useful part

The first run of this fixture **segfaulted**, and the honest history matters more than the green tick,
because the crash looked exactly like a defect in this slice's design:

```
Song::processNextBuffer (Song.cpp:483)      <- this slice's call
  -> PatternStore::play -> InstrumentTrack::play
    -> NotePlayHandleManager::acquire (NotePlayHandle.cpp:736)
       s_available[s_availableIndex--]      <- SIGSEGV
```

It is **not** an entry-point problem, and the conclusion I first drew — "the store is not a supported
entry point from the Song loop; play a PatternTrack instead" — was **wrong**. The real cause:
`NotePlayHandleManager::init()` is called **only from `src/core/main.cpp:579`**, so in a test binary
`s_available` is null and any test that plays a NOTE crashes until it calls `init()` itself.
`MpePlaybackTest`, `MpeInputPathTest` and `SessionTuningTest` all call it for exactly this reason. One
line in `initTestCase` turned a segfault into a passing proof.

Calling `patternStore()->play()` from the column loop is **correct** — pattern mode reaches the same
code (a PatternTrack's `play()` delegates to the store), and the store owns the instruments a pattern's
notes belong to.

**Three environment traps, each measured, none of them the product's fault:**

1. `NotePlayHandleManager::init()` — see above. A note-playing test must call it.
2. `ControlRegistry::setReady(true)` — the surface refuses engine commands until startup is declared
   finished; without it `session.set_grid` answers `engine_starting`.
3. `MidiClip::addNote` must be called with `quant_pos = FALSE`: with the default it evaluates
   `gui::getGUI()->pianoRoll()` unguarded (`src/tracks/MidiClip.cpp:184`) and segfaults headless. The
   agent surface passes `false` too (`src/core/ControlCommandsNotes.cpp:126`), so this is a latent
   landmine for other headless callers rather than a reachable product crash. **Worth a guard upstream**
   — a null check matching `Song.cpp:1738`'s idiom — but it is not needed for this slice.

**One finding about the agent surface, from the control:** `session.launch_slot` **refuses an empty
cell** ("there is no clip to launch; session.set_slot defines one"). So the empty-cell state is not
reachable through the command surface at all; the control therefore drives the scheduler directly, as
`SessionSchedulerRenderTest` already does, and the launch path is still proved through the surface in
the proof itself (SPEC §5).

### A phantom regression, recorded so it is not chased again

Later in the same session this proof went red deterministically (3 runs of 3) with the diagnostics
showing everything correct — model cell `patternId=0`, view `playing=1 patternId=0`, pattern well
formed — and the output silent. It was **not** a regression in the slice, and **not** a build issue:
`Song.cpp` had a bare `continue;` because a temporary bisect somewhere else had reverted that file and
the later "restore my files" step copied back a **HEAD** copy rather than the modified one. The
diagnostics could not see it, because `playbackForColumn()` is a scheduler query that answers correctly
whether or not `Song::processAudio` calls it.

Two things follow. **The hook's presence is the first thing to check** when this proof goes silent
(`grep -n "patternStore()->play" src/core/Song.cpp` — one line, and it would have saved the detour).
And a restore-from-backup must be verified, not trusted: the same slip briefly had this lane failing a
render proof, the golden programme, and a scan-cache test at the same time, all for different
non-reasons.
