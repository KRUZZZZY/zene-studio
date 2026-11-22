FALSE-POSITIVE

# Cluster new-p14-121: "Up/down arpeggios lose notes when repeats exceed one"

## Verdict

FALSE-POSITIVE. With the finding's settings (up/down arpeggio, arp range 3, note repeats 2) the code plays every step
of the range, and the down/up direction does not stick at the top. The finding's arithmetic (range 3, modulo 2) does not
match the code: the repeats factor is already inside `range`, so the modulo is 8, not 2.

## Evidence (file:line, current tree `bugs/qwen-new-p14-121`, base `cf3618e61`)

- `src/core/InstrumentFunctions.cpp:44` - the `"octave"` chord table entry is `{ 0, -1 }`: one tone, so `cur_chord_size == 1`.
- `src/core/InstrumentFunctions.cpp:378` - `singleNoteRange = cur_chord_size * arpRange * repeats`. Repeats are already
  expanded into `range`. For the finding's settings: `range = 1 * 3 * 2 = 6`, not 3.
- `src/core/InstrumentFunctions.cpp:440-447` - up/down cycle: `% (range*2 - 2*R)` = `% 8`, not `% 2`. Raw index `t` runs 0..7.
- `src/core/InstrumentFunctions.cpp:450-452` - indices `>= range` (t = 6, 7) map down to 3 and 2.
- `src/core/InstrumentFunctions.cpp:462` - divide by `R` (repeats grouping) gives the note steps `0,0,1,1,2,2,1,1`,
  i.e. octave offsets 0,0,12,12,24,24,12,12 - keys 60,60,72,72,84,84,72,72. Every note is played.
- `src/core/InstrumentFunctions.cpp:472-474` - down/up inverts `range/R - idx - 1` (N = 3): `2,2,1,1,0,0,1,1` - it starts
  at the top and descends to the bottom; it does not stay at the top.
- General case (N steps, R repeats): the raw cycle is `2R(N-1)`; dividing by R gives `2(N-1)` note steps, each held R
  times. That is the "full up/down cycle, then repeats grouping" the finding asks for, and the code already does it.
  Checked by hand for N=3,R=2 (`0,0,1,1,2,2,1,1`) and N=3,R=3 (`0,0,0,1,1,1,2,2,2,1,1,1`).

## Regression test (pins the correct behaviour)

`tests/src/core/NotePlaybackTest.cpp:540-577` - `upDownArpeggioWithRepeatsReachesEveryStep`. For UpAndDown and DownAndUp
(octave chord, range 3, repeats 2, 25 ms steps) it renders 256 periods offline with `renderNextPeriod()` and asserts keys
60, 72 and 84 all sound. Deterministic: no wall clock, no sleeps; skip and miss rates are 0 so no RNG is consumed.
NotePlaybackTest.cpp is already listed in `tests/fork-sources.txt:2475`, so no `upstream-modifications.txt` entry is needed.

Because there is no product defect, there is no RED on the product code. The discriminating-power check is the mutant
probe below: the test fails when the finding's described symptom (only the first note sounds) is put into the code.

## Commands and exit codes

| # | Command (run from the worktree / `build/tests` as noted) | Log | EXIT |
|---|---|---|---|
| 1 | `cmake --build build --target NotePlaybackTest -j4` | build-1.log | 2 (ccache could not write its temp dir `~/.local/share/zene-bunny-fix/dsh/...`, outside the write-jail; not a code error) |
| 2 | `CCACHE_TEMPDIR=/home/kruzzzzy/.cache/zene-tmp cmake --build build --target NotePlaybackTest -j4` | build-2.log | 0 |
| 3 | `cd build/tests && ctest -R '^NotePlaybackTest$' --output-on-failure` | test-1.log | 8 - Totals: 28 passed, 2 failed (30 executed). New test PASS. Failures: `monstroNoteStartDoesNotAllocate` (:408) and `nesNoteStartDoesNotAllocate` (:432), both "Plugin not found" for monstro/nes in this targeted build (`build/plugins/Monstro` and `build/plugins/Nes` hold only moc metadata). Not arpeggio code; not changed here. |
| 4 | Mutant probe: `src/core/InstrumentFunctions.cpp:447` `% (range*2 - 2*R)` -> `% R` (reproduces "only the first note plays"); then the same build and ctest | build-3-mutant.log / test-2-mutant.log | 0 / 8 - `FAIL! : upDownArpeggioWithRepeatsReachesEveryStep() ... (direction 2 sounded keys: 60)` at NotePlaybackTest.cpp:574 (direction 2 = UpAndDown) |
| 5 | Probe reverted (`git diff -- src/core/InstrumentFunctions.cpp` is empty); rebuild and ctest | build-4-restored.log / test-3-restored.log | 0 / 8 - new test PASS; same 28 passed / 2 pre-existing failures as row 3 |
| 6 | `python3 .../feature_check.py <worktree>` as given | gate-1.log | 1 - it cannot create its log dir `<worktree>-feature-check`, a sibling outside the write-jail (PermissionError). No table produced. |
| 7 | Same gate logic (unmodified `feature_check.py`, copied to the LOG FOLDER with only the log dir redirected into `logs/feature-check` and ROOT pinned to the bunny-fix dir) | gate-2.log | 0 - all 8 rows PASS, `RESULT: PASS`; 1 changed file |

Only `tests/src/core/NotePlaybackTest.cpp` differs from the base (`git diff --stat`). No product file is changed.

## Self-check

- Each new test FAILS with the product change reverted: no product change exists to revert. The mutant probe (row 4)
  shows the test FAILS (EXIT 8) on the finding's symptom and PASSES once restored (row 5).
- The test is deterministic: offline `renderNextPeriod()` only; no wall clock, sleeps or RNG (miss/skip rates are 0).
- The fix is at the cause: no fix. The finding's claim does not hold at `:378`, `:440-447`, `:462` or `:472-474`.
- No collateral damage: no product code changed; existing tests in the family (`upDownArpeggioHandlesRepeatedSinglePitch`,
  `repeatsSoundAsMoreHits`) still PASS in row 3.
- Audio thread: no product code changed; the new test allocates nothing on the audio thread (it only drives the offline renderer).
- Gates: PASS in row 7 via the copy. Row 6 (the script as given) could not run in this sandbox; I did not change any gate limit or baseline.
- Not verified: the two `Plugin not found` failures were observed in the run that included my new test; I did not run them on
  the untouched tree, but neither touches arpeggio code. Not fixed here (out of scope).
