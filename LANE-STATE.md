# LANE-STATE — 030/retro-capture (owner's-31 item 14, retrospective MIDI capture)

**Worktree:** `/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/lmms-fl-research/lmms/zene-030/wret`
**Base:** `release/0.3.0` tip `334790219`. **Branch:** `030/retro-capture`. **Build dir:** `build`
(local-ci configuration: `RelWithDebInfo -DUSE_WERROR=ON -DWANT_QT3=OFF… -DWANT_VST3=ON -DWANT_CLAP=ON -DWANT_QT6=ON`,
wasmtime absent, telemetry ON). ~21 GB; disk is the binding constraint — delete it when the lane is done.

## Done and committed

| commit | what |
|---|---|
| `dc666ad5e`, `73e7a1542` | the design doc `docs/MIDI-RETRO-CAPTURE.md` (cherry-picked from `next/midi-retro` @ `2858b77df`) |
| `5805c2617` | slice 1 — the ring, the two receive seams, the off-by-default arm (cherry-picked from `next/midi-retro-impl` @ `3abae78fb`, conflicts resolved additively in `Song.cpp`, `tests/fork-sources.txt`, `tests/upstream-modifications.txt`) |
| `5c01a263e` | slice 2 — the three `midi.retro_capture_*` commands, the persisted arm switch, the note matcher, the menu, the 14-slot unit test (cherry-picked @ `0496af26d`; the table rows re-homed into the post-split files) |
| `6e9cb4a06` | **the missing half**: the registered socket proof `tests/control-retro-capture.py` (ctest `ControlRetroCapture`), `docs/MIDI-RETRO-CAPTURE-BOUNDS.md`, the two UI-absence lines, the A16 histogram move to 167/87/13/4/63, item 15's deferral |
| `6b6e4d517` | gate 7 fix: `include/ControlRegistry.h` 503→490 (the `applyPersistedRetroCaptureArm` declaration moves verbatim into `include/RetroMidiCaptureSettings.h`) and `src/core/ControlReversibilityTable.cpp` 506→490 (the four dead `transport.tempo_map_*` duplicates removed — action's copies already win the assembly) |
| `90f5eab6c` | the two suite failures the release configuration exposed: `RetroMidiRingTest`'s push inside `Q_ASSERT` (compiled away under `-DNDEBUG` → 300 s timeout), and `ControlRetroCapture`'s own `undone_command` / `finish()` / un-paced-burst defects |

## Green at `90f5eab6c` (all unpiped, logs in `/tmp/rc-030-retro-capture-verify/`)

- `bash tools/local-ci.sh --build-dir build --jobs 2` — configure EXIT=0, **build EXIT=0**
- `ctest` from `build/tests` — 126/131 (5 reds, see below)
- `ctest -R '^RetroMidiRingTest$'` — **EXIT=0, Passed 0.13 s** (was a 300 s abort)
- gates 4/6/7/8/9 + evidence + unregistered-tests — **all EXIT=0**
- both manifest recipes print **REPRODUCES**

## Left to do (in this order)

1. **`ControlRetroCapture`'s bound half: pace the burst.** The last run measured
   `8192 retained + 11412 overwritten + 0 paused` against 20004 played — 400 events were lost
   **in the kernel**, not in the ring: the engine's ALSA client has a bounded input pool and a
   200-event-per-run burst already overflows it (one 18000-event burst delivered only 614 events).
   The fix in progress: play ONE Standard MIDI File whose 10000 note pairs are spaced one tick
   apart, so aplaymidi paces it at ~960 events/s (20.8 s) and the pool drains between events;
   then `retained + overwritten + paused == played` holds exactly. Bounds to keep:
   `BURST_PAIRS`/`BURST_RUNS` are what change; `docs/MIDI-RETRO-CAPTURE-BOUNDS.md` §6 and the
   release-notes section quote 18000/18004 and must be corrected to whatever the final run plays.
2. **Regenerate `tools/mcp-zene-control/zene_control/commands_snapshot.json`** — `ControlCommandsSnapshot`
   is RED because the three `midi.retro_capture_*` ids are missing from the committed snapshot:
   ```bash
   cd <wret> && mkdir -p /tmp/rc-snap && \
   ( QT_QPA_PLATFORM=offscreen HOME=/tmp/rc-snap XDG_CONFIG_HOME=/tmp/rc-snap/cfg XDG_DATA_HOME=/tmp/rc-snap/data \
     build/bin/zene --control-socket /tmp/rc-snap/zene.sock --config /tmp/rc-snap/lmmsrc.xml & echo $! > /tmp/rc-snap/pid ) ; \
   python3 tools/mcp-zene-control/snapshot_commands.py --socket /tmp/rc-snap/zene.sock ; kill $(cat /tmp/rc-snap/pid)
   ```
   (a config file with `<audioengine audiodev="Dummy (no sound output)"/>` and `<app configured="1"/>`
   is required — copy the one `control_socket_harness.config_xml()` writes). Then commit the JSON and
   re-run `ctest -R ControlCommandsSnapshot`.
3. **Check whether `MasteringTest` and `PluginPortsMigrationTest` are pre-existing reds** (they are not
   this lane's code: neither file is touched by the branch). Run them, read the failure text, and say
   plainly in the report whether they are pre-existing or introduced — do not assume either way.
4. **The full acceptance list** — `bash /tmp/rc-030-retro-capture-verify/accept.sh 334790219`, then
   report every exit code. `bash tools/local-ci.sh --build-dir build --jobs 2` must end with ctest 100%
   (0 tests is an error, not a pass).
5. **Delete `build/`** when the lane is done (disk) and hand the branch to the parent. Never push.

## Not verified by this lane (say so in the report)

Real USB/PCI MIDI hardware delivery; the sequencer-tick vs transport-tick agreement for the raw
clients; the MIDI thread's end-to-end allocation profile; `Ctrl+Shift+M`'s availability across the
whole shortcut table (which is why no shortcut was taken). The ctest's MIDI source is `aplaymidi`, a
real external ALSA client, not a keyboard's driver.
