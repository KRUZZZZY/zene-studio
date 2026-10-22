new-p12-342-port: FIXED

# Cluster new-p12-342-port: the library scan's poll overwrote the search's status

Port of the accepted fix for cluster `new-p12-342` (branch `bugs/qwen-new-p12-342`, commits
`a78a987d0` the test seam and `b02a353a8` the fix) onto the newest line, re-applied by hand - no
merge, no cherry-pick, no rebase. The newer line did not carry the fix, so the bug was still
present here.

## Verdict: FIXED

Where (this tree, before the fix):

- `src/gui/BrowserSearchView.cpp:465-478` - `BrowserSearchView::refreshIndexStatus()` wrote the
  scan's message into `m_status`, the same label the search writes its count into
  (`showBrowsed()`, `src/gui/BrowserSearchView.cpp:327`, via `countText()`).
- `src/gui/BrowserSearchView.cpp:225-226` - the "Rescan Library" button's failure message also went
  into `m_status`.

What: while a reindex ran, every poll replaced the search's own feedback (its count, or why it
failed); when the scan finished, its last message stayed on the line and the result count was
hidden.

Fix (this tree, after the fix):

- `src/gui/BrowserSearchView.cpp:152-154` - the scan has its own line: `m_scanStatus`, a second
  `QLabel` (accessible name "Library scan").
- `src/gui/BrowserSearchView.cpp:473-488` - `refreshIndexStatus()` writes the scan's progress and
  its final message into `m_scanStatus`; `m_status` keeps the search's own feedback.
- `src/gui/BrowserSearchView.cpp:229-230` - the Rescan button's failure message goes to the scan's
  line too, so a refused rescan cannot hide the search's count either.
- `include/BrowserSearchView.h:102-106, 146` - the seam: the status line and the scan's line are
  readable (`statusText()`, `scanStatusText()`) and the scan poll (`refreshIndexStatus()`) is
  public; `m_scanStatus` is the new member.

The poll timer, the "running" branch, the final message and every other writer of `m_status`
(the search's count, a search's error, the preview's failure, Project Media's count, a refused
collection save/removal) are unchanged. No command was added, nothing on the audio thread changed,
no saved data or project file format was touched.

Regression: `tests/src/gui/LibraryMenusTest.cpp:866-878`,
`theScanProgressDoesNotHideTheSearchResult()` - a search for a name no file has writes the status
line, the scan's state is then polled, and the status line must still hold the search's text; the
scan line must hold the scan's message. No timing, no sleeping: the poll is called directly.

## Evidence

All logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-342-port.r1/logs`.

| Check | Exact command and result |
| --- | --- |
| RED before the fix | `cmake --build build --target zene LibraryMenusTest > logs/build-red-1.log 2>&1` - **EXIT=0**; from `build/tests`: `ctest -R '^LibraryMenusTest$' --output-on-failure > logs/test-red-1.log 2>&1` - **EXIT=8**. `FAIL! : LibraryMenusTest::theScanProgressDoesNotHideTheSearchResult() Compared values are not the same; Actual (search->statusText()): "Library index: 9 files, built 2026-10-22T03:59:48"; Expected (afterSearch): "No results"`. `Totals: 24 passed, 1 failed, 0 skipped`. |
| GREEN after the fix | `cmake --build build --target zene LibraryMenusTest > logs/build-green-1.log 2>&1` - **EXIT=0**; `ctest -R '^LibraryMenusTest$' --output-on-failure > logs/test-green-1.log 2>&1` - **EXIT=0**, "100% tests passed out of 1". |
| Reversed-fix proof | `git diff -- src/gui/BrowserSearchView.cpp include/BrowserSearchView.h > logs/fix.patch` - **EXIT=0** (49 lines); `git apply -R logs/fix.patch` - **EXIT=0**; `cmake --build build --target zene LibraryMenusTest > logs/build-rev-1.log 2>&1` - **EXIT=0**; `ctest -R '^LibraryMenusTest$' --output-on-failure > logs/test-rev-1.log 2>&1` - **EXIT=8**, the same assertion (`Actual "Library index: 9 files, built 2026-10-22T04:01:54"`, `Expected "No results"`), `Totals: 24 passed, 1 failed`. |
| Restored-fix proof | `git apply logs/fix.patch` - **EXIT=0**; `cmake --build build --target zene LibraryMenusTest > logs/build-restore-1.log 2>&1` - **EXIT=0**; `ctest -R '^LibraryMenusTest$' --output-on-failure > logs/test-restore-1.log 2>&1` - **EXIT=0**, "100% tests passed out of 1". |
| Affected family | `cmake --build build --target zene HelpAccessTest LibraryMenusTest > logs/build-deps-1.log 2>&1` - **EXIT=0** (the header changed, so every target that includes `BrowserSearchView.h` was rebuilt: `zene`, `HelpAccessTest`, `LibraryMenusTest`); from `build/tests`: `ctest -R '^(HelpAccessTest|LibraryMenusTest)$' --output-on-failure > logs/test-family-1.log 2>&1` - **EXIT=0**, **2 CTest test targets executed**, `LibraryMenusTest` 25 cases (24 + the new one) and `HelpAccessTest` both passed, "100% tests passed out of 2". |
| Gates | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` - **EXIT=0** (`logs/gate-1.log`): every gate PASS, including fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures, complexity and file-length ("no new item in your files"). |

Files committed for this bug: `include/BrowserSearchView.h`, `src/gui/BrowserSearchView.cpp`,
`tests/src/gui/LibraryMenusTest.cpp`, `tests/fork-sources.txt` (the new report file),
`docs/qwen-fixes-2026-10/cluster-new-p12-342-port.md`.

## Self-check

- **Verdict with evidence**: FIXED. The bug was live in this tree before the change
  (`src/gui/BrowserSearchView.cpp:465-478` wrote the scan's message into `m_status`, the label the
  search writes its count into at `src/gui/BrowserSearchView.cpp:327`), and the RED run shows the
  poll replacing the search's "No results" with the scan's "Library index: 9 files, built ...".
- **Deterministic regression**: the test calls `refreshIndexStatus()` directly - no clock, no
  sleeping, no polling race. The index the fixture holds is deterministic (the test's own
  temporary tree), so the "Library index: N files" line is stable.
- **The fix is at the cause**: the cause is the shared label, so the scan was given its own line
  rather than a guard on the poll. Every writer of the scan's text was moved with it: the running
  branch (`src/gui/BrowserSearchView.cpp:481`), the final message
  (`src/gui/BrowserSearchView.cpp:487`) and the Rescan button's refusal
  (`src/gui/BrowserSearchView.cpp:230`). The scan's own call sites of `refreshIndexStatus()` are the
  constructor (`:207`) and the Rescan button (`:229`) - both keep working, because the function
  only changed the label it writes.
- **No collateral damage**: `m_status` still carries every other message the view shows (the
  search's count, a search's error, the preview's failure, Project Media's count, a refused
  collection save or removal) - only the scan's text moved. No command, save file or project
  format changed.
- **Realtime / undo / commands**: the change is a GUI label; nothing runs on the audio thread, no
  allocation or locking was added there, no new control command was introduced (the
  ControlCommandsSnapshot canary is untouched), and undo is unaffected.
- **Registration**: the new report file is registered in `tests/fork-sources.txt`;
  `include/BrowserSearchView.h` and `src/gui/BrowserSearchView.cpp` are fork-owned sources already
  registered there, so no `tests/upstream-modifications.txt` entry was needed.
