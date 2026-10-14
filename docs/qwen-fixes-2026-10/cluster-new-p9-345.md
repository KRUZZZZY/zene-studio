# Cluster new-p9-345 — FIXED

`PatchesDialog::validateForm()` only checked for a selected bank. Filtering the
program list to zero rows invalidated its current selection, but left
`m_selProg` unchanged and still allowed OK/Enter to accept the dialog.

The fix requires a selected program row with a valid current index before the
dialog can be enabled or accepted. `progChanged()` now clears the stored
program and name when the proxy reports that the current row disappeared, then
updates validation.

Evidence:

- `plugins/Sf2Player/PatchesDialog.cpp:262` — validation checks bank and
  current program selection.
- `plugins/Sf2Player/PatchesDialog.cpp:413` — invalid program row clears
  `m_selProg` and `m_selProgName`.
- `tests/src/plugins/PluginPortsMigrationTest.cpp:226` — regression opens the
  actual SF2 dialog, filters the configured soundfont to no matches, and clicks
  OK.

The configured build has no `PatchesDialog` CTest entry (`ctest -N` exited 0
with 402 tests). The similarly named reference source is a frozen copy, so the
regression uses the existing `PluginPortsMigrationTest` CTest family instead.

## Verification evidence

| Check | Command | Exit | Result |
|---|---|---:|---|
| RED with production fix reverse-applied | `./PluginPortsMigrationTest sf2DialogRejectsAFilteredOutProgram` | 1 | The assertion showed the dialog accepted the filtered-out program (`test-7.log`). |
| GREEN after restoring production fix | `./PluginPortsMigrationTest sf2DialogRejectsAFilteredOutProgram` | 0 | 3 QtTest totals passed (init, regression, cleanup; `test-8.log`). |
| Revert-proof | The same RED/GREEN pair after `git apply -R` / `git apply` and rebuild | 1 / 0 | The same test fails without the fix and passes with it. |
| Affected CTest family | `ctest -R '^PluginPortsMigrationTest$' --output-on-failure` | 0 | 1/1 CTest test passed (`test-9.log`). |
| Complexity gate | `bash tests/complexity-gate.sh --check` | 0 | Passed. |
| File-length gate | `bash tests/file-length-gate.sh --check` | 0 | Passed. |

All builds used:

```sh
cmake --build build --target zene sf2player PluginPortsMigrationTest
```

The build exited 0 with the fix reverse-applied before RED and after restoring
the fix before GREEN. Those runs are recorded in `build-5.log` and `build-6.log`;
the test runs are in `test-7.log` through `test-9.log` in
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-345.r1/logs`.

The reverse-application proof used these commands, each exiting 0:

```sh
git diff -- plugins/Sf2Player/PatchesDialog.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-345.r1/logs/fix.patch
git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-345.r1/logs/fix.patch
git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-345.r1/logs/fix.patch
```

## Files committed

- `plugins/Sf2Player/PatchesDialog.cpp`
- `tests/src/plugins/PluginPortsMigrationTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p9-345.md`
