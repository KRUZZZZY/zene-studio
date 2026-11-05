IMPLEMENTED

# Q050-438 — Document the existing Revisions UI

The manual and `docs/KNOWN-LIMITATIONS.md` now describe the Revisions list that is in the tree:
**Project tools ▸ Revisions...**, its row fields and `revisions.list` sources, **Compare With
Now** as a structural summary, and **Restore** as a confirmed file replacement that leaves the
open session alone. The obsolete "no interface" claim is gone. A registered GUI test opens the
real menu entry and holds the live list against both documents.

Acceptance:

| # | Criterion | Command and evidence | Exit |
|---|---|---|---|
| 1 | The documented Revisions list matches the current GUI command/menu and `revisions.list` sources | `RevisionsDocsTest::theDocumentsMatchTheMenuAndTheLiveList` triggers **Project tools ▸ Revisions...**, checks the opened `CommandListDialog` against `CommandLists::revisions()`, and requires both documents to name every source the live list reports. With both document hunks reverted to HEAD the test fails on the absent `autosave` source at `tests/src/gui/RevisionsDocsTest.cpp:281` (`ctest-docs-reverted-4.log`, exit 8); restored it passes. Head run: `ctest -R "RevisionsDocsTest\|RevisionTimelineTest"` from `build/tests` (`ctest-7.log`) | 0 at HEAD; 8 with the documents reverted |
| 2 | `KNOWN-LIMITATIONS` no longer claims there is no Revisions UI | `RevisionsDocsTest::theDocumentsNameTheProjectToolsListAndItsSources` reads `docs/KNOWN-LIMITATIONS.md` and fails on the old present-tense claim, then requires **Project tools ▸ Revisions...** and the retained limits. With the documents reverted it fails on the stale sentence at `tests/src/gui/RevisionsDocsTest.cpp:189` (`ctest-docs-reverted-4.log`, exit 8); restored it passes (`ctest-7.log`) | 0 at HEAD; 8 with the documents reverted |

Build/test logs (`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/Q050-438.r1/logs/`):
`build-7.log` (zene + RevisionsDocsTest + RevisionTimelineTest, exit 0), `ctest-7.log`
(RevisionsDocsTest + RevisionTimelineTest, 2/2, exit 0), `ctest-docs-reverted-4.log` (both document
hunks reversed, exit 8), `doc-figures-1.log` (exit 0); the pre-submit check also passes (exit 0).

## Self-check

- **Each new test FAILS with the product change reverted, then PASSES restored.** Product change
  = the two document hunks. I ran `cp` backups, `git checkout HEAD -- docs/KNOWN-LIMITATIONS.md
  data/manual/index.html`, then `ctest -R RevisionsDocsTest --output-on-failure` from
  `build/tests`: exit **8**, both test functions failed (criterion 2 at line 189, criterion 1 at
  line 281); log `ctest-docs-reverted-4.log`. I restored both files and ran `ctest -R
  "RevisionsDocsTest|RevisionTimelineTest"`: exit **0**, 2/2 passed; log `ctest-7.log`.
- **Every acceptance criterion is proven by a test I ran.** Criterion 1:
  `RevisionsDocsTest::theDocumentsMatchTheMenuAndTheLiveList` (part of `ctest-7.log`, exit 0).
  Criterion 2: `RevisionsDocsTest::theDocumentsNameTheProjectToolsListAndItsSources` (`ctest-7.log`,
  exit 0). The same test with the documents reverted fails both, exit 8
  (`ctest-docs-reverted-4.log`).
- **One undo step per edit; honest reversibility.** No command, handler, list action or
  reversibility table was added or changed. `revisions.list` and `revisions.compare` remain
  `not_mutating`; `revisions.restore` remains `true_inverse`; the GUI's **Restore** row action
  dispatches that same command and is confirmed. The new test only reads the menu/list and clicks
  **Compare With Now** (read-only). No new undo step exists to count.
- **Nothing added runs on the audio thread.** The change is Markdown/HTML plus one Qt GUI test;
  no `src/core` or audio-path file changed and nothing allocates, frees, locks, waits, does I/O
  or emits a signal on the audio thread. The test runs on the interface thread and stops the
  audio device in `initTestCase` as its neighbours do.
- **Old projects load and render exactly as before; new data round-trips.** No project reader,
  writer, format, revision store or data model changed; the fixture project is only read and
  hashed. `RevisionTimelineTest` (2/2 run above, exit 0) still covers list/compare/restore.
- **Portability (Qt 5.15 / MSVC / macOS < 10.15).** The new code uses only Qt APIs already used
  by the neighbouring GUI tests (`QMenu`, `QAction`, `QPushButton`, `QRegularExpression`,
  `QTemporaryDir`, `QTest::mouseClick`); no Qt-6-only API, no `windows.h` macro names, no
  `std::filesystem`, no POSIX-only call. No conditional compilation was needed.
