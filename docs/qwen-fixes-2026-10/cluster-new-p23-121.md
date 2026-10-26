FALSE-POSITIVE

Cluster new-p23-121 (Luna finding "unchecked articulation index read on the audio thread", reported at `src/tracks/InstrumentTrackNotes.cpp:247`) does not hold on `bugs/qwen-new-p23-121`. The read has moved to `src/tracks/InstrumentTrackNotes.cpp:278`, and the call it makes cannot throw `std::out_of_range`. An out-of-range index already yields no articulation, which is the fix the finding asks for.

## Evidence (file:line)

1. **The read.** `src/tracks/InstrumentTrackNotes.cpp:278`: `const Articulation* articulation = m_articulations.at(note.conditions().articulation);`
2. **It is not `std::vector::at`.** `include/InstrumentTrack.h:467` declares `ArticulationMap m_articulations;`. `ArticulationMap::at` is a custom member (`include/ArticulationMap.h:64`) defined at `src/core/ArticulationMap.cpp:36-38`:
   `return index >= 1 && index <= static_cast<int>(m_entries.size()) ? &m_entries[static_cast<std::size_t>(index - 1)] : nullptr;`
   Every `int` either selects a valid element or returns `nullptr`. There is no throwing path, and the only indexing happens after the guard.
3. **The caller already falls back to no articulation.** `src/tracks/InstrumentTrackNotes.cpp:279`: `articulation != nullptr ? articulation->events() : ArticulationEvents{}`. That is the behaviour the finding proposes as its fix.
4. **Malformed project data cannot reach an unchecked index.**
   - `NoteConditions::load` clamps the saved value: `src/core/NoteConditions.cpp:60-61` (`std::clamp(..., 0, high)`), `:76` `articulation = number("art", MaxArticulations);`, with `MaxArticulations = 64` at `include/NoteConditions.h:50`.
   - A malformed track `articulations` attribute clears the map: `src/core/ArticulationMap.cpp:113` (empty attribute) and `:123` (unreadable or non-array JSON). Every index then resolves to `nullptr`.
5. **History.** The guarded `at()` was present when the method was introduced: `git show c414ab31d:src/core/ArticulationMap.cpp` lines 35-38 have the same body. `git log -S` on the guard expression returns only `c414ab31d`, so no later commit changed it. This branch has never had the unchecked read the finding describes.
6. **No other raw articulation index.** `rg` for `entries().at(`, `m_entries.at(` and `articulations().entries()[` over `src/` and `include/` finds nothing relevant (the only `.at(` hits are in `src/gui/ShortcutsPage.cpp`). Writers are bounded too: `src/core/ControlCommandsNoteEdit.cpp:85` limits the note command argument to `0..MaxArticulations`.

## Why there is no patch or regression test

- Step 2 verdict is not-real, so there is no product change. Step 4b (revert and re-apply the patch) does not apply because there is nothing to revert.
- A test of the no-throw contract would pass both before and after any change, so it is not regression proof. I did not add one.
- `tests/src/core/ArticulationPlaybackTest.cpp` does not drive an out-of-range index (checked with `rg` for `articulation\s*=`, `MaxArticulations`, out-of-range cases; none). The no-throw result therefore rests on the code above, not on an existing test.
- No build or ctest was run for this verdict; none is needed because no product file changed. If a pinning test is wanted later, it would be a characterization test (`ArticulationMap::at` returns `nullptr` for `0`, `-1` and `65`), not a regression test.

## Commands and results

| Evidence | Command | EXIT |
| --- | --- | --- |
| Source audit of the read and its types | `rg -n 'm_articulations\|ArticulationMap::at\|articulation' src/tracks/InstrumentTrackNotes.cpp include/ArticulationMap.h src/core/ArticulationMap.cpp src/core/NoteConditions.cpp > .../logs/001-source-audit.log 2>&1; echo EXIT=$? >> .../logs/001-source-audit.log` | 0 |
| Line anchors for every cited line | `rg -n 'MaxArticulations = 64\|const Articulation\* at\|ArticulationMap::at\|m_articulations;\|articulation = number' include/NoteConditions.h include/ArticulationMap.h src/core/ArticulationMap.cpp src/core/NoteConditions.cpp include/InstrumentTrack.h src/tracks/InstrumentTrackNotes.cpp` (log `003-line-anchors.log`) | 0 |
| Canonical gate, run as documented | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` (log `004-feature-check-canonical.log`) | 1: `PermissionError` creating `projects/zene-studio-program/zene-qwen-new-p23-121-feature-check`, a directory outside the write area. No gate ran. |
| Gate with relocated log directory | `python3 .../runs/new-p23-121.r1/feature_check_relocated.py <worktree>` (log `002-feature-check.log`) | 0: all eight rows PASS |

**Gate relocation, disclosed.** `feature_check_relocated.py` is a copy of the shared `feature_check.py` in my run folder. Only two lines differ (`diff` shows them): `ROOT` points at the shared `qwen/` directory, so `FIX-BASE` and the baseline cache are read from the same place, and the per-gate log directory is `runs/new-p23-121.r1/gate-logs/zene-qwen-new-p23-121` instead of a sibling of the worktree. The gate logic is unchanged. Gate rows from that run:

```
  PASS  fork-sources            exit 0
  PASS  all-sources-reproduce   exit 0
  PASS  no-upstream-regression  exit 0
  PASS  check-namespace         exit 0
  PASS  no-tautology            exit 0
  PASS  doc-figures             exit 0
  PASS  complexity              no new item in your files
  PASS  file-length             no new item in your files
RESULT: PASS
```

Files committed for this verdict: `docs/qwen-fixes-2026-10/cluster-new-p23-121.md` only. No product or test file changed.

## Self-check

- **Each new test FAILS with the product change reverted, then PASSES.** Not applicable: no product change and no new test (see "Why there is no patch or regression test").
- **Every acceptance criterion is proven by a test I ran.** Not applicable to a not-real verdict. The claim is established by reading `InstrumentTrackNotes.cpp:278-279`, `ArticulationMap.cpp:36-38` and `NoteConditions.cpp:60-76`; no test was run.
- **Each edit is one undo step; every command has a reversibility class.** Not applicable: no edit and no command.
- **Nothing I added runs on the audio thread.** Not applicable: nothing added.
- **Old projects still load and render exactly as before.** Not applicable: no change to load or render paths.
- **Qt 5.15 / MSVC / macOS portability.** Not applicable: no code change.
- **Regression test deterministic.** Not applicable: no regression test.
- **Fix at the cause and complete.** Not applicable: the finding's cause does not exist. The only articulation read is `InstrumentTrackNotes.cpp:278`, and it is guarded.
- **No collateral damage.** Not applicable: no change.
- **Verdict evidence (file:line).** Given in items 1-6 above.
- **Gates.** Relocated copy of `feature_check.py`: RESULT PASS, EXIT 0 (log `002`). Canonical run: EXIT 1 from a sandbox permission error on its log directory (log `004`), not from a gate failure.
