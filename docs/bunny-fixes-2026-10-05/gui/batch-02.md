# Space Bunny fixer lane `gui` — batch 02 (2026-10-05)

Branch `bugs/bunny-gui-2026-10-05`. Six findings, six fixed. Both gates green on the
final tree: `bash tests/complexity-gate.sh --check` EXIT=0,
`bash tests/file-length-gate.sh --check` EXIT=0.

Clusters 5487 and 903 share one commit: both were defects in the same gesture
(`ArrangerRange` in `src/gui/ArrangerOverlays.cpp`), the fix for 903 reverts a state
that 5487's test also leaves behind, and splitting them would have meant shipping the
cancellation fix with the known selection bug still present. That is a deviation from
"one commit per fixed defect" and is called out here rather than hidden.

### Cluster 5487 — A range dragged from above the tracks to below them selects no tracks

Verdict: FIXED

Evidence: `src/gui/ArrangerOverlays.cpp:240` — `dispatchRange` tested
`if ((first == nullptr) != (last == nullptr))`, so the one-off-row fallback to
`nearestTrackAt` ran only when exactly one endpoint missed a row. With both endpoints
off the rows neither hit, the loop got an empty array and `range.set` selected nothing.
Now `if (first == nullptr || last == nullptr)`, so both off-row endpoints resolve to the
nearest boundary track.

Design note: the first track row starts at y=0, so there is no off-row space above the
tracks; the only off-row region is below the last row, which is where the test drags.

Commit: 84f910e7c

Test: ArrangerTimelineTest::aRangeWithBothEndsOffTheRowsReachesTheBoundaryTrack

Red/green: `cmake --build build --target zene ArrangerTimelineTest` EXIT=0;
`cd build/tests && ctest -R '^ArrangerTimelineTest$' --output-on-failure` EXIT=8 ("Compared
lists have different sizes"), then EXIT=0 (15 passed).

### Cluster 903 — Cancelled range drags remain active

Verdict: FIXED

Evidence: `src/gui/ArrangerOverlays.cpp:178` — `event()` cleared `m_dragging` on
`WindowDeactivate` / `UngrabMouse` / `Hide` only in the Audition branch, so a Range or
Smart drag stayed marked active after a popup took the grab, and a later `mouseMoveEvent`
could still change the range. Cancellation now applies to every tool; `transport.audition_stop`
stays Audition-only.

Commit: 84f910e7c (shared with Cluster 5487, see the note above)

Test: ArrangerTimelineTest::aRangeDragCutShortByTheGrabBeingTakenStops

Red/green: same commands as above — EXIT=8 (range still active), then EXIT=0 (15 passed).

### Cluster 5516 — Missing project media shows fabricated size and date

Verdict: FIXED

Evidence: `src/gui/BrowserSearchView.cpp:369` fed `QFileInfo(path).size()` and
`lastModified().toMSecsSinceEpoch()` straight into the row. The reported "-1 / invalid" was
not what Qt actually returns: a probe in this environment shows a missing file reports
size `0` and timestamp `-3600000`, so the row divided a real `0` into "0 KB" and the
consumer could not tell unknown from zero. `BrowserResults.cpp:103-104` then formatted
whatever it got, epoch 0 included, as 1970-01-01. The view now marks both `-1` for a
missing reference, and `rowOf` (`src/gui/BrowserResults.cpp:103-110`) renders a `-`
placeholder for either while keeping the numeric sort value for known sizes.

Commit: 7aaa783d9

Test: LibraryMenusTest::aResultRowForMissingMediaShowsPlaceholdersInsteadOfZeroAnd1970

Red/green: `cmake --build build --target zene LibraryMenusTest` EXIT=0;
`cd build/tests && ctest -R '^LibraryMenusTest$' --output-on-failure` EXIT=8 (actual "0",
expected "-"), then EXIT=0 (15 passed).

### Cluster 5503 — Tag menu hides tag-write failures

Verdict: FIXED

Evidence: `src/gui/BrowserTagMenu.cpp:61` — `setTag` discarded the `ControlResult`, so a
refusal or a failed store write (`src/core/BrowserTagStore.cpp:188-192` sets `m_error` and
returns false; `src/core/ControlCommandsBrowserTags.cpp:118-122` turns that into a Refused
"the tag was not persisted") produced no user-visible sign at all. `setTag` now returns the
result; on failure the toggled entry reverts its check under a `QSignalBlocker` and both
entries carry the command's `errorMessage` in `statusTip`. A modal box was deliberately not
used — it would hang the offscreen tests.

Commit: d325f41ff

Test: BrowserTagMenuTest::aTagTheStoreCannotSaveTellsTheUserInsteadOfLookingSaved

Red/green: `cmake --build build --target zene BrowserTagMenuTest` EXIT=0;
`cd build/tests && ctest -R '^BrowserTagMenuTest$' --output-on-failure` EXIT=8 (empty
statusTip — "the failed tag write was reported to no one"), then EXIT=0 (5 passed).
BrowserCatalogTest and ControlBrowserCommandsTest also pass (EXIT=0).

### Cluster 5504 — Tag checkboxes compare spelling case-sensitively

Verdict: FIXED

Evidence: `src/gui/BrowserTagMenu.cpp:87` checked membership with
`mine.contains(tag)` (case-sensitive), while the store's own rule is
`QString::compare(..., Qt::CaseInsensitive)` (`src/core/BrowserTagStore.cpp:54-57`). A file
carrying `Bass` whose menu entry was spelled `bass` showed unchecked, and checking it
retried an add the store had already applied. `BrowserTagStore::has` (line 208) already
implements that rule, so the menu now asks the store; the now-unused `tagsOf` read was
dropped rather than left dead.

Commit: e78d176d4

Test: BrowserTagMenuTest::theMenuChecksTagsWhoseSpellingDiffersOnlyInCase

Red/green: `cmake --build build --target zene BrowserTagMenuTest` EXIT=0;
`cd build/tests && ctest -R '^BrowserTagMenuTest$' --output-on-failure` EXIT=8
("not checked for .../case-a.wav"), then EXIT=0 (4 passed). BrowserCatalogTest and
ControlBrowserCommandsTest also pass (EXIT=0).

Note: the test locates the entry case-insensitively on purpose. The store dedups
case-insensitively but keeps one arbitrary spelling in the aggregated list — an early
version of this test hardcoded `Bass` and failed for the wrong reason (no such entry, not
an unchecked one).

### Cluster 5560 — Failed rerun leaves a stale result visible

Verdict: FIXED

Evidence: `src/gui/CommandForm.cpp:351-355` — on a failed run `m_result` was replaced and
the error shown, but `m_answer` and `m_copy` kept the previous successful run's content and
stayed visible while `m_succeeded` stayed true, so the displayed answer and the JSON handed
over by Copy Result disagreed. `accept()` now calls the new `dropAnswer()` first
(`src/gui/CommandForm.cpp:317-327`, called at line 355, declared at
`include/CommandForm.h:137`), which clears
and hides the stale pair, resets `m_succeeded`, and restores the Run Again / Close labels.

Commit: 079ef60b3

Test: CommandFormTest::aFailedRerunDropsTheAnswerThePreviousRunLeft

Red/green: `cmake --build build --target zene CommandFormTest` EXIT=0;
`cd build/tests && ctest -R '^CommandFormTest$' --output-on-failure` EXIT=8 (resultText not
empty after the failed rerun), then EXIT=0 (17 passed). CommandPaletteTest also passes
(EXIT=0).

Leftovers: none. Clusters 5487 and 903 are fixed but share commit `84f910e7c` rather than
having one commit each, as noted above. For 903 the extra "left button still held" check in
`mouseMoveEvent` was left out as beyond the reported defect; cancellation on grab loss, hide
and deactivation now covers the reported case.
