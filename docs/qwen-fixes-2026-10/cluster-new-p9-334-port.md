# Cluster new-p9-334-port: OpulenZ SBI signature check

Verdict: **FIXED**

Port of the accepted fix `77324f1cd` ("Fix cluster new-p9-334 SBI signature check",
branch `bugs/qwen-new-p9-334`) onto the newest line (`050/m0-train-5`, base
`ded355440`). The product change is re-applied by hand against the current code;
nothing was merged, cherry-picked or rebased.

## The bug

`plugins/OpulenZ/OpulenZ.cpp:605` (before the fix) read:

```cpp
QByteArray sbidata = sbifile.read(52);
if( !sbidata.startsWith("SBI\0x1a") ) {
```

`"SBI\0x1a"` is the C string `{'S','B','I','\0','x','1','a','\0'}`. `QByteArray::startsWith(const char*)`
compares only `qstrlen(ba)` bytes, i.e. the three bytes `SBI` - the embedded NUL ends the
literal, so the required fourth signature byte `0x1a` was never checked. A 47-byte-or-longer
file whose first three bytes are `SBI` was accepted and decoded as an SBI patch
(`sbiname = sbidata.mid(4, 32)` at `plugins/OpulenZ/OpulenZ.cpp:621`, the patch data
applied from `plugins/OpulenZ/OpulenZ.cpp:633` on).

The fix compares all four bytes with an explicit length:

```cpp
// plugins/OpulenZ/OpulenZ.cpp:605-607
// The SBI signature is four bytes: "SBI" followed by 0x1a. A plain
// string literal stops at the first NUL, so the length is explicit.
if( !sbidata.startsWith(QByteArray{"SBI\x1a", 4}) ) {
```

This is the same expression the accepted branch used; it is the only SBI parser in the
product (`rg -n 'startsWith|\.read\(' plugins/OpulenZ/OpulenZ.cpp` -> two hits, the read at
line 604 and this check at line 607).
`tests/reference/OpulenZ/OpulenZ.cpp:600` is the frozen pre-migration upstream snapshot
(`tests/reference/ORIGIN.tsv` records its blob id), so it is deliberately left untouched.

## The regression test

`tests/src/plugins/PluginPortsMigrationTest.cpp:826` (data) and
`tests/src/plugins/PluginPortsMigrationTest.cpp:839` (body) add
`PluginPortsMigrationTest::invalidSbiSignatureIsRejected`. It loads the live `opulenz`
module, names the track `OpulenZ` (the plugin's stored name, so a decoded file renames
it), writes a 52-byte file whose first bytes are the row's signature and calls
`OpulenzInstrument::loadFile()` on it.

| Row | Signature bytes | Expected |
|---|---|---|
| `wrong fourth byte` | `SBIX` | the track name is unchanged - the file is not an SBI patch |
| `truncated signature` | `SBI\0` | the track name is unchanged |
| `genuine signature` | `SBI\x1a` | the track IS renamed from the file's name field - the fix does not reject every file |

The third row is the positive control: it fails if the check is weakened into "reject
everything", and it proves the rename the two negative rows rely on is a real observable.

## Why the original branch's test failed on train 5

The original branch's test body was run verbatim on this line (temporarily added as
`origInvalidSbiSignatureIsRejected`, with the product fix in place): it PASSED
(`test-11-orig-test-on-train5.log`, 49 passed / 0 failed / 2 skipped, exit 0). The test
logic is therefore valid on the newest line; what failed on train 5 was the merge, which
did not carry the one-line product hunk into `plugins/OpulenZ/OpulenZ.cpp` while the test
came along - the branch's test file and this line's differ by 2080 lines, and the
signature line sits in the same file the `new-p9-327` patch-locking change rewrote.
This port re-applies the product change by hand against the current code, so the test and
the fix now arrive together.

## Evidence

All commands were run in `/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-334-port`
(ctest from `build/tests`). Every log is a new numbered file in
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-334-port.r1/logs`.

| Stage | Exact command | Exit | Log / result |
|---|---|---:|---|
| Baseline build | `cmake --build build --target PluginPortsMigrationTest opulenz -j20` | 0 | `build-1.log` |
| Build with the new test, product unfixed | `cmake --build build --target PluginPortsMigrationTest -j20` | 0 | `build-2.log` |
| **RED** (new test, product unfixed) | `cd build/tests && ctest -R '^PluginPortsMigrationTest$' --output-on-failure` | **8** | `test-1.log`; the two negative rows fail: `Actual "Invalid SBI Patch               "`, `Expected "OpulenZ"`; totals 45 passed, 3 failed, 2 skipped |
| Family baseline without the new test (product unfixed) | `cd build/tests && ctest -R '^PluginPortsMigrationTest$' --output-on-failure` | **8** | `test-2.log`; 44 passed, **1 failed**, 2 skipped - the one failure is `nonRealtimeLadspaEffectsAreInvalid` ("SWH Decimator test fixture was not loaded"), pre-existing and unrelated (the `decimator_1202` LADSPA module is not a dependency of the test target, so it was never built in this build tree) |
| Build with the fix | `cmake --build build --target zene opulenz PluginPortsMigrationTest -j20` | 0 | `build-4.log` |
| Run with the fix | `cd build/tests && ctest -R '^PluginPortsMigrationTest$' --output-on-failure` | **8** | `test-3.log`; 47 passed, 1 failed (the same pre-existing LADSPA fixture), 2 skipped - all three new rows pass |
| Build the missing SWH LADSPA fixture | `cmake --build build --target decimator_1202 -j8` | 0 | `build-5.log` |
| **GREEN** (whole family) | `cd build/tests && ctest -R '^PluginPortsMigrationTest$' --output-on-failure` | **0** | `test-4.log`; 1/1 ctest case passed, 100% tests passed |
| **GREEN** (QTest totals) | `cd build/tests && QT_QPA_PLATFORM=offscreen ./PluginPortsMigrationTest` | **0** | `test-5-qtest-direct.log`; totals 48 passed, 0 failed, 2 skipped (the two skips are `sf2DialogRejectsAFilteredOutProgram` and `sf2FailedLoadPreservesCurrentFontAndSource`: "Sf2Player is not built in this configuration") |
| Product-only patch | `git diff -- plugins/OpulenZ/OpulenZ.cpp > .../logs/fix.patch` | 0 | `fix.patch` (1 hunk, product file only) |
| Reverse the product patch | `git apply -R .../logs/fix.patch` | 0 | product change removed |
| Rebuild without the fix | `cmake --build build --target zene opulenz PluginPortsMigrationTest -j20` | 0 | `build-11.log` |
| **Reversal proof RED** | `cd build/tests && ctest -R '^PluginPortsMigrationTest$' --output-on-failure` | **8** | `test-14-reversed-final.log`; the two negative rows fail again with the same `Actual "Invalid SBI Patch               "` / `Expected "OpulenZ"`; 46 passed, 2 failed, 2 skipped - the `genuine signature` row still passes |
| Restore the product patch | `git apply .../logs/fix.patch` | 0 | product change restored |
| Rebuild restored fix | `cmake --build build --target zene opulenz PluginPortsMigrationTest -j20` | 0 | `build-12.log` |
| **Reversal proof GREEN** | `cd build/tests && ctest -R '^PluginPortsMigrationTest$' --output-on-failure` | **0** | `test-15-restored-final.log`; 1/1 ctest case passed, 100% tests passed |
| Original branch's test body on this line | `cmake --build build --target PluginPortsMigrationTest -j20` + `cd build/tests && QT_QPA_PLATFORM=offscreen ./PluginPortsMigrationTest` | 0 / 0 | `build-8.log`, `test-11-orig-test-on-train5.log`; the verbatim original slot PASSED (49 passed, 0 failed, 2 skipped); the temporary slot was removed again afterwards |
| Final test wording (every row writes a 52-byte file) | `cmake --build build --target PluginPortsMigrationTest -j20` + `cd build/tests && QT_QPA_PLATFORM=offscreen ./PluginPortsMigrationTest` | 0 / 0 | `build-10.log`, `test-13-final-qtest.log`; 48 passed, 0 failed, 2 skipped; the three rows are `PASS` |
| Gates (after the last edit) | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | **0** | `feature-check-3.log`; RESULT: PASS (fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures, complexity, file-length) |
| Complexity ratchet (train-wide) | `bash tests/complexity-gate.sh --check` | 1 | `complexity-gate-2.log`; the 6 reported regressions are all in files this change does not touch (`src/core/ControlCommandsMixerTools.cpp`, `tests/src/gui/XyPadPairTest.cpp`, `src/core/DawProjectReadTracks.cpp`, `src/core/ControlCommandsMixerStrip.cpp`, `src/core/ProjectRevisions.cpp`, `src/core/ControlCommandsShortcuts.cpp`). `feature_check.py` scopes the same gate to this change: "complexity - no new item in your files" (PASS). My new function is CCN 3 (`lizard tests/src/plugins/PluginPortsMigrationTest.cpp`); `OpulenzInstrument::loadFile` keeps its previous CCN 19 baseline entry. |
| File-length ratchet (train-wide) | `bash tests/file-length-gate.sh --check` | 1 | `file-length-gate-2.log`; the 14 reported regressions are all pre-existing new files over 500 lines in `src/core/` and `tests/src/core|src/gui`, none of them this change's files. `feature_check.py` scopes it: "file-length - no new item in your files" (PASS). |

An earlier round of the same reversal proof ran with a slightly different wording of the
test file (the truncated-signature row then produced a 51-byte file instead of a 52-byte
one): `build-6.log` / `test-6-reversed.log` (exit 8, same two rows failed) and
`build-7.log` / `test-7-restored.log` (exit 0). The table above is the final round, run
with the committed test file.

## Self-check

- **The regression test FAILS with the product change reverted and PASSES with it (exit codes).**
  RED `ctest -R '^PluginPortsMigrationTest$'` exit 8 (`test-1.log`, `test-14-reversed-final.log`);
  GREEN exit 0 (`test-4.log`, `test-15-restored-final.log`, `test-13-final-qtest.log`).
  The reversal proof was done with the product-only patch (`fix.patch`,
  `plugins/OpulenZ/OpulenZ.cpp` only, test file untouched), rebuilt and rerun: exit 8
  again, then exit 0 after `git apply fix.patch`.
- **The test is deterministic: no wall-clock timing, sleeps or races.**
  It writes a fixed 52-byte file into a `QTemporaryDir`, calls `loadFile()` once and
  compares the track name. No timer, no sleep, no thread, no timing value.
- **The fix is at the cause and complete: every caller and sibling of what you changed was checked and stays consistent.**
  The only SBI parser in the product is `OpulenzInstrument::loadFile`
  (`plugins/OpulenZ/OpulenZ.cpp:593`); `rg -n 'startsWith|\.read\(' plugins/OpulenZ/OpulenZ.cpp`
  finds the read at line 604 and the single signature check at line 607. `OpulenZ` is the
  only plugin advertising the `sbi` file type (`plugins/OpulenZ/OpulenZ.cpp:79`). Every
  caller of the load path
  (`src/gui/FileBrowser.cpp:727`, `src/gui/editors/TrackContainerView.cpp:491`,
  `src/gui/instrument/InstrumentTrackWindow.cpp:609`, `src/core/ControlCommandsBrowserMedia.cpp:300`,
  `src/core/PresetPreviewPlayHandle.cpp:148`) goes through this same function and now
  rejects a file whose fourth byte is not `0x1a`. The sibling copy
  `tests/reference/OpulenZ/OpulenZ.cpp:600` is the frozen upstream snapshot (blob id in
  `tests/reference/ORIGIN.tsv`) used as the pre-migration rendering baseline; it is not
  built into the product and is deliberately left as it is.
- **No collateral damage: old projects and saved data still load as before, and any limit or check you added does not hit other callers that legitimately exceed it.**
  The check is a four-byte signature comparison, not a size limit; the existing size rules
  (52 bytes / minimum 47) are untouched. A genuine SBI file always carries `SBI` + `0x1a`
  (the format the plugin's own comment at `plugins/OpulenZ/OpulenZ.cpp:594` cites), so
  the `genuine signature` row shows real patches are still decoded and still rename the
  track. Nothing about a saved project changes: a project stores the instrument's knob
  values, never the bytes of an `.sbi` file, and the check only runs when a user opens an
  `.sbi` file.
- **Realtime, undo and commands.**
  The change is in a user-driven file-load path on the main thread; it adds no branch to
  `processImpl`/`playImpl`, allocates nothing, takes no lock and logs nothing new (the
  `printf` on the rejection path is pre-existing). No undo step is involved (loading a
  file is not an undoable command) and no control command was added, so the
  `ControlCommandsSnapshot` canary is untouched.
- **If your verdict is ALREADY-FIXED or FALSE-POSITIVE: the code (file:line) or the intended contract that shows it.**
  Not applicable - the verdict is FIXED. The contract is the SBI header itself: four
  bytes `S`, `B`, `I`, `0x1a` (the format the plugin cites at
  `plugins/OpulenZ/OpulenZ.cpp:594`). The pre-fix code at
  `plugins/OpulenZ/OpulenZ.cpp:605` compared only the three bytes `SBI`, which the RED
  runs above show accepted a `SBIX` file.

## Files in the commit

- `plugins/OpulenZ/OpulenZ.cpp`
- `tests/src/plugins/PluginPortsMigrationTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p9-334-port.md`
