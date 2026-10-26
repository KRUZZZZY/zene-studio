IMPLEMENTED

## Integration onto `050/m0-train-16` (round 6, 2026-10-26)

The accepted patch (`feat/050-Q050-121-oc` @ `ea978bf5a`, base `2da2ed356`) was applied three-way onto
`050/m0-train-16` @ `cf3618e61`. Logs: `~/.local/share/zene-bunny-fix/qwen/runs/Q050-121.r6/logs/`.
The accepted design and its regression tests are unchanged; what changed is listed here.

Conflicts resolved:

- `src/gui/BrowserSearchView.cpp` - the patch moves `addPreviewEntries`/`showRowMenu` into
  `BrowserSearchViewMedia.cpp` (file-length split); the train had grown both: `removeCollection` (kept in
  `BrowserSearchView.cpp`) and a category submenu + `refreshCategories()` in `showRowMenu`. The moved
  `showRowMenu` in `BrowserSearchViewMedia.cpp` now carries the train's category entries verbatim.
- `data/manual/index.html` - the train's metadata-search/category text kept, the patch's Project Media
  usage/unused/cleanup text replaces the old one-line Project Media sentence.
- `tests/CMakeLists.txt`, `tests/fork-sources.txt` - both sides' entries kept; `tests/all-sources.txt`
  re-derived (its verify recipe prints `REPRODUCES`).
- `commands_snapshot.json` - regenerated live: 851 ids / 76 groups = train 849 + `project.media_usage` +
  `project.media_cleanup` (`snapshot-1.log`); README canary 851; `doc-figures-gate.py` PASS (`figs-2.log`).
- `docs/RELEASE-NOTES-v0.3.0-alpha.md` - A16 block re-taken: `rows=860 true_inverse=425 snapshot=42
  irreversible=25 not_mutating=368` (train 858 + one irreversible + one not_mutating); a Q050-121 note added.

Demonstrated regression fixed: `HelpAccessTest::theSearchViewListsColumns` asserted 5 result columns; the
patch adds the hidden Uses/State columns, so it failed with 7 (`test-4.log`, exit 8 - the accepted branch had
the same failure, its focused suite did not include this test). The test now asserts 7 columns, the five search
columns visible and Uses/State hidden by default, which is what the design does (`test-5.log`, exit 0).

Semantic difference from the accepted patch: the train added `sfzplayer` to the scanner's reference elements
(`src/core/ControlProjectAssets.cpp:98`), so an `.sfz` referenced by SfzPlayer now counts as a use and is
never an unused cleanup candidate. This only widens what is protected; no Q050-121 code was changed for it.

| Check | Command (exact, from `build/tests`, env of the brief, `HOME=/tmp/q121home`) | Saw | Exit |
|---|---|---|---|
| Build | `cmake --build build --target zene ControlProjectMediaTest BrowserProjectMediaTest LibraryMenusTest ControlProjectArchiveTest ReversibilityContractTest -- -j4` | built (`build-3.log`; `build-1` failed on a ccache temp-dir permission, `build-2` stopped by the session time limit; `CCACHE_TEMPDIR=/tmp/ccache-q121` fixed the first) | 0 |
| Focused suite | `ctest -R '^(ControlProjectMediaTest\|BrowserProjectMediaTest\|ControlMediaAgentTranscript\|ReversibilityContractTest\|ControlCommandsSnapshot\|agent_surface\|ControlProjectArchiveTest\|LibraryMenusTest\|HelpAccessTest\|BrowserCatalogTest\|BrowserCatalogQueryTest\|ControlBrowserCommandsTest\|BrowserTagMenuTest\|BrowserDragUrlTest)$' --output-on-failure` | 14/14 passed (`test-6.log`) | 0 |
| Revert proof | `ControlProjectMedia.cpp:89` `++file.usageCount;` -> `file.usageCount = 1;`, rebuilt (`build-rev.log`, 0), `ctest -R '^(ControlProjectMediaTest\|BrowserProjectMediaTest\|ControlMediaAgentTranscript)$'` | all 3 fail (`test-rev.log`) | 8 |
| Restored | line restored, rebuilt (`build-6.log`, 0), focused suite above | 14/14 (`test-6.log`) | 0 |

## Earlier rounds (round 5 report, kept)

Cluster Q050-121 — Show usage and unused assets in Project Media, on `feat/050-Q050-121-oc`.

Scope: this branch carries Q050-121 only. Review r4 found two independent features bundled
into it, Q050-173 (embedded audio metadata columns/editing) and Q050-192 (key/BPM search, Fits
this project, match mode). Their product code, tests, docs, manual text, command-snapshot
entries and release-note figures were removed from this tree; the combined earlier state is
kept intact on the branch `split/Q050-173-192-from-121` (created from the tip
`150df6604`, which already includes the loop's FLAC-tag QA fix) so those units can be cut from it as their own changes. `feat/050-Q050-173-oc`
is checked out in another worktree and was not touched.

Logs: `~/.local/share/zene-bunny-fix/qwen/runs/Q050-121.r5/logs/` (build-N.log, test-N.log).
Tests need a writable `$HOME`: `BrowserCatalogQueryTest` writes the browser tag store to
`~/Documents/Zene Studio/browser-tags.json`, which this session's write jail forbids, so every
`ctest` below ran as `HOME=/tmp/q121home ctest ...` with `Documents/Zene Studio/` created
there (an environment workaround only; no test was changed for it).

## What the unit does

Engine: `controlScanProjectAssets()` stays the only reference parser;
`controlScanProjectMedia()` (`src/core/ControlProjectMedia.cpp`) groups its references by
resolved file identity and counts every occurrence. `project.media_usage` is `not_mutating`;
`project.media_cleanup` is `irreversible` and needs `confirm: true` for a real removal
(`dry_run` previews). `project.missing_assets` also carries the per-reference `usage_count`.
GUI (`src/gui/BrowserSearchViewMedia.cpp`): **Project Media** (Uses and State columns),
**Unused Only**, and **Clean Up Selected...** with a confirmation that names every absolute
path before `project.media_cleanup` runs; Cancel changes nothing. GUI and agent use the same
two registered commands.

## Acceptance evidence

| Criterion | Command (exact) | Saw | Exit |
|---|---|---|---|
| Project Media shows a per-file usage count, including repeated clip and instrument references. | `HOME=/tmp/q121home ctest -R '^(ControlProjectMediaTest\|BrowserProjectMediaTest\|ControlMediaAgentTranscript)$' --output-on-failure` | `used.wav` referenced by one `audiofileprocessor` and two `sampleclip`s reports `usage_count=3`; the GUI `Uses` cell is `3`; the socket transcript returns the same count (`test-3.log`). | 0 |
| The unused list distinguishes unreferenced local assets from missing or externally referenced files. | same three tests | `unused`/`missing`/`external` are distinct states; only `media/unused.wav` is `cleanup_candidate`; `gone.wav` and the outside `external.wav` never are; the GUI Unused view keeps missing/external visible. | 0 |
| Cleanup previews the selected files, asks for confirmation, and leaves referenced assets untouched. | same three tests | A dry run names only the unused selection and writes nothing; a real removal without `confirm` is refused; GUI Cancel leaves the file; Confirm removes only the unused file; a file referenced after the preview is refused by the fresh scan; referenced and external files survive; a dirty open project is refused. | 0 |

Revert proof: with line 89 of `src/core/ControlProjectMedia.cpp` changed from
`++file.usageCount;` to `file.usageCount = 1;` (one count per file, not per reference) and
rebuilt (`build-rev.log`, exit 0), `ctest -R '^(ControlProjectMediaTest|BrowserProjectMediaTest)$'`
failed on `usageCountsRepeatedReferencesAndSeparatesStates` and
`projectMediaShowsUsageAndDistinctStates` (`test-rev.log`, exit 8). Restored and rebuilt
(`build-3.log`, exit 0), the three tests pass (`test-3.log`, exit 0).

## Shared checks

| Check | Command (exact) | Saw | Exit |
|---|---|---|---|
| Build | `cmake --build build --target zene ControlProjectMediaTest BrowserProjectMediaTest ReversibilityContractTest ControlProjectArchiveTest BrowserCatalogTest BrowserCatalogQueryTest ControlBrowserCommandsTest BrowserTagMenuTest LibraryMenusTest -- -j4` | built (`build-2.log`; the first attempt `build-1.log` was cut off by the 10-minute background limit, not by an error) | 0 |
| Focused suite | `HOME=/tmp/q121home ctest -R '^(ControlProjectMediaTest\|BrowserProjectMediaTest\|ReversibilityContractTest\|ControlMediaAgentTranscript\|ControlCommandsSnapshot\|agent_surface\|ControlProjectArchiveTest\|BrowserCatalogTest\|BrowserCatalogQueryTest\|ControlBrowserCommandsTest\|BrowserTagMenuTest\|LibraryMenusTest)$' --output-on-failure` | 12/12 passed (`test-2.log`) | 0 |
| MCP snapshot | `python3 .../snapshot_live.py $PWD` | regenerated live: 835 ids / 76 groups; the only ids added over the base are `project.media_usage` and `project.media_cleanup` (`snapshot-1.log`); `ControlCommandsSnapshot` passes | 0 |
| A16 histogram | `ctest -R '^ReversibilityContractTest$'` | published block `rows=844 true_inverse=420 snapshot=42 irreversible=23 not_mutating=359` (base 842 + `project.media_usage` not_mutating + `project.media_cleanup` irreversible); passes (`test-1.log`) | 0 |
| Docs figures | `python3 tests/doc-figures-gate.py` | `PASS` — README 835 ids / 76 groups (`figs-1.log`) | 0 |
| Agent surface | in the focused suite | `agent_surface` passes with the two new commands | 0 |

## Self-check

- **Each new test fails with the product change reverted, then passes restored** — see the
  revert-proof paragraph above (exit 8, then exit 0). `ControlMediaAgentTranscript` drives the
  same behaviour over the real socket.
- **Every acceptance criterion is proven by a test I ran** — the three rows above name the
  tests and exit codes.
- **One undo step, honest reversibility class** — `project.media_usage` is `not_mutating`;
  `project.media_cleanup` is `irreversible` (deleted bytes are outside project undo; the
  transaction records path/size/mtime, never a fake inverse). Both rows are in
  `ControlReversibilityTableArchive.cpp`; `ReversibilityContractTest` passes.
- **Nothing runs on the audio thread** — the functions added are `controlScanProjectMedia`
  (QFileInfo/QDirIterator over the project file and media folder), `controlCleanupProjectMedia`
  (stat + `QFile::remove`), the two command handlers, and the Search-view slots
  (`showProjectMedia`, `setShowUnused`, `cleanupSelectedMedia`) on the GUI thread. None is
  called from `AudioEngine`, a processor's `process()`, a play handle or a realtime scope.
- **Old projects** — no project-format change; old `.mmp`/`.mmpz` are read by the existing
  `controlScanProjectAssets()`; cleanup never rewrites the project document.
- **Portability** — only Qt APIs already used in the tree (`QFileInfo`, `QDirIterator`,
  `QMessageBox`, `std::function`); no `std::filesystem`, no POSIX-only call, no
  Qt-6-only API.
- **Gates** — `feature_check.py`, relocated to log under `/tmp/q121fc` (the stock script cannot
  create its sibling log directory in this jail; same script, only the log path changed), on
  the committed tree: all eight rows PASS, `RESULT: PASS`, exit 0 (`fc-2.log`). The first
  attempt (`fc-1.log`) failed `no-upstream-regression` only because the Q050-173 files were
  still committed at HEAD while the work tree no longer declared them; it passed once the
  removal was committed. The removal landed as two commits (`9c1800bb0`, `3db24cecb`); the
  first alone does not build, the pair does.
