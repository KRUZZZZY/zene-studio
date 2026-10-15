# Cluster new-p9-509 — overlapping same-pitch notes share a VST3 note ID

**Verdict: FALSE-POSITIVE** (on the path LMMS can reach). No product code changed.

Branch `bugs/qwen-new-p9-509`, base `ded355440`.

## The finding

Luna's patrol: "Two overlapping same-pitch notes on one channel share a VST3 note ID, so an expression
update or note-off can hit the wrong voice." Its cited line (`Vst3Instrument.cpp:297`) is now
`reprepare()`, so the line number is stale. The code that actually derives the ID is
`Vst3MidiEvent.cpp:69-71`.

## Evidence

| Claim | Where | What the code does |
| --- | --- | --- |
| The ID is channel and key only | `plugins/Vst3Effect/Vst3MidiEvent.cpp:69-71` | `noteIdOf` returns `channel * 128 + key` when `withNoteId`, else `-1`. No per-note state. |
| The ID is set on note-on, note-off and poly pressure | `plugins/Vst3Effect/Vst3MidiEvent.cpp:96,108,116,125` | All call `noteIdOf`. Expression events use the same formula (`:131`). |
| The plug-in side only sets the flag | `plugins/Vst3Instrument/Vst3Instrument.cpp:325`, `:290` | `withNoteId` comes from `hasNoteExpressions()`. Pitch-only plug-ins keep `-1`. |
| **Overlap cannot reach the host through the track.** A retrigger of a sounding key first sends a note-off for the old note, then the new note-on | `src/tracks/InstrumentTrackNotes.cpp:248-251` | `if (m_runningMidiNotes[key] > 0) handleMidiEvent(MidiNoteOff, previousChannel, key, 0)` runs before the new note-on. |
| Overlapping same-key notes are counted, not sounded twice | `src/tracks/InstrumentTrackNotes.cpp:262-275` | `sendNoteOff` decrements the per-key count and sends a note-off only when it reaches 0. So the host sees ON(A), OFF(A, cut), ON(B), OFF(B). |
| Ordering between cut and retrigger is preserved | `plugins/Vst3Effect/Vst3MidiEvent.cpp:161` | The drain is an insertion sort by `frameOffset` with a strict `>`, so it is stable. Equal-offset OFF-then-ON stays in FIFO order. |
| Track path reaches the host for both clip notes and live input | `src/tracks/InstrumentTrack.cpp:623-628` | `processOutEvent` calls `sendNoteOn` / `sendNoteOff`. |
| MX-120 voice model agrees: one voice per (track, channel, key); a retrigger restarts it | `tests/src/core/InstrumentRoutingTest.cpp:283-285` | `voicePoolIsFixed` asserts `activeCount()==1` after two same-key note-ons. |

Result: on the track path, a (channel, key) pair has at most one sounding note in the plug-in at any
time. The shared ID is then unambiguous. An expression or note-off for that ID can only refer to the
sounding note, and the earlier note was already ended by the cut.

## Residual risk (not a bug today)

- `Vst3MidiEvent.cpp:69-71` does not enforce uniqueness. The host relies on the track-layer invariant.
  Any caller that bypasses `InstrumentTrack` and sends two note-ons on the same channel and key with
  no note-off between them would get colliding IDs. Nothing in tree does that. This is design
  reliance, not a defect.
- A fix that gives each note its own ID would need a per-key FIFO of IDs, held in a fixed table and
  shared with the CLAP host (`plugins/ClapEffect/ClapHostNotes.cpp:210,255`, which uses the same
  channel/key scheme). It would also need a rule for which sounding note a note expression targets.
  The current MX-120 design settles neither question. This is why the finding is marked
  `needs_context`.
- Doc drift (not changed here): `plugins/Vst3Effect/Vst3MidiEvent.h:58-62` says both note events
  carry `-1` because the host does not track note identities. That is true only when `withNoteId` is
  false. The MX-120 path sets channel*128+key. Fixing the comment belongs to a docs pass.

## What was run

No code was built or changed, so no RED/GREEN run applies. A runtime probe was not run. The verdict
rests on the static evidence above. Commands (all from the worktree root; logs in
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-509.r1/logs`):

| Command | EXIT |
| --- | --- |
| `git status --short` (worktree clean on entry) | 0 |
| `bash tests/complexity-gate.sh --check > gate-complexity-1.log 2>&1` | 1 |
| `bash tests/file-length-gate.sh --check > gate-filelength-1.log 2>&1` | 1 |

## Gates (pre-existing failures on `ded355440`, not from this cluster)

Both gates were run on the unchanged tree, so these failures are not from this report. Neither gate
was re-anchored.

- Complexity ratchet, 6 regressions, for example `XyPadPairTest.cpp dotCentreAlong` (CCN 16),
  `DawProjectReadTracks.cpp parseNotes` (CCN 13), `ControlCommandsMixerTools.cpp adjust` (CCN 12).
  None are in the VST3 or MIDI files for this finding.
- File-length ratchet, 13 new files over 500 lines, for example `src/core/TakeLane.cpp` (520),
  `tests/src/core/DeviceDspTest.cpp` (752), `tests/src/gui/ArrangerTimelineTest.cpp` (689).

These belong to other clusters or lanes. They need their own owner decision: either split the files
or ratchet the baseline. I did not do either.

## Commit

Only this report is committed, as `docs/qwen-fixes-2026-10/cluster-new-p9-509.md`. Message:
`docs(qwen-fixes): cluster new-p9-509 FALSE-POSITIVE (VST3 note IDs; track cuts retriggered notes)`.
