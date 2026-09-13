# LANE-STATE — 030/retro-capture (owner's-31 item 14, retrospective MIDI capture)

**Worktree:** `/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/lmms-fl-research/lmms/zene-030/wret`
**Base:** `release/0.3.0` tip `334790219`. **Branch:** `030/retro-capture` @ `0bb7de86c`. **Build dir:** `build`
(local-ci configuration: `RelWithDebInfo -DUSE_WERROR=ON -DWANT_VST3=ON -DWANT_CLAP=ON -DWANT_QT6=ON`,
wasmtime absent, telemetry ON). ~21 GB — **delete `build/` when the lane is done** (disk is the binding
constraint).

## Done and committed (10 commits on the base)

| commit | what |
|---|---|
| `dc666ad5e`, `73e7a1542` | `docs/MIDI-RETRO-CAPTURE.md` — the design + the audit corrections (from `next/midi-retro` @ `2858b77df`) |
| `5805c2617` | slice 1 — the ring, the two receive seams, the off-by-default arm (from `next/midi-retro-impl` @ `3abae78fb`) |
| `5c01a263e` | slice 2 — the three `midi.retro_capture_*` commands, the persisted arm switch, the note matcher, the menu (from `0496af26d`) |
| `6e9cb4a06` | **the missing half**: the registered socket proof, `docs/MIDI-RETRO-CAPTURE-BOUNDS.md`, the two UI-absence lines, the A16 histogram → 167/87/13/4/63, item 15's deferral |
| `6b6e4d517` | gate 7: `include/ControlRegistry.h` 503→490, `src/core/ControlReversibilityTable.cpp` 506→490 |
| `90f5eab6c` | the release configuration's two suite failures: `RetroMidiRingTest`'s push inside `Q_ASSERT`, and `ControlRetroCapture`'s own three defects |
| `81ee2fda3` | this handover file |
| `41017d639` | the proof PASSES; the paced-file measurement and the input-pool probe |
| `0bb7de86c` | `commands_snapshot.json` regenerated from a live instance (164 → 167 commands, +3 ids, 0 lost) |

## Green (every code unpiped, logs in `/tmp/rc-030-retro-capture-verify/`)

- `bash tools/local-ci.sh --build-dir build --jobs 2` — configure `/build` **EXIT=0**
- `ctest -R '^ControlRetroCapture$'` — **EXIT=0, Passed 31.80 s**; run directly it prints
  `window [0, 480] of 4 event(s) -> clip clip-2 with 2 note(s): [(0, 240, 60, 100), (240, 240, 64, 64)]`
  and `played 20004 event(s) into a 8192-event window: retained 8192, overwritten 11812, paused 0, refused 0`
- `ctest -R '^RetroMidiRingTest$'` — **EXIT=0, 0.13 s** (was a 300 s abort)
- `ctest -R '^ControlCommandsSnapshot$'` — **EXIT=0, 3.54 s**
- gates 4/6/7/8/9, evidence, unregistered-tests — **all EXIT=0**; both manifest recipes print **REPRODUCES**
- `MasteringTest` and `PluginPortsMigrationTest` — **EXIT=0 each when run alone**; they only failed inside
  the full `ctest -j2` run, so their reds read as parallel-load flakes, not code. Re-check in the final run.

## The final acceptance run — `bash /tmp/rc-030-retro-capture-verify/accept.sh 334790219`

Every code unpiped, each gate's own log in `/tmp/rc-030-retro-capture-verify/a-*.log`:

| command | exit |
|---|---|
| `bash tools/local-ci.sh --build-dir build --jobs 2` (configure + build + ctest) | **0** — ctest `100% tests passed, 0 tests failed out of 131`, `ControlRetroCapture` Passed 31.92 s, `RetroMidiRingTest` Passed 0.05 s |
| `bash tests/run-all-gates.sh` | **3** — PASS-WITH-SKIPS: all 11 gates PASS (ctest, no-tautology, complexity, mutation, upstream-regression, file-length, duplication, fork-sources, unregistered-tests, evidence); gate 2 (coverage) SKIPs without `--with-coverage`. 3 is the accepted outcome; 1 never appeared |
| `bash tests/release-honesty-gate.sh --header build/lmmsversion.h --artifacts build` | **0** |
| `bash tests/complexity-gate.sh --check` | **0** |
| `bash tests/file-length-gate.sh --check` | **0** |
| `bash tests/duplication-gate.sh` | **0** |
| `bash tests/fork-sources-gate.sh` | **0** |
| `bash tests/no-upstream-regression-gate.sh` | **0** |
| `bash tests/unregistered-tests-gate.sh` | **0** |
| `bash tests/evidence-gate.sh` | **0** |
| both manifest recipes (`tests/all-sources.txt`, `tests/fork-sources.txt` "Verify it") | **0** — each prints `REPRODUCES` |

`run-all-gates.sh` states in its own summary that the WHOLE-TREE scope (gates 4/7/8 `--scope all`) was NOT
measured by it; only the enforced fork+tools scope was. That is the scope `WAVE-1-BRIEFS.md` asks for.

### The two load-flake reds, resolved

`MasteringTest` (34) and `PluginPortsMigrationTest` (101) failed only in the first `ctest -j2` sweep, which
ran under a heavily loaded box (that sweep took 342 s against 134 s for the final one, and the SAME window
produced a `/usr/bin/ld: final link failed: file truncated` on an unrelated target). Re-run alone they both
pass (`ctest -R '^(MasteringTest|PluginPortsMigrationTest)$'` → EXIT=0, 2/2), and in the final full sweep
they are green: 131/131. They are not this lane's code — neither file is touched by the branch.

## Left to do

1. **The parent merges `030/retro-capture` and re-runs the build, suite and gates on the merged tip.** That
   re-run is the verification; this lane's green is a hypothesis until then.
2. **`build/` has been deleted** (disk is the binding constraint, ~21 GB). Rebuild with
   `bash tools/local-ci.sh --build-dir build --jobs 2` if the suite has to be run again.

## Not verified by this lane (say so in the report)

Real USB/PCI MIDI hardware delivery (the ctest's MIDI source is `aplaymidi`, a real external ALSA client, but
not a keyboard's driver); the sequencer-tick vs transport-tick agreement for the raw clients; the MIDI
thread's end-to-end allocation profile; `Ctrl+Shift+M`'s availability across the whole shortcut table (which is
why no shortcut is taken). Owner's-31 item 15 (retrospective AUDIO capture) is deliberately NOT built, and is
declared in `docs/KNOWN-LIMITATIONS.md` and `docs/RELEASE-NOTES-v0.3.0-alpha.md` in the same line as the UI
absence.
