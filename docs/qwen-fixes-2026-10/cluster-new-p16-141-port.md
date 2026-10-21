FIXED

new-p16-141-port: FIXED

## Verdict

REAL. The bug is present on this branch (the newest line) and is fixed by re-applying
the accepted fix from `bugs/qwen-new-p16-141` by hand (no merge, no cherry-pick, no rebase).

This is a PORT job: the original fix was accepted as `bugs/qwen-new-p16-141`
(commit `a0b69ec77`, "Fix Note.cpp batch: new-p16-141, new-p18-66, new-p16-140") but could
not go on the newest train because of a merge conflict in `src/gui/editors/PianoRoll.cpp`.
That conflict came from the *other two* clusters carried in the same batch commit
(`new-p18-66` and `new-p16-140`); the `new-p16-141` half of the batch touches only
`src/core/Note.cpp` and `tests/src/core/NoteTransformTest.cpp`, so it ports cleanly and
`src/gui/editors/PianoRoll.cpp` is NOT touched by this port.

Only the `new-p16-141` half of the batch commit is ported here. The `new-p18-66`
(`relvel` / MPE attribute parsing) and `new-p16-140` (exact MIDI release velocity)
halves are different clusters and are out of scope for this port.

## Evidence (REAL)

Bug location in this tree: `src/core/Note.cpp:482` `Note::quantized()`.

Pre-fix body (read from `git show a0b69ec77` and confirmed present in this tree):

```cpp
float p = ( (float) m / qGrid );
if( p - floorf( p ) < 0.5f )
{
    return static_cast<int>( p ) * qGrid;
}
return static_cast<int>( p + 1 ) * qGrid;
```

Why it is wrong: a `float` has 24 bits of integer precision, so `TimePos( 16777217 )`
(2^24 + 1) becomes `16777216.0f` *before* the rounding decision is taken. With a grid of
1 the note is therefore moved from tick 16,777,217 to 16,777,216 - a grid of 1 must
leave the tick unchanged. Near the `tick_t` limit the same float path also converts an
out-of-range value back to `int` (`tick_t` is `std::int32_t`, see `include/LmmsTypes.h:35`),
which is undefined behaviour and returns a garbage/negative position.

Fix (this tree, `src/core/Note.cpp:482`): snap with signed 64-bit quotient/remainder
arithmetic and keep the old rounding rule exactly - the floor remainder decides the
direction, the low side truncates toward zero (as `static_cast<int>(p)` did for a
negative `p`), the high side is `trunc((ticks + grid) / grid)`. A round-up that would
leave `tick_t` saturates at the type's limit instead of converting an out-of-range value.

### Callers and siblings checked

`Note::quantized()` is the single snapping point; every caller benefits and none needed
a change:

- `src/core/Note.cpp:505` `Note::quantizeLength()` and `src/core/Note.cpp:521`
  `Note::quantizePos()` - the note's own path (covered by the regression).
- `src/core/AutomationClip.cpp:322`, `:378`, `:676` - automation node quantising.
- `src/gui/editors/AutomationEditor.cpp:311`, `:312`, `:582` - the editor's grid.
- `src/gui/editors/PianoRoll.cpp:2882`, `:2898`, `:2904`, `:3304`, `:4935` - the piano roll.

No signature or declaration changed, so no header changed and no other target needed a
rebuild beyond the library.

Sibling that is NOT the same thing and was deliberately left alone:
`TimePos::quantize( float bars, bool )` (`include/TimePos.h`) quantises in *bars* with a
float argument - a different contract, not the tick snapping this finding is about.
`NoteTransform::quantizePositions()` (`src/core/NoteTransform.cpp:215`) has its own
Floor/Ceil/Nearest integer logic and never used the float path.

### Collateral

Behaviour outside the bug is unchanged: for every tick below 2^24 the new arithmetic
returns exactly what the float path returned (checked by hand for positive, negative and
exact-half remainders, and by the pre-existing `NoteTransformTest` cases). No file format,
attribute or saved data changes, so old projects load exactly as before. No new control
command, no new undo step, and nothing on the audio thread changed (this is a pure
arithmetic function called from control-thread code).

## Regression test

`tests/src/core/NoteTransformTest.cpp` ->
`NoteTransformTest::quantizeKeepsTickPrecisionPastTheFloatMantissa`

It pins: `quantized( 16777217, 1 ) == 16777217`, `quantized( 16777217, 2 ) == 16777218`,
the same tick through `Note::quantizePos( 1 )`, and the two `tick_t` limit cases
(`tick_t::max()` with grids 1 and 2 saturate instead of leaving the type's range).
The test is deterministic - no clock, no sleep, no race.

## Commands and exit codes

Log folder:
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p16-141-port.r1/logs`

| step | command | exit | log |
|---|---|---|---|
| RED (test written first, product code untouched) | `cmake --build build --target NoteTransformTest` | 0 | `build-test-red-1.log` |
| RED | `cd build/tests && ctest -R '^NoteTransformTest$' --output-on-failure` | **8** | `test-red-1.log` |
| GREEN (after the fix) | `cmake --build build --target zene NoteTransformTest` | 0 | `build-green-1.log` |
| GREEN | `cd build/tests && ctest -R '^NoteTransformTest$' --output-on-failure` | **0** | `test-green-1.log` |
| affected family | `cd build/tests && ctest -R 'Note\|Automation\|PianoRoll' --output-on-failure` | 8 | `test-family-green-2.log` |
| family build | `cmake --build build --target <43 family targets>` | 2 (see note) | `build-family-1.log` |
| gates | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py .` | 1 | `feature-check-1.log` |
| gates (after the manifest re-derivation below) | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py .` | 0 | `feature-check-2.log` |

RED failing assertion (`test-red-1.log`):

```
FAIL!  : NoteTransformTest::quantizeKeepsTickPrecisionPastTheFloatMantissa() Compared values are not the same
   Actual   (Note::quantized( TimePos( aboveMantissa ), 1 ).getTicks()): 16777216
   Expected (aboveMantissa)                                            : 16777217
Totals: 14 passed, 1 failed, 0 skipped, 0 blacklisted, 2ms
```

### Step 4b - proof that the test catches the bug

```
git diff -- src/core/Note.cpp > .../logs/fix.patch          # exit 0
git apply -R .../logs/fix.patch                              # exit 0
cmake --build build --target zene NoteTransformTest NotePlaybackTest PianoRollBenchmarkTest   # exit 0  (build-revert-1.log)
cd build/tests && ctest -R '^(NoteTransformTest|NotePlaybackTest|PianoRollBenchmarkTest)$' --output-on-failure   # exit 8  (test-revert-1.log)
git apply .../logs/fix.patch                                 # exit 0
cmake --build build --target zene NoteTransformTest NotePlaybackTest PianoRollBenchmarkTest   # exit 0  (build-reapply-1.log)
cd build/tests && ctest -R '^(NoteTransformTest|NotePlaybackTest|PianoRollBenchmarkTest)$' --output-on-failure   # exit 8 (test-reapply-1.log) - NoteTransformTest PASSES
```

Without the fix the regression FAILS (`Actual 16777216`, `Expected 16777217`,
`test-revert-1.log`); with the fix it PASSES (`test-reapply-1.log`).

### Affected-family run

`ctest -R 'Note|Automation|PianoRoll'` -> **43 tests, 41 passed, 2 failed**
(`test-family-green-2.log`). The two failures are NOT caused by this change - they fail
identically with the product fix reverted (`test-revert-1.log`):

- `NotePlaybackTest::monstroNoteStartDoesNotAllocate` / `::nesNoteStartDoesNotAllocate`:
  the "monstro" and "nes" plugins are not built/loadable in this checkout
  (`"The plugin \"monstro\" wasn't found or could not be loaded!"`), so the instrument is
  the dummy one. Environmental.
- `PianoRollBenchmarkTest::frameTimeAtFourSizes`: a GUI frame-time comparison between the
  note editor and the classic roll. It failed in the family run and in the reverted run
  and passed in the re-applied run - a flaky timing benchmark, not a behaviour change.

### Gates

First run: `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py .` -> exit **1**
(`feature-check-1.log`):

| gate | result | exit |
|---|---|---|
| fork-sources | FAIL | 1 |
| all-sources-reproduce | FAIL | 1 |
| no-upstream-regression | PASS | 0 |
| check-namespace | PASS | 0 |
| no-tautology | PASS | 0 |
| doc-figures | PASS | 0 |
| complexity | PASS | no new item in your files |
| file-length | PASS | no new item in your files |

The two FAILs were **pre-existing on this branch and unrelated to this port**: both report
that `tests/all-sources.txt` lists `plugins/SfzPlayer/sfizz/**` paths (1,151 of them) that
the manifest's own "Regenerate with" recipe excludes
(`grep -vE '...plugins/SfzPlayer/sfizz/'`), i.e. vendored-tree drift on the newest line.
Proof that this change is not the cause: with this port's three files temporarily restored
to their HEAD contents (so the working tree was pristine), the same two gates failed the
same way - `bash tests/all-sources-reproduce.sh` exit **1**
(`gate-all-sources-pristine.log`) and `bash tests/fork-sources-gate.sh` exit **1**
(`gate-fork-sources-pristine.log`), with the diff naming only `sfizz` paths and none of
this port's files.

The repair the gate itself prescribes ("Fix by re-deriving the list: run the 'Regenerate
with' command from the manifest's own header and write its output over the entry list
(never hand-edit, never delete a path the recipe derives)") was applied: `tests/all-sources.txt`
now holds the byte-exact output of its own recipe, so the vendored sfizz tree (which the
recipe excludes on purpose - it is not this repo's code) is no longer measured by the
whole-tree scope, and the sources added on this newer line are listed. No other manifest
was touched. After that:

```
bash tests/all-sources-reproduce.sh                 # exit 0 - REPRODUCES (gate-all-sources-after-fix.log)
bash tests/fork-sources-gate.sh                    # exit 0 - PASS (gate-fork-sources-after-fix.log)
python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py .   # exit 0 (feature-check-2.log)
```

The registration gate for this change (`no-upstream-regression`) PASSED in both runs:
`src/core/Note.cpp` and `tests/src/core/NoteTransformTest.cpp` are declared in
`tests/upstream-modifications.txt` with a reason.

## Files committed

- `src/core/Note.cpp` - `Note::quantized()` snaps with 64-bit quotient/remainder
  arithmetic; a round-up past `tick_t` saturates. (Product fix.)
- `tests/src/core/NoteTransformTest.cpp` - the regression
  `quantizeKeepsTickPrecisionPastTheFloatMantissa`.
- `tests/upstream-modifications.txt` - registration of the two inherited files above.
- `tests/all-sources.txt` - the whole-tree scope manifest re-derived from its own header
  recipe (the gate's prescribed repair for the pre-existing vendored-sfizz drift that made
  `fork-sources` / `all-sources-reproduce` red on this branch). No entry was hand-edited;
  the entry list is now the byte-exact output of the recipe in the manifest's own header.
- `docs/qwen-fixes-2026-10/cluster-new-p16-141-port.md` - this report.

## Self-check

- **Is the finding real on this branch?** Yes - the float body of `Note::quantized()` was
  present at `src/core/Note.cpp:482` before this change and the RED run failed with the
  exact value from the finding (`16777216` instead of `16777217`).
- **Is the fix at the cause?** Yes - the cause is the float conversion of an integer
  tick. The fix removes the float entirely from the snapping path; nothing clamps or
  filters around it.
- **Are all callers consistent?** Yes - the single snapping function is fixed, so the
  note path, the automation clip path, the automation editor and the piano roll all get
  the same corrected arithmetic. No caller needed a change and no caller was left on the
  old path.
- **Collateral damage?** None - no file format, no attribute, no saved data, no command,
  no undo step. Below 2^24 the arithmetic returns the same answers as the float path.
- **Realtime / undo / commands?** Not touched: `Note::quantized()` is pure arithmetic
  called from control-thread code; no allocation, locking, waiting or logging was added,
  and no new control command was introduced.
- **New files?** None - no new product or test file, so no `tests/fork-sources.txt`
  registration was needed; the two inherited files changed are registered in
  `tests/upstream-modifications.txt` (the file ends with a newline).
- **Headers?** Unchanged - the declaration in `include/Note.h` is untouched.
- **Why did `tests/all-sources.txt` change?** Because `fork-sources` and
  `all-sources-reproduce` were red before this port (proved on a pristine working tree,
  see the Gates section) and the gate names the repair itself: re-derive the entry list
  from the manifest's own header recipe. The re-derivation is mechanical, reproducible and
  touches no product code; it only makes the manifest agree with the recipe it publishes,
  which drops the vendored sfizz tree (excluded on purpose - it is not this repo's code)
  from the whole-tree measurement scope and adds the sources added on this newer line.
