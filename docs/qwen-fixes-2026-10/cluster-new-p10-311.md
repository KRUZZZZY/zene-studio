FIXED

# Cluster new-p10-311: zero-length MIDI clip with notes crashes pattern preview painting

## Finding

Luna BUG 3: `PatternClipView::paintEvent` divides by `mClip->length()` for the repetition count without checking it. A MIDI clip in a pattern track with notes and zero length makes that a divide by zero.

## Evidence (file:line, current tree)

- Division site, before the fix: `src/gui/clips/PatternClipView.cpp:179` (`getClip()->length() / mClip->length() + 1 + 1`). `TimePos` converts implicitly to `int` (`include/TimePos.h`, `operator int()`), so this is an integer division by a `TimePos` value of 0.
- The painter reaches the division only when the MIDI clip has notes (`PatternClipView.cpp:171-176` skips clips with no notes).
- The zero length is representable in memory: `Clip::changeLength` is public and unguarded (`include/Clip.h:233`); `Clip` starts at length 0 (`src/core/Clip.cpp:77`).
- The crash reproduces: the RED run dies with `SIGFPE, FPE_INTDIV` in `PatternClipView::paintEvent` at `idivl` (`logs/test-1-red.log`, frame `#5`).

### Reachability (by reading the code; the finding's trigger was not reproduced)

The finding says a malformed project triggers it. I did not find that path:

- Project load: `src/tracks/MidiClip.cpp:557` calls `updateLength()` when the saved `len` is `<= 0`, and `:561` applies a positive `len`. `updateLength()` gives at least one bar while auto-resize is on, and auto-resize defaults to `true` (`include/Clip.h:380`). A zero-length MIDI clip therefore does not come out of the loader. I did not run a malformed project through the loader.
- Live paths that set a length all clamp to at least 1: `clip.resize` schema minimum 1 (`src/core/ControlCommandsClip.cpp:248`); script `setLengthTicks` uses `std::max(1, ticks)` (`src/core/ScriptBindings.cpp:797`); retro-capture length `clipLengthFor` returns the request only when `> 0`, else at least one bar (`src/core/ControlCommandsMidi.cpp:330-338`); left-edge resize requires `>= 1` before `changeLength` (`src/gui/clips/ClipView.cpp:1013`, call at `:1016`); split positions are rejected at the clip ends (`src/core/ArrangementEdits.cpp:70-71`).
- I did not audit the GUI MIDI split at `src/gui/clips/MidiClipView.cpp:1070-1084` beyond its shown lines.

So the state is real and the painter crashes on it, but I found no user path that reaches it. The fix is a defensive guard at the cause; it does not claim a user-visible trigger.

## Fix

`src/gui/clips/PatternClipView.cpp:178-181`:

```cpp
// A clip of zero length has no period to repeat, so it has no full repetitions (not a division by zero).
const int fullRepetitions = mClip->length() > 0 ? getClip()->length() / mClip->length() : 0;
const int maxPossibleRepetions = fullRepetitions + 1 + 1;
```

- For `mClip->length() > 0` the value is the same quotient as before, so behaviour is unchanged.
- For a zero-length clip, the count is the same as a clip longer than the view: notes are drawn at the base copy and the previous-bar slot (`i = -1..0`), and no division happens.
- The contract for "what to draw" is my choice; the finding only asked for the check. A zero-length clip playing nothing would be a separate product decision and is not made here.

## Regression test

`tests/src/gui/ArrangerTimelineTest.cpp:545-600`, slot `aZeroLengthMidiClipWithNotesPreviewsWithoutDividingByItsLength`, in the existing offscreen pattern-preview fixture:

- sets 64 px per bar, adds a note to the pattern's MIDI clip, disables auto-resize and sets the clip length to 0 with `changeLength(TimePos(0))`;
- creates the pattern clip, renders its `PatternClipView` into a 128x40 image, and asserts a note-coloured pixel is painted.

Determinism: no timers, sleeps or threads. The only inputs are the zoom value and the note and length values set in the test; the pixel scan is a fixed loop. The assertion is that the render completes and paints a note; it does not compare exact pixel positions.

## Commands and exit codes

Each command is run from the worktree unless stated. Logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-311.r1/logs/`.

| Step | Command | EXIT | Result |
| --- | --- | --- | --- |
| Build, first try | `cmake --build build --target zene ArrangerTimelineTest -j4 > build-1-red.log 2>&1; echo EXIT=$?` | 2 | Failed in ccache (`failed to create temporary file ... Permission denied`, outside the jail), not in the compiler. Environment issue. |
| Build, RED | `CCACHE_DISABLE=1 cmake --build build --target zene ArrangerTimelineTest -j4 > build-2-red.log 2>&1` | 0 | Built, tree without fix (test added only). |
| RED test | `cd build/tests && ctest -R '^ArrangerTimelineTest$' --output-on-failure > test-1-red.log 2>&1` | 8 | `Received signal 8 (SIGFPE), code FPE_INTDIV` in `aZeroLength...` through `PatternClipView::paintEvent`. Earlier functions passed. |
| Build, GREEN | `CCACHE_DISABLE=1 cmake --build build --target zene ArrangerTimelineTest -j4 > build-3-green.log 2>&1` | 0 | Built with fix. |
| GREEN test | `cd build/tests && ctest -R '^ArrangerTimelineTest$' --output-on-failure > test-2-green.log 2>&1` | 0 | `100% tests passed, 0 tests failed out of 1`. |
| GREEN verbose | `cd build/tests && ctest -R '^ArrangerTimelineTest$' -V > test-3-green-verbose.log 2>&1` | 0 | `aZeroLength...` PASS; `Totals: 39 passed, 0 failed, 0 skipped`. |
| Proof: save fix | `git diff -- src/gui/clips/PatternClipView.cpp > fix.patch` | 0 | Saved product diff. |
| Proof: revert | `git apply -R fix.patch` | 0 | Fix removed. |
| Proof: build unfixed | `CCACHE_DISABLE=1 cmake --build build --target zene ArrangerTimelineTest -j4 > build-4-unfixed.log 2>&1` | 0 | Built. |
| Proof: test unfixed | `cd build/tests && ctest -R '^ArrangerTimelineTest$' -V > test-4-unfixed-red.log 2>&1` | 8 | Fails: `SIGFPE, FPE_INTDIV` inside `aZeroLength...`. |
| Proof: reapply | `git apply fix.patch` | 0 | Fix restored (`git diff --stat`: PatternClipView.cpp 3 insertions, 1 deletion). |
| Proof: build fixed | `CCACHE_DISABLE=1 cmake --build build --target zene ArrangerTimelineTest -j4 > build-5-refixed.log 2>&1` | 0 | Built. |
| Proof: test fixed | `cd build/tests && ctest -R '^ArrangerTimelineTest$' -V > test-5-refixed-green.log 2>&1` | 0 | `aZeroLength...` PASS; `Totals: 39 passed, 0 failed`. |

Affected family (`ArrangerTimelineTest`): 39 tests executed, all passing with the fix.

## Gates

`feature_check.py` (`python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>`) was run as `gate-1.log`: **EXIT=1**. The cause is `PermissionError` when the script creates its log folder `zene-qwen-new-p10-311-feature-check`, a sibling of the worktree outside the write jail. This is not a gate result. I did not create that folder or change the script. Instead I ran each check the script lists, from the worktree, with logs in the run folder:

| Check | EXIT | Log |
| --- | --- | --- |
| fork-sources | 0 | `gate2-fork-sources.log` |
| all-sources-reproduce | 0 | `gate2-all-sources-reproduce.log` |
| no-upstream-regression | 0 | `gate2-no-upstream-regression.log` |
| check-namespace | 0 | `gate2-check-namespace.log` |
| no-tautology | 0 | `gate2-no-tautology.log` |
| doc-figures | 0 | `gate2-doc-figures.log` |
| complexity (raw, ratchet) | 1 | `gate2-complexity.log` |
| file-length (raw, ratchet) | 1 | `gate2-file-length.log` |

The two ratchets fail as raw exits. Applying the script's rule (only items not in the base listing and in files this branch changed count as NEW):

- complexity: no item names `PatternClipView` or `ArrangerTimelineTest` (`gate2-complexity.log`). No new item in my files.
- file-length: `new file over 500 lines: tests/src/gui/ArrangerTimelineTest.cpp (999)` is listed. The base listing `gate-baselines/37eaf07557842a34c66bd34f6706cb0c31b0c737.json` (file-length) contains the same key, so it is not NEW. No new item in my files.

## Files changed

- `src/gui/clips/PatternClipView.cpp` (fix)
- `tests/src/gui/ArrangerTimelineTest.cpp` (regression slot)
- `tests/upstream-modifications.txt` (two registration lines, reason text for both files)
- `docs/qwen-fixes-2026-10/cluster-new-p10-311.md` (this report)

## Self-check

Answers to the completion-check items (`submit_check.py`, `logs/submit-check-1.log`).

- **Regression fails with the fix reverted and passes with it.** RED: `test-1-red.log` EXIT 8 (SIGFPE in the new slot). Proof: `test-4-unfixed-red.log` EXIT 8 with the fix reverted; `test-5-refixed-green.log` EXIT 0 with the fix reapplied. Both are in the table above.
- **Deterministic: no wall-clock timing, sleeps or races.** The test uses no timers or threads. Its inputs are the set zoom (64 px/bar), note (pos 96, length 36) and clip length (0). The pixel scan is a fixed loop over an offscreen image.
- **Fix at the cause and complete; every caller and sibling checked.** The guard sits on the only division of a MIDI clip length in `PatternClipView.cpp`. Siblings checked: `PatternClipView.cpp:121-122` uses `% ticksPerPattern` and `% pixelsPerPattern`. `pixelsPerPattern` is at least 4 px per bar because `src/gui/editors/SongEditor.cpp:81` has `MIN_PIXELS_PER_BAR = 4`, so that modulo is not zero. The float divisions at `PatternClipView.cpp:155-162` are float and guarded by ternaries. Not changed: `src/gui/clips/MidiClipView.cpp:747,752` divide floats by `m_clip->length()`. A zero length gives inf and no trap, so it is not a SIGFPE; I did not run it and it is outside this finding.
- **No collateral damage.** The guard changes only `mClip->length() <= 0`. For positive lengths the quotient is the same expression, so behaviour and painted output are unchanged. Old projects: the loader does not produce a zero length (`MidiClip.cpp:557-561`, see Reachability), and no load or save code changed. No limit was added to any other caller.
- **Verdict FIXED: the code evidence.** The verdict is FIXED, not ALREADY-FIXED or FALSE-POSITIVE: the division with an unguarded zero denominator is on this branch (`PatternClipView.cpp:179` before the fix) and the test reproduces the crash. Its user-reachability is unproven and stated above.
- **Real-time and undo.** The painter runs on the GUI thread. No audio-thread code, allocation, lock, log, undo or command was added.
- **Registration.** `tests/upstream-modifications.txt` has a `cluster new-p10-311` line for each changed inherited/test file. No new file was added, so `fork-sources.txt` is unchanged.
- **`feature_check.py` gate.** Could not run as written (see Gates); the listed checks were run by hand with the results above.
