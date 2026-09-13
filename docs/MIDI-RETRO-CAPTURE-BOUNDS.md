# Retrospective MIDI capture — the bound, stated rather than implied

**Status:** implemented in `030/retro-capture`, branched from the 0.3.0 integration tip. This is the
DECISION RECORD for the one call the feature makes that a user can be disappointed by: **how far back it
can reach.** The obligation is owner's-31 item 14 ("retrospective MIDI capture"); `docs/MIDI-RETRO-CAPTURE.md`
is the design and the engine inventory, `docs/KNOWN-LIMITATIONS.md` carries the one-line user-facing
statement, and the registered ctest `ControlRetroCapture` (`tests/control-retro-capture.py`) measures
every figure on this page.

Read this with `include/RetroMidiRing.h` (the ring and its policy), `include/RetroMidiCapture.h` (the
capacity and the arm switch) and `src/core/ControlCommandsMidi.cpp` (the three commands that report it).

This page follows `docs/UNDO-BOUNDS.md`, this repo's precedent for stating a limit honestly instead of
promising more.

---

## 1. The bound, in one line

> **8192 events**, the most recent ones, per open MIDI client — 8192 × 16 bytes = **128 KiB**, allocated
> once when the client is constructed. It is a **memory bound**, not a time bound and not a bar count.

That is the whole promise. `midi.retro_capture_status` reports it as `capacity_events`, and the number the
build reports is asserted equal to the figure on this page by the registered ctest, so the two cannot drift.

## 2. What 8192 events is worth in time, and why the question has no single answer

| input | events/s | the window covers |
|---|---|---|
| two-hand keyboard playing at a moderate rate | 10–20 | **≈ 7–13 minutes** |
| a fast passage with sustain-pedal CCs | 40–80 | ≈ 1.5–3.5 minutes |
| one controller turned quickly (CC at ~100/s) | ~100 | ≈ 80 seconds |
| a note-per-tick sequence written into a file and played back | ≥ 1000 | seconds |

The figure that a user reads in the release notes (**minutes, not hours**) is the FIRST row, and it is the
honest one for the case the feature exists for — a human who played something and did not hit record. It is
**not** a guarantee: a dense controller stream fills the same window in well under a minute, and nothing in
the engine can slow that down, because the ring is bounded by memory on purpose. §3 is why.

## 3. Why a memory bound and not a longer one

The ring has to be written from the MIDI input thread. The one property that thread's path must keep is
that it **allocates nothing, takes no lock and makes no syscall** (`include/RetroMidiCapture.h`: the
threading contract). A growable buffer, a file, or a window whose length is set in bars all violate one of
those: allocation on the receiving thread, or a size that depends on the tempo and on how much has been
played. So the storage is allocated **exactly once**, in the client's constructor, off the MIDI thread, and
is never resized. 128 KiB per open client is small enough to be paid without asking, which is what makes
"off by default, armed on demand" an honest description rather than a cost the engine always carries.

**Drop-OLDEST, and the loss is counted.** The producer owns one monotonically increasing index and
overwrites the oldest slot, so the window always holds the newest events — the opposite of the
drop-newest policy `RecordRingBuffer` uses for audio, and the right way round here: the note a user just
played is the one they want back. Two counters make the loss visible rather than silent:
`overwritten` (events pushed out of the window) and `paused_dropped` (events the producer refused to write
because a snapshot was in flight). `midi.retro_capture_status` reports both, and the ctest asserts

```
retained + overwritten + paused_dropped == events played
```

so an event that is lost without being counted is a test failure, not a rounding error.

## 4. Per CLIENT, not per project, and not per track

- The ring is a **member of `MidiClient`** — one per open MIDI client, not one per project and not one per
  track. Every channel (`0..15`) and every source port land in the **same** window, tagged with the source
  port; there is no per-track filtering, and a capture written into a clip goes to one track.
- **Arming and disarming do not clear it.** `arm(false)` stops recording; the retained events stay readable
  until they are pushed out. There is no `clear` command in 0.3.0.
- **A client swap discards it.** Choosing a different MIDI backend (`settings.set audioengine/mididev`)
  constructs a new client, and the old window goes with the old object.
- **It does not travel with the project.** The window is runtime state; `project.save` writes none of it.
  The only thing the config file keeps is the arm flag itself (`midi/retrocapture`), which is the mode's
  persistent default, not a captured window.
- **A capture written into a clip is project state from that moment on** — `midi.retro_capture_to_clip`
  creates a real `MidiClip` and one `control.undo` (a Track checkpoint, inverse `clip.delete`) takes it
  back off. The window it came from is untouched by the undo.

## 5. What the window is NOT

- **Not a recording.** No audio is captured, by this feature or by anything it calls. Owner's-31 item 15
  ("retrospective audio capture") is **not in this release** — see §7.
- **Not lossless for SysEx.** A SysEx is variable-length; the ring stores a 16-byte flagged placeholder
  (`RetroMidiFlagSysExDropped`) rather than the bytes, so a recovered window can say a SysEx was seen and
  never reproduces it.
- **Not every byte on the wire.** System real-time and system common messages (clock, start/stop, active
  sensing) are not stored at all: they are not music, and in a window that holds only the most recent
  events they would evict the notes the feature exists to keep.
- **Not a quantiser and not a repairer.** A release with no note-on inside the window is counted
  (`unmatched_offs`) and invents no note; a note-on still open at the window's edge is closed there and
  counted (`unmatched_ons`), so a window that starts mid-phrase is reported as truncated rather than
  silently cleaned up.
- **Not free of the tick's provenance.** The ALSA-sequencer path stamps each event with the sequencer's own
  event tick. The raw clients (`MidiClientRaw`) have no tick of their own and use the transport tick the
  audio thread publishes once per rendered period, so for those clients the window's ticks are the
  transport's — and a capture taken while the transport is stopped stamps the last published value.

## 6. How the bound is measured (the registered ctest)

`ControlRetroCapture` (`tests/control-retro-capture.py`, registered in `tests/CMakeLists.txt`) starts the real
binary under `QT_QPA_PLATFORM=offscreen` with `--control-socket`, opens a project whose `<midiport>` is
readable (that is what makes the engine open an ALSA-sequencer input port), and then **plays real MIDI into
it with `aplaymidi`** — an external process, exactly like a keyboard. It asserts:

1. the same file played **before** arming leaves the window empty, and played again **after** arming is
   recovered — one reading is the measurement of the other;
2. the recovered notes' positions, lengths, keys and velocities, read back from the engine through
   `roll.get_state`, equal the file's own — position `0` and `240`, length `240`, key `60` at velocity `100`
   and key `64` at velocity `64`;
3. exactly **one** `control.undo` removes the clip (SPEC A16);
4. with a further **18000** events played into the window, `events_buffered` is exactly the
   `capacity_events` the build reports, that capacity equals the figure stated on this page, and
   `retained + overwritten + paused_dropped` equals **every event played in the run** (18004: the 4 of
   point 2 and the 18000 of this one) — the documented policy, not a crash;
5. the instance is still answering `control.ping` afterwards.

It reports ctest *Skipped* (exit 77), never *Passed*, on a host with no ALSA-sequencer tooling or an engine
that came up on the dummy MIDI client.

**The deterministic-ticks probe this test relies on**, re-runnable on this box: `aplaymidi` sends a
sequencer event carrying the Standard MIDI File's own tick, and a receiver reads the tick back unchanged
(`aplaymidi -p <port>` playing a file whose notes are at ticks 0/240/480 is received with
`time.tick = 0/240/480`). That is what lets §6.2 assert positions rather than a range.

## 7. What this lane did NOT build: owner's-31 item 15 (retrospective AUDIO capture)

**Item 15 is deferred, and this line is the deferral.** It was never in danger of arriving by accident:
item 15 needs the same rolling-window idea applied to **audio frames**, and this build has no capture path to
apply it to — `docs/KNOWN-LIMITATIONS.md` already records that ALSA records nothing and that the
two-track recorder prototype is fed by tests. A retrospective audio window built on top of a recorder that
cannot record would be a window nothing could fill, and a bound stated for it could not be measured by any
test this lane could register. Item 14's own recorded dependency is *none* ("days-weeks, no dependency"),
so it is the half that can be built, proved and bounded today; item 15's is the capture path itself, which
is a separate piece of engine work. The release notes and `docs/KNOWN-LIMITATIONS.md` say the same thing in
one line each.

## 8. Not verified here

Each of these needs hardware or a live session, not a build:

- that a **real USB/PCI MIDI keyboard** delivers on the thread `docs/MIDI-RETRO-CAPTURE.md` §2 assumes
  (the ctest's `aplaymidi` source is a real ALSA client, but it is not a keyboard's driver);
- that the ALSA sequencer's tick and the transport tick agree for the **raw** clients;
- the MIDI thread's **end-to-end allocation profile**: `InstrumentTrack::processInEvent` already allocates,
  and this feature adds nothing there, but the whole input path is not allocation-free and this page does
  not claim it is;
- that `Ctrl+Shift+M` is free across the whole shortcut table, which is why no shortcut is taken at all.
