FIXED

# cluster gui-1105-tab-pattern-add-automation-track: "Adding an automation track crashes the application"

Finding: the sweep run `m0-train-25` clicked the Pattern Editor's "Add automation-track" button and the app died with
`terminate called after throwing an instance of 'std::bad_alloc'`
(`~/.local/share/zene-bunny-fix/terra-pilot/buttons/runs/m0-train-25/results.jsonl:170`).

## Verdict: FIXED (the exception path); the original trigger of the allocation failure is still unknown

Round 1 could not reproduce the allocation failure and made no change. The review pointed out that the slot has no
error boundary and that `Track::create` can throw while it holds the model-change lock. That is a real defect on the
exceptional path, whatever made the allocation fail in the sweep:

1. `PatternEditor::addAutomationTrack` (`src/gui/editors/PatternEditor.cpp`, was line 118-121) called
   `Track::create(...)` directly. An exception out of a Qt slot terminates the application, which is exactly the
   `terminate called ... bad_alloc` in the log.
2. `Track::create` (`src/core/Track.cpp`, was lines 490-524) took the lock with `requestChangeInModel()` and gave it
   back with `doneChangeInModel()` only on the success path. A throw in between (the track's constructor, its clips,
   or `updateAfterTrackAdd`) left the audio engine's change mutex locked: the next model change or the audio thread
   would then wait on it for good. A track whose constructor finished but whose follow-up work threw also stayed
   registered in the container with missing clips.

## The fix

- `Track::Track` now removes its just-registered row if its constructor body throws. This covers the allocation in
  `QObject::connect` after `TrackContainer::addTrack`: a constructor that throws never runs `Track::~Track`.
- `Track::create` now holds the lock through `AudioEngine::requestChangesGuard()` (released on every exit). The steps
  after the track exists live in a file-local `finishCreatedTrack`; if they throw, the half-made track is deleted
  (its destructor unlinks it from the container) and the exception is rethrown. Success behaviour is identical.
  Every caller of `Track::create` (commands, GUI, loaders) gets the same lock and cleanup guarantee. The XML overload
  (`Track::create(const QDomElement&, ...)`) takes the same guard, so a throw out of its `create()` or `restoreState()` also
  releases the lock.
- The Pattern Editor's two "add track" buttons (`addSampleTrack`, `addAutomationTrack`) go through one file-local
  helper that catches `std::bad_alloc` and says so in the status bar. The Song Editor's Add automation-track button
  (same `Track::create` call, via `Song::addAutomationTrack`) gets the same catch. The Song Editor's Add sample-track
  button already goes through `track.add` (a command, which has its own error boundary), so it is unchanged.
- A test-only seam, `Track::setCreateFaultProbeForTest` (inside `LMMS_TESTING`), lets the test throw after the base
  constructor registers a row and again during `Track::create` follow-up.

Not changed: the unknown cause of the allocation failure in the sweep (no stack trace; the same click sequence passed in
`m0-train-24`). The click path itself is sound in the GUI harness (tests below, which pass before and after). If it
recurs, the user now sees a status-bar message and the project is intact, instead of a crash.

## Regression test

`tests/src/gui/ArrangerFileDropTest.cpp`:
- `aFailedAllocationInAddAutomationTrackIsReportedAndLeavesTheProjectAsItWas`: the seam throws `std::bad_alloc`
  after Track's container registration and again during factory follow-up; each time the test asserts the exception
  does not escape the slot, the model-change lock is free (`AudioEngine::isChangeInModelHeld()`), the store and Song
  track counts are unchanged, and the status bar names the failure. The button works again afterwards.
- `addAutomationTrackInAnEmptyPatternEditorMakesTheRow` and
  `addAutomationTrackAfterNewAndClonePatternMakesTheRow`: the happy path (empty project; the sweep's New pattern,
  Clone pattern, Add sample-track sequence), including one clip per pattern on every store track.

Deterministic: no timers, sleeps or threads; the failure is injected through the seam, and the slots are called directly.

## Commands and results (logs in `~/.local/share/zene-bunny-fix/qwen/runs/gui-1105-tab-pattern-add-automation-track.r2/logs/`)

All builds used `CCACHE_DISABLE=1` (the ccache temp directory is outside the write-jail), `-j4`.

| Step | Command | EXIT |
| --- | --- | --- |
| build with the fix | `cmake --build . --target zene ArrangerFileDropTest -j4` (`build-1.log`) | 0 |
| GREEN | `ctest -R '^ArrangerFileDropTest$' -V` from `build/tests` (`test-green-1.log`) | 0, 16 passed |
| RED (a): handler catch reverted, seam kept | `git diff -- src/gui/editors/PatternEditor.cpp src/gui/editors/SongEditor.cpp > fix-a.patch && git apply -R fix-a.patch`, rebuild (`build-red-a.log`), same ctest (`test-red-a.log`) | build 0, test 8: `'!escaped' returned FALSE. (std::bad_alloc left the Add automation-track slot)`, 15 passed 1 failed |
| RED (b): `Track::create` guard and cleanup reverted, seam kept | edited back to the old request/done pair without the catch, rebuild (`build-red-b.log`), same ctest (`test-red-b.log`) | build 0, test 8: `'!Engine::audioEngine()->isChangeInModelHeld()' returned FALSE. (the model-change lock was left taken)`, then `cleanupTestCase()` fatal error, 14 passed 2 failed |
| GREEN again | fix restored (`fix-a.patch` re-applied, `Track.cpp` restored from its saved copy), rebuild (`build-green-2.log`), ctest (`test-green-2.log`) | build 0, test 0, 16 passed |
| neighbouring families | `cmake --build . --target StableTrackIdsTest AutomationTrackTest -j4` (`build-family.log`), `ctest -R '^(StableTrackIdsTest\|AutomationTrackTest\|ArrangerFileDropTest)$'` (`test-family-2.log`) | build 0, 3 of 3 passed |
| final rebuild after the XML-overload guard | `cmake --build . --target zene ArrangerFileDropTest StableTrackIdsTest AutomationTrackTest -j4` (`build-final.log`), `ctest -R '^(StableTrackIdsTest\|AutomationTrackTest\|ArrangerFileDropTest)$'` (`test-final.log`) | build 0, test 0, 3 of 3 passed |
| gates | `python3 .../qwen/feature_check.py <worktree>` (`gates-1.log`) | 1: cannot create its log folder next to the worktree (write-jail, `PermissionError`) |
| gates (copy) | a copy of `feature_check.py` with only its log folder and `ROOT` path changed; limits and baselines untouched (`gates-2.log`) | 0, all 8 rows PASS |

The test seam lives in the product files, so reverting every product file would stop the test from compiling. The
two reversals therefore revert the behaviour (the catch, then the lock guard and cleanup) and keep the seam; each
alone makes the test fail.

## Evidence table

| Item | Result |
| --- | --- |
| RED command and exit code | `ctest -R '^ArrangerFileDropTest$' -V` exit 8 for each reversal (a) and (b) above |
| GREEN command and exit code | `ctest -R '^ArrangerFileDropTest$' -V` exit 0, 16 passed |
| Affected-family test count | `ArrangerFileDropTest` 16, `StableTrackIdsTest` and `AutomationTrackTest` pass (3 ctest entries, 3 passed) |
| Gates | `feature_check.py` exit 1 (jail: cannot write its log folder); the log-folder-only copy exit 0, 8 of 8 PASS |
| Files committed | `include/Track.h`, `src/core/Track.cpp`, `src/gui/editors/PatternEditor.cpp`, `src/gui/editors/SongEditor.cpp`, `tests/src/gui/ArrangerFileDropTest.cpp`, `tests/upstream-modifications.txt`, this report |

The QA reviewer found one additional exceptional path: the functor connection in `Track::Track` allocates after
`TrackContainer::addTrack` has published the raw pointer. The constructor now unregisters that pointer if its body
throws. The same deterministic regression injects a failure at this point and during factory follow-up. Reviewer
verification after this change: `cmake --build build --target zene ArrangerFileDropTest -j12` exit 0;
`ctest -R '^ArrangerFileDropTest$' --output-on-failure` from `build/tests` exit 0; the same ctest with the constructor
rollback reversed fails (exit 8: the store count rises from 1 to 2, then the following test sees the dangling entry);
the exact `feature_check.py <worktree>` command exits 0, all 8 rows PASS.

## Self-check

- **Test fails with the product change reverted, passes with it:** yes, see the RED (a), RED (b) and GREEN rows above
  (exit 8, 8, 0).
- **Determinism:** the failure is injected through `Track::setCreateFaultProbeForTest`; no timing, sleeps or
  threads. The lock state is read with `AudioEngine::isChangeInModelHeld()`, a per-thread depth counter.
- **Fix at the cause and complete:** the cause on the exceptional path is in `Track::create` (lock released only on
  success, half-made track left registered) and in the slots (no error boundary). Siblings checked: the XML overload `Track::create(const QDomElement&, ...)` had the same
  request/done pair and now uses the same guard (its nested `create(Type)` is covered too; the mutex is recursive).
  `Song::addPatternTrack` and the Song Editor's Add sample-track button (a `track.add` command) are not the reported
  slot and are unchanged. The Pattern Editor's two buttons and the Song Editor's automation button are all covered.
- **Collateral damage:** none on success: the same constructor, clips and `updateAfterTrackAdd` run in the same order
  under the same lock. No saved data changes. No new control command (the canary is untouched). Nothing runs on the
  audio thread; the lock guard is the same recursive mutex the old calls took.
- **Remaining risk (stated, not hidden):** what made the allocation fail in the `m0-train-25` sweep is not found. This
  change makes that failure survivable and non-crashing on this path; it does not explain it.
