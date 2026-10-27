FIXED

# Cluster new-p17-271: a short collection-store write is reported as success

`writeAll()` in `src/core/ControlCommandsBrowserCollections.cpp` ignored the return value of `QSaveFile::write()` and committed whatever had been written. The finding is real on this Qt 6 build. A payload larger than Qt's internal write buffer goes straight to the file. When the kernel cuts that write short, `write()` returns fewer bytes and sets no error flag, so `commit()` publishes the truncated JSON, and `browser.collection_save` and `browser.collection_remove` answer success. The fix checks the byte count and cancels the save on a short write. On success the bytes written are unchanged.

## Evidence

- Bug site at the base (`639e1d23e`): `src/core/ControlCommandsBrowserCollections.cpp:93-98`. The `file.write(...)` result is discarded and the function returns `file.commit()`.
- Fixed code: `src/core/ControlCommandsBrowserCollections.cpp:93-104`. `file.write(bytes) != bytes.size()` cancels the save with `cancelWriting()` and returns false.
- One write point for both mutating commands: `store()` at `:108-119` calls `writeAll(next)` at `:113`, and `save` (`:121-139`) and `remove` (`:142-150`) both call `store()`. The refusal message is unchanged. A failed save returns before `addUndoStep` at `:117`, so it adds no undo step.
- Short-write reproduction, run in a scratch probe outside the worktree. Source and output are copied to the LOG folder as `probe-short-write-*.cpp` and `probe-short-write-*-run.log`:
  - 73-byte payload, `RLIMIT_FSIZE`=8: `wrote=73 of 73 error=0`, `commit=0`, the store keeps `{"old":1}`. Qt buffers a small write, and the failure surfaces at the flush inside `commit()`. Small payloads were already refused.
  - 200000-byte payload, `RLIMIT_FSIZE`=8: `wrote=8 of 200000 error=0`, `commit=1`, and the store on disk becomes 8 bytes (`xxxxxxxx`). This is the reported failure.
- Siblings checked. `src/core/BrowserTagStore.cpp:231`, `src/gui/ControlCommandsShortcuts.cpp:165` and `:244`, and `src/core/SmfInterchange.cpp:303` all compare `write()` with the size before `commit()`. The fix now matches them.
- Not changed, on purpose:
  - `src/core/BrowserIndex.cpp:384` has the same unchecked `write()`, but it writes the browser index file, not the collections store. Follow-up candidate, outside this finding.
  - The undo and redo lambdas at `ControlCommandsBrowserCollections.cpp:117` still ignore `writeAll`'s result. A failed undo write now leaves the store as it is instead of publishing a truncated file. Whether undo should report that failure is a separate question.

## Regression test

`tests/src/core/BrowserCollectionStoreWriteTest.cpp` (new). It is registered in `tests/CMakeLists.txt` beside `LibraryProjectsTest` and in `tests/fork-sources.txt`. Test `aShortCollectionStoreWriteIsRefusedNotPublished`:

1. Saves `Snares` and then `Hats`, where `Hats` has a 20000-character query. Every store written afterwards is larger than Qt's write buffer, so the write goes straight to the file.
2. Under `RLIMIT_FSIZE`=8 (`SIGXFSZ` ignored only inside the scope, previous values restored), `browser.collection_save` must be refused with `Refused`, and so must `browser.collection_remove`.
3. The store file must be byte-identical to before, and `browser.collections` must still list two collections.

It is deterministic: no sleeps, no timing, and the size cap is enforced by the kernel. It is POSIX-only and `QSKIP`s elsewhere. The test points `ZENE_BROWSER_COLLECTIONS` at its own file in its temp directory. The first version of the test lived in `LibraryProjectsTest.cpp`, but that pushed the file to 572 code lines, over the 500-line file-length limit. That limit is not grandfathered for this file, so the test moved to its own file. `LibraryProjectsTest.cpp` is unchanged from the base.

## Commands and exit codes

Environment note: the default ccache temp directory is not writable from this sandbox, so every build ran with `CCACHE_TEMPDIR=$TMPDIR/ccache-tmp-p17-271`. All logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p17-271.r1/logs/`.

| Step | Command (run from the worktree, test runs from `build/tests`) | Log | EXIT |
|---|---|---|---|
| RED: fix reverted | `git apply -R logs/fix.patch`; `cmake --build build --target zene -j4` | `build-red-6.log` | 0 |
| RED: test | `ctest -R BrowserCollectionStoreWriteTest --output-on-failure` | `test-red-6.log` | 8 |
| GREEN: fix restored | `git apply logs/fix.patch`; `cmake --build build --target zene -j4`; `cmake --build build --target BrowserCollectionStoreWriteTest -j4` | `build-green-2.log` | 0, 0 |
| GREEN: test | `ctest -R BrowserCollectionStoreWriteTest --output-on-failure` | `test-green-2.log` | 0 |
| Family | rebuild `LibraryProjectsTest LibraryMenusTest ControlBrowserCommandsTest BrowserCollectionStoreWriteTest` (`build-family-2.log`), then `ctest -R "LibraryMenusTest\|ControlBrowserCommandsTest\|LibraryProjectsTest\|BrowserCollectionStoreWriteTest"` | `test-family-2.log` | 0, 0 |
| Gates | `feature_check.py` through `fc_driver.py` (see below) | `gates-feature-check-3.log` | 0 |

- RED output: `FAIL! : BrowserCollectionStoreWriteTest::aShortCollectionStoreWriteIsRefusedNotPublished() '!saved.ok' returned FALSE. (a save whose store write was cut short was reported as success)`, at `tests/src/core/BrowserCollectionStoreWriteTest.cpp:136`. Totals: 2 passed, 1 failed. The two passes are the init and cleanup slots.
- GREEN: 1/1 passed.
- Family: 4/4 passed (`ControlBrowserCommandsTest`, `LibraryProjectsTest`, `BrowserCollectionStoreWriteTest`, `LibraryMenusTest`).
- The 4b check ran in the order above. The patch is `git diff -- src/core/ControlCommandsBrowserCollections.cpp`, the only product file changed.

Gates, with a gap I did not work around inside the script. `feature_check.py` writes its logs to `<worktree>/../<name>-feature-check/`, which is outside the write-jail. It fails on its first step with `PermissionError` and exits 1 (`gates-feature-check-1.log`). I did not edit the script. I ran the same gate functions and commands through `fc_driver.py` (copied to the LOG folder), with logs redirected into the LOG folder. The driver reads the cached base listings for `639e1d23e`, which already existed. Result: 8 of 8 rows PASS, exit 0 (`gates-feature-check-3.log`). Rows: fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures, complexity ("no new item in your files") and file-length ("no new item in your files"). An earlier version of the test in `LibraryProjectsTest.cpp` failed file-length (`gates-feature-check-2.log`); that is why the test moved.

## Self-check

1. **Fails without the fix, passes with it.** Yes. Reverting the product change gives RED with exit 8 (`test-red-6.log`), and restoring it gives GREEN with exit 0 (`test-green-2.log`).
2. **Deterministic.** Yes. The short write comes from a kernel-enforced `RLIMIT_FSIZE` cap, with `SIGXFSZ` ignored only inside the scope. The 20000-character payload forces the direct-write path. No sleeps, timing or races are involved.
3. **Fix at the cause, siblings checked.** Yes. `writeAll()` is the only write path for the collections store, and both `save` and `remove` go through it. The siblings that already check the write count are listed above. `BrowserIndex.cpp:384` is the same bug in a different file and is left for a follow-up.
4. **No collateral damage.** On success the bytes are the same: the same `QJsonDocument::toJson(Indented)` call, the same file and format. No limit or check is added to any caller. A refused save returns the same `Refused` message as before (`store()`, `:113-115`). The only behaviour that changes is the case where a write was cut short.
5. **Realtime, undo, commands.** Nothing here runs on the audio thread. No command is added or changed, so the command snapshot is unchanged. `submit_check.py` matches any change under `ControlCommands*` files, so its snapshot item will read FAIL for a handler-body change with no command id or schema change. A refused save adds no undo step.
6. **Registration and gates.** `tests/CMakeLists.txt` and `tests/fork-sources.txt` are updated. The gates result is in the table above (8/8 PASS through the driver).
7. **Verdict-specific question.** Not applicable: the verdict is FIXED, not ALREADY-FIXED or FALSE-POSITIVE.
8. **Sibling commands in the changed command file.** `src/core/ControlCommandsBrowserCollections.cpp` registers four commands: `browser.collection_save` (`:193`), `browser.collection_remove` (`:201`), `browser.collections` (`:203`) and `browser.collection_run` (`:204`). Save and remove both go through `store()` → `writeAll()` and are fixed by this change. `browser.collections` and `browser.collection_run` only read the store (`readAll`), and `writeAll` is in this file's anonymous namespace, so they are not affected. The other `browser.*` ids that the check lists are registered in other files and do not reach `writeAll`.
9. **`submit_check.py` gates item: FAIL, environment only.** `submit_check.py` runs `feature_check.py` itself, and that script creates `<worktree>/../<name>-feature-check/`, outside the write-jail. It exits 1 with `PermissionError` at `logs.mkdir` (`gates-feature-check-1.log`). The gate commands and logic were run through the driver instead, and all 8 rows pass (`gates-feature-check-3.log`). The `submit_check` run that produced this item is `submit-check-1.log` in the LOG folder; every other item in it passes.
