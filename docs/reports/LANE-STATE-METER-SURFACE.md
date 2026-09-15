# LANE STATE — `030/meter-surface` (loudness metering, feature row 24, board task 654)

> **Location (2026-09-15).** Moved from the repository root to `docs/reports/LANE-STATE-METER-SURFACE.md` by
> REPO-4 ("move lane reports and transcripts out of the repository root"), the second pass —
> the 2026-09-13 pass moved `DOCS-NAMING.md`, `CMDN-REPORT.md` and `CMDN-TRANSCRIPT.md` and
> left the rule in `docs/reports/README.md`. `docs/METER-SURFACE.md` — the design document this file's build/test state belongs to —
> carries the new path in the same change.
> The lane's own text is unchanged: it is a record of what the lane measured, and editing a
> record is how a record stops being evidence. A citation that names it by bare name still
> resolves to this file; the rule is recorded in `docs/reports/README.md` and
> `docs/CONVENTIONS.md`.

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
| `28a9246aa` | the two registered proofs (MeterTapTest, ControlMeterCommands), the split that keeps every file under the 500-line ratchet, the docs and this file |
| `8a4a9cec5` | `fix(meter)`: a re-arm always starts a fresh measurement (found by the socket proof), the surface snapshot regenerated from a live instance, the LANE-STATE/METER-SURFACE evidence |
| `d216c0131` | the loudness surface's four rows in the documented A16 histogram (base 229 / 122 / 18 / 7 / 82), named in `docs/RELEASE-NOTES-v0.3.0-alpha.md` too |

All four are on `030/meter-surface` on top of `f611c888b`; nothing was pushed and nothing was merged.

## 3. What was RUN here, and what it returned

Every command below was run in this worktree; the exit codes are unpiped (`cmd > log; echo EXIT=$?`).

```
$ cmake --build build -j2 --target zene MeterTapTest audiofileprocessor > /tmp/meter-build3.log 2>&1; echo EXIT=$?
EXIT=0                      # the four new sources compile; no warnings (the tree builds with -Werror)
$ cd build/tests && ./MeterTapTest > /tmp/meter-tap2.log 2>&1; echo EXIT=$?
EXIT=0                      # Totals: 11 passed, 0 failed, 0 skipped
$ cd build/tests && ctest -R ControlMeterCommands --output-on-failure > /tmp/meter-ctest3.log 2>&1; echo EXIT=$?
EXIT=0                      # 1/1 Test #142: ControlMeterCommands ... Passed  13.35 sec
$ QT_QPA_PLATFORM=offscreen python3 tests/control-meter-commands.py build/zene > /tmp/meter-checks.log 2>&1; echo EXIT=$?
EXIT=0                      # 28/28 checks ok (the full list is in that log)
$ python3 tools/mcp-zene-control/snapshot_commands.py --socket <own instance> > /tmp/meter-snapshot.log 2>&1; echo EXIT=$?
EXIT=0                      # 231 commands, including meter.arm / meter.get_state / meter.measure_file / export.set_loudness_report
$ cd build/tests && ctest -R ControlCommandsSnapshot --output-on-failure > /tmp/meter-snap-ctest.log 2>&1; echo EXIT=$?
EXIT=0                      # 1/1 Passed 3.09 sec (the surface snapshot now carries the four new ids)
$ cd build/tests && ./ReversibilityContractTest > /tmp/meter-rev2.log 2>&1; echo EXIT=$?
EXIT=0                      # 8 passed, 0 failed - the A16 table and the registry agree in both directions
$ cd build/tests && ctest -R "MeterTapTest|ControlMeterCommands|ControlCommandsSnapshot|ReversibilityContractTest|LufsMeterTest|LoudnessReportTest|ControlExportSettings"
EXIT=0                      # 100% tests passed, 0 tests failed out of 7 (24.47 sec)
```

The three negative controls the task names are asserted in **both** proofs, and both pass:
`tone-23.wav` / `tone-33.wav` measure -23.00 / -33.00 LUFS-I (±0.1) and **10 LU apart**, live and from a
file; `silent.wav` reports `null` for every reading with `measured: false` and verdict
`NOT MEASURED (...)`; and the tap's buffer is hash-identical after being fed (C++) while two renders of one
project have identical PCM frames with the tap armed and disarmed (socket).

**One defect the socket proof found and this lane fixed** (recorded because the fix is a contract change):
a re-arm of an already-armed tap used to be a no-op, so the second of two measured sections came out
**2.59 LU** from the first instead of 10 - the earlier material was still in the integrated value while the
*momentary* value was correct. `MasterLoudnessTap::setEnabled(true)` now always starts a fresh measurement
(bounded quiesce, reset in place), the command description says so, `MeterTapTest` asserts the reset, and the
socket proof's LIVE 10 LU separation is the control that caught it.

## 4. What the parent must run (and what was still open here)

1. **The full tree build** (`JOBS=2 bash tools/local-ci.sh --build-dir build --jobs 2`). This lane built
   `zene`, `MeterTapTest` and the `audiofileprocessor` plugin (needed by the fixtures) and those are green;
   the full `all` target was interrupted here once by an unrelated target
   (`AutomationModesTest`: `undefined reference to 'main'` - a stale object left by this lane's own kill of a
   build, so its `.o` was deleted and it must be allowed to rebuild). **Not verified in this lane:** the rest
   of the suite, so the parent should re-run it.
2. **The two proofs**: commands and expected exit codes above.
3. **`ControlCommandsSnapshot`**: regenerated from a live instance in this lane and green (see §3). If the
   parent merges another lane that also adds ids, regenerate again with
   `tools/mcp-zene-control/snapshot_commands.py --socket <its socket>` - never by hand.
4. **Static gates** (`no-tautology`, `complexity`, `file-length`, `duplication`, `fork-sources`,
   `no-upstream-regression`): not run in this lane. All new files are under the 500-line ratchet (largest:
   `ControlCommandsMeter.cpp` 338, `MeterTapTest.cpp` 338) and the two ledgers were updated in the same
   commits, but the gates want a run on the merged tip.
5. **This lane's build environment note**: `-DWANT_QT6=ON` (this box has Qt6 dev files, no Qt5), `-j2` as the
   8-lane box requires. An instance killed mid-test in another lane's worktree by a `pkill` pattern that was
   too broad (this lane's mistake, stated rather than hidden): the patterns used after that are exact-path.

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
