# Core batch-26 — Luna findings 665, 5236, 4352, 5257, 5256, 5255

Worktree `zene-bunny-core`, branch `bugs/bunny-core-2026-10-05`. Every verdict below was
decided from the CURRENT tree; the line numbers in the reports came from older revisions.

### Cluster 665 — Export cancellation uses a racy `volatile` flag

Verdict: ALREADY-FIXED

Evidence: `include/ProjectRenderer.h:133` already declares `std::atomic<bool> m_abort;`, with a
comment recording that `volatile` is not a synchronisation. The render loop reads it at
`src/core/ProjectRenderer.cpp:270, 277, 301` and `abortProcessing()` writes it at
`src/core/ProjectRenderer.cpp:316`. There is no `volatile` abort flag left in the class.

Commit: none

Test: none

Red/green: none

### Cluster 5236 — Revision rotation silently loses backups when a copy fails

Verdict: FIXED

Evidence: `src/core/ProjectRevisions.cpp` `copyRevision()` removed the destination (`QFile::remove(to)`)
before copying into it, and the shift loop passed a local `bool ignored` and stepped over the
result — so a failing shift left an empty slot and rotation still returned a kept count. The copy now
lands in a sibling `.rotate` file and is renamed over the destination only once complete, and a
failed shift is reported through the existing `refused`/`refusedReason` channels. No command
parameter or result schema changed: both channels already existed.

Commit: 02492a165

Test: RevisionTimelineTest::aFailedShiftKeepsTheBackupAndReportsARefusal()

Red/green:
- Red: `cmake --build build --target zene RevisionTimelineTest` EXIT=0; `cd build/tests && ctest -R '^RevisionTimelineTest$' --output-on-failure` EXIT=8 — `FAIL! ... 'QFileInfo::exists(rev1)' returned FALSE. (the failed shift deleted the destination backup)`
- Green: `cmake --build build --target zene RevisionTimelineTest` EXIT=0; `cd build/tests && ctest -R '^RevisionTimelineTest$' --output-on-failure` EXIT=0. RevisionTimelineTest, ProjectRecoveryTest, ProjectRevIdsTest, ProjectStateTest, ProjectOpenIntegrityTest all EXIT=0. `bash tests/complexity-gate.sh --check` EXIT=0; `bash tests/file-length-gate.sh --check` EXIT=0.

### Cluster 4352 — Restoring a revision can delete the live project on copy failure

Verdict: FIXED

Evidence: `src/core/ProjectRevisions.cpp` `replaceProjectWithStaged()` removed the live file and only
then renamed the staged one into place; its recovery copied revision 0 back, which fails for the same
reason in the reported case (the destination ran out of disk after the removal). The staged bytes are
now copied to a sibling `.replace` file first, so a failed copy leaves the live project untouched and
the live file is removed only once the replacement is known to be on disk. The helper is shared with
`src/core/RevisionTimeline.cpp:438`; that caller gets the same guarantee, and no signature changed.

Commit: be00be4f2

Test: RevisionTimelineTest::aFailedReplaceKeepsTheLiveProject()

Red/green:
- Red: `cmake --build build --target zene RevisionTimelineTest` EXIT=0; `cd build/tests && ctest -R '^RevisionTimelineTest$' --output-on-failure` EXIT=8 — `FAIL! ... 'QFileInfo::exists(m_project)' returned FALSE. (the failed replace left no project file at all)`
- Green: `cmake --build build --target zene RevisionTimelineTest ProjectRecoveryTest ProjectRevIdsTest ProjectStateTest ProjectOpenIntegrityTest` EXIT=0; `cd build/tests && ctest -R '^(RevisionTimelineTest|ProjectRecoveryTest|ProjectRevIdsTest|ProjectStateTest|ProjectOpenIntegrityTest)$' --output-on-failure` EXIT=0. Both gates EXIT=0.

### Cluster 5257 — Removing a chain leaves macro targets pointing at stale indices

Verdict: FIXED

Evidence: `Rack::removeChain()` in `src/core/Rack.cpp` erased the chain, reindexed `m_selectedChain` and
rebuilt the graph, but never touched `RackMacroTarget::chain` (`include/RackMacros.h:61`) — so after a
removal a target either drove whichever chain slid into the removed index, or named nothing. Added
`RackMacros::reindexChains()` (`src/core/RackMacros.cpp`), called from `removeChain()`: a target on a
later chain slides down, a target on the removed chain is dropped.

Commit: d4259f7f5

Test: RackMacrosTest::removingAChainReindexesTheMacroTargets()

Red/green:
- Red: `cmake --build build --target zene RackZonesTest RackMacrosTest` EXIT=0; `cd build/tests && ctest -R '^(RackZonesTest|RackMacrosTest)$' --output-on-failure` EXIT=8 — `FAIL! RackMacrosTest::removingAChainReindexesTheMacroTargets() Compared values are not the same`
- Green: `cmake --build build --target zene RackMacrosTest RackTest RackZonesTest RackZoneRoutingTest` EXIT=0; `cd build/tests && ctest -R '^(RackMacrosTest|RackTest|RackZonesTest|RackZoneRoutingTest)$' --output-on-failure` EXIT=0. Both gates EXIT=0.

### Cluster 5256 — Removing a chain leaves zone assignments pointing at the old indices

Verdict: FIXED

Evidence: same site — `Rack::removeChain()` never updated `RackZone::chain` (`include/RackZones.h:81`).
Added `RackZones::reindexChains()` (`src/core/RackZones.cpp`), called from `removeChain()`; it takes
the existing model-change guard, which nests inside the one `removeChain()` already holds. A zone on a
later chain slides down; a zone on the removed chain is dropped rather than left naming an index that
would silently route somewhere nobody asked for. Persisted per-cluster, so the RackTest regression for
5255 and the RackZonesTest regression for 5256 build and pass in either state.

Commit: a77d30116

Test: RackZonesTest::removingAChainReindexesTheZones()

Red/green:
- Red: `cmake --build build --target zene RackZonesTest RackMacrosTest` EXIT=0; `cd build/tests && ctest -R '^(RackZonesTest|RackMacrosTest)$' --output-on-failure` EXIT=8 — `FAIL! RackZonesTest::removingAChainReindexesTheZones() Compared values are not the same`
- Green: `cmake --build build --target zene RackZonesTest RackTest RackZoneRoutingTest` EXIT=0; `cd build/tests && ctest -R '^(RackZonesTest|RackTest|RackZoneRoutingTest)$' --output-on-failure` EXIT=0. Both gates EXIT=0.

### Cluster 5255 — Clearing or loading a rack without a `<rack>` element leaves old macros and zones

Verdict: FIXED

Evidence: `Rack::clear()` reset `m_parallelChains`, `m_selectedChain`, `m_split` and cleared `m_zones`,
but never `m_macros`, and `Rack::loadSettings()` returns early when the project has no `<rack>` element
— so a channel that already had macros kept them across the load and wrote them into the next project
on save. `Rack::clear()` now clears the macros too. Its only other caller, `Mixer::clearChannel()`
(`src/core/Mixer.cpp:2160`), clears the channel to its plain state, where keeping a persisted macro
would be wrong as well.

Commit: ec503f3f1

Test: RackTest::clearDropsMacros()

Red/green:
- Red: `cmake --build build --target zene RackTest` EXIT=0; `cd build/tests && ctest -R '^RackTest$' --output-on-failure` EXIT=8 — `FAIL! RackTest::clearDropsMacros() 'rack.macros().macroCount() == 0' returned FALSE.`
- Green: `cmake --build build --target zene RackTest` EXIT=0; `cd build/tests && ctest -R '^RackTest$' --output-on-failure` EXIT=0 (9/9 cases). RackMacrosTest, RackZonesTest, RackZoneRoutingTest EXIT=0. Both gates EXIT=0.

Leftovers: none. Cluster 5236 and 4352 both change shared helpers in `src/core/ProjectRevisions.cpp`
(`copyRevision()` and `replaceProjectWithStaged()`); `src/core/RevisionTimeline.cpp:438` and
`src/core/ControlCommandsProjectFiles.cpp:148` are the other callers and were not re-verified beyond
the project/revision test executables listed above. The two reindex helpers assume a removed chain is
never re-inserted at its old index by an undo — an inner-chain removal records no structural undo step
in the current code (`journalRemoval`, `src/core/ControlCommandsRack.cpp:213`), so no journal entry is
silently invalidated, but a future inverse that re-creates an inner chain would need the same treatment.
## Rework correction (2026-10-08)

Source: `9b92e4180` (REWORK in `review-core.md`, item 12).

- Cluster 5236 is narrowed: the failed **staging copy** case was covered by `02492a165`. The final rename
  failure, the destination-first removal and the fixed `.rotate` name were not. Fixed in `6dbeb12f4`
  (`fix(rework 02492a165)`): unique staging via `QTemporaryFile`, replacing move via `std::filesystem::rename`,
  and a test-only failure seam. Tests `RevisionTimelineTest::aFailedReplacementAfterStagingKeepsTheSlot` (red at
  HEAD: slot emptied) and `aRotationLeavesAnUnrelatedRotateFileAlone` (red at HEAD: stray file deleted).
- Cluster 4352 (`be00be4f2`): "a failed copy leaves the live project untouched" held for the copy only. The
  rename failure and its unchecked fallback copy could still lose the live project. Fixed in `7f1af2029`
  (`fix(rework be00be4f2)`), test `RevisionTimelineTest::aFailedReplacementAfterStagingKeepsTheLiveProject`.
