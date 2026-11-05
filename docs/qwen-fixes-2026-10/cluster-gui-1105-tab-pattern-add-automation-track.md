NEEDS-CONTEXT

# cluster gui-1105-tab-pattern-add-automation-track: "Adding an automation track crashes the application"

Finding: the sweep run `m0-train-25` clicked the Pattern Editor's "Add automation-track" button and the app died with
`terminate called after throwing an instance of 'std::bad_alloc'`
(`~/.local/share/zene-bunny-fix/terra-pilot/buttons/runs/m0-train-25/results.jsonl:170`). Luna's own triage rates it
`needs_context` and notes the log has no stack trace.

## Verdict: NEEDS-CONTEXT (no product change made)

The crash did not reproduce, and nothing in the code the button runs allocates in a way that can fail on its own.
I made no product change, because a guessed fix (for example a `catch (std::bad_alloc&)` around the slot) would hide
the real allocation failure rather than fix its cause.

## Evidence

1. The button's slot is `PatternEditor::addAutomationTrack` at `src/gui/editors/PatternEditor.cpp:118-121`. It makes one
   call, `Track::create(Track::Type::Automation, model())`.
2. `Track::create` (`src/core/Track.cpp:490-524`) takes `requestChangeInModel`, builds the `AutomationTrack`, and for the
   pattern store calls `createClipsForPattern(numOfPatterns() - 1)`. That loop (`Track.cpp:1206-1213`) runs
   `while (numOfClips() < pattern + 1)` and is bounded. It is a no-op with no patterns (`-1`). Then
   `PatternStore::updateAfterTrackAdd` (`src/core/PatternStore.cpp:74-80`) makes the Song's first pattern track when
   there is none, and `PatternTrack`'s constructor (`src/tracks/PatternTrack.cpp:68-80`) gives every store track its clip.
   No loop or size on this path is unbounded.
3. I replayed the sweep's click sequence in the real GUI test harness (`GuiApplication`, offscreen). The two
   replays are the tests added to `tests/src/gui/ArrangerFileDropTest.cpp`:
   - an empty project (no pattern track): the click makes the automation row and the Song's first pattern track;
   - the sweep's state: an instrument row in the store, a Song-level automation track, a second pattern, Clone pattern
     (`PatternEditor::cloneClip`), Add sample-track, then Add automation-track: the row appears and every store track
     holds exactly one clip per pattern (3 tracks, 3 patterns, 3 clips each; measured with a temporary probe, then
     turned into an assertion).
   Neither crashes, and no `bad_alloc` is thrown.
4. The same click sequence (New pattern, Clone pattern, Add sample-track, Add automation-track) ran without a crash in
   the earlier sweep `m0-train-24` (`runs/m0-train-24/results.jsonl`, outcome `clicked`). In `m0-train-25` it was one
   crash in 263 clicks (`sweep.log:14`). Play had been clicked earlier in the same view, so the audio thread was
   running during the click, and the machine was shared with other jobs (see the journal's per-process memory peaks).
   The only difference I can see between the two runs is load or timing, or a change merged into train-25; I could not
   bisect that without a stack.
5. The `JO-ID ... already in use by ...` lines in the crash log come from "Clone pattern"
   (`Clip::copyStateTo` / `JournallingObject::changeID`, `src/core/JournallingObject.cpp:171`), which ran earlier in the
   sequence. They also appear in my passing test run (`logs/test-5.log`), so they are not related to the crash.

## What is needed to go further

One of these would let a fix be made at the cause:
- a core dump or backtrace of the `m0-train-25` run (`gdb` `bt` at the `std::terminate`), or
- a rerun of the sweep with `ulimit -v`/memory use logged around click 169, to tell a real out-of-memory from a
  corrupted size, or
- the sweep run again on the current tip to see whether it recurs (it passed in train-24).

## What I changed

Only `tests/src/gui/ArrangerFileDropTest.cpp`: two characterization tests
(`addAutomationTrackInAnEmptyPatternEditorMakesTheRow`, `addAutomationTrackAfterNewAndClonePatternMakesTheRow`).
They pass on the unchanged product code, so they are NOT regression proof for the crash. They pin the behaviour the
bug report expects (a new row appears, one clip per pattern) so a later fix, or a change that breaks this path,
shows up. No product file changed, so no registration entry is needed.

## Commands run (all in `build/`, logs in the run's `logs/` folder)

| Step | Command | EXIT |
| --- | --- | --- |
| first build | `cmake --build . --target ArrangerFileDropTest -j4` (`build-1.log`) | 2 (ccache cannot write its temp dir under the write-jail) |
| builds | `CCACHE_DISABLE=1 cmake --build . --target ArrangerFileDropTest -j4` (`build-2.log` ... `build-8.log`; `build-4.log` and `build-6.log` were my own compile slips, fixed in the next build) | 0 for `build-2/3/5/7/8` |
| probe 1: empty project | `ctest -R '^ArrangerFileDropTest$' -V` (`test-2.log`) | 0, 14 passed |
| probe 2: New+Clone sequence | `ctest -R '^ArrangerFileDropTest$' -V` (`test-3.log`, `test-5.log`) | 0, 15 passed |
| final run | `ctest -R '^ArrangerFileDropTest$' -V` (`test-8.log`) | 0, 15 passed, 0 failed (includes both new tests) |
| gates | `python3 .../qwen/feature_check.py <worktree>` | 1: it cannot create its log folder outside the write-jail (`PermissionError`, `logs/gates-1.log`) |
| gates (copy) | a copy of `feature_check.py` with only its log folder and `ROOT` path changed (limits and baselines untouched), `logs/gates-2.log` | 0, all 8 rows PASS |

## Evidence table

| Item | Result |
| --- | --- |
| RED command and exit code | none: the bug did not reproduce, so no failing test exists |
| GREEN command and exit code | `ctest -R '^ArrangerFileDropTest$' -V` exit 0 (15 tests) |
| Affected-family test count | `ArrangerFileDropTest`: 15 run, 15 passed |
| Gates | `feature_check.py` exit 1 (cannot write its log folder under the jail); the same checks via a log-folder-only copy exit 0 |
| Files committed | `tests/src/gui/ArrangerFileDropTest.cpp`, this report |

## Self-check

- Regression test fails with the fix reverted and passes with it: NOT APPLICABLE. There is no product change and no
  RED run, because the crash did not reproduce. The two tests I added are characterization tests and say so in their
  comments.
- Determinism: the tests drive the editor slot directly (`addAutomationTrack`, `addSampleTrack`, `cloneClip`) and
  call `processEvents()`; there are no sleeps, timers or races, and nothing depends on wall-clock time or the audio
  thread.
- Fix at the cause and complete: no fix is made. I checked the siblings of the button's path: `Song::addAutomationTrack`
  and `Song::addSampleTrack` (`src/core/Song.cpp:1446-1457`) make the same `Track::create` call, and the Pattern Editor's
  own track drop (`PatternEditor::dropEvent`) already handles the empty-store case (`firstPatternTrack`).
- Collateral damage: none; no product code or saved data is touched.
- Verdict is NEEDS-CONTEXT, not ALREADY-FIXED or FALSE-POSITIVE: the evidence is in points 2-5 above. The click path
  is sound in the headless harness, but a real crash was observed once and I cannot rule out a timing or memory
  cause in the live app.
