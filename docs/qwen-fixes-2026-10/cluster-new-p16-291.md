FIXED

# Cluster new-p16-291: saving one I/O setup can erase all setups after malformed config JSON

## Finding

`audioio/setups` is malformed, and the user saves an I/O setup: every previously saved device setup is lost.

## Evidence (file:line, branch `bugs/qwen-new-p16-291`, base `b61e5f965`)

Before the fix, `allEntries()` (`src/core/AudioIoSetup.cpp:62`) parsed the stored value with
`QJsonDocument::fromJson(...).object()` and discarded the parse error. Invalid JSON, a non-object value, and
"nothing stored yet" all produced `{}`. `writeSetup` (`:228`) then did `allEntries()`, inserted the one new entry and
called `storeAll` (`:67`), so the store was rewritten with only that entry. `restoreEntry` (`:242`, the undo path,
called from `src/core/ControlCommandsHardwareIo.cpp:140`) had the same read-modify-write, so it had the same wipe.

Fix:

- `readEntries` (`src/core/AudioIoSetup.cpp:64`) distinguishes three cases: unset or whitespace-only (no entries,
  success); a JSON object (its entries, success); anything else, including a parse error or a non-object (false,
  the output untouched).
- `allEntries` (`:76`) keeps the tolerant read for `setupFor`, `requestedOutputs` and `storedEntry`. Readers still
  fall back to the defaults and never write, so their behaviour is unchanged.
- `writeSetup` (`:244`, check at `:248`) refuses with the error "the saved I/O setups ('audioio/setups') are not valid
  JSON, so nothing was written". The caller `ioSetup` already turns a `writeSetup` false into `InvalidArgs` and
  records no undo step (`ControlCommandsHardwareIo.cpp:138`).
- `restoreEntry` (`:259`, check at `:262`) returns `bool` and refuses in the same way. Its header
  (`include/AudioIoSetup.h:101-103`) is updated with the matching declaration and comment. Its undo callers
  (`ControlCommandsHardwareIo.cpp:140`, `tests/src/core/ImportTrackIoTest.cpp:142`) ignore the return value, so
  they compile unchanged.

Sibling check: `setupFor`, `requestedOutputs` and `storedEntry` (the other paths into the stored value) read only,
so the tolerant `allEntries` is correct for them. A valid store, an unset store and every existing entry are read
and written exactly as before (the only behaviour change is the refusal for an unreadable value).

## Regression test

`tests/src/core/HardwareIoTest.cpp`, new test `aMalformedSetupStoreIsNeverReplaced()` (new `#include
"ConfigManager.h"`). It sets the in-memory `audioio/setups` to two unreadable values (an unterminated object, and
the array `[1, 2]`), calls `writeSetup` and `restoreEntry`, checks that `writeSetup` returns false with an error and
that the stored value is still the malformed text, then puts the original value back. No timing, sleeps or races. The
test uses only in-memory `ConfigManager` values, so it does not save the config file.

## Commands and results

Note on the environment: the first build failed with `ccache: error: failed to create temporary file for
/run/user/1000/ccache-tmp/...: Permission denied` (`build-1-red.log:21`). That is the sandbox's ccache temp dir, not
the product code. Every later build set `CCACHE_TEMPDIR=/tmp/ccache-tmp-p16-291`.

| step | command (run from the worktree / `build/tests` as noted) | EXIT | log (LOG FOLDER) |
| --- | --- | --- | --- |
| RED build (first try) | `cmake --build build --target HardwareIoTest -j4` | 2 (ccache permission, not a test result) | `build-1-red.log` |
| RED build | `CCACHE_TEMPDIR=/tmp/ccache-tmp-p16-291 cmake --build build --target HardwareIoTest -j4` | 0 | `build-2-red.log` |
| RED run | `cd build/tests && ctest -R '^HardwareIoTest$' --output-on-failure` | 8 | `test-2-red.log` |
| GREEN build | `cmake --build build --target zene -j4` then `--target HardwareIoTest ImportTrackIoTest -j4` (both with `CCACHE_TEMPDIR`) | 0 for the chain (background task reported exit 0); `zene` and both test targets report "Built target" | `build-3-green-zene.log`, `build-4-green-tests.log` |
| GREEN run | `cd build/tests && ctest -R '^(HardwareIoTest\|ImportTrackIoTest)$' --output-on-failure` | 0 | `test-3-green.log` |
| 4b: save fix | `git diff -- include/AudioIoSetup.h src/core/AudioIoSetup.cpp > logs/fix.patch` | 0 | `fix.patch` |
| 4b: revert | `git apply -R logs/fix.patch` | 0 | |
| 4b: build unfixed | `cmake --build build --target zene HardwareIoTest -j4` | 0 | `build-5-proof-unfixed.log` |
| 4b: RED on unfixed | `cd build/tests && ctest -R '^HardwareIoTest$' --output-on-failure` | 8 | `test-4-proof-unfixed.log` |
| 4b: reapply | `git apply logs/fix.patch` | 0 | |
| 4b: build fixed | `cmake --build build --target zene HardwareIoTest ImportTrackIoTest -j4` | 0 | `build-6-reapply-fixed.log` |
| 4b: GREEN on fixed | `cd build/tests && ctest -R '^(HardwareIoTest\|ImportTrackIoTest)$' --output-on-failure` | 0 | `test-5-proof-fixed.log` |
| gates (script as written) | `python3 feature_check.py <worktree>` | 1 (`PermissionError`: the script makes `<worktree>-feature-check`, outside the write-jail; no gate ran) | `gates-1.log` |
| gates (same logic, logs in lane folder) | `python3 -I runs/new-p16-291.r1/gate_driver.py <worktree> <LOG FOLDER>/gates` | 0 | `gates-2.log` |

The gate driver is `runs/new-p16-291.r1/gate_driver.py`. It loads the unmodified `feature_check.py` and repeats
its `main()` with the log folder changed, because the script can only write logs next to the worktree. The gate
commands and the pass/fail rules are the script's own.

Test counts: `ctest -R '^(HardwareIoTest|ImportTrackIoTest)$'` ran 2 tests, both passed (`test-3-green.log`,
`test-5-proof-fixed.log`: "100% tests passed out of 2"). The RED run of `HardwareIoTest` ran its QTest functions:
"Totals: 24 passed, 1 failed" (`test-2-red.log:33`), the one failure being the new test.

## The proof: the assertion that fails without the fix

```
FAIL!  : HardwareIoTest::aMalformedSetupStoreIsNeverReplaced() '!written' returned FALSE. ({"Other device": {"outputs": {)
```

(`test-2-red.log:24`, and the same line in `test-4-proof-unfixed.log:24` on the reverted code.) Without the fix,
`writeSetup` reports success over an unreadable store. With the fix, it refuses and the store is unchanged.

## Files committed

- `src/core/AudioIoSetup.cpp`
- `include/AudioIoSetup.h`
- `tests/src/core/HardwareIoTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p16-291.md`

All three code files are fork-owned and already registered in `tests/fork-sources.txt` (lines 721, 1294, 2346) and
`tests/all-sources.txt`, so `tests/upstream-modifications.txt` needed no change. No new files were added. No control
command was added, so the `ControlCommandsSnapshot` canary is not affected.

## Not checked

- The full ctest suite was not run, only the `HardwareIoTest` and `ImportTrackIoTest` families. The changed header
  `include/AudioIoSetup.h` is included by `src/` files (rebuilt through `zene`) and, under `tests/`, only by
  `HardwareIoTest.cpp` and `ImportTrackIoTest.cpp` (`rg -l "AudioIoSetup.h|HardwareIo.h|ControlHardwareIoSupport.h"
  tests src include`); both were rebuilt and run.
- No GUI or realtime path changed (the change is config I/O on the control thread), so no GUI or audio-thread check
  applies.
