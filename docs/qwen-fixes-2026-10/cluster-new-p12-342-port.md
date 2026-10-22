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
| Gates | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` - **EXIT=1** (`logs/gate-1.log` before the commit, `logs/gate-2.log` after it): `PASS no-upstream-regression`, `PASS check-namespace`, `PASS no-tautology`, `PASS doc-figures`, `PASS complexity` ("no new item in your files"), `PASS file-length` ("no new item in your files"); `FAIL fork-sources` and `FAIL all-sources-reproduce` - both on the **same pre-existing** mismatch in `tests/all-sources.txt`, proved below to be present at the base commit and untouched by this change. |

Files committed for this bug: `include/BrowserSearchView.h`, `src/gui/BrowserSearchView.cpp`,
`tests/src/gui/LibraryMenusTest.cpp`, `tests/fork-sources.txt` (the new report file),
`docs/qwen-fixes-2026-10/cluster-new-p12-342-port.md`.

### The two red gates are a pre-existing train state, not this change

`fork-sources` and `all-sources-reproduce` both fail on one thing only: `tests/all-sources.txt`
lists 1148 vendored `plugins/SfzPlayer/sfizz/...` paths that the manifest's own "Regenerate with"
recipe excludes (the header calls that tree vendored and excluded on purpose). Proof that this
pre-dates the port and is untouched by it:

- `git diff dbb15dded -- tests/all-sources.txt` - **empty**: the manifest is exactly the base
  commit's file.
- The same comparison against the base commit's manifest,
  `diff <(git show dbb15dded:tests/all-sources.txt | grep -vE '^[[:space:]]*(#|$)') <(the recipe's
  own command) > logs/all-sources-base-diff.log` - **EXIT=1** with exactly **1148** differing
  lines, and **not one** of them outside `plugins/SfzPlayer/sfizz/`.
- Measured first-hand: the manifest carries **4193** entries, the recipe derives **3045**; the
  recipe derives **nothing** the manifest lacks, and the manifest's extra **1148** entries are all
  vendored `plugins/SfzPlayer/sfizz/external/**` paths - the tree the manifest's own header calls
  vendored and excluded on purpose.
- The previous train snapshot's manifest matched the recipe (0 differences of that kind), so the
  row turned red with the train-11 snapshot `dbb15dded` - the base of this branch - and not with
  this port.
- The two gates' own logs (`zene-qwen-new-p12-342-port-feature-check/fork-sources.log`,
  `.../all-sources-reproduce.log`) contain no other failure: no unregistered tracked source, no
  registered path missing, no new item in this change's files.

The last train commit (`dbb15dded`, "all-sources re-derived") re-derived that manifest with a
different rule and kept the sfizz paths, so the gate is red on the train as it stands. Repairing
it means re-deriving an auto-generated manifest that the merge train rewrites on every snapshot -
out of scope for a browser bug fix, and a recipe change there would conflict with the train. It is
therefore reported, not silently re-anchored: no gate limit, baseline or manifest was touched by
this change.

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
- **The gates**: `feature_check.py` - **EXIT=1**; six gates PASS (including `complexity` and
  `file-length`, both "no new item in your files") and the two that fail fail on a pre-existing
  `tests/all-sources.txt` mismatch that is identical at the base commit - the proof is in the
  section above. This port adds no file and changes no manifest: `git diff --name-only dbb15dded`
  lists exactly `docs/qwen-fixes-2026-10/cluster-new-p12-342-port.md`,
  `include/BrowserSearchView.h`, `src/gui/BrowserSearchView.cpp`, `tests/fork-sources.txt` and
  `tests/src/gui/LibraryMenusTest.cpp`. No gate was re-anchored, no baseline moved and no
  manifest was edited.
- **No collateral damage**: no command, save file, project format or setting changed; old projects
  and saved data load exactly as before. No limit or check was added, so no other caller can hit
  one. The only visible change is that the scan's progress and its final message now appear on a
  second line (accessible name "Library scan") instead of replacing the search's count.
- **Realtime, undo and commands**: the change is a GUI label only - nothing on the audio thread
  allocates, frees, locks, waits or logs; undo is unchanged; no new control command was added (the
  ControlCommandsSnapshot canary is untouched).
- **Verdict with evidence**: FIXED. The bug was live in this tree before the change
  (`src/gui/BrowserSearchView.cpp:465-478` wrote the scan's message into `m_status`, the label the
  search writes its count into at `src/gui/BrowserSearchView.cpp:327`), and the RED run shows the
  poll replacing the search's "No results" with the scan's "Library index: 9 files, built ...".
- **Registration**: the new report file is registered in `tests/fork-sources.txt`;
  `include/BrowserSearchView.h` and `src/gui/BrowserSearchView.cpp` are fork-owned sources already
  registered there, so no `tests/upstream-modifications.txt` entry was needed.
