FIXED

# Cluster new-p14-384 - different AAF files overwrote each other's extracted media

Finding: `src/core/AafImportModel.cpp` (historical line 121). In a saved project, the default extraction folder was
`<project>-aaf-media/<aaf base name>`. Every AAF in that project shares `<project>-aaf-media/`, so `/one/Reel.aaf` and
`/two/Reel.aaf` both extracted into `<project>-aaf-media/Reel`. Matching essence filenames overwrote media the first
import's clips still reference.

Note on the finding's "no existing test includes this header": `tests/src/core/AafImportTest.cpp` does include
`include/AafImport.h` and already tests `aafDefaultMediaDir`. The regression test was added to that file.

## Evidence (current tree, after the fix)

- `src/core/AafImportModel.cpp:133-137` - the saved-project branch now names the folder
  `<aaf base name>-<first 12 hex of SHA-256(absolute AAF path)>` inside `<project>-aaf-media/`. The hash is
  `QCryptographicHash` (stable across runs; `qHash` is seeded per process, so it cannot be used).
- `src/core/AafImportModel.cpp:124-129` - the never-saved branch is unchanged. It places the folder beside each AAF
  as `<aaf base name>-aaf-media`, so two AAFs can only share a folder if they share a directory and a base name,
  which cannot happen for different files in one directory.
- `include/AafImport.h:285` - the declaration's doc comment states the new folder rule.
- `src/core/ControlCommandsAafImport.cpp:107` - the `media_rule` text of `interchange.aaf_import` /`aaf_read` states the
  new default. This is a result string only; `tools/mcp-zene-control/zene_control/commands_snapshot.json` does not
  contain `media_rule` (`rg -n media_rule` finds it only in this source file), so the snapshot and canary are not moved.
- The 12-hex hash of `/in/Reel 1.aaf` is `4cb0fd651a1a` (`printf '/in/Reel 1.aaf' | sha256sum | cut -c1-12`).

Residual, not covered by this fix: an AAF re-exported to the same path after a first import gets the same folder, so
its new essence can overwrite the first import's extracted files. Covering that needs a content or mtime identity,
which is a design choice (it reads or stats the AAF at import time). Not changed here; flagged for a follow-up.

## Regression test

`tests/src/core/AafImportTest.cpp:252-267` - `twoAafsNamedAlikeGetSeparateMediaFolders()`:
- `/one/Reel.aaf` and `/two/Reel.aaf` in `/songs/Film.mmpz` must map to different folders, both under
  `/songs/Film-aaf-media/Reel`.
- The same AAF must map to the same folder again, so re-importing reuses it.
- Unsaved project: the two alike names must still map to different folders.

The existing assertion `theSmallHelpersSayWhatTheyMean` (`AafImportTest.cpp:241-242`) pinned the old saved-project
folder `/songs/Film-aaf-media/Reel 1`. Its expected value is now `/songs/Film-aaf-media/Reel 1-4cb0fd651a1a`. The
unsaved assertion (`/in/Reel 1-aaf-media`) is unchanged. This is a deliberate change to the folder contract the
finding asks for, not a weakened check: the old value is the bug.

The test uses no files, timers or threads: `aafDefaultMediaDir` is a pure function of its two strings.

## Commands and exit codes

| Step | Command (run in the worktree unless noted) | EXIT | Result |
| --- | --- | --- | --- |
| RED build, first try | `cmake --build build --target AafImportTest -j4 > .../logs/build-red-1.log 2>&1` | 2 | Not a product failure: ccache tried to write `/home/kruzzzzy/.local/share/zene-bunny-fix/dsh/home/ccache-tmp-r6`, which is outside the write-jail ("Permission denied"). |
| RED build, retry | `CCACHE_TEMPDIR=$HOME/.cache/zene-tmp/ccache-tmp-new-p14-384 cmake --build build --target AafImportTest -j4 > .../logs/build-red-2.log 2>&1` | 0 | Built with the test added and the product code unchanged. |
| **RED test** | `cd build/tests && ctest -R AafImportTest --output-on-failure > .../logs/test-red-1.log 2>&1` | **8** | `FAIL! : AafImportTest::twoAafsNamedAlikeGetSeparateMediaFolders() 'first != second' returned FALSE. (/songs/Film-aaf-media/Reel)`, `Loc: [AafImportTest.cpp(259)]`. |
| GREEN build | `CCACHE_TEMPDIR=... cmake --build build --target AafImportTest -j4 > .../logs/build-green-1.log 2>&1` | 0 | |
| GREEN build (zene) | `CCACHE_TEMPDIR=... cmake --build build --target zene -j4 > .../logs/build-green-2-zene.log 2>&1` | 0 | `ControlCommandsAafImport.cpp` and `AafImport.h` includers rebuilt. |
| **GREEN test** | `cd build/tests && ctest -R AafImportTest --output-on-failure > .../logs/test-green-1.log 2>&1` | **0** | 1/1 passed. |
| GREEN, per function | `cd build/tests && ./AafImportTest > .../logs/qtest-green-verbose.log 2>&1` | 0 | `Totals: 19 passed, 0 failed, 0 skipped`. |
| 4b: revert product diff | `git diff -- include/AafImport.h src/core/AafImportModel.cpp src/core/ControlCommandsAafImport.cpp > .../logs/fix.patch` then `git apply -R .../logs/fix.patch` | 0 / 0 | Only the test file remained modified. |
| 4b: mutant build | `cmake --build build --target AafImportTest -j4 > .../logs/build-mutant-revert.log 2>&1` | 0 | |
| **4b: mutant test** | `cd build/tests && ctest -R AafImportTest --output-on-failure > .../logs/test-mutant-revert.log 2>&1` | **8** | `theSmallHelpersSayWhatTheyMean` (`AafImportTest.cpp:241`, "Compared values are not the same") and `twoAafsNamedAlikeGetSeparateMediaFolders` (`:259`) both fail. |
| 4b: restore | `git apply .../logs/fix.patch`, then `cmake --build build --target zene AafImportTest AafImportMenuTest -j4 > .../logs/build-final.log 2>&1` | 0 / 0 | |
| 4b: restored test | `cd build/tests && ctest -R "^AafImportTest$" --output-on-failure > .../logs/test-final-aaf.log 2>&1` | 0 | PASS again. |
| Family | `cd build/tests && ctest -R "Aaf" --output-on-failure > .../logs/test-aaf-family-2.log 2>&1` | 8 | `AafImportTest` passed. `AafImportMenuTest` hung (see below). |
| Menu test, fix | `cd build/tests && timeout 60 ./AafImportMenuTest > .../logs/menu-fix-{1,2}.log 2>&1` | 124, 124 | Hung in the file-dialog step `aCancelledDialogRunsNothingAndAChosenFileIsImported`. |
| Menu test, pristine (A/B) | product patch reverted, rebuilt `AafImportMenuTest`, `timeout 60 ./AafImportMenuTest > .../logs/menu-pristine-{1,2}.log` | 124, 124 | Same hang with the product change reverted, so it predates this fix. Also recorded in `docs/qwen-fixes-2026-10/cluster-new-p9-546-port.md:98-99` (5/5 pristine hangs). |
| Gates | `python3 -I .../runs/new-p14-384.r1/feature_check_logdir.py <worktree> > .../logs/gates-2.log 2>&1` | 0 | RESULT PASS, all eight rows (see below). |

Gate notes:
- The unmodified `feature_check.py` exited 1 before running any gate. It writes its logs to
  `<worktree>/../<name>-feature-check`, which is outside this lane's write-jail (`gates-1.log`: `PermissionError`).
  I ran a copy with only two lines changed: its `ROOT` points at the qwen directory and its log folder at this lane's
  `logs/gates/`. The gate logic is the same. The copy is `.../runs/new-p14-384.r1/feature_check_logdir.py`.
- Gate table: fork-sources PASS, all-sources-reproduce PASS, no-upstream-regression PASS, check-namespace PASS,
  no-tautology PASS, doc-figures PASS, complexity PASS ("no new item in your files"), file-length PASS ("no new item in
  your files"). RESULT PASS, exit 0.

## Affected family

- `AafImportTest`: 1/1 ctest test, 19/19 QTest functions pass after the fix.
- `AafImportMenuTest` (family member by name, `tests/CMakeLists.txt`): it does not pass in this environment, with or
  without the fix. Its first two functions pass. The third (`aCancelledDialogRunsNothingAndAChosenFileIsImported`) times
  out in its modal file-dialog step, both with the fix (exit 124 twice) and with the product change reverted (exit 124
  twice). I did not pin the exact line of the hang. Its first step is a cancelled dialog, which runs no import, so
  `aafDefaultMediaDir` is not on that path. The hang is pre-existing and offscreen-specific, not something this fix
  introduces, but it means the family is not fully green in this environment.

## Files committed

- `include/AafImport.h`
- `src/core/AafImportModel.cpp`
- `src/core/ControlCommandsAafImport.cpp`
- `tests/src/core/AafImportTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p14-384.md`

No file registration was needed: no new source files were added, and `AafImportModel.cpp` is already in
`tests/fork-sources.txt` (line 1329) and `tests/all-sources.txt` (line 1459).

## Self-check

- **Regression test fails with the product change reverted, passes with it.** RED: `test-red-1.log`, ctest EXIT=8,
  `'first != second' returned FALSE` (`AafImportTest.cpp:259`). Mutant (`git apply -R fix.patch`): `test-mutant-revert.log`,
  EXIT=8, and the restored GREEN is `test-final-aaf.log`, EXIT=0. The table above has the commands.
- **Deterministic.** `aafDefaultMediaDir` is a pure function of two strings. The test uses no files, timers, threads or
  sleeps and needs no seam or injected values. The only external value is the SHA-256 of a fixed string, which the
  expected value `4cb0fd651a1a` pins (`printf '/in/Reel 1.aaf' | sha256sum`).
- **At the cause, and complete.** The cause is that the saved-project folder was keyed on the base name alone. The
  only production caller of `aafDefaultMediaDir` is `mediaDirFor` (`ControlCommandsAafImport.cpp:183`). It feeds
  both the import's `extractDir` (`:197`) and the reply's `media_dir` (`:212`), so one fix covers both. The unsaved
  branch is unchanged for the reason given above, `AafImportModel.cpp:124-129`.
- **No collateral damage.** Loading a saved project never calls this function: `rg` finds no caller outside
  `ControlCommandsAafImport.cpp`, and the saved project stores extracted media as absolute clip paths. Old projects
  therefore load as before. No new limit or check was added, so no caller can be newly refused. An explicit
  `media_dir` argument still overrides the default (`ControlCommandsAafImport.cpp:182`), and its absolute-path check
  is unchanged (`:190-193`).
- **Verdict is FIXED, so no ALREADY-FIXED or FALSE-POSITIVE contract is cited.**
- **Sibling commands** (`interchange.aaf_convention`, `aaf_import`, `aaf_read`, `smf_convention`, `smf_export`,
  `smf_import`, `smf_read`):
  - `interchange.aaf_convention` (`ControlCommandsAafImport.cpp:107`) reports the `media_rule` text, which now
    states the new default. It is a result string only; `commands_snapshot.json` does not contain `media_rule`, so
    the snapshot and canary are not affected. Updated, not a behaviour change.
  - `interchange.aaf_read` and `interchange.aaf_import` get the new default through `mediaDirFor` and report it in
    `media_dir`. Both are consistent with the fix and need no code change.
  - The `smf_*` commands have no AAF media path (the grep above finds no `aafDefaultMediaDir` or `aaf-media` in them),
    so they are not affected.
- **Submit-check gate row (`gates (feature_check.py)`) reports FAIL, but that is the sandbox, not the code.**
  `submit_check.py` calls the unmodified `feature_check.py`, which exits 1 with `PermissionError` at
  `<worktree>/../<name>-feature-check` (`logs/gates-1.log`). That sibling folder is outside this lane's write-jail. The
  same gate logic run with the log folder redirected (`logs/gates-2.log`, EXIT=0) passes all eight rows. The gate
  scripts themselves were not changed, and no limit or baseline was touched.
