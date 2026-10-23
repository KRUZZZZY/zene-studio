# Cluster new-p12-342-port

Verdict: **FIXED**

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
| Gates | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` - **EXIT=0** (`logs/gate-3.log`, after the manifest repair below): `PASS fork-sources`, `PASS all-sources-reproduce`, `PASS no-upstream-regression`, `PASS check-namespace`, `PASS no-tautology`, `PASS doc-figures`, `PASS complexity` ("no new item in your files"), `PASS file-length` ("no new item in your files") - **RESULT: PASS**. The earlier runs of the same command (`logs/gate-1.log`, `logs/gate-2.log`) exited **1** on `fork-sources` and `all-sources-reproduce` only; the repair is described below. |

Files committed for this bug: `include/BrowserSearchView.h`, `src/gui/BrowserSearchView.cpp`,
`tests/src/gui/LibraryMenusTest.cpp`, `tests/fork-sources.txt` (the new report file),
`docs/qwen-fixes-2026-10/cluster-new-p12-342-port.md`, and `tests/all-sources.txt` (the gate
repair below - a manifest re-derivation, no product code).

### The gate repair: `tests/all-sources.txt` re-derived from its own recipe

`fork-sources` and `all-sources-reproduce` were red on this branch's base commit, and the repair is
the one the gate itself prescribes. What was wrong, measured first-hand:

- `tests/all-sources.txt` listed **1148** vendored `plugins/SfzPlayer/sfizz/external/**` paths that
  the manifest's own "Regenerate with" recipe excludes - the header calls that tree vendored and
  excluded on purpose ("they are not this repo's code and no gate here rewrites them"), and
  `tests/fork-sources-gate.sh`'s own `is_excluded()` skips the same tree.
- The manifest carried **4193** entries, the recipe derives **3045**, and the recipe derived
  **nothing** the manifest lacked: the whole difference was those 1148 vendored paths
  (`diff <(git show dbb15dded:tests/all-sources.txt | grep -vE '^[[:space:]]*(#|$)') <(the
  recipe's own command) > logs/all-sources-base-diff.log` - **EXIT=1**, exactly 1148 differing
  lines, none outside `plugins/SfzPlayer/sfizz/`).
- The inconsistency entered with the train-10 snapshot `5ef150729` ("all-sources re-derived"), which
  added those 1148 entries to the list in the same commit that added the sfizz exclusion to the
  header recipe - the manifest and its recipe were out of step from that point on, and the row has
  been red since.

The repair follows the gate's own instruction ("Fix by re-deriving the list: run the 'Regenerate
with' command from the manifest's header and write its output over the entry list (never hand-edit,
never delete a path the recipe derives)"): the entry list was re-derived from the manifest's own
header recipe, the header left untouched. The diff is **1148 deletions and nothing else** - no
added line, and not one changed line outside `plugins/SfzPlayer/sfizz/`
(`git diff --stat -- tests/all-sources.txt` - "1 file changed, 1148 deletions(-)"). The vendored
tree is simply no longer in the whole-tree scope, which is what the header and
`tests/fork-sources-gate.sh` both describe. No gate limit, baseline or ratchet was touched, and
no product source changed; `bash tests/all-sources-reproduce.sh` now prints "REPRODUCES" (**EXIT=0**).

This is a train-level repair, kept in its own commit so the merge train can take it or drop it
independently of the browser fix.

## Self-check

- **The regression test FAILS with the product change reverted and PASSES with it (exit codes)**:
  RED `ctest -R '^LibraryMenusTest$'` - **EXIT=8**, `Actual "Library index: 9 files, built
  2026-10-22T03:59:48"` vs `Expected "No results"`, `Totals: 24 passed, 1 failed`; with the fix
  reversed (`git apply -R logs/fix.patch`, **EXIT=0**) the same run is **EXIT=8** with the same
  assertion; with the fix restored (`git apply logs/fix.patch`, **EXIT=0**) it is **EXIT=0**,
  "100% tests passed out of 1".
- **The test is deterministic**: the seam is the public `BrowserSearchView::refreshIndexStatus()`
  (`include/BrowserSearchView.h:106`) and the two readers `statusText()` / `scanStatusText()`
  (`include/BrowserSearchView.h:102-104`); the test calls the poll directly, so no clock, no
  sleeping and no race are involved. The values it compares are the view's own label text and the
  fixture index in the test's temporary tree - no timestamp is asserted.
- **The fix is at the cause and complete**: the cause is the shared label, so the scan was given
  its own line rather than a guard on the poll. Every writer of the scan's text moved with it -
  the running branch (`src/gui/BrowserSearchView.cpp:481`), the final message
  (`src/gui/BrowserSearchView.cpp:487`) and the Rescan button's refusal
  (`src/gui/BrowserSearchView.cpp:230`). Every caller of `refreshIndexStatus()` was checked: the
  constructor (`:207`) and the Rescan button (`:229`) - both keep working, because only the label
  written changed. The siblings of the changed value - the other `m_status` writers (the search's
  count at `:327`, a search's error at `:321`, the preview's failure at `:163`, Project Media's
  count at `:407`, a refused collection save/removal at `:501`/`:508`) - still write the search's
  own status line, which is the contract the class documentation states.
- **The gates**: `feature_check.py` - **EXIT=0**, **RESULT: PASS** - all eight rows green
  (`logs/gate-3.log`): `fork-sources`, `all-sources-reproduce`, `no-upstream-regression`,
  `check-namespace`, `no-tautology`, `doc-figures`, `complexity` and `file-length` (the last two
  "no new item in your files"). The two rows that were red before the repair are green after it,
  because `tests/all-sources.txt` was re-derived from its own header recipe - the repair the gate
  itself prescribes, described in the section above: 1148 deletions, no added line and no changed
  line outside the vendored `plugins/SfzPlayer/sfizz/` tree. No gate limit, threshold or baseline
  was moved, no recipe was rewritten and no product source was touched by it; the manifest's header
  (the recipe itself) is byte-for-byte unchanged.
- **No collateral damage**: no command, save file, project format or setting changed; old projects
  and saved data load exactly as before. No limit or check was added, so no other caller can hit
  one. The only visible change is that the scan's progress and its final message now appear on a
  second line (accessible name "Library scan") instead of replacing the search's count. The gate
  repair touches only a scope manifest's entry list (vendored third-party paths out of the
  whole-tree measurement scope) - it changes no compiled source, no baseline and no product
  behaviour.
- **Realtime, undo and commands**: the change is a GUI label only - nothing on the audio thread
  allocates, frees, locks, waits or logs; undo is unchanged; no new control command was added (the
  ControlCommandsSnapshot canary is untouched).
- **Verdict with evidence**: FIXED. The bug was live in this tree before the change
  (`src/gui/BrowserSearchView.cpp:465-478` wrote the scan's message into `m_status`, the label the
  search writes its count into at `src/gui/BrowserSearchView.cpp:327`), and the RED run shows the
  poll replacing the search's "No results" with the scan's "Library index: 9 files, built ...".
- **Registration**: the new report file is registered in `tests/fork-sources.txt`;
  `include/BrowserSearchView.h` and `src/gui/BrowserSearchView.cpp` are fork-owned sources already
  registered there, so no `tests/upstream-modifications.txt` entry was needed. The manifest repair
  changes no registration: the vendored paths it removes were never required to be registered
  (`tests/fork-sources-gate.sh`'s `is_excluded()` skips `plugins/SfzPlayer/sfizz/*`, and the
  gate's own log shows no unregistered tracked source).
