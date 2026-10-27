Verdict: FIXED

new-p16-222: FIXED - a `<z:subscenes>` section that carries text, CDATA or a processing instruction beside its rows is no longer claimed. It is kept verbatim, so the next save does not drop the payload. Whitespace-only text and comments still count as layout.

Evidence: the loader `readRows()` at [SessionSubScenes.cpp:60](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p16-222/src/core/SessionSubScenes.cpp:60) used to skip every non-element child (`if (!node.isElement()) { continue; }`), so `load()` claimed the section and `save()` rewrote it from the model. The fix keeps two skips and makes everything else fail:
- comments are skipped as before ([SessionSubScenes.cpp:64](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p16-222/src/core/SessionSubScenes.cpp:64));
- whitespace-only text and CDATA are skipped as layout ([SessionSubScenes.cpp:65](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p16-222/src/core/SessionSubScenes.cpp:65));
- any other non-element child (non-blank text, CDATA, processing instruction) returns false ([SessionSubScenes.cpp:68](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p16-222/src/core/SessionSubScenes.cpp:68)).

`load()` then takes the existing "unclaimed" path, which stores the section verbatim in `m_preserved` ([SessionSubScenes.cpp:128-131](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p16-222/src/core/SessionSubScenes.cpp:128)), and `save()` writes `m_preserved` back first ([SessionSubScenes.cpp:97-104](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p16-222/src/core/SessionSubScenes.cpp:97)).

Regression test: `SessionSubSceneTest::aSectionWithOtherContentIsKeptNotDropped()` at [SessionSubSceneTest.cpp:333-357](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p16-222/tests/src/core/SessionSubSceneTest.cpp:333), with helper `loadWithSection()` at [SessionSubSceneTest.cpp:362-390](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p16-222/tests/src/core/SessionSubSceneTest.cpp:362). It edits a saved project's section into four variants (text, CDATA, processing instruction, whitespace-only layout) and checks the rows read and the section the next save writes.

Design choices, to redirect if wanted:
- A processing instruction is treated as content, like text: the same silent drop applies to it, so it is covered by the same rule. The finding named only text and CDATA.
- Comments stay ignored, as before; they are not content the section models, and changing them would change behaviour outside this bug.
- Consequence for a file that has text beside valid rows: its rows are no longer read into the session (the section is kept as it came), the same contract as a malformed row. The finding asks for exactly this ("preserve the whole section when it contains other text").

| Step | Command (run from the stated directory) | Result |
| --- | --- | --- |
| Build, first try | `cmake --build build --target SessionSubSceneTest -j4 > logs/build-red-1.log 2>&1; echo EXIT=$?` | EXIT=2: ccache could not create a temp file under `dsh/home/ccache-tmp-r6` (Permission denied); no compile error |
| Build, retry with writable ccache temp dir | `CCACHE_TEMPDIR=$TMPDIR/ccache-tmp-new-p16-222 cmake --build build --target SessionSubSceneTest -j4 > logs/build-red-2.log 2>&1; echo EXIT=$?` | EXIT=0 |
| RED: test on unfixed code | `ctest -R '^SessionSubSceneTest$' --output-on-failure > logs/test-red-1.log 2>&1` (from `build/tests`) | EXIT=8; `FAIL! : aSectionWithOtherContentIsKeptNotDropped()`, `Actual (rows): 1`, `Expected 0`, at SessionSubSceneTest.cpp:342 |
| GREEN: build product and tests | `cmake --build build --target zene SessionSubSceneTest -j4 > logs/build-green-1.log 2>&1` | EXIT=0 |
| GREEN: sub-scene family, both targets | `cmake --build build --target SessionSubSceneGridTest -j4 > logs/build-green-2.log 2>&1`; then `ctest -R 'SessionSub' -V > logs/test-green-3.log 2>&1` (from `build/tests`) | EXIT=0 and EXIT=0; SessionSubSceneTest 9 passed, 0 failed (Totals includes initTestCase and cleanupTestCase; 7 test functions); SessionSubSceneGridTest 4 passed, 0 failed; 2/2 tests |
| 4b: revert product fix | `git diff -- src/core/SessionSubScenes.cpp > logs/fix.patch && git apply -R logs/fix.patch` | EXIT=0 (apply -R) |
| 4b: rebuild and run without fix | `cmake --build build --target SessionSubSceneTest -j4 > logs/build-4b-unfixed.log 2>&1`; `ctest -R '^SessionSubSceneTest$' -V > logs/test-4b-unfixed.log 2>&1` | build EXIT=0; test EXIT=8; same FAIL at SessionSubSceneTest.cpp:342, `Actual (rows): 1`; Totals 8 passed, 1 failed |
| 4b: reapply fix and run with fix | `git apply logs/fix.patch`; `cmake --build build --target SessionSubSceneTest -j4 > logs/build-4b-fixed.log 2>&1`; `ctest -R '^SessionSubSceneTest$' -V > logs/test-4b-fixed.log 2>&1` | apply EXIT=0; build EXIT=0; test EXIT=0; Totals 9 passed, 0 failed |
| Gate: `feature_check.py` as written | `python3 .../qwen/feature_check.py <worktree> > logs/feature-check-1.log 2>&1` | EXIT=1 before any gate ran: it creates `<worktree>/../zene-qwen-new-p16-222-feature-check/` and that path is outside the write-jail (PermissionError, `logs/feature-check-1.log`). Not a product failure |
| Gates run one by one from the worktree (the script's own commands) | `fork-sources-gate.sh`, `all-sources-reproduce.sh`, `no-upstream-regression-gate.sh`, `tests/scripted/check-namespace`, `no-tautology-gate.sh`, `doc-figures-gate.py` | EXIT=0 for all six (logs `logs/gate-*.log`) |
| Ratchet gates, same commands | `complexity-gate.sh --check`; `file-length-gate.sh --check` | EXIT=1 for both (`logs/gate-complexity.log`, `logs/gate-file-length.log`); every REGRESSION line names a file this branch does not touch; none names `SessionSubScenes.cpp` or `SessionSubSceneTest.cpp` (the `readRows` function is not listed as over CCN 10). Matches the train-wide red the task describes |
| Completion check | `python3 .../qwen/submit_check.py <worktree> new-p16-222 > logs/submit-check-1.log 2>&1` | EXIT=1 before commit: commit, clean-tree, report and self-check rows were pending; the gates row fails for the feature_check path reason above |

Files committed for this verdict: `src/core/SessionSubScenes.cpp`, `tests/src/core/SessionSubSceneTest.cpp` (existing test file; already registered in `tests/fork-sources.txt`, so no registration change), and this report. No header changed, so no other target needed a rebuild. No new file, so `tests/fork-sources.txt` and `tests/upstream-modifications.txt` are unchanged.

Noticed, not changed: `SessionModel.cpp:566` (the `<z:scenes>` reader, `restoreScenesSection`) skips non-element children the same way (`if( !node.isElement() ) { continue; }`). It is a different section and outside this finding; it probably has the same data-loss shape and deserves its own finding.

## Self-check

1. **RED fails, GREEN passes.** Yes. RED: `logs/test-red-1.log` (EXIT=8, SessionSubSceneTest.cpp:342, `Actual (rows): 1`). GREEN: `logs/test-green-3.log` (EXIT=0, 9/9 and 4/4). Step 4b reverted and reapplied the product change: `logs/test-4b-unfixed.log` EXIT=8, `logs/test-4b-fixed.log` EXIT=0.
2. **Deterministic.** The test has no clock, sleep, thread or race. It uses `QTemporaryDir` (`m_dir`), fixed XML strings and a save-then-load round trip through the real project file.
3. **Fix is at the cause; callers and siblings checked.** `readRows()` has one caller, `load()` (SessionSubScenes.cpp:128). The save path already writes `m_preserved` verbatim when set (SessionSubScenes.cpp:97-104), so no save change is needed. The sibling loader in `SessionModel.cpp:566` is a different section; it is reported above, not changed.
4. **No collateral damage.** Layout (whitespace-only text), comments, and rows-only sections load and save as before. Existing tests `theProjectFileKeepsThemInTheirOwnSection` (byte-for-byte resave) and `aProjectWithoutSubScenesIsUnchanged` pass with the fix (9/9 in `logs/test-green-3.log`). The `MaxCount` check is unchanged, so no limit moved. The one behaviour change is intended and is the finding's own contract: a section with text beside valid rows is kept verbatim rather than read.
5. **Not applicable.** Only the load path changed. Nothing runs on the audio thread. No control command added. No undo step involved.
6. **If the verdict were ALREADY-FIXED or FALSE-POSITIVE:** not applicable; the verdict is FIXED, with the bug at SessionSubScenes.cpp:60-68 before the change.
7. **Gates.** The `feature_check.py` gate row could not run as written because of the write-jail (the log folder is outside the worktree). The six simple gates and the two ratchets were run from the worktree with their own commands and logged; the ratchet REGRESSION lines do not name files this branch touches. Ask the owner whether that counts as a pass, or give the lane a writable log path.
