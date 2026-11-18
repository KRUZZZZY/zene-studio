# Space Bunny fixer lane `cmds2` — batch 14

Worktree `zene-bunny-cmds2`, branch `bugs/bunny-cmds2-2026-10-05`, base commit
`7a3a7fec8 docs(bunny): cmds2 batch-13 report`.

Six findings adjudicated: 3 FIXED, 2 FALSE-POSITIVE, 1 SKIPPED-SCOPE.

### Cluster 2976 — Section edits omit their reversibility payload

Verdict: FALSE-POSITIVE

Evidence: `src/core/ControlCommandsSections.cpp:72-83` (`recorded()`) calls `addUndoStep`
with no explicit `__transaction`, but `ControlRegistry.cpp:389-408` (`recordTransactionOf`)
synthesizes the record: it stamps the contract class and sets `reversible = (step != 0 &&
cls == "true_inverse")`. `src/core/ControlReversibilityTableDevices.cpp:172-174` declares
`section.add`/`set`/`remove` as `RC::TrueInverse`, so `reversible:true` is recorded.
`src/core/ControlSchema.cpp:230` (`finishResult`) strips `__transaction` from the wire
payload, so adding it explicitly would be cosmetic and unobservable by any client. Same
precedent as batch-13 cluster 283.

Commit: none
Test: none
Red/green: none

### Cluster 3980 — Session model edits may leave project marked clean

Verdict: FIXED

Evidence: `recordSessionEdit()` (`src/core/ControlCommandsSession.cpp:78-96`) called
`addUndoStep` but never `setModified()`; all six block-edit callers (`:308`, `:342`, `:420`,
`:479`, `:516`, `:547`) pass through it. `SessionModel` is a plain class
(`include/SessionModel.h:257`), not a `JournallingObject`, and has no Song back-pointer,
so nothing else marks the project dirty. Sibling precedent:
`src/core/ControlCommandsSessionCapture.cpp:133` and
`src/core/ControlCommandsSessionPerform.cpp:106-111`. The fix marks the song modified at
that single seam and re-marks dirty from both the undo and redo closures, so undoing back
to the saved state does not silently clear the flag.

Commit: `87622d5ba`
Test: `SessionCommandArgsTest::aSessionModelEditMarksTheSongModified`
Red/green:

- `cmake --build build --target zene SessionCommandArgsTest` → EXIT=0
- red: `cd build/tests && ctest -R '^SessionCommandArgsTest$' --output-on-failure` → EXIT=8
  (`FAIL! aSessionModelEditMarksTheSongModified() 'song->isModified()' returned FALSE. (session.set_grid left the project marked clean)`)
- green: same command → EXIT=0 (100% tests passed, 0 tests failed out of 1)

### Cluster 4935 — `session.set_slot` accepts references to nonexistent patterns

Verdict: SKIPPED-SCOPE

Evidence: `applySlotReference()` (`src/core/ControlCommandsSession.cpp:108-145`) checks
only that a `pattern` key is present and never validates it against
`PatternStore::numOfPatterns()` (`include/PatternStore.h:86`,
`src/core/PatternStore.cpp:105-108`), a gap the sibling handler
`src/core/ControlCommandsSessionPatterns.cpp:56-59` does guard. But
`Song::clearProject()` empties the store (`src/core/Song.cpp:1325`,
`Engine::patternStore()->clearAllTracks()`), and the existing passing case
`tests/src/core/SessionPlaybackRenderTest.cpp:186-235`
(`everyQuantisationNameSurvivesASetGetRoundTrip`) calls `song->clearProject()` and then
drives `session.set_slot` with `{"type":"midi","pattern":0}`, asserting `slot.ok`. Strict
validation would reject pattern 0 on an empty store and break that existing coverage, i.e.
it is a behaviour change, not a behaviour-preserving fix.

Commit: none
Test: none
Red/green: none

### Cluster 4933 — Invalid Follow Action field types are silently accepted

Verdict: FIXED

Evidence: `followActionFromJson()` in `src/core/ControlCommandsSessionFollow.cpp`
type-checked only `type` and then converted the rest with `toDouble(1.0)`,
`toBool(true)` and `toInt()` — defaults that happen to equal `FollowAction`'s own
defaults (`include/SessionModel.h:94-117`), so a wrong-typed value such as
`{"type":"jump","jump_to":"oops"}` was indistinguishable from an absent field and became
scene 0. Qt6-compatible helpers `jsonTypeName`, `numberField`, `boolField` and
`intField` were added: an absent (`isUndefined()`) field keeps the default, a present
field of the wrong type is `InvalidArgs`. The logic is split into
`followActionFieldsFromJson` (chance/linked/time_bars) and
`followActionTargetFromJson` (jump_to plus the existing grid range check) purely to stay
under the CCN ≤ 10 ratchet; all pre-existing range checks are preserved.

Commit: `4ec3067be`
Test: `SessionCommandArgsTest::aWrongTypedActionFieldIsRefused`
Red/green:

- `cmake --build build --target zene SessionCommandArgsTest` → EXIT=0
- red: `cd build/tests && ctest -R '^SessionCommandArgsTest$' --output-on-failure` → EXIT=8
  (`FAIL! aWrongTypedActionFieldIsRefused() Compared values are not the same`)
- green: same command → EXIT=0

### Cluster 3988 — A non-array `actions` value silently arms the slot's stored chain

Verdict: FALSE-POSITIVE

Evidence: `ControlRegistry::invoke()` validates the args against the command's schema at
`src/core/ControlRegistry.cpp:251` and returns `InvalidArgs` before any handler dispatch,
and `session.follow_set` declares `{QStringLiteral("actions"), arrayProperty()}`
(`src/core/ControlCommandsSessionFollow.cpp:300-305`), which
`src/core/ControlSchema.cpp:56` enforces via `value.isArray()`. `followPlanFromArgs` has
exactly one caller (`:335`, inside `session.follow_set`), so its non-array fallback at
`:198` is unreachable from the control surface. Confirmed empirically: the regression case
passed with the reported product change fully reverted, which is what identified the
finding as a false positive.

Commit: none
Test: `SessionCommandArgsTest::aNonArrayActionsValueIsRefused` (kept as a schema-guard pin;
its comment now records the upstream guarantee)
Red/green: none — the case passes in both the red (reverted) and green states

### Cluster 4934 — Supplying only one cell coordinate addresses row or column zero

Verdict: FIXED

Evidence: in the `session.follow_get_state` handler
(`src/core/ControlCommandsSessionFollow.cpp`), `track` and `scene` were independently
optional and the omitted one defaulted to 0 through `toInt()`, so `{"track":2}` silently
reported cell `(2,0)`. The handler now computes `hasTrack`/`hasScene`: neither present
returns the whole grid, exactly one is `InvalidArgs` with "name both 'track' and 'scene'
to address one cell, or neither for the whole grid", both present proceeds as before. The
one-cell reading block was extracted into `addCellReading(...)` to keep the handler under
the complexity ratchet; the `clipSlotState`, armed-mask, `followPlanJson` and
`followActionTicks` logic is preserved verbatim.

Commit: `c0b72bd9c`
Test: `SessionCommandArgsTest::aCellAddressNeedsBothCoordinates`
Red/green:

- `cmake --build build --target zene SessionCommandArgsTest` → EXIT=0
- red: `cd build/tests && ctest -R '^SessionCommandArgsTest$' --output-on-failure` → EXIT=8
  (`FAIL! aCellAddressNeedsBothCoordinates() Compared values are not the same`)
- green: same command → EXIT=0

Final state, with all three fixes applied:

- `cd build/tests && ctest -R '^(SessionPlaybackRenderTest|SessionSceneFollowTest|SessionPerformTest|SessionFollowTest|SessionCommandArgsTest)$' --output-on-failure` → EXIT=0 (100% tests passed, 0 tests failed out of 5)
- `bash tests/complexity-gate.sh --check` → EXIT=0
- `bash tests/file-length-gate.sh --check` → EXIT=0
- `bash tests/unregistered-tests-gate.sh --verbose` → EXIT=0

Test-file note: the four regression cases live in a new
`tests/src/core/SessionCommandArgsTest.cpp` rather than in
`tests/src/core/SessionSceneFollowTest.cpp`. That file supports only one
`Engine::init(true)`/`Engine::destroy()` cycle — running its own pre-existing case twice
(`./SessionSceneFollowTest theSocketPersistsAndArmsSceneAndCellChains
theSocketPersistsAndArmsSceneAndCellChains`) aborts with `std::bad_alloc` in
`Engine::destroy()` → `Song::~Song()` — so adding command cases there kills the
executable. The new file follows the repository-standard single
`initTestCase`/`cleanupTestCase` lifecycle used by
`tests/src/core/SessionPlaybackRenderTest.cpp:121-134` and
`tests/src/core/SessionPerformTest.cpp:78-89`, and is registered in both
`tests/CMakeLists.txt` and `tests/fork-sources.txt`.

Leftovers: `tests/fork-sources-gate.sh --verbose` exits 1, but the failure is pre-existing
and unrelated to this lane — `tests/all-sources.txt` is missing
`tests/src/core/RecordToolsNoEngineTest.cpp`, added by unrelated commit `41be6d4bc`. That
manifest is not regenerated by this work, and the new
`tests/src/core/SessionCommandArgsTest.cpp` does not appear in the diff. The same
manifest is also already stale for `tests/src/core/SessionSceneFollowTest.cpp`.
