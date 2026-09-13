# LANE 030/midi-clock — MIDI clock / MTC (item: engine half + `clock.*` group + proof + UI-absence)

**Branch:** `030/midi-clock`
**Worktree:** `lmms/zene-030/wpc` (base = `501d2cd3e`, the `release/0.3.0` tip at lane start)
**Build dir:** `wpc/build` (RelWithDebInfo, USE_WERROR=ON, WANT_VST3=OFF, WANT_CLAP=OFF, WANT_QT6=ON)
**Lane private log dir:** `/tmp/wpc-030-*` — never a generic name.

## Item, as posed
MIDI clock / MTC. This is the largest genuine gap in the 0.3.0 scope: engine work with no
architectural gate, so D12 puts it in 0.3.0, and NO 0.3.0 document places it
(`PLANNED-WORK-MASTER-LIST-2026-09-13.md` ~line 164, unboarded Bar-2 gap; agent-surface
inventory Group 11).

## Findings that shape the design (measured in the tree, not assumed)
- `grep -rniI 'midi clock|mtc|midi time code' src include` finds **no engine code** — the only
  hits are vendored `src/3rdparty/jack2` and an unrelated `readFmtChunk` in
  `ControlCommandsProject.cpp` (`fmt ` vs `mtc` substring). The feature is genuinely absent.
- The MIDI **event vocabulary already exists**: `include/Midi.h` declares `MidiTimeCode=0xF1`,
  `MidiSongPosition=0xF2`, `MidiSync=0xF8`, `MidiStart=0xFA`, `MidiContinue=0xFB`, `MidiStop=0xFC`.
- The MIDI **output path exists but is per-device and narrow**: `MidiClient::processOutEvent` is
  the only write path, and every implementation drops real-time/common messages —
  `MidiClientRaw::processOutEvent` (`src/core/midi/MidiClient.cpp:255`) handles only
  note on/off/key pressure and `qWarning`s the rest; `MidiAlsaSeq::processOutEvent`
  (`src/core/midi/MidiAlsaSeq.cpp:234`) has the same `default:` warn. So the engine half must
  *widen these two switches*, not invent a path.
- The MIDI **input parser drops clock**: `MidiClientRaw::parseData` returns early for every
  `c >= 0xF8` except `MidiSystemReset`. Clock cannot reach the engine today.
- The transport's own seam for per-audio-period engine readers is `Song::processNextBuffer()`
  (`src/core/Song.cpp:245`): the Session View block (`#ifdef LMMS_HAVE_SESSION_VIEW`) runs
  **before** the `if (!m_playing) return;` gate, for exactly the reason a clock master needs —
  STOP is an *edge* and a stopped transport is when it must be sent.
- Resolution: `DefaultTicksPerBar = 192`, `DefaultStepsPerBar = 16` (`include/TimePos.h:38`) ⇒
  a step (LMMS "beat" = a MIDI beat = a 16th note) is 12 ticks, a quarter note is 48 ticks, so
  **1 MIDI clock pulse (1/24 quarter) = 2 ticks** and a song-position pointer (unit = 1 MIDI
  beat = a 16th) is `ticks / 12`.
- The A16 table is DATA in four TUs, and the row count is an **assertion**:
  `ReversibilityContractTest::documentedHistogram()` (`tests/src/core/ReversibilityContractTest.cpp:87`)
  carries `{162, 86, 13, 4, 59}` and the same figure must move in
  `docs/RELEASE-NOTES-v0.3.0-alpha.md` §"The A16 contract table, and its histogram".
- The ninth registration touch-point: `tools/mcp-zene-control/zene_control/commands_snapshot.json`
  (164 ids today) must be regenerated from a live instance or `ControlCommandsSnapshot` goes red.

## Plan (four-part scope contract)
1. **Engine** — new `include/MidiClock.h` + `src/core/MidiClock.cpp`: one object that is BOTH
   the master generator and the slave follower. Master: `processAudioPeriod()` called from
   `Song::processNextBuffer()`, accumulate fractional ticks, emit 24 pulses/quarter + START/STOP/
   CONTINUE + SPP on the transport's edges, record every emitted message in a bounded monitor.
   Slave: `handleInputRealtime()`, fed by the widened `MidiClientRaw::parseData`, measures the
   pulse interval against a monotonic clock and reports `locked` + `tempo_measured` + drift.
   Widen the two `processOutEvent` switches to actually send F1/F2/F8/FA/FB/FC.
2. **Command group** `clock.*` — `clock.get_state` (not_mutating), `clock.master_set`
   (true_inverse, action checkpoint), `clock.slave_set` (**snapshot, reversible=false** — a
   tempo-following slave writes a *trajectory*, so no bounded inverse covers it; the row says so
   and names the fallback).
3. **Proof** — `tests/src/core/MidiClockTest.cpp` (registered QTest ctest: synthetic pulses at a
   known rate ⇒ the measured tempo; master emission sequence for a known tick advance; unlocked
   with no pulses) **and** `tests/control-clock-commands.py` (registered ctest `ControlClockCommands`:
   the REAL binary over `--control-socket`, asserting the transport does not move and reports
   unlocked with slave enabled and no clock, plus the A16 records).
4. **Docs + manifests** — one UI-absence line in `docs/RELEASE-NOTES-v0.3.0-alpha.md` and one in
   `docs/KNOWN-LIMITATIONS.md`; new files in `tests/fork-sources.txt` **and**
   `tests/all-sources.txt`; widened upstream files in `tests/upstream-modifications.txt`;
   regenerate the snapshot; re-run gates 4/7/8/9/6 after touching the manifests.

## BOUND I will document rather than claim
The master's bytes reach a MIDI **device** only through a real backend; on this box the
headless proof can assert the engine's own emission (the message sequence and counters it
publishes) and the slave's timing math, but **not** that an external synth received them. That
sentence goes in `docs/KNOWN-LIMITATIONS.md` in the `docs/UNDO-BOUNDS.md` style.

## State
- [x] worktree + branch + build dir at `501d2cd3e`; configure EXIT=0
- [ ] first full build (running)
- [ ] engine half
- [ ] command group + A16 rows + histogram
- [ ] MidiClockTest + ControlClockCommands
- [ ] docs + manifests + snapshot
- [ ] acceptance: local-ci, run-all-gates, release-honesty, 4/7/8/9/6/10/11

## Next command
```
cd /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/lmms-fl-research/lmms/zene-030/wpc
git add -p   # explicit paths only, never -A
```
