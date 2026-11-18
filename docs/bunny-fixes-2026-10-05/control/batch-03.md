# Space Bunny fixer lane `control` — batch-03 report

Worktree `zene-bunny-control`, branch `bugs/bunny-control-2026-10-05`. Six findings triaged: two fixed
with red/green regression tests, one verified false positive (with a test that pins the refusal), one
false positive on the reported impact, and two out of scope for this lane.

### Cluster 4047 — `notes_moved` undercounts notes changed in different ways

Verdict: FIXED

Evidence: `addMoveCounts` counted `positions_moved` and `velocities_moved` separately and inserted
`std::max(movedPositions, movedVelocities)` as `notes_moved` (`src/core/ControlGrooveSupport.cpp:310`
before the fix). That is the larger of two counts, not the number of notes: a groove that moves one
note's position and a *different* note's velocity has moved two notes, and both component counts are 1,
so the reply said `notes_moved: 1`. The fix counts a note once when either of its two values moved, so
a note that moved both ways is still one note. The result schema is unchanged — `notes_moved` is still
an integer, only what that integer means is corrected.

Commit: `c2761079f`

Test: `ControlGrooveCommandsTest::notesMovedIsTheUnionOfBothKindsOfChange`

Red/green: RED `cmake --build build --target zene ControlGrooveCommandsTest > build/bunny-b.log 2>&1`
EXIT=0, then `cd build/tests && ctest -R '^ControlGrooveCommandsTest$' --output-on-failure >
build/bunny-t.log 2>&1` EXIT=8 (`notes_moved` Actual 1, Expected 2, `ControlGrooveCommandsTest.cpp:605`).
GREEN same build and same ctest command EXIT=0; also
`ctest -R '^(GrooveTemplateTest|ControlRegistryTest|ControlResultSchemaTest|ControlSurfaceReferenceTest)$'`
EXIT=0; both gates EXIT=0.

The fixture is built so the clip's note order does not change — both travellers land in slot 2 and the
clip still sorts them key-descending, 64 before 62 — so the case measures the union and is independent of
cluster 4993. One earlier draft of this fixture was wrong and the test caught it: a note at 150 ticks is
in slot 2, not slot 1, because `absoluteSlotOf` rounds the half up (`src/core/GrooveTemplate.cpp:76`), so
that note moves in *both* axes by default. Giving it the slot's velocity is what isolates its position
change.

### Cluster 3049 — Unreadable existing WAVs are omitted from mastering capture

Verdict: FIXED

Evidence: `masteringWavFiles` enumerated entries with `QDir::Files | QDir::Readable | QDir::NoDotAndDotDot`
(`src/core/ControlMasteringSupport.cpp:92-93` before the fix), so a write-only `.wav` in the output
directory appeared in neither the before-state capture nor the after-state scan. The inverse then held
no copy of that file's old bytes and did not know to remove it, while still reporting a working inverse
over a directory it never looked at. Removing `QDir::Readable` makes every `.wav` enumerated; the
refusal already in `captureMasteringWavDirectory` (`src/core/ControlMasteringSupport.cpp:135-142`) names
the file it cannot read and returns `Refused`, which stops the run before the child starts — that is the
"refuse the run" half of the suggested fix, already present.

Commit: `2acced5d8`

Test: `MasteringTest::anUnreadableWavRefusesTheRunInsteadOfBeingOmitted`

Red/green: RED `cmake --build build --target zene MasteringTest > build/bunny-b.log 2>&1` EXIT=0, then
`cd build/tests && ctest -R '^MasteringTest$' --output-on-failure > build/bunny-t.log 2>&1` EXIT=8
(`files.size()` Actual 1, Expected 2, `MasteringTest.cpp:493`). GREEN same build and same ctest command
EXIT=0; both gates EXIT=0.

The case runs as uid 1000 and asserts the fixture file really is unreadable before asserting anything
else, so it proves the filter and not the fixture. The suite also restores the permissions at the end.

### Cluster 4992 — Fractional tick values are silently truncated

Verdict: FALSE-POSITIVE

Evidence: `readTicks` does range-check and then cast (`src/core/ControlGrooveSupport.cpp:244-259`), but
no fractional value can reach it. Every tick argument is declared through `tickProperty()`, which is
`integerProperty(0, 0x7fffffff)` (`src/core/ControlVocabulary.cpp:117-120`), or through
`integerProperty(...)` directly (`src/core/ControlGrooveSupport.cpp:211-214`); ControlSchema's integer
check requires a whole value — `std::floor(value.toDouble()) == value.toDouble()`
(`src/core/ControlSchema.cpp:50-53`) — and `ControlRegistry::invoke` validates the arguments before it
invokes the handler (`src/core/ControlRegistry.cpp:251`). So `length_ticks: 1.9` is refused as
`InvalidArgs` at the registry, and never reaches the cast.

Commit: `7e32cf19a` (test only — no product change)

Test: `ControlGrooveCommandsTest::fractionalTickArgumentsAreRefused`

Red/green: none, by design — there is no red, because the behaviour is already correct.
`cmake --build build --target zene ControlGrooveCommandsTest > build/bunny-b.log 2>&1` EXIT=0, then
`cd build/tests && ctest -R '^ControlGrooveCommandsTest$' --output-on-failure > build/bunny-t.log 2>&1`
EXIT=0 on unmodified product code; both gates EXIT=0. The test pins `length_ticks: 12.5`, a quantise
`grid: 1.5` and `humanise_ticks: 1.5` as `InvalidArgs`, and asserts no groove was created and the
clip's notes are unchanged.

### Cluster 4993 — Groove move counts compare different notes after reordering

Verdict: FALSE-POSITIVE (the misattribution is reachable in principle; the reported aggregate counts are
not observably wrong)

Evidence: the counts are compared by list index (`src/core/ControlGrooveSupport.cpp:299-307`) and the
order is restored after the edit (`clip->rearrangeAllNotes()`,
`src/core/ControlCommandsGrooveEdit.cpp:254-255`), so the concern in principle is right. The reported
*impact* is not reachable through `groove.apply`. `targetTickFor` maps every note in a slot to the same
target tick (`src/core/GrooveTemplate.cpp:183-188`) and a slot's timing step is bounded strictly below
half a slot (`src/core/GrooveTemplate.cpp:158-160`), so two adjacent slots can never cross — reordering
can only occur among notes whose target tick is *identical*. At strength 1 those notes also end with
identical positions and identical velocities, so the index-wise comparison and an identity-wise
comparison count the same notes. At strength below 1 the resulting positions differ, which removes the
tie that would reorder in the first place. The only route to a genuine divergence is seeded per-note
random/humanise jitter (`src/core/GrooveTemplate.cpp:415-433`), and the deterministic fixtures tried
produced equal counts. Fixing this would mean snapshotting by persistent note id, which cannot be shown
to change any reported number, so no product code was changed.

Commit: none

Test: none

Red/green: none

### Cluster 1274 — Undo leaves a newly created mastering output directory behind

Verdict: SKIPPED-SCOPE

Evidence: the run does create the directory — `MasteringJob` calls `QDir().mkpath(m_outputDirectory)`
(`src/core/MasteringJob.cpp:241`) — and the inverse captures WAV files but not whether the directory
existed, so undo removes the files and leaves the new empty directory behind
(`src/core/ControlMasteringSupport.cpp:192-217`). The finding is real. Recording the directory's prior
existence means changing the recorded inverse's transaction payload and signature, which moves the
`ControlCommandsSnapshot` canary and is the parent's decision (rule 8), and the inverse is written to the
project journal, so the payload change is not local to this helper. Product code left alone.

Commit: none

Test: none

Red/green: none

### Cluster 4056 — Mastering undo silently ignores file-operation failures

Verdict: SKIPPED-SCOPE

Evidence: `recordMasteringUndo` ignores the results of `QFile::remove()` and `QFile::write()`
(`src/core/ControlMasteringSupport.cpp:205-216`). Surfacing those failures needs an error channel the
undo step does not have: `addUndoStep` takes `std::function<void()>` with no return value
(`include/ControlReversibility.h:474-475`), and the step is pushed onto ProjectJournal's stack so that
an agent's command and a user's Ctrl+Z are one history. Reporting a failed undo would mean changing the
undo-step contract across every caller, which is a design change rather than a defect fix. Product code
left alone.

Commit: none

Test: none

Red/green: none

Leftovers: clusters 1274 and 4056 are real and still unfixed — 1274 needs a transaction-payload/signature
change to the mastering inverse (canary decision, parent's lane) and 4056 needs an error channel on the
undo step itself. Cluster 4993 is left unfixed deliberately: comparing by persistent note id would change
no reported number in any case I could construct deterministically, and no red test exists for it.