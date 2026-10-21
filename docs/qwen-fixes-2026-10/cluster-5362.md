# Cluster 5362 — script package discovery: three bugs in `src/core/ScriptPackage.cpp`

Verdict: **FIXED** (all three: 5362, new-p16-214, new-p10-366).

5362: FIXED
new-p16-214: FIXED
new-p10-366: FIXED

All three findings were real on this branch (`bugs/qwen-5362`), all three were
fixed at their cause in `src/core/ScriptPackage.cpp`, and each has its own
regression test in `tests/src/core/ScriptStabilisationTest.cpp` that fails
without its fix.

## 5362 — package scanning reads each entire script to parse a 4 KB header

- **REAL.** `src/core/ScriptPackage.cpp:121` (historical) — `scan()` opened the
  entry script and called `file.readAll()`, then handed the whole file to
  `parseManifest()`, which only ever looks at the first 4096 characters. A
  multi-megabyte `package.lua` therefore cost memory proportional to the file
  on every listing.
- **Fix** (`src/core/ScriptPackage.cpp:206`): `const QByteArray head =
  file.read(ManifestScanBytes);` — only the manifest window is read. The file
  is opened `QIODevice::ReadOnly` (binary) so the window is byte-exact.
- **Test** — `tests/src/core/ScriptStabilisationTest.cpp:454`
  `packageScanningReadsOnlyTheManifestWindow()`: a 4 MB entry script, and the
  bytes discovery read are counted through the seam
  `ScriptPackages::entryScriptBytesReadForTesting()`
  (`include/ScriptPackage.h:105`, `src/core/ScriptPackage.cpp:225`).
- **Evidence:** RED — `FAIL! packageScanningReadsOnlyTheManifestWindow() 'read
  <= 4096' returned FALSE. (discovery read 4194304 bytes of a 4 MB entry
  script)`. GREEN — `100% tests passed out of 1` (15/15 slots of the binary).

## new-p16-214 — package discovery can follow a package directory symlink outside its root

- **REAL.** `src/core/ScriptPackage.cpp:95` (historical) — `scan()` took every
  directory entry whose name validated, without checking where it pointed. A
  symlink in the scripts root to a package directory outside it was listed, and
  the entry script of that package established an import root outside
  `scriptsRoot`.
- **Fix** (`src/core/ScriptPackage.cpp:167,176,187`): the scripts root is
  canonicalised once (`canonicalRoot`), and the canonical path of the package
  directory and of the entry script must lie under it (`isInsideRoot()`,
  `src/core/ScriptPackage.cpp:88`). A candidate that escapes is **skipped**,
  like any other unusable directory — discovery never breaks a listing.
- **Contract already established in this codebase:** the Lua import sandbox
  refuses the same escape (`src/core/ScriptImport.cpp:280` canonicalises the
  root, `src/core/ScriptImport.cpp:334` refuses a module that resolves outside
  it; documented in `docs/LUA-PACKAGE-FORMAT.md:44`). The package scanner now
  holds the same line.
- **Test** — `tests/src/core/ScriptStabilisationTest.cpp:535`
  `packageDiscoveryStaysInsideTheScriptsRoot()`: a package directory that is a
  symlink out of the root, an entry script that is a symlink out of the root,
  and a symlink that stays inside the root (still listed). The symlink test is
  skipped on Windows (symlink creation needs privileges), as
  `tests/src/core/ScriptImportTest.cpp:266` does.
- **Evidence:** RED — `FAIL! packageDiscoveryStaysInsideTheScriptsRoot()
  Compared values are not the same` (the escaping package was listed). GREEN —
  `100% tests passed out of 1`.

## new-p10-366 — manifest scan limit counts characters instead of bytes

- **REAL.** `src/core/ScriptPackage.cpp:71` (historical) — the 4096 limit was
  applied to a `QString` (`source.left(ManifestScanBytes)`), so it counted
  UTF-16 code units, while the format documents a *4 KB window of the file*
  (`docs/LUA-PACKAGE-FORMAT.md:96`) and the constant is named
  `ManifestScanBytes`. A manifest past byte 4096 but inside character 4096 was
  seen by discovery.
- **Fix** (`src/core/ScriptPackage.cpp:66,115,147,210`): the limit is enforced
  on the encoded bytes, before the text is decoded. `manifestWindow()` takes
  the first `ManifestScanBytes` **bytes** and, when the source continues past
  the window, cuts back to the last *complete* line — a declaration the window
  cuts through is no longer matched in part (which would report a truncated
  name or version). `parseManifest()` encodes its `QString` argument and calls
  the same byte-level parser, so the public API has the byte semantics too.
- **Test** — `tests/src/core/ScriptStabilisationTest.cpp:488`
  `manifestWindowIsMeasuredInBytes()`: 3000 two-byte characters put the
  manifest past byte 4096 and inside character 4096 (it must not be seen), the
  same manifest 1000 characters in (it must be seen), and both through
  `parseManifest()` and through `scan()`.
- **Evidence:** RED — `FAIL! manifestWindowIsMeasuredInBytes() 'parsed.name
  .isEmpty()' returned FALSE. (the manifest window is 4096 bytes, not 4096
  characters)`. GREEN — `100% tests passed out of 1`.

## Commands run (this session)

All logs are in
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5362.r1/logs/`.

### RED / GREEN per bug (step 4b: revert the product change, rebuild, rerun)

Cluster 5362 (`fix-5362.patch`):

```
git diff -- src/core/ScriptPackage.cpp > .../logs/fix-5362.patch
git apply -R .../logs/fix-5362.patch                       # reverted the fix
cmake --build build --target ScriptStabilisationTest        # build-3.log  EXIT=0
cd build/tests && ctest -R '^ScriptStabilisationTest$' --output-on-failure
                                                      # test-3.log  EXIT=8
#   FAIL! packageScanningReadsOnlyTheManifestWindow() 'read <= 4096' returned FALSE.
#   (discovery read 4194304 bytes of a 4 MB entry script)
git apply .../logs/fix-5362.patch                          # restored the fix
cmake --build build --target ScriptStabilisationTest        # build-4.log  EXIT=0
cd build/tests && ctest -R '^ScriptStabilisationTest$' --output-on-failure
                                                      # test-4.log  EXIT=0
#   100% tests passed out of 1
```

new-p10-366 (`fix-p10-366.patch`, product files only):

```
git diff -- src/core/ScriptPackage.cpp include/ScriptPackage.h > .../logs/fix-p10-366.patch
git apply -R .../logs/fix-p10-366.patch                    # reverted the fix
cmake --build build --target ScriptStabilisationTest        # build-6.log  EXIT=0
cd build/tests && ctest -R '^ScriptStabilisationTest$' --output-on-failure
                                                      # test-6.log  EXIT=8
#   FAIL! manifestWindowIsMeasuredInBytes() 'parsed.name.isEmpty()' returned FALSE.
#   (the manifest window is 4096 bytes, not 4096 characters)
git apply .../logs/fix-p10-366.patch                       # restored the fix
cmake --build build --target ScriptStabilisationTest        # build-7.log  EXIT=0
cd build/tests && ctest -R '^ScriptStabilisationTest$' --output-on-failure
                                                      # test-7.log  EXIT=0
#   100% tests passed out of 1
```

new-p16-214 (`fix-p16-214.patch`):

```
git diff -- src/core/ScriptPackage.cpp > .../logs/fix-p16-214.patch
git apply -R .../logs/fix-p16-214.patch                    # reverted the fix
cmake --build build --target ScriptStabilisationTest        # build-8.log  EXIT=0
cd build/tests && ctest -R '^ScriptStabilisationTest$' --output-on-failure
                                                      # test-8.log  EXIT=8
#   FAIL! packageDiscoveryStaysInsideTheScriptsRoot() Compared values are not the same
git apply .../logs/fix-p16-214.patch                       # restored the fix
cmake --build build --target ScriptStabilisationTest        # build-9.log  EXIT=0
cd build/tests && ctest -R '^ScriptStabilisationTest$' --output-on-failure
                                                      # test-9.log  EXIT=0
#   100% tests passed out of 1
```

Each revert touched only the product file, so exactly one test failed per
revert — the test of that bug, and no other.

### Affected family

```
cmake --build build --target zene ScriptStabilisationTest ControlAutomationScriptTest \
    ScriptDawRemovalUndoTest ScriptEngineTest ScriptMemoryBudgetTest   # build-15.log EXIT=0
cd build/tests && ctest -R 'Script' --output-on-failure                # test-15.log  EXIT=8
#   8 of 12 Script* binaries pass; the 4 failures below are pre-existing
```

The four failures — `ControlAutomationScriptTest`,
`ScriptDawRemovalUndoTest`, `ScriptEngineTest`, `ScriptMemoryBudgetTest` — are
**pre-existing on this branch and unrelated to this batch**: they fail
identically with the base version of the changed product files
(`build-14.log` / `test-14.log`: the base `src/core/ScriptPackage.cpp` and
`include/ScriptPackage.h` of `b61e5f965` were restored, the four binaries
rebuilt, and the same four tests failed with the same assertions). They are
about automation track ids, effect-chain undo and the memory budget, none of
which touch `ScriptPackage`.

The family that covers this file, `ScriptStabilisationTest`, is green in full:
`Totals: 15 passed, 0 failed, 0 skipped` (`test-9b.log`, the binary run
directly), 1/1 ctest test executed (`test-15.log`).

### Gates

```
python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py \
    /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-5362
                                                      # gate-1.log  EXIT=0
#   PASS fork-sources / all-sources-reproduce / no-upstream-regression /
#        check-namespace / no-tautology / doc-figures
#   PASS complexity     (no new item in your files)
#   PASS file-length    (no new item in your files)
#   RESULT: PASS
```

The same command was run again after the documentation wording was added
(`gate-2.log`, EXIT=0, RESULT: PASS, 5 changed files).

`scan()` stays at cyclomatic complexity 9 and the new helpers at 5, 3 and 3
(`lizard src/core/ScriptPackage.cpp`), all at or below the target of 10.

## Files committed

- `src/core/ScriptPackage.cpp` — the three fixes (bounded read, byte window,
  canonical-root containment) and the byte counter seam.
- `include/ScriptPackage.h` — `entryScriptBytesReadForTesting()` (test seam),
  the byte-window wording of `parseManifest()` and the skip rule in the
  `scan()` documentation.
- `docs/LUA-PACKAGE-FORMAT.md` — the format document now says the manifest
  window is 4 KB *of the file* (counted in bytes) and states the
  outside-the-root skip rule.
- `tests/src/core/ScriptStabilisationTest.cpp` — the three regressions.
- `docs/qwen-fixes-2026-10/cluster-5362.md` — this report.

Commits (branch `bugs/qwen-5362`):

- `a26b33f66 test(cluster 5362): byte-read seam + failing regression for the
  whole-file entry read`
- `5d1244f51 fix(cluster 5362): read only the manifest window of an entry script`
- `25500371f fix(new-p10-366): count the manifest window in bytes, not characters`
- `b18eb30a4 fix(new-p16-214): keep package discovery inside the scripts root`

## Self-check

1. **"Each new test FAILS with your product change reverted, then PASSES
   restored."** — Done per bug, commands and exit codes above: cluster 5362
   `test-3.log` EXIT=8 → `test-4.log` EXIT=0; new-p10-366 `test-6.log` EXIT=8
   → `test-7.log` EXIT=0; new-p16-214 `test-8.log` EXIT=8 → `test-9.log`
   EXIT=0. The reverted patch contained product files only
   (`fix-5362.patch`, `fix-p10-366.patch`, `fix-p16-214.patch` in the log
   folder), never the test file, so each revert produced exactly one failing
   slot — the one belonging to that bug.
2. **"Every acceptance criterion is proven by a test you ran yourself."** —
   There is one criterion per bug, and each is a slot of
   `ScriptStabilisationTest` (15 slots, all pass): the bounded read
   (`packageScanningReadsOnlyTheManifestWindow`, `test-9b.log` EXIT=0), the
   byte window (`manifestWindowIsMeasuredInBytes`, EXIT=0), and the root
   containment (`packageDiscoveryStaysInsideTheScriptsRoot`, EXIT=0).
3. **"Each edit is ONE undo step, and every new or changed command has an
   honest reversibility class."** — Not applicable: no control command was
   added or changed (no file under `src/core/ControlRegistry.cpp`,
   `ControlCommands*` or `tools/mcp-zene-control/` is touched), and no undo
   step or project data format is involved — the change is in a read-only
   discovery path.
4. **"Nothing you added runs on the audio thread and allocates, frees, locks,
   waits, does I/O or emits a signal."** — `ScriptPackages::scan()` is a
   host-side file-I/O helper called from the UI/host side (documented as
   "host side, outside any run" in `docs/LUA-API-STABILISATION.md:273`); it
   is not on the audio thread, and no audio-path file was touched. The new
   code allocates a `QByteArray` of at most 4096 bytes per entry script and
   uses one relaxed atomic counter — no locks, no waits, no signals, no
   logging.
5. **"Old projects still load and render exactly as before; new data
   round-trips through save and load."** — Nothing in the save/load or
   project format was touched. The three changes only affect *what discovery
   lists*: a package entry script is now read for its first 4 KB only (the
   manifest is the only thing read from it), a manifest past byte 4096 is no
   longer seen (it never matched the documented 4 KB window), and a package
   directory or entry script that escapes the scripts root is no longer listed.
   An existing installation inside the root is unaffected: its manifest is in
   the first bytes of `package.lua`, as the format requires
   (`docs/LUA-PACKAGE-FORMAT.md:96`).
6. **"Qt 5.15 / MSVC / macOS < 10.15 portability."** — Only Qt API already
   used in this file and stable across Qt 5.15/6: `QFile::read`,
   `QFile::size`, `QFileInfo::canonicalFilePath`, `QByteArray::lastIndexOf`,
   `QByteArray::left`, `QString::toUtf8`. No Qt-6-only API, no `windows.h`
   macro names, no `std::filesystem`. The symlink test is skipped on Windows
   (`#ifdef _WIN32 QSKIP(...)`), exactly as the existing sandbox test
   `tests/src/core/ScriptImportTest.cpp:266` does.
7. **"Deterministic: no wall-clock timing, sleeps or races."** — The tests use
   a fixed 4 MB file and a byte counter; the symlink test uses `QFile::link`
   on a real directory. No timing, no sleeps, no threads. The one seam added
   (`entryScriptBytesReadForTesting()`) is a plain counter of bytes read, so
   the bounded-read claim is measured, not timed.
8. **"The fix is at the cause and complete."** — The three causes are in
   `src/core/ScriptPackage.cpp` only. Every caller was checked:
   `ScriptPackages::scan()` and `ScriptPackages::parseManifest()` are called
   from `src/core/ScriptPackage.cpp` itself and from
   `tests/src/core/ScriptStabilisationTest.cpp`; no product code calls them
   yet (`grep -rn "parseManifest\|ScriptPackages::scan" src include plugins`
   → only `src/core/ScriptPackage.cpp`), and `include/ScriptPackage.h` is
   included by no other file, so the header change needed no other rebuild
   (the `zene` target was rebuilt anyway). The sibling path into the same
   function — `parseManifest()`'s public `QString` overload — was given the
   same byte semantics as `scan()`, so the two cannot disagree.
9. **"No collateral damage: a limit or check you add must not hit other
   callers that legitimately exceed it."** — The 4096-byte limit applies only
   to the manifest window (the documented 4 KB); it is not applied to the
   script *body*, which the engine still reads whole
   (`src/core/ScriptEngine.cpp:337`). The root check applies only to package
   discovery and is the same rule the import sandbox already applies to
   modules. A symlink that stays inside the root is still listed (asserted by
   the test), so the check refuses escapes, not symlinks.
