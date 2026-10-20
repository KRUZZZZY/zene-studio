FIXED

Cluster new-p12-255 — Concurrent downloads overwrite the same partial file.

Cause and fix (production code, unchanged this round):
- `src/core/StemModelStore.cpp:414-422`: each download stages into a unique
  `<name>.onnx.part.<uuid>` path, so concurrent downloads of one model never share
  or truncate one partial file.
- `src/core/StemModelStore.cpp:445-464`: after a verified staging file is ready, the
  final path is removed and the staging file renamed into place. If a concurrent
  installer wins that rename, `QFile::rename()` returns false because the destination
  now exists (Qt's documented contract: rename fails when the destination already
  exists and does not overwrite it), but the installed file verifies against the
  pinned spec, so this attempt reports success. The cleanup at
  `src/core/StemModelStore.cpp:458` removes this attempt's now-redundant staging file
  instead of leaving a full-size duplicate model behind.

Test seam:
- `include/StemSeparation/StemModelStore.h:116-123` /
  `src/core/StemModelStore.cpp:446-449,468-473`:
  `StemModelStore::setDownloadRenameHook(...)` invokes a caller-supplied hook
  between `QFile::remove(finalPath)` and `QFile::rename(partPath, finalPath)`.
  Production leaves it unset.
- `tests/src/core/StemModelStoreTest.cpp:599-708`:
  `testDownloadRenameLoserReportsSuccessAndRemovesStaging` uses the hook to
  deterministically create a verified final file in that window, then asserts
  `download()` returns true, the final file verifies, and no staging file remains.

This round (r6, rework of r5): the hook no longer swallows its own failures.
`tests/src/core/StemModelStoreTest.cpp:674-687` records four facts - the hook ran
(`hookRan`), it opened the destination (`hookOpenedDestination`), it wrote every
expected byte (`hookWroteAllBytes`), and the destination it left behind verifies
against the pinned spec (`hookDestinationVerified`, computed inside the hook, before
`download()` continues) - and `tests/src/core/StemModelStoreTest.cpp:696-699` asserts
all four. If the injection had failed, the ordinary rename would have succeeded and
the rename-loser path would never have been exercised; the test now fails instead of
passing through that path.

Evidence (all commands run from the worktree
/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p12-255,
logs in .../runs/new-p12-255.r6/logs):

| Step | Command | EXIT | Result |
| --- | --- | --- | --- |
| Build (cleanup present) | `cmake --build build --target zene StemModelStoreTest -j16` (log `build-1.log`) | 0 | built |
| GREEN, focused ctest | `ctest -R '^StemModelStoreTest$' --output-on-failure` in `build/tests` (log `test-1.log`) | 0 | 1/1 test passed (443 StemModelStoreTest) |
| GREEN, binary detail | `./StemModelStoreTest -silent` in `build/tests` (log `test-2.log`) | 0 | `22 passed, 0 failed` |
| RED: reverse only the cleanup | `git apply -R .../logs/fix.patch` (removes `QFile::remove(partPath);` at `src/core/StemModelStore.cpp:458`) | 0 | source file now has the cleanup removed |
| RED build | `cmake --build build --target StemModelStoreTest -j16` (log `build-2-cleanup-reversed.log`) | 0 | built |
| RED, focused ctest | `ctest -R '^StemModelStoreTest$' --output-on-failure` in `build/tests` (log `test-3-cleanup-reversed.log`) | 8 | `FAIL! ... 'partialFiles(dir.path(), spec.name).isEmpty()' returned FALSE. (leftover staging files: rename-loser.onnx.part.89c9d759-eb3)`, `21 passed, 1 failed` |
| Restore the cleanup | `git apply .../logs/fix.patch` | 0 | source file restored to the committed state |
| GREEN build | `cmake --build build --target zene StemModelStoreTest -j16` (log `build-3-restored.log`) | 0 | built |
| GREEN again, focused ctest | `ctest -R '^StemModelStoreTest$' --output-on-failure` in `build/tests` (log `test-4-restored.log`) | 0 | 1/1 test passed (443 StemModelStoreTest) |
| Stem family | `ctest -R '^Stem' --output-on-failure` in `build/tests` (log `test-6-stem-family.log`) | 0 | 4/4 passed: StemExportTest, StemJobManagerTest, StemModelStoreTest, StemSplitPipelineTest |
| Gate (all) | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` (log `gate-1-feature-check.log`) | 0 | `RESULT: PASS`; complexity and file-length rows: "no new item in your files" |
| Gate: complexity ratchet | `bash tests/complexity-gate.sh --check` (log `gate-2-complexity.log`) | 1 | 6 `REGRESSION:` rows, all in files this commit does not touch (`tests/src/gui/XyPadPairTest.cpp`, `src/core/DawProjectReadTracks.cpp`, `src/core/ControlCommandsMixerTools.cpp`, `src/core/ControlCommandsMixerStrip.cpp`, `src/core/ProjectRevisions.cpp`, `src/gui/ControlCommandsShortcuts.cpp`) |
| Gate: file-length ratchet | `bash tests/file-length-gate.sh --check` (log `gate-3-file-length.log`) | 1 | 14 `REGRESSION:` rows, all in files this commit does not touch (`src/core/SmfInterchangeReader.cpp`, `src/core/TakeLane.cpp`, `tests/src/core/{CollabTransport,ControlAutomationScript,ControlDeviceCatalogue,ControlEditCommands,CustomAccess,DeviceDsp,HardwareIo,NotePlayback,RackMacros,RevisionTimeline,VideoTrack}Test.cpp`, `tests/src/gui/ArrangerTimelineTest.cpp`) |
| Pre-submit check | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/submit_check.py <worktree> new-p12-255` (log `submit-2.log`) | 0 | RESULT: PASS |

The two ratchet gates are red on the train, not on this commit: the `REGRESSION:` rows
are byte-identical to the r5 run before this change (`diff` of the sorted
`REGRESSION:` lines of `runs/new-p12-255.r5/logs/qa-07-complexity-gate.log` and
`qa-08-file-length-gate.log` against `runs/new-p12-255.r6/logs/gate-2-complexity.log`
and `gate-3-file-length.log` reports no difference), none of them names
`src/core/StemModelStore.cpp`, `include/StemSeparation/StemModelStore.h` or
`tests/src/core/StemModelStoreTest.cpp`, and `feature_check.py`, which compares the
same gates against the base commit `de5a85a96` and fails only on items new in the
changed files, reports PASS (EXIT 0).

Files committed this round:
- `tests/src/core/StemModelStoreTest.cpp` (the hook now records and asserts that it
  ran, opened the destination, wrote all expected bytes and left a destination that
  verifies against the pinned spec).

## Self-check

- The regression test FAILS with the product change reverted and PASSES with it
  (exit codes). RED: `git apply -R .../logs/fix.patch` (EXIT 0) removed only
  `QFile::remove(partPath);` at `src/core/StemModelStore.cpp:458`;
  `cmake --build build --target StemModelStoreTest -j16` (EXIT 0);
  `ctest -R '^StemModelStoreTest$' --output-on-failure` in `build/tests` (EXIT 8) with
  `FAIL! ... 'partialFiles(dir.path(), spec.name).isEmpty()' returned FALSE. (leftover
  staging files: rename-loser.onnx.part.89c9d759-eb3)`, `21 passed, 1 failed`.
  GREEN: `git apply .../logs/fix.patch` (EXIT 0);
  `cmake --build build --target zene StemModelStoreTest -j16` (EXIT 0);
  `ctest -R '^StemModelStoreTest$' --output-on-failure` in `build/tests` (EXIT 0), 1/1
  test passed, binary reports `22 passed, 0 failed`. The patch artifact
  `runs/new-p12-255.r6/logs/fix.patch` is that single-hunk reversal of the product
  fix; reversing the whole product diff would also delete the test seam and the test
  would not compile, so the targeted hunk reversal is the meaningful proof. The RED
  run failed at the leftover-staging assertion, which is *after* the four new hook
  assertions - so the hook had run, opened, written and verified, i.e. the
  rename-loser branch was genuinely the code under test.
- The test is deterministic: no wall-clock timing, sleeps or races. The seam is
  `StemModelStore::setDownloadRenameHook(...)`
  (`include/StemSeparation/StemModelStore.h:120`), invoked synchronously by
  `download()` between `QFile::remove(finalPath)` (`src/core/StemModelStore.cpp:445`)
  and `QFile::rename(partPath, finalPath)` (`src/core/StemModelStore.cpp:450`). The
  hook's outcome is not raced for: the hook writes the pinned bytes and verifies them
  before returning, and the four recorded facts are asserted after `download()`
  returns. The injected values are fixed (`"abc"` and the SHA-256 of `"abc"`). The
  only timing in the test is the HTTP server's incidental 50 ms response delay and
  the 100 ms disconnect timer - neither decides pass/fail.
- The fix is at the cause and complete: `StemModelStore::download()` is the single
  installer path for a model (`src/core/StemModelStore.cpp:390-466`); its siblings
  (transfer failure at line 434, verify failure at line 442, rename failure at line
  462) all remove the same per-attempt staging file, and the rename-loser branch at
  line 458 now does too. The unique staging name is created per attempt at
  `src/core/StemModelStore.cpp:421-422`, so no other caller shares it. The hook is a
  private static (`src/core/StemModelStore.cpp:473`) that production never sets
  (`src/core/StemModelStore.cpp:446`), so no other caller's behaviour changes;
  `StemModelStore::setDownloadRenameHook` has exactly one caller, the test
  (`tests/src/core/StemModelStoreTest.cpp:678`), and it is reset to `{}` right after
  the download (`tests/src/core/StemModelStoreTest.cpp:694`).
- No collateral damage: no project or saved-data format is touched, no limit or
  check is added that other callers could trip over, and the only production
  behaviour change is the removal of this attempt's own staging file on the
  rename-loser branch. The verified-in-place fast path
  (`src/core/StemModelStore.cpp:400`), the offline and HTTPS/pinned refusals
  (`src/core/StemModelStore.cpp:401-406`) and the concurrent-downloads test
  (`tests/src/core/StemModelStoreTest.cpp:592-593`) are unchanged and still pass;
  `ctest -R '^Stem'` is 4/4 green.
- The verdict is FIXED, not ALREADY-FIXED or FALSE-POSITIVE, so the code-contract
  item does not apply. The evidence for it is the diff above: before this branch the
  code shared one `<name>.onnx.part` path opened with truncation
  (`src/core/StemModelStore.cpp:414` in the finding), and the branch now stages per
  attempt (`src/core/StemModelStore.cpp:421-422`) and cleans up the loser
  (`src/core/StemModelStore.cpp:458`).
