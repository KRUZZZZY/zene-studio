FIXED: the cycled arpeggio divided by zero, but through fractional note repeats, not through repeats exceeding the range as the finding says.

## Decision and evidence

- **The finding's trigger cannot happen with whole repeats.** The slot count is built from the chord size, the range and the repeats: `singleNoteRange = cur_chord_size * range * repeats` (pre-fix `src/core/InstrumentFunctions.cpp:378`, then `range` at line 380). So `range / repeats` is `chord size * range`, at least 1, because every chord table entry has at least one note (first entry `octave`, `{ 0, -1 }`, `src/core/InstrumentFunctions.cpp:42`) and both models clamp to a minimum of 1 (`src/core/InstrumentFunctions.cpp:306-307`).
- **The real zero divisor is a fractional repeat count.** `m_arpRepeatsModel` is created with step 1.0 but without a strict step (`src/core/InstrumentFunctions.cpp:307`; `src/core/AutomatableModel.cpp:147`, `m_hasStrictStepSize( false )`). `fittedValue()` therefore clamps but does not round (`src/core/AutomatableModel.cpp:606-616`), so a project value of 1.5 is kept. With range 1 and a one-note chord, `range = int(1 * 1 * 1.5) = 1`, and `int(1 / 1.5) = 0`, so the cycle path ran `cur_arp_idx %= 0` (pre-fix line 468). The same expression in the downward inversion (pre-fix line 474) gave index -1, a key one octave down and, with larger chords, an out-of-bounds chord index. The regression reproduces this from a project attribute `arprepeats="1.5"`.
- **Fix at the cause.** `processNote` now converts range and repeats to whole numbers once (`src/core/InstrumentFunctions.cpp:379-383`) and uses them at every site that divides or indexes by them: the up/down-and-up guard (line 446), the modulo (452), the bounce (457), the repeat division (467), the cycle modulo (473) and the downward inversion (479). With whole numbers, range is an exact multiple of repeats, so `range / repeats >= 1` and the downward index stays in `[0, slots - 1]`.
- **Behaviour change, stated.** For whole-number values the arithmetic is identical to before. Only fractional values change: they now play as the whole number below them (1.5 plays as 1). Before, they crashed or produced negative indices. Fractional `arpRange` is floored by the same change, because the range has to be an exact multiple of repeats for the slot count to be right. Loading is unchanged: the model still stores the saved value.
- **Constraints.** No allocation, lock, log or wait is added to the audio path; the change is integer casts. Undo and control commands are untouched (no new command, no snapshot change).

## Regression tests (fork-new file `tests/src/core/NotePlaybackTest.cpp`, `fork-sources.txt:2475`)

Placed in `NotePlaybackTest` rather than `NoteCurvesAcrossTest`: the latter covers note pitch curves, not the arpeggio. `NotePlaybackTest` is the file that already covers arpeggio playback (`upDownArpeggioHandlesRepeatedSinglePitch`).

- `cycledArpeggioWithFractionalRepeatsDoesNotDivideByZero`: up, cycle 1, `arprepeats="1.5"`, one-note chord. RED: SIGFPE in the cycle modulo.
- `downArpeggioWithFractionalRepeatsStaysOnTheChord`: down, `arprepeats="1.5"`, no cycle. RED: assertion fails at `tests/src/core/NotePlaybackTest.cpp:584` (keys not all 60).
- Deterministic: no skip, miss or random direction; the existing `playFrom` helper renders three audio periods.

Note: the Up-and-down branch's `range > repeats` guard (line 446) predates this change (`new-p10-172`); it is unchanged.

## Commands and EXIT codes (logs in the run folder `logs/`)

| step | command | EXIT | log |
|---|---|---|---|
| build (ccache failed to write its temp file: sandbox path, not the code) | `cmake --build build --target zene NotePlaybackTest -j4` | 2 | build-1-red.log |
| build before fix | `env CCACHE_DISABLE=1 cmake --build build --target zene NotePlaybackTest -j4` | 0 | build-2-red.log |
| **RED**, whole family | `cd build/tests && ctest -R '^NotePlaybackTest$' --output-on-failure` | 8 | test-1-red.log: `Received signal 8 (SIGFPE), code FPE_INTDIV`, backtrace in `cycledArpeggioWithFractionalRepeatsDoesNotDivideByZero` |
| **RED**, down test alone | `./NotePlaybackTest downArpeggioWithFractionalRepeatsStaysOnTheChord` | 1 | test-2-red-down.log: `FAIL!` at NotePlaybackTest.cpp:584 |
| mutation: reverse fix | `git diff -- src/core/InstrumentFunctions.cpp > logs/fix.patch; git apply -R logs/fix.patch` | 0; 0 | — |
| mutation: rebuild | `env CCACHE_DISABLE=1 cmake --build build --target zene NotePlaybackTest -j4` | 0 | build-4-mutant.log |
| mutation: **RED** | `cd build/tests && ctest -R '^NotePlaybackTest$' --output-on-failure` | 8 | test-4-mutant.log: SIGFPE in the cycle test |
| re-apply fix | `git apply logs/fix.patch` | 0 | — |
| rebuild | `env CCACHE_DISABLE=1 cmake --build build --target zene NotePlaybackTest -j4` | 0 | build-5-fixed-again.log |
| **GREEN**, whole family | `cd build/tests && ctest -R '^NotePlaybackTest$' --output-on-failure` | 8 | test-5-fixed-again.log: both new tests `PASS`; 29 PASS, 2 FAIL (below) |
| affected family | `ctest -R '^ArticulationPlaybackTest$' --output-on-failure` (arpeggio at `arprepeats 1`, whole-number path) | 0 | test-6-articulation.log: 100% (1 test) |

Affected-family count: `NotePlaybackTest` executed 31 QTest functions: 29 PASS (including init and cleanup) and 2 FAIL.

**Pre-existing failures, not caused by this change:** `monstroNoteStartDoesNotAllocate` and `nesNoteStartDoesNotAllocate` fail on the unmodified product code too. They failed in the first run (test-1-red.log), before any product edit. Both report `Actual nodeName() "dummyinstrument"` against `monstro` / `nes` (NotePlaybackTest.cpp:408, :432): the plugin is not loaded in this environment. `LMMS_PLUGIN_DIR` was not set, as the rules require.

## Gates (run from the worktree with each gate's own command)

`python3 .../feature_check.py <worktree>` could not run: it creates its log folder `<worktree>/../zene-qwen-new-p15-78-feature-check`, outside the write-jail (PermissionError, EXIT 1). I ran each row's command myself, with output in the run folder.

| row | command | EXIT | result |
|---|---|---|---|
| fork-sources | `bash tests/fork-sources-gate.sh` | 0 | PASS (rerun after registration: 0) |
| all-sources-reproduce | `bash tests/all-sources-reproduce.sh` | 0 | PASS |
| no-upstream-regression | `bash tests/no-upstream-regression-gate.sh` | 0 | PASS (rerun after the new `upstream-modifications.txt` line: 0) |
| check-namespace | `tests/scripted/check-namespace` | 0 | PASS |
| no-tautology | `bash tests/no-tautology-gate.sh` | 0 | PASS |
| doc-figures | `python3 tests/doc-figures-gate.py` | 0 | PASS |
| complexity | `bash tests/complexity-gate.sh --check` | 1 | 7 regressions, none in a file I changed (WasmSandbox, XyPadPairTest, DawProjectReadTracks, ControlCommandsMixerTools, ControlCommandsSessionRecordLand, ControlCommandsMixerStrip, ControlCommandsShortcuts). `processNote` keeps baseline CCN 36 (`tests/complexity-baseline-all.tsv:34`): no branch added. |
| file-length | `bash tests/file-length-gate.sh --check` | 1 | 19 regressions; only one is in a changed file: `NEW file over 500 lines: tests/src/core/NotePlaybackTest.cpp (807)`. The gate counts code lines (`code_lines`, `tests/file-length-gate.sh:106-137`): the base file counts 768 with the same function, so it was already over the limit and not in the baseline. The change adds 39 code lines. Not new in this change. |

## Registration

- `src/core/InstrumentFunctions.cpp` is inherited: a line was appended to `tests/upstream-modifications.txt` with the reason and both regressions.
- `tests/src/core/NotePlaybackTest.cpp` is fork-new (`tests/fork-sources.txt:2475`): no upstream entry needed.
- No header changed, so no other target needed a rebuild. `ArticulationPlaybackTest` (the other test that drives the arpeggio) was rebuilt and passes.

## Self-check

- **Verdict is FIXED,** so the ALREADY-FIXED / FALSE-POSITIVE evidence item does not apply. The finding's stated trigger (repeats above range) is shown unreachable above (`src/core/InstrumentFunctions.cpp:378-380`, `306-307`, `42`), and the real path is evidenced by the RED run.
- **Regression fails without the fix and passes with it:** the mutation rows in the table. Reverted: `ctest` EXIT 8 with SIGFPE in `cycledArpeggioWithFractionalRepeatsDoesNotDivideByZero`; the down test alone fails its assertion on the base code (EXIT 1). Re-applied: both new tests `PASS` (`test-5-fixed-again.log`).
- **Deterministic:** no clock, sleep, thread or race. The inputs are fixed XML attributes (`arprepeats` "1.5", `arpcycle`, `arpdir`, `arprange`, `arptime`); skip and miss stay at 0, so the random draws (`fastRandInc`) are never reached; the audio is rendered by the existing `playFrom` helper for a fixed number of periods.
- **Fix at the cause and complete:** every read of `m_arpRangeModel` and `m_arpRepeatsModel` in `processNote` is now the whole-number local (`rg` shows the only reads at `src/core/InstrumentFunctions.cpp:380-381`; the others are initialisation and save/load at lines 306-307 and 535-554, untouched). Every divide and index site is covered (lines 446, 452, 457, 467, 473, 479), including the Sort-mode range (`singleNoteRange * cnphv.size()`, still at least 1) and the Random direction (no slot arithmetic). The divisor `range / repeats` is at least 1 for every mode.
- **No collateral damage:** saving and loading (lines 535-554) are unchanged, so the model stores and reloads the value exactly as before. Whole-number values compute identically; `ArticulationPlaybackTest` (arpeggio at `arprepeats 1`) and the existing `upDownArpeggioHandlesRepeatedSinglePitch` pass. No limit or check was added. The only behaviour change is for fractional values, stated above.
- **Gate row not passed here:** the `gates (feature_check.py)` row is FAIL because `feature_check.py` cannot create its log folder outside the write-jail (PermissionError, see the Gates section). Each of its rows was run by hand above with EXIT 0 except the two ratchets (complexity, file-length), which are red on the train and have no new items in changed files.
