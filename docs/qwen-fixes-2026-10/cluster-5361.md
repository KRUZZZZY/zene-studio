# Cluster 5361 — a directory named `package.lua` is listed as a package entry

Verdict: **ALREADY-FIXED** on this branch (`bugs/qwen-5361`).

## Finding

- `src/core/ScriptPackage.cpp:105` (historical): the scanner gated the entry
  path on `QFileInfo::exists()`, which is true for a directory, so a
  subdirectory named `package.lua` made the parent directory scan as a
  package even though the entry script cannot be opened.

## Evidence

Current code already requires a regular file:

- `src/core/ScriptPackage.cpp:105` — `if (!QFileInfo(entryPath).isFile())`
  then `continue;` (inside `ScriptPackages::scan()`).

The fix landed on this branch's history in commit
`170b2fa5f fix(script): a directory named package.lua was listed as a package entry`
(`git log --oneline -3 -- src/core/ScriptPackage.cpp`), which changed
`src/core/ScriptPackage.cpp:105` from `if (!QFileInfo::exists(entryPath))`
to `if (!QFileInfo(entryPath).isFile())`.

Regression test exists and covers exactly this case:

- `tests/src/core/ScriptStabilisationTest.cpp:454` —
  `aDirectoryNamedPackageLuaIsNotAPackage()`: creates `bundle/package.lua` as a
  directory plus a well-formed `arp` package beside it and asserts
  `scan()` returns exactly one package named `arp`.

## Commands run (this session)

RED was not re-run: the buggy code predates this branch's tip; the fix commit
`170b2fa5f` recorded the RED itself (`ctest -R '^ScriptStabilisationTest$'`
EXIT=8, `FAIL! aDirectoryNamedPackageLuaIsNotAPackage()`). I did not revert
production code to reproduce it.

GREEN (verified on the current tree):

```
cmake --build build --target zene ScriptStabilisationTest   # build-1.log
EXIT=0
cd build/tests && ctest -R '^ScriptStabilisationTest$' --output-on-failure   # test-1.log
EXIT=0   # 1/1 test executed and passed: "1/1 Test #206: ScriptStabilisationTest .......... Passed"
```

Ratchet gates (no production edit was needed; run anyway):

```
bash tests/complexity-gate.sh --check   # gate-complexity-1.log
EXIT=0
bash tests/file-length-gate.sh --check  # gate-length-1.log
EXIT=0
```

Logs: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5361.r1/logs/`
(`build-1.log`, `test-1.log`, `gate-complexity-1.log`, `gate-length-1.log`).

## Files committed

- `docs/qwen-fixes-2026-10/cluster-5361.md` (this report; no code change —
  the fix and its test are already in the branch history).
