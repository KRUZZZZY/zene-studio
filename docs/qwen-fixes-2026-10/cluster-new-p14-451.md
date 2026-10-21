# Cluster new-p14-451

**Verdict: FIXED**

- new-p16-332: FIXED

The comma list of a plain-list field was parsed by splitting the text on every comma and skipping
every empty part, so `1,,2` was read as the shorter list `[1,2]` and the schema accepted it: the
command ran with values the user never typed, and nothing said so.

## Evidence

- [CommandFormFields.cpp:343](../../src/gui/CommandFormFields.cpp#L343) `parseList()` now refuses an
  empty component instead of skipping it, and says why in the field's error.
- [CommandFormFields.cpp:407](../../src/gui/CommandFormFields.cpp#L407) `readText()` keeps that
  reason for a list field, and falls back to the generic "not a list of values separated by commas"
  for the other refusals.
- [CommandFormTest.cpp:510](../../tests/src/gui/CommandFormTest.cpp#L510) `commaListsRefuseEmptyItems()`
  is the regression: `1,,2` and `1, 2,` are refused with a reason, while a blank field stays blank
  (Undefined) and the command's own default is used.

The affected CTest family contained one registered test (`CommandFormTest`). The RED run failed in
the new regression slot; after the fix CTest reported 1/1 passed.

## Commands and results

Builds and tests ran with output redirected to the named log and the exit status captured with
`set +e; <command> > <log> 2>&1; rc=$?; echo EXIT=$rc; exit $rc`. CTest ran from `build/tests`;
builds ran from the worktree root.

| Stage | Command | EXIT | Evidence |
|---|---|---:|---|
| Regression build (original code) | `cmake --build build --target CommandFormTest -j8` | 0 | `build-1.log` |
| RED | `ctest -R '^CommandFormTest$' --output-on-failure` | 8 | `test-1.log`: 1/1 CTest failed; `commaListsRefuseEmptyItems` failed ("a comma list with an empty item was read as a shorter list"). |
| Fixed build | `cmake --build build --target zene CommandFormTest -j8` | 0 | `build-2.log` |
| GREEN | `ctest -R '^CommandFormTest$' --output-on-failure` | 0 | `test-2.log`: 1/1 passed. |
| Save production patch | `git diff -- src/gui/CommandFormFields.cpp > .../logs/fix-b.patch` | 0 | `fix-b.patch` (46 lines). |
| Remove production patch | `git apply -R .../logs/fix-b.patch` | 0 | Reversal proof. |
| Reversed build | `cmake --build build --target zene CommandFormTest -j8` | 0 | `build-3.log` |
| Reversal RED | `ctest -R '^CommandFormTest$' --output-on-failure` | 8 | `test-3.log`: the same regression slot failed. |
| Restore production patch | `git apply .../logs/fix-b.patch` | 0 | Reversal proof. |
| Final build | `cmake --build build --target zene CommandFormTest -j8` | 0 | `build-4.log` |
| Final GREEN | `ctest -R '^CommandFormTest$' --output-on-failure` | 0 | `test-4.log`: 1/1 passed. |

All numbered logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p14-451.r1/logs`.

## Files committed

- `src/gui/CommandFormFields.cpp`
- `tests/src/gui/CommandFormTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p14-451.md`
