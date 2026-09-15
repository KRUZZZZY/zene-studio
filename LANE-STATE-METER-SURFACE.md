# LANE STATE — `030/meter-surface` (loudness metering, feature row 24, board task 654)

**Worktree:** `zene-030/wmeter` · **Branch:** `030/meter-surface` · **Base:** `release/0.3.0` @ `f611c888b`
**Do not merge yet** — see §4 (`what the parent must run`). The parent re-runs the build, the suite and the
gates on the merged tip before accepting anything here.

## 1. What this lane delivered

| | |
|---|---|
| **Commands** | `meter.get_state`, `meter.arm`, `meter.measure_file` (the `meter.*` group) + `export.set_loudness_report`; `export.get_settings` now exposes `loudness_report` |
| **Engine** | `include/MasterLoudnessTap.h` + `src/core/MasterLoudnessTap.cpp` (the PASSIVE live tap, fed from `AudioEngine::renderStageMix()`); `LoudnessReport::addPlanarBlock()` |
| **A16** | `src/core/ControlReversibilityTableMeter.cpp` (4 rows, joined into `reversibilityRowTable()`) |
| **Registered proofs** | `MeterTapTest` (`tests/src/core/MeterTapTest.cpp`, in `LMMS_TESTS`) and `ControlMeterCommands` (`tests/control-meter-commands.py`, a ctest) |
| **Docs** | `docs/METER-SURFACE.md` (the feature record), the absence line in `docs/KNOWN-LIMITATIONS.md` and in `docs/RELEASE-NOTES-v0.3.0-alpha.md` |
| **Fork ledgers** | `tests/fork-sources.txt` (6 new files), `tests/upstream-modifications.txt` (4 inherited files, reason appended) — same commits as the files |

No DSP was forked: every number comes out of the merged `LufsMeter` (live through `MasterLoudnessTap`, files
through `LoudnessReport`).

## 2. Commits

| SHA | What |
|---|---|
| `69c39078e` | `feat(meter)`: the group, the tap, the export exposure, the A16 rows, the registration, the ledgers |
| (this commit) | the two registered proofs, the docs, this file |

## 3. What was RUN here, and what it returned

*(filled in from the lane's own runs; anything not run is named as not run — nothing in this file is a
prediction)*

- Static review only at the time of writing: the lane's build was started
  (`JOBS=2 bash tools/local-ci.sh --build-dir build --jobs 2`, log `/tmp/meter-local-ci.log`) and its
  **configure step exited 0**; the compile/test steps had not finished when the lane closed. The exact
  state is in §4.

## 4. What the parent must run (and what is expected to be red first)

1. **Build**: `JOBS=2 bash tools/local-ci.sh --build-dir build --jobs 2` in this worktree. Eight lanes share
   this box; `-j2` is deliberate.
2. **The two proofs**:
   `cd build/tests && ./MeterTapTest > /tmp/meter-tap.log 2>&1; echo EXIT=$?` and
   `cd build/tests && ctest -R ControlMeterCommands --output-on-failure; echo EXIT=$?`.
3. **Known-red, by design, until the surface snapshot is regenerated:**
   `ControlCommandsSnapshot` (`tests/control-commands-snapshot.py`) compares the committed
   `tools/mcp-zene-control/zene_control/commands_snapshot.json` against the binary BOTH ways, and the four
   new ids are not in that snapshot. The snapshot is **derived from a live instance and is never
   hand-written** (`tools/mcp-zene-control/snapshot_commands.py` says so in its own docstring), so this lane
   did not edit it by hand. Regenerate it once the binary builds:
   `python3 tools/mcp-zene-control/snapshot_commands.py --socket <a running instance's socket>`.
   The expectation is 4 new ids in the snapshot (`meter.get_state`, `meter.arm`, `meter.measure_file`,
   `export.set_loudness_report`) and nothing else changed.
4. **Gate 7 (file length)**: the new files are all under the 500-line ratchet (largest: 338). Run
   `bash tests/file-length-gate.sh --check` after the build to confirm nothing was entered into a baseline.
5. **Gate 3 (no tautology), gate 4 (complexity), gate 8 (duplication)**: not run in this lane; the new
   command file is long and comment-heavy, and `tests/no-tautology-gate.sh` is the one that has flagged
   assert-free slots before.

## 5. Stated limits (also in `docs/KNOWN-LIMITATIONS.md`)

No meter in the interface (no widget, no readout, no bridge) · master mix only (no per-track/bus loudness,
no LUFS-M history, no LRA) · stereo in the application; `meter.measure_file` takes 1–6 channels and refuses
more · EBU R 128 (−23.0 LUFS-I ±0.5 LU, −1.0 dBTP) is the only graded target · the live tap is toggled
through a bounded quiesce and never replaces the meter object — the residual one-period window is stated in
the header, not hidden · no independent cross-check of the live readings against a second BS.1770-4
implementation (the file path has one, `docs/LUFS-WIRING.md` §4.1).

## 6. Hotspots touched (7 sibling lanes edit these the same minute)

- `src/core/ControlRegistryRegistrations.cpp` — one `registerMeterCommands(registry);` call + comment.
- `include/ControlRegistryGroups.h` — one declaration block appended.
- `src/core/ControlReversibilityTable.cpp` — `reversibilityMeterRowTable` added to the join list.
- `include/ControlReversibility.h` — one declaration appended.
- `tests/CMakeLists.txt` — one `LMMS_TESTS` entry + one `add_test(ControlMeterCommands …)` block.
- `tests/fork-sources.txt`, `tests/upstream-modifications.txt` — additive entries only (no reordering).
- `docs/KNOWN-LIMITATIONS.md`, `docs/RELEASE-NOTES-v0.3.0-alpha.md` — one paragraph / one section appended.
