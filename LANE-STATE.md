# LANE 030/midi-clock — MIDI clock / MTC (engine half + `clock.*` group + proof + UI-absence)

**Branch:** `030/midi-clock`
**Worktree:** `lmms/zene-030/wpc` (base = `501d2cd3e`, the `release/0.3.0` tip at lane start)
**Build dir:** `wpc/build` (RelWithDebInfo, USE_WERROR=ON, WANT_VST3=OFF, WANT_CLAP=OFF, WANT_QT6=ON)
**Lane private log dir:** `/tmp/wpc-030-*` — never a generic name.

## Item, as posed
MIDI clock / MTC. The largest genuine gap in the 0.3.0 scope: engine work with no
architectural gate, so D12 puts it in 0.3.0, and NO 0.3.0 document places it
(`PLANNED-WORK-MASTER-LIST-2026-09-13.md` ~line 164, unboarded Bar-2 gap; agent-surface
inventory Group 11).

## Findings that shaped the design (measured in the tree, not assumed)
- `grep -rniI 'midi clock|mtc|midi time code' src include` finds **no engine code** — the only
  hits are vendored `src/3rdparty/jack2` and an unrelated `readFmtChunk` in
  `ControlCommandsProject.cpp` (`fmt ` vs `mtc ` substring). The feature was genuinely absent.
- The MIDI **event vocabulary already existed**: `include/Midi.h` declares `MidiTimeCode=0xF1`,
  `MidiSongPosition=0xF2`, `MidiSync=0xF8`, `MidiStart=0xFA`, `MidiContinue=0xFB`, `MidiStop=0xFC`.
- The MIDI **output path existed but was narrow**: `MidiClient::processOutEvent` was the only
  write path and every implementation warned "unhandled" for the clock family
  (`MidiClientRaw::processOutEvent`, `MidiAlsaSeq::processOutEvent` default cases).
- The MIDI **input parser dropped the clock**: `MidiClientRaw::parseData` returned early for
  every byte `>= 0xF8` except a system reset, and cancelled-and-dropped system-common bytes.
- The transport's own seam for per-audio-period engine readers is `Song::processNextBuffer()`
  (`src/core/Song.cpp:245`); its Session View block runs **before** the `if (!m_playing) return;`
  gate for the same reason a clock master needs — STOP is an *edge*.
- Resolution: `DefaultTicksPerBar = 192`, `DefaultStepsPerBar = 16` ⇒ a step (a MIDI beat = a
  16th note) is 12 ticks, a quarter is 48, so **1 clock pulse = 2 ticks**, **1 SPP unit = 12 ticks**.
- The A16 row count is an **assertion**: `ReversibilityContractTest::documentedHistogram()`
  carries `{162, 86, 13, 4, 59}` and `docs/RELEASE-NOTES-v0.3.0-alpha.md` quotes the release
  configuration's `164 / 86 / 13 / 4 / 61`. Both move with a new group.
- Ninth touch-point: `tools/mcp-zene-control/zene_control/commands_snapshot.json` (164 ids).

## Files this lane adds / changes
NEW: `include/MidiClock.h` · `src/core/MidiClock.cpp` · `src/core/MidiClockState.cpp` ·
`src/core/MidiClockTracker.cpp` · `src/core/ControlCommandsClock.cpp` ·
`tests/src/core/MidiClockTest.cpp` · `tests/control-clock-commands.py`
MODIFIED (upstream, divergence ledger): `src/core/Song.cpp` · `src/core/midi/MidiClient.cpp` ·
`include/MidiClient.h` · `src/core/midi/MidiAlsaSeq.cpp`
MODIFIED (fork): `src/core/CMakeLists.txt` · `include/ControlRegistryGroups.h` ·
`src/core/ControlRegistry.cpp` · `src/core/ControlReversibilityTable*.cpp` ·
`tests/src/core/ReversibilityContractTest.cpp` · `tests/CMakeLists.txt` ·
`tests/fork-sources.txt` · `tests/all-sources.txt` · `tests/upstream-modifications.txt` ·
`docs/RELEASE-NOTES-v0.3.0-alpha.md` · `docs/KNOWN-LIMITATIONS.md` · the bridge snapshot

## The BOUND, stated not implied
The master's bytes reach a MIDI **device** only through a real backend. The headless proof
asserts the engine's own emission (the message sequence + counters it publishes) and the
slave's timing math, NOT that an external synth received them. MTC is **not generated**: a
full-frame timecode master needs a frame rate, a drop-frame flag and a SMPTE offset the engine
has no model for, so `clock.get_state` reports `mtc: "absent"` and KNOWN-LIMITATIONS says why.

## State
- [x] worktree + branch + build dir at `501d2cd3e`; configure EXIT=0
- [x] engine half written (master generator, slave tracker, parser + output switches, Song hook)
- [ ] engine half COMPILES + committed
- [ ] command group + A16 rows + histogram + notes figure
- [ ] MidiClockTest + ControlClockCommands
- [ ] docs one-liners + manifests + snapshot
- [ ] acceptance: local-ci, run-all-gates, release-honesty, gates 4/7/8/9/6/10/11

## Next command
```
cd /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/lmms-fl-research/lmms/zene-030/wpc/build
cmake . > /tmp/wpc-030-cfg2.log 2>&1; echo CFG=$?
cmake --build . -j4 > /tmp/wpc-030-build2.log 2>&1; echo BUILD=$?
```
