# Cluster new-p11-97: Favourite markers break anchored effect searches

**Verdict: FIXED**

`src/gui/modals/EffectPickerOrganiser.cpp:72` adds `★ ` to the name column's display text for favourites, while line 74 records `FavouriteRole`. `include/EffectSelectDialog.h:76` now removes that prefix from the local name used for regex matching when `FavouriteRole` is set. The list still displays the favourite marker.

The regression at `tests/src/gui/EffectHostingMenuTest.cpp:94` searches `^Foo$` against plain `Foo` and favourite `★ Foo` rows and expects both to match. Before the fix, `filter.rowCount()` was 1 instead of 2.

## Verification evidence

Commands ran in the worktree root unless the working directory is stated. Build and test output is in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-97.r1/logs`.

| Check | Exact command | Exit | Evidence |
| --- | --- | ---: | --- |
| RED build | `cmake --build build --target EffectHostingMenuTest` | 0 | `build-1.log` |
| RED test (from `build/tests`) | `ctest -R EffectHostingMenuTest --output-on-failure` | 8 | `test-1.log`: regression failed, actual 1, expected 2 |
| GREEN build | `cmake --build build --target zene EffectHostingMenuTest` | 0 | `build-2.log` |
| GREEN test (from `build/tests`) | `ctest -R EffectHostingMenuTest --output-on-failure` | 0 | `test-2.log`: 1/1 CTest test passed |
| Remove-fix patch | `git diff -- include/EffectSelectDialog.h > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-97.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-97.r1/logs/fix.patch` | 0 | Patch contains only the production header change |
| RED build without fix | `cmake --build build --target zene EffectHostingMenuTest` | 0 | `build-3.log` |
| RED test without fix (from `build/tests`) | `ctest -R EffectHostingMenuTest --output-on-failure` | 8 | `test-3.log`: same regression failed, actual 1, expected 2 |
| Restore fix patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-97.r1/logs/fix.patch` | 0 | Fix restored |
| Final build | `cmake --build build --target zene EffectHostingMenuTest` | 0 | `build-4.log` |
| Final test (from `build/tests`) | `ctest -R EffectHostingMenuTest --output-on-failure` | 0 | `test-4.log`: 1/1 CTest test passed |

The affected family executed one registered CTest case, `EffectHostingMenuTest`; the regression failed both without the fix and passed after restoration.

| Gate | Exact command | Exit |
| --- | --- | ---: |
| Complexity | `bash tests/complexity-gate.sh --check` | 0 |
| File length | `bash tests/file-length-gate.sh --check` | 0 |

The fff grep tool was unavailable in this session, so logs were checked with targeted `rg -n` searches for `error`, `FAILED`, and `Assertion`.

## Files in this commit

- `include/EffectSelectDialog.h`
- `tests/src/gui/EffectHostingMenuTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p11-97.md`
