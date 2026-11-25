FIXED

# Cluster 3471: score export snapping overflowed on large note positions

## Verdict: REAL
`src/core/ScoreExport.cpp` (before the fix, line 44) snapped with `(ticks + Grid / 2) / Grid * Grid` in `int`,
and `startsOf` took `note->endPos()`, whose `pos() + length` also wraps in 32-bit ticks (`include/Note.h:344-348`).
A note ending at `INT_MAX` wrapped negative; its length collapsed to one grid step. The score was written with
the note silently missing or corrupt. The bar maths in `writeMeasures` (`end + barTicks - 1`, `bars * barTicks`) overflows too.

RED evidence: before the fix `musicXmlOf` returned a score for a note at 2147483646 for 1 tick
(`test-red-1.log`: `'musicXmlOf(...).isEmpty()' returned FALSE. (note at 2147483646 for 1)`).

## Fix
- `src/core/ScoreExport.cpp`: `snapped` and the note end (`pos + length`) are computed in `int64`. `barTicksOf` computes the bar length in `int64`.
  `startsOf` returns `std::nullopt` when a snapped start or end is past `lastScoreTick(barTicks)` (`INT_MAX - 2 * barTicks`, which keeps the bar maths inside `int`). `musicXmlOf` returns an empty string, never a partial score, in that case.
- `src/core/ControlCommandsScoreExport.cpp`: `clip.export_musicxml` turns the empty score into a `Refused` failure and writes no file. No new command.
- `include/ScoreExport.h`: contract comment only.
- Nothing is rejected that fit before: every snapped value below the limit produces the same score as before. Only values within two bars of `INT_MAX` are refused (they previously overflowed).

Regression test: `ArrangeToolsTest::aScoreRefusesTicksTooLargeToSnap` (`tests/src/core/ArrangeToolsTestParts1.h`, declared in `ArrangeToolsTest.cpp`). Three notes near `INT_MAX` (end wrapping or touching the top); each must give an empty score and a `Refused` command with no file. Deterministic, no timing.

## Commands (from logs)
- RED build: `cmake --build build --target ArrangeToolsTest -j4 > logs/build-1.log` EXIT=0
- RED test: `cd build/tests && ctest -R ArrangeToolsTest --output-on-failure > logs/test-red-1.log` EXIT=8 (aScoreRefusesTicksTooLargeToSnap fails)
- GREEN: `cmake --build build --target zene ArrangeToolsTest -j4 > logs/build-2.log` EXIT=0; `ctest -R ArrangeToolsTest > logs/test-green-1.log` EXIT=0
- 4b: `git diff -- src/core/ScoreExport.cpp src/core/ControlCommandsScoreExport.cpp include/ScoreExport.h > logs/fix.patch` EXIT=0; `git apply -R logs/fix.patch` EXIT=0; rebuild (`build-3.log`) EXIT=0; `ctest -R ArrangeToolsTest > logs/test-red-2.log` EXIT=8 (1 failed of 27: aScoreRefusesTicksTooLargeToSnap, same message); `git apply logs/fix.patch` EXIT=0; rebuild (`build-4.log`) EXIT=0; `ctest -R ArrangeToolsTest > logs/test-green-2.log` EXIT=0
- Gates: `feature_check.py <worktree> > logs/gates-1.log` EXIT=0, all PASS (fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures, complexity, file-length).

Realtime/undo: export is a UI/control-thread file write; nothing on the audio thread changed. No state change, no undo.
Only the `ArrangeToolsTest` family was run, not the full suite. No TSan (not available locally).

## Other findings
- A note ending a little under the limit (for example 2 billion ticks) is still accepted and writes about 10 million `<measure>` elements, which is slow and memory-hungry. This is old behaviour, and a cap such as `MaxSongLength` is a policy decision, so I left it.
- `Note::endPos()` (`include/Note.h:344`) wraps for positions near `INT_MAX`; other callers of it were not audited.

Delegation: Haiku ran the red build and test, then the green, revert-proof and re-apply runs, and reported exit codes and failing slots. I read the logs' summary lines via its report and wrote the code, the test and this report myself.

## Self-check
- Red/green with the fix reverted: with `fix.patch` reverse-applied the slot failed (`ctest` EXIT=8, `test-red-2.log`); with it applied it passed (EXIT=0, `test-green-2.log`).
- Deterministic: the test uses fixed tick values (`INT_MAX - 1`, `INT_MAX - 10`, `INT_MAX`) with an in-memory clip and a `QTemporaryDir`. No clocks, sleeps, threads or randomness.
- Complete: the only callers of `musicXmlOf` are `src/core/ControlCommandsScoreExport.cpp` (now handles the refusal) and two existing tests in `ArrangeToolsTestParts1.h` (unchanged, still pass). Every int tick computation in the file is now bounded: `snapped`, the note end, the bar length, and the `end + barTicks - 1` / `bars * barTicks` maths in `writeMeasures`, because `lastScoreTick` caps the end at `INT_MAX - 2 * barTicks`.
- No collateral damage: this is an export that writes a file. It never reads or changes saved data or the project. Every score whose snapped ends are within the limit is byte-identical to before. The limit only affects values that overflowed `int`.
- Not ALREADY-FIXED / FALSE-POSITIVE: the verdict is FIXED (see the red test output above).
- Sibling commands: `src/core/ControlCommandsScoreExport.cpp` registers only `clip.export_musicxml` (its definition is at the end of the file). The `app.*` and `arrangement.*` commands listed by the check are in other files and are not touched or affected. I added no command, so the ControlCommandsSnapshot canary does not move.
