Verdict: FIXED

Cluster new-p23-77 (BUG 2): pending-write recovery runs for an empty filename.

## Evidence

Read path before the fix: `DataFile::DataFile( const QString & _fileName, ... )` calls `recoverPendingWrite( _fileName )` at [src/core/DataFile.cpp:455](src/core/DataFile.cpp:455). For an empty name, `fileName + ".new"` is `./.new`, a path in the working directory. `QFile::exists` then `isCompleteDocument`, and on an incomplete temp `QFile::remove( pending )` (line [src/core/DataFile.cpp:374](src/core/DataFile.cpp:374), which an unnamed load reached before the fix). So `DataFile()` built from an empty name could delete an unrelated interrupted-save temp in the working directory, and the report line "Discarded incomplete ... left by an interrupted save of" printed while no save of that file had ever been made.

Fix (at the cause, inside the function that derives the temp path): [src/core/DataFile.cpp:339-347](src/core/DataFile.cpp:339) returns at once when `fileName.isEmpty()`, before `./.new` is built at line 348. The guard lives in `recoverPendingWrite` rather than only at its call site, so any later caller gets it too.

Callers and siblings checked:
- `recoverPendingWrite` has one caller, [src/core/DataFile.cpp:455](src/core/DataFile.cpp:455) (the QString constructor). The QByteArray constructor passes the name `"<internal data>"` and never reaches recovery (`src/core/DataFile.cpp` QByteArray constructor, at [src/core/DataFile.cpp:512](src/core/DataFile.cpp:512)).
- Named loads are unchanged: the guard only returns for an empty name, so every real path still runs recovery exactly as before.
- Save side, not changed: `DataFile::writeFile` ([src/core/DataFile.cpp:773](src/core/DataFile.cpp:773), temp at line 804) builds its temp from `nameWithExtension( filename )` ([src/core/DataFile.cpp:590](src/core/DataFile.cpp:590)), which appends an extension for a project type. Saving to an empty name therefore targets `.mmpz` (and `.mmpz.new`) in the working directory. That is a separate write-side question, not the recovery read this finding is about, and I did not change it. Not run; read only.
- Complete `./.new` for an empty name: pre-fix the code would call `renameOverwrite( ".new", "" )`. I did not run this case, so I make no claim about its outcome. The fix skips it either way.

Caller reach: the QString constructor is called with names that come from UI and control paths (for example [src/gui/FileBrowser.cpp:854](src/gui/FileBrowser.cpp:854), [src/core/ControlCommandsBrowserMedia.cpp:297](src/core/ControlCommandsBrowserMedia.cpp:297)). I did not establish that any of them can pass an empty name in practice. The guard is correct regardless.

Regression test: `tests/src/core/DataFileSaveIntegrityTest.cpp` `unnamedDataFileLeavesTheWorkingDirectoryTempAlone()` at [line 707](tests/src/core/DataFileSaveIntegrityTest.cpp:707). It writes a truncated `.new` into a `QTemporaryDir`, enters that directory with the `WorkingDirectory` RAII guard ([line 140](tests/src/core/DataFileSaveIntegrityTest.cpp:140)), constructs `DataFile{ QString() }` ([line 725](tests/src/core/DataFileSaveIntegrityTest.cpp:725)), and asserts the temp still exists and no "Discarded incomplete" line was printed.

## Commands and exit codes

Environment note: the first two build attempts failed for reasons outside the code. ccache writes its temp files to `zene-bunny-fix/dsh/...`, which the write-jail refuses (`build-1-red.log`, EXIT=2). I redirected only ccache's temp dir with `CCACHE_TEMPDIR=$TMPDIR/ccache-tmp-p23-77` for every later build. A `vexing parse` compile error in the test (`DataFile unnamed( QString() )`, `build-2-red.log`, EXIT=2) was fixed by brace init (`DataFile unnamed{ QString() }`).

| Step | Command (run from the worktree, or `build/tests` for ctest) | Log | EXIT |
|---|---|---|---|
| RED build (fix not yet written) | `CCACHE_TEMPDIR=$TMPDIR/ccache-tmp-p23-77 cmake --build build --target zene DataFileSaveIntegrityTest -j4` | `build-3-red.log` | 0 |
| RED test | `ctest -R '^DataFileSaveIntegrityTest$' --output-on-failure` (in `build/tests`) | `test-1-red.log` | 8 |
| GREEN build | same build command, after the fix | `build-4-green.log` | 0 |
| GREEN test | same ctest command | `test-2-green.log` | 0 |
| Family build | `cmake --build build --target DataFileFormatTest AudioFileProcessorView -j4` | `build-5-family.log` | 2 (see below) |
| Family test | `ctest -R '^(DataFileFormatTest\|DataFileSaveIntegrityTest\|AudioFileProcessorView)$'` | `test-3-family.log` | 0 |
| 4b: save patch | `git diff -- src/core/DataFile.cpp > fix.patch` | `fix.patch` | 0 |
| 4b: revert fix | `git apply -R fix.patch` | | 0 |
| 4b: build without fix | same build command | `build-6-4b-nofix.log` | 0 |
| 4b: test without fix | same ctest command | `test-4-4b-nofix-RED.log` | 8 (FAIL) |
| 4b: reapply fix | `git apply fix.patch` | | 0 |
| 4b: build with fix | same build command | `build-7-4b-fix.log` | 0 |
| 4b: test with fix | same ctest command | `test-5-4b-fix-GREEN.log` | 0 |

RED assertion (with the fix reverted): `FAIL! : DataFileSaveIntegrityTest::unnamedDataFileLeavesTheWorkingDirectoryTempAlone() 'QFile::exists( temp )' returned FALSE. (an unnamed DataFile discarded the working directory's .new temp)`. The test ran and failed on that line, so the run is not a stale or no-op result.

Tests executed: the save-integrity binary has 17 test functions. RED run: `Totals: 16 passed, 1 failed`. GREEN run: ctest `100% tests passed, out of 1` (the binary as one ctest entry). Family `^(DataFileFormatTest|DataFileSaveIntegrityTest|AudioFileProcessorView)$`: 2 registered tests run, both passed. `AudioFileProcessorView` is not registered in this configured tree: `build/tests/CTestTestfile.cmake` has 0 matches for it, and there is no build target with that name (`build-5-family.log` line 20, `No rule to make target 'AudioFileProcessorView'`, EXIT=2). I did not run it. The other family member named in the finding, `DataFileFormatTest`, passed.

## Gates

`feature_check.py` could not complete here (EXIT=1 with `PermissionError: [Errno 13]` at `logs.mkdir`, for `zene-qwen-new-p23-77-feature-check`). It writes its logs to a sibling of the worktree and caches baselines under `zene-bunny-fix/qwen/gate-baselines`, both outside the write-jail. Its log folder and base cache also fall outside it, so the script cannot finish from the sandbox. I ran the same commands it runs, from the worktree, with logs in the LOG FOLDER:

| Gate | Command | EXIT |
|---|---|---|
| fork-sources | `bash tests/fork-sources-gate.sh` | 0 |
| all-sources-reproduce | `bash tests/all-sources-reproduce.sh` | 0 |
| no-upstream-regression | `bash tests/no-upstream-regression-gate.sh` (re-run after registration: 0) | 0 |
| check-namespace | `tests/scripted/check-namespace` | 0 |
| no-tautology | `bash tests/no-tautology-gate.sh` | 0 |
| doc-figures | `python3 tests/doc-figures-gate.py` | 0 |
| complexity | `bash tests/complexity-gate.sh --check` | 1 |
| file-length | `bash tests/file-length-gate.sh --check` | 1 |

The two ratchet gates (complexity, file-length) exit 1, which the brief says is expected on this train. Their REGRESSION lines (`gate-complexity.log`, `gate-file-length.log`) name no change in `src/core/DataFile.cpp` or `tests/src/core/DataFileSaveIntegrityTest.cpp`. Every listed item is in a file this branch does not touch. The script's own comparison against the base uses the baseline cache, which I could not compute, so this is my reading of the lines and not the script's verdict.

Registration: `src/core/DataFile.cpp` is an inherited file, so I added a reason line to `tests/upstream-modifications.txt` (tab-separated, `new-p23-77:` prefix, same format as the existing lines). `tests/src/core/DataFileSaveIntegrityTest.cpp` is already registered in `tests/fork-sources.txt`. No new files were added.

## Self-check

- **The regression test FAILS with the product change reverted and PASSES with it.** RED: with the fix reverted by `git apply -R fix.patch`, the test fails with the assertion above (`test-4-4b-nofix-RED.log`, EXIT=8). GREEN: with the fix reapplied, `test-5-4b-fix-GREEN.log` EXIT=0.
- **The test is deterministic: no wall-clock timing, sleeps or races.** It uses a `QTemporaryDir` with fixed content, a process-wide working-directory change restored by the `WorkingDirectory` destructor (it runs even when a QVERIFY returns early), and the existing `MessageRecorder` to capture Qt log lines. Nothing depends on timing or on another thread.
- **The fix is at the cause and complete.** The guard sits in `recoverPendingWrite` (`src/core/DataFile.cpp:339-347`), the function that derives the temp path from the name. Its one caller is `src/core/DataFile.cpp:455`, and the QByteArray constructor never reaches it. The save-side sibling (`writeFile`, line 804) is a separate path and is not changed; see Evidence.
- **No collateral damage.** The guard only returns for an empty name. For any non-empty name, recovery runs exactly as before. Old projects and saved data are not touched: the change is in an error-path recovery step on open, and the existing `DataFileFormatTest` and `DataFileSaveIntegrityTest` cases all pass.
- **Verdict FIXED, so this item is not a claim of ALREADY-FIXED or FALSE-POSITIVE.** Evidence for the defect is the RED run above and the code at `src/core/DataFile.cpp:339-348` (line 374 is the `QFile::remove( pending )` call that an unnamed path reached before the fix).
- **Realtime, undo, commands.** The change is in the file-open path only (`DataFile` construction on load), never on an audio or realtime path. No allocation or lock is added in any path that runs per period. No undo step and no control command is added.
