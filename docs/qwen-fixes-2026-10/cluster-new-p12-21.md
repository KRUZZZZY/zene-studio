# cluster new-p12-21 — BUG 4: "Fractional track selections can import the wrong track"

**Verdict: FALSE-POSITIVE**

The finding claimed `project.import_tracks` with `tracks: [1.9]` may import track 1
instead of rejecting the argument (`src/core/ControlCommandsImportTracks.cpp`, the
`tracks` loop in `chosen()`, historical line 151, now line 148). It does not: the
entry is rejected and no track is created.

## Evidence

`chosen()` converts each entry with `QJsonValue::toInt(-1)` (ControlCommandsImportTracks.cpp:148).
Qt6's `toInt` answers its default for a non-integral double — measured on the system
Qt6 the build links (`Qt6Core_DIR=/usr/lib/x86_64-linux-gnu/cmake/Qt6Core`) with a
standalone snippet:

```
[1.9]   isDouble=1 toInt(-1)=-1
[-1.5]  isDouble=1 toInt(-1)=-1
[1.0]   isDouble=1 toInt(-1)=1
["1.9"] isDouble=0 toInt(-1)=-1
```

So `[1.9]` yields index `-1`, and the loop at ControlCommandsImportTracks.cpp:157
rejects every `index < 0` with `ControlErrorKind::NotFound` before any document is
built (`buildAsOneStep` is never reached). The command's args schema
(`arrayProperty()`, ControlVocabulary.cpp:98) lets the fractional array through to
the handler, but the handler itself refuses it — the wrong track is never imported.

## Regression pin added (no product change)

`tests/src/core/LibraryProjectsTest.cpp` — `tracksComeFromAnotherProject()` now also
runs `project.import_tracks` with `tracks: [1.9]` and asserts the reply is not ok,
its `errorKind` is `NotFound`, and the song's track count is unchanged. On the
original product code this assertion **passes** (the bug does not exist), which is
the FALSE-POSITIVE proof — not a regression, so there is no RED on unmodified code.

To show the assertion has teeth (that it fails exactly for the truncating conversion
the finding assumed), the product loop was temporarily sabotaged to
`static_cast<int>(value.toDouble(-1))` — 1.9 then truncates to index 1 and the test
fails at `!fractional.ok`. The sabotage was reverted immediately; the committed tree
contains no product change (`git diff --stat -- src/core/ControlCommandsImportTracks.cpp`
is empty, file identical to HEAD).

## Commands and EXIT codes (all observed)

| Step | Command | EXIT |
|---|---|---|
| Build test (original code) | `cmake --build build --target LibraryProjectsTest > LOGS/build-1.log 2>&1` | 0 |
| Family run (original code) | `cd build/tests && ctest -R LibraryProjectsTest --output-on-failure > LOGS/test-1.log 2>&1` | 0 (100% passed, 0 failed out of 1) |
| Direct run (original code) | `QT_QPA_PLATFORM=offscreen ./build/tests/LibraryProjectsTest > LOGS/test-2-direct.log 2>&1` | 0 (`PASS: LibraryProjectsTest::tracksComeFromAnotherProject()`, Totals 9 passed, 0 failed) |
| Sabotage build (teeth proof) | `cmake --build build --target LibraryProjectsTest > LOGS/build-2.log 2>&1` | 0 |
| Sabotage run (assumed bug reproduced) | `QT_QPA_PLATFORM=offscreen ./build/tests/LibraryProjectsTest > LOGS/test-3-sabotage.log 2>&1` | 1 (`FAIL! ... '!fractional.ok' returned FALSE`, Loc LibraryProjectsTest.cpp(232)) |
| Revert build | `cmake --build build --target LibraryProjectsTest > LOGS/build-3.log 2>&1` | 0 |
| Family run after revert | `cd build/tests && ctest -R LibraryProjectsTest --output-on-failure > LOGS/test-4.log 2>&1` | 0 (100% passed, 0 failed out of 1) |
| Ratchet gate | `bash tests/complexity-gate.sh --check > LOGS/gate-complexity.log 2>&1` | 0 |
| Ratchet gate | `bash tests/file-length-gate.sh --check > LOGS/gate-filelength.log 2>&1` | 0 |

Affected-family test count: 1 ctest test (`LibraryProjectsTest`), 9 Qt test slots,
0 failures on the original code. Log folder:
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-21.r1/logs`.

## Files in this commit

- `tests/src/core/LibraryProjectsTest.cpp` — pinning assertion for fractional
  `tracks` entries (already registered in `tests/fork-sources.txt`; no new file).
- `docs/qwen-fixes-2026-10/cluster-new-p12-21.md` — this report.

No product file changed: the reported wrong-track import cannot occur with Qt6's
`QJsonValue::toInt` semantics.
