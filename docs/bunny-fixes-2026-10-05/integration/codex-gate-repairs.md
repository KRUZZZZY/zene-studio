# Integration gate repairs — 2026-10-05

Baseline: `bugs/bunny-integration-2026-10-05` at `7eb51f44e`, Linux/GCC/Qt6.

The first full `env -u LMMS_PLUGIN_DIR bash tests/run-all-gates.sh` completed without interruption,
EXIT=1: ctest 401/402, only NoteToolsTest segfaulted. Checked coverage 806/809 passed (three listed
exceptions); gates 3, 4, 5, 7, 8, 10, 12, 13, 15 passed. Gate 5 killed 22/25 valid mutants (88%);
RoutingGraph.cpp was restored. Gate 2 (coverage) was not run. Gates 6, 9, 11 and 14 failed.
Full original output: `build/gates-codex-2026-10-05.log`.

## NoteToolsTest fixture lifetime

`aSplitIsRebuiltAfterItsSourceTrackIsRemovedAndUndone` removed the fixture's shared `m_track`.
Undo rebuilds that track as a new object; the old pointer survives in the fixture and later cases
dereference it. The full-gate stack names `clipWith` from the note-removal inverse regression.
A plain rerun happened to pass (allocator reuse), so passing alone is not evidence of safety.

Added a QPointer lifetime assertion for the shared track. With only that assertion and the old
source selection, targeted rebuild passed, then ctest failed (EXIT=8): the sharedTrack assertion
failed and the later case segfaulted. Evidence: `build/codex-note-tools-lifetime-red-full.log`.
The destructive case now creates its own source track; all pre-existing assertions are retained.
Rebuilt `zene NoteToolsTest -j12`; ctest from build/tests with `-R '^NoteToolsTest$'
`--output-on-failure --repeat until-fail:10` passed 10/10 (EXIT=0). After the red control, the exact
fixed source was restored and rebuilt before rechecking.

## Gate failures

- Gate 6: declare the inherited InstrumentPlayHandle header/source changes from be2130796,
  naming the existing ArrangingCatchUpTest regressions.
- Gates 6/11: preserve all twelve raw campaign logs as Markdown transcripts, with original
  output SHA-256 digests and fenced output. No evidence is discarded and no exemption is added.
- Gate 9: regenerate all-sources.txt using its own header recipe. The only change is the C-locale
  ordering of NoteEditRangeTest and NoteEditingCommandsTest; no path is removed.
- Gate 14: its nominally green HEAD control failed the same manifest reproduction check;
  commit the regenerated manifest before rerunning the release oracle harness.

Targeted gates 9 and 11 passed (EXIT=0). Gate 6 still reads base..HEAD until the renames are
committed; its remaining refusals name only the historical raw log paths. Full post-repair gates
are pending. Non-Linux platforms, coverage, and hardware/soak verification are not run here.

## Full post-repair result and landing

Full gates at `521a90156`, alone on the machine, completed normally: EXIT=3, PASS-WITH-SKIPS.
CTest: 402/402; checked-command ratchet: 806/809, all three unreached entries listed.
Gates 1 and 3–15 PASS; gate 2 coverage SKIPPED (not requested with --with-coverage).
Mutation: 22/25 valid mutants killed (88%), five invalid; final pristine control PASS and
RoutingGraph.cpp has no diff. Whole-tree gates 4/7/8 were not measured by this run.

The 0.5.0/ui-train worktree zene-ui was fast-forwarded in the prescribed order:
050/w2-lanes-2 → bugs/luna-r7-2026-10-05 → bugs/bunny-integration-2026-10-05.
Each merge exited 0; the resulting train tip is exactly the fully tested 521a90156 (no merge
resolution or new product source). Nothing pushed. The train's own build directory was not
created; the existing integration build verified this identical commit.

The Markdown transcripts beside this report preserve both full gate outputs and the red/green
fixture controls. This evidence-only follow-up changes no code, test, command, schema or gate.
