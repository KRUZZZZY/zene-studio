# Cluster 4873 — Stem export rejects absolute paths on Windows

**Verdict: FIXED**

## The finding

`src/core/ControlCommandsRenderStems.cpp:119` — `parseStemArgs` decided whether
`render.stems`' `out` argument was an absolute directory with a hardcoded
`'/'` test instead of Qt's platform-aware check:

```cpp
if (!request->directory.startsWith(QLatin1Char('/')))
```

On Windows every real destination (`C:\Exports\Stems`, a `C:\...\Temp\...`
directory) fails that test, so the command refused a valid absolute path as
`invalid_args` with the message
`'out' must be an absolute directory path ...`. The finding is REAL: the check
is platform-wrong, and the file already includes `<QFileInfo>` and uses
`QFileInfo` for the sibling checks (line 124).

## The fix (smallest correct change)

`src/core/ControlCommandsRenderStems.cpp:119` — one line, exactly the fix the
finding prescribes:

```cpp
if (!QFileInfo(request->directory).isAbsolute())
```

Behaviour-preserving on POSIX: `QFileInfo::isAbsolute()` agrees with
`startsWith('/')` for every POSIX path, so no Linux/POSIX behaviour changes;
on Windows drive-rooted and UNC paths are now accepted as absolute.

## The regression test

`tests/src/core/StemExportTest.cpp` (the existing stem-export suite) gained
`stemExportAbsolutePathIsPlatformAware()`:

* every platform: a relative `out` (`"stems"`) must still be refused as
  `invalid_args`;
* Windows (`#ifdef Q_OS_WIN`): the suite's own drive-rooted temporary
  directory (`C:\...\Temp\...`) is absolute, so `render.stems` must NOT be
  refused as `invalid_args` — this is the finding's edge case and the genuine
  RED on Windows (the pre-fix check rejected every Windows path);
* POSIX: the handler's accept/reject decision must agree with
  `QFileInfo(path).isAbsolute()` for a Windows-style path
  (`C:\Exports\Stems`), i.e. the handler follows Qt's platform-aware check
  rather than either hardcoding.

**Honest platform note:** this run is on a Linux host, where the Windows
edge case is *not observable* — `QFileInfo("C:\\Exports\\Stems").isAbsolute()`
is false on POSIX, identical to the old `'/'` test, so the test passes both
before and after the fix here (no fake RED is claimed). The Windows-only
assertion is compiled in under `Q_OS_WIN` and is the RED/GREEN pair there.

## Evidence (commands run, exit codes copied verbatim)

Logs: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/4873.r1/logs`

| Step | Command (run from the worktree unless noted) | EXIT |
|---|---|---|
| build test, pre-fix | `cmake --build build --target StemExportTest > $LOGS/build-1.log 2>&1` | `0` |
| RED attempt, pre-fix | `ctest -R StemExportTest --output-on-failure > $LOGS/test-1.log 2>&1` (from `build/tests`) | `0` — 1/1 test passed; see platform note above |
| rebuild after fix | `cmake --build build --target zene StemExportTest > $LOGS/build-2.log 2>&1` | `0` |
| GREEN rerun, family | `ctest -R StemExport --output-on-failure > $LOGS/test-2.log 2>&1` (from `build/tests`) | `0` — 2/2 tests passed |
| verb lane after fix | `ctest -R ControlStemExportVerb --output-on-failure > $LOGS/test-3.log 2>&1` (from `build/tests`) | `0` — 1/1 test passed |
| complexity gate | `bash tests/complexity-gate.sh --check > $LOGS/gate-1.log 2>&1` | `0` |
| file-length gate | `bash tests/file-length-gate.sh --check > $LOGS/gate-2.log 2>&1` | `0` |

## Files committed

* `src/core/ControlCommandsRenderStems.cpp` — the one-line fix (fork-new file,
  already registered in `tests/fork-sources.txt:1259`; no manifest change).
* `tests/src/core/StemExportTest.cpp` — the regression slot (fork-new file,
  already registered; no manifest change).
* `docs/qwen-fixes-2026-10/cluster-4873.md` — this report.
