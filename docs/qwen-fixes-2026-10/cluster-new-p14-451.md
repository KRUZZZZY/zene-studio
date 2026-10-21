# Cluster new-p14-451

**Verdict: FIXED**

- new-p14-451: FIXED
- new-p16-332: FIXED

Both reports point at one shared defect: the comma list of a plain-list field had no format of its
own. The parser split the text on *every* comma, so a comma inside an item changed the submitted
values (new-p14-451), and it skipped every empty part, so `1,,2` was read as the shorter list
`[1,2]` and the schema accepted it (new-p16-332). One codec now serves both ends of the field: the
reader (`splitListItems()`) and the writer (`listText()`) agree on what a comma list says.

## Evidence

### new-p16-332 - a comma list with an empty item

- [CommandFormFields.cpp:397](../../src/gui/CommandFormFields.cpp#L397) `parseList()` refuses an
  empty component (`"1,,2"`, `"1, 2,"`) and says why in the field's error; the reason reaches the
  user through `readText()` at
  [CommandFormFields.cpp:454](../../src/gui/CommandFormFields.cpp#L454). A blank field is still no
  list at all (Undefined), so an optional field keeps using the command's own default.
- [CommandFormTest.cpp:512](../../tests/src/gui/CommandFormTest.cpp#L512) `commaListsRefuseEmptyItems()`
  is the regression: `1,,2` and `1, 2,` are refused with a reason, a blank field stays blank.

### new-p14-451 - a comma inside a string item

- [CommandFormFields.cpp:347](../../src/gui/CommandFormFields.cpp#L347) `splitListItems()` splits on
  the commas no escape protects: a backslash makes the next character literal, so `take\, 1` is one
  item, `take, 1`. A text ending in a stray escape is refused rather than silently losing it.
- [CommandFormFields.cpp:365](../../src/gui/CommandFormFields.cpp#L365) `listText()` is the writer
  for the same format: it escapes a comma and a backslash inside an item, so what the editor holds
  reads back as the list that was set. It is used by `writeValue()` at
  [CommandFormFields.cpp:574](../../src/gui/CommandFormFields.cpp#L574), which is the only writer of a
  list field. A list of records, an item the comma list cannot say, and a text that would read as
  JSON are written as JSON, which the reader already understood.
- [CommandFormFields.cpp:226](../../src/gui/CommandFormFields.cpp#L226) the field's placeholder now
  says that a backslash escapes a comma.
- [CommandFormTest.cpp:535](../../tests/src/gui/CommandFormTest.cpp#L535)
  `stringListItemsKeepTheirCommas()` is the regression: the list `["take, 1", "rest"]` survives the
  writer and the reader unchanged, and the typed text `take\, 1, rest` reads as the same two items.

The affected CTest family contained one registered test (`CommandFormTest`). Each regression failed
on the original code and passed after its fix; the wider `Command|Control|Form|Metadata|Palette`
family (135 registered tests) gave exactly the same result with and without the change (41 % passed,
79 failed: 20 real failures and 59 "Not Run" for targets this tree does not build). The 20 real
failures are environmental (no LADSPA plugin, no loadable built-in effect, socket-path safety) and
were reproduced on a pristine build of the same file.

## Commands and results

Builds and tests ran with output redirected to the named log and the exit status captured with
`set +e; <command> > <log> 2>&1; rc=$?; echo EXIT=$rc; exit $rc`. CTest ran from `build/tests`;
builds and gates ran from the worktree root.

### new-p16-332 (commit `50d13b7d0`)

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

### new-p14-451 (commit 2)

| Stage | Command | EXIT | Evidence |
|---|---|---:|---|
| Regression build (original code) | `cmake --build build --target CommandFormTest -j8` | 0 | `build-5.log` |
| RED | `ctest -R '^CommandFormTest$' --output-on-failure` | 8 | `test-5.log`: 1/1 CTest failed; `stringListItemsKeepTheirCommas` failed ("Compared values are not the same"). |
| First fixed build | `cmake --build build --target zene CommandFormTest -j8` | 0 | `build-6.log` |
| First GREEN | `ctest -R '^CommandFormTest$' --output-on-failure` | 0 | `test-6.log`: 1/1 passed. The first implementation was rewritten afterwards to satisfy the file-length gate (see gate-1.log); the rewrite was proved again below. |
| Final fixed build | `cmake --build build --target zene CommandFormTest -j8` | 0 | `build-13.log` |
| Final GREEN | `ctest -R '^CommandFormTest$' --output-on-failure` | 0 | `test-15.log`: 1/1 passed. |
| Save production patch | `git diff -- src/gui/CommandFormFields.cpp > .../logs/fix-a.patch` | 0 | `fix-a.patch` (92 lines). |
| Remove production patch | `git apply -R .../logs/fix-a.patch` | 0 | Reversal proof. |
| Reversed build | `cmake --build build --target zene CommandFormTest -j8` | 0 | `build-14.log` |
| Reversal RED | `ctest -R '^CommandFormTest$' --output-on-failure` | 8 | `test-16.log`: `stringListItemsKeepTheirCommas` failed; the new-p16-332 slot passed (its fix is already committed). |
| Restore production patch | `git apply .../logs/fix-a.patch` | 0 | Reversal proof. |
| Final build | `cmake --build build --target zene CommandFormTest -j8` | 0 | `build-15.log` |
| Final GREEN | `ctest -R '^CommandFormTest$' --output-on-failure` | 0 | `test-17.log`: 1/1 passed. |

### Gates and collateral

| Stage | Command | EXIT | Evidence |
|---|---|---:|---|
| Gates (first implementation) | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | 1 | `gate-1.log`: every row PASS except `file-length` - the first implementation put `src/gui/CommandFormFields.cpp` at 533 code lines, over the 500-line limit. The codec was rewritten (same behaviour, 486 code lines); no gate limit or baseline was changed. |
| Gates (final) | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | 0 | `gate-2.log`: all eight rows PASS (`complexity`: no new item in your files; `file-length`: no new item in your files). |
| Wider family (with the fix) | `ctest -R 'Command|Control|Form|Metadata|Palette' --output-on-failure` | 8 | `test-9.log` (first implementation) and `test-18-family-final.log` (final): identical - 41 % passed, 79 failed (20 real failures, 59 "Not Run" for unbuilt targets). `CommandFormTest` passed in both. |
| Baseline, same 18 failing tests on the pristine file | `ctest -I 554,559,564,568,569,573,574,576,577,582,591,604,606,607,612,614,616,619,621,627 --output-on-failure` | 8 | `test-10-baseline.log`: 18 of 18 failed on the pristine build - the same failures, without the change. |
| With the fix | `ctest -R '^(ControlFreezeCommandsTranscript|ControlEngineWiring)$' --output-on-failure` | 8 | `test-11.log`, `test-12.log`: fail with the fix. |
| Baseline, the same two tests on the pristine file | `ctest -R '^(ControlFreezeCommandsTranscript|ControlEngineWiring)$' --output-on-failure` | 8 | `test-13-baseline.log`: the same two failures on the pristine build - environmental (no loadable built-in effect), not caused by this change. |

All numbered logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p14-451.r1/logs`.

## Self-check

- **The regression test FAILS with the product change reverted and PASSES with it.** Both bugs have
  their own proof. new-p16-332: `ctest -R '^CommandFormTest$'` EXIT 8 on the original code
  (`test-1.log`), EXIT 0 after the fix (`test-2.log`), EXIT 8 with the product patch reversed
  (`test-3.log`), EXIT 0 with it restored (`test-4.log`). new-p14-451: EXIT 8 on the original code
  (`test-5.log`), EXIT 0 after the fix (`test-6.log`, `test-15.log`), EXIT 8 with the product patch
  reversed (`test-16.log`), EXIT 0 with it restored (`test-17.log`). Each reversal failed in its own
  slot only.
- **The tests are deterministic.** Both regressions drive the widgets directly
  (`formfield::makeEditor()`, `QLineEdit::setText()`, `formfield::readValue()`,
  `formfield::writeValue()`) with literal text and compare JSON values. No clock, no sleep, no
  thread, no I/O - the same text always yields the same value.
- **The fix is at the cause and complete.** The comma list's format now lives in one place, and both
  ends of the field go through it: the reader `parseList()` -> `splitListItems()`, and the only
  writer of a list field, `writeValue()` -> `listText()`. The siblings were checked: `Kind::Json`
  (raw JSON) and `Kind::Text` keep `valueText()`, the JSON branch of `parseList()` is untouched, and
  `readValue()` still answers Undefined for a blank field, so an optional field keeps using the
  command's own default. The other reader of a list value, the socket and MCP surface, sends JSON
  arguments and never goes through this text format.
- **No collateral damage.** Nothing outside the command form's text format changed: no save format,
  no project file, no command, no registry. The new refusal applies only to the comma syntax, and
  only to a field that has text - a blank field is still blank. The empty-item refusal does not hit
  callers that legitimately exceed it: a JSON array keeps whatever items it has, and the item-count
  bounds (`minItems`/`maxItems`) are checked as before. The wider family gave identical results with
  and without the change, and the failing tests fail the same way on a pristine build.
- **Both verdicts are FIXED**, so no code citation is needed for a false positive.

## Files committed

- `src/gui/CommandFormFields.cpp`
- `tests/src/gui/CommandFormTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p14-451.md`
