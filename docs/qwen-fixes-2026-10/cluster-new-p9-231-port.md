# Cluster new-p9-231-port: Hydrogen TinyXML hex references

**Verdict: ALREADY-FIXED** on `050/m0-train-5`, by `f6b9ea0ed` (cluster new-p10-224).
This port adds the regression that was missing for the finding's named inputs and
ports one `isxdigit` guard from the original fix.

## Evidence

`plugins/HydrogenImport/LocalFileMng.cpp:141-163` on this tree:

- `:141` checks `str->size() - pos >= 6` before any indexed read, so a truncated
  `&#x` at line end cannot read past the buffer.
- `:161-162` the `else` branch advances `searchFrom = pos + 3`, so a marker that is
  not a complete two-digit byte escape (for example `&#xA;` or `&#x444;`) is skipped
  instead of found again forever.

The original `faf155be3` hang is the same logic: its pre-fix loop searched from
offset 0 each time, and `&#xA;` never satisfied the guard, so it never advanced.

What was missing on this tree: `tests/src/plugins/HydrogenImportLocalFileMngTest.cpp`
covered only truncated escapes and `&#x41;`. It did not cover the standard-width
references the finding names.

## Changes in this commit

1. `tests/src/plugins/HydrogenImportLocalFileMngTest.cpp:32-41` adds
   `standardWidthEscapesReturnAndRemainUnchanged` for `&#xA;` and `&#x444;`.
   The probe loop moved into `requireConversionReturns` (`:43-58`), shared with the
   truncated-escape slot.
2. `plugins/HydrogenImport/LocalFileMng.cpp:142-143` casts the `str->at()` argument of
   the two `isxdigit` calls to `unsigned char`, as in `faf155be3`. `isxdigit` on a
   negative `char` is undefined behaviour for high bytes. **No RED exists for this
   line**: glibc returns the same result for negative and unsigned indices, so no
   observable difference can be asserted. It is included as a conformance guard
   only.

No new files, so no registration is needed. `LocalFileMng.cpp` already has its
`tests/upstream-modifications.txt` entry from `f6b9ea0ed`.

## RED / GREEN

RED is the pre-fix converter. The reversal patch is the `f6b9ea0ed` diff to
`LocalFileMng.cpp`, applied in reverse and then restored. The probe is killed after
2 s, so the hang is reported as a failure.

| Step | Command (cwd: worktree; `L` = LOG FOLDER) | EXIT |
|---|---|---:|
| Baseline build, default env | `cmake --build build --target HydrogenImportLocalFileMngTest > $L/build-1.log 2>&1` | 2 |
| cause | ccache could not write `/run/user/1000/ccache-tmp` (Permission denied) | — |
| Baseline build, ccache temp in /tmp | `CCACHE_TEMPDIR=/tmp/ccache-tmp-new-p9-231-port cmake --build build --target HydrogenImportLocalFileMngTest > $L/build-2.log 2>&1` | 0 |
| Baseline ctest (before the test edit) | `cd build/tests && ctest -R '^HydrogenImportLocalFileMngTest$' --output-on-failure > $L/test-2.log 2>&1` | 0 |
| GREEN, extended test on this tree | build `build-3.log` (0); `ctest -R '^HydrogenImportLocalFileMngTest$' ... > $L/test-3.log` | 0 |
| GREEN per-slot | `build/tests/HydrogenImportLocalFileMngTest > $L/run-green.log 2>&1` | 0 (4 passed, 0 failed) |
| Reverse check | `git show f6b9ea0ed -- plugins/HydrogenImport/LocalFileMng.cpp > $L/fix.patch`; `git apply -R --check $L/fix.patch` | 0 |
| RED: remove fix | `git apply -R $L/fix.patch`; build `build-4.log` | 0 |
| RED run | `build/tests/HydrogenImportLocalFileMngTest > $L/run-red.log 2>&1` | 2 |
| RED output | `FAIL! : ...standardWidthEscapesReturnAndRemainUnchanged() conversion did not return for an escape` (also the truncated slot, which lacks the bounds check) | — |
| Restore fix | `git apply $L/fix.patch` | 0 |
| Final build (affected family) | `CCACHE_TEMPDIR=... cmake --build build --target hydrogenimport HydrogenImportLocalFileMngTest HydrogenImportTest > $L/build-5.log 2>&1` | 0 |
| Final ctest family | `cd build/tests && ctest -R '^HydrogenImport' --output-on-failure > $L/test-5.log 2>&1` | 0 (2/2 tests) |
| Final per-slot | `build/tests/HydrogenImportLocalFileMngTest > $L/run-green-2.log 2>&1` | 0 (4 passed) |

The RED run was done with the cast absent. The cast was added after the restore.
`fix.patch` is the `f6b9ea0ed` change; the cast is not in it.

Affected family: `ctest -R '^HydrogenImport'` ran 2 CTest tests
(`HydrogenImportLocalFileMngTest`, `HydrogenImportTest`), both passed.

## Gates

| Gate | Command | EXIT on this commit | EXIT on HEAD (same gate, HEAD copies of the two changed files) |
|---|---|---:|---:|
| Complexity | `bash tests/complexity-gate.sh --check > $L/gate-1.log 2>&1` | 1 | 1 (`gate-base-1.log`) |
| File length | `bash tests/file-length-gate.sh --check > $L/gate-2.log 2>&1` | 1 | 1 (`gate-base-2.log`) |

Both gates fail identically on HEAD: the `REGRESSION`/`FAIL` line sets are equal
(6 complexity lines, 14 file-length lines). None of them names
`LocalFileMng.cpp` or `HydrogenImportLocalFileMngTest.cpp`. These are pre-existing
ratchet regressions in unrelated files (for example
`tests/src/core/VideoTrackTest.cpp (589)` and `src/core/TakeLane.cpp (520)`).
This change adds no new failures. I did not change any gate, limit or baseline.

## Files in this commit

- `plugins/HydrogenImport/LocalFileMng.cpp`
- `tests/src/plugins/HydrogenImportLocalFileMngTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p9-231-port.md`

Not ported: the `faf155be3` test `ImportDetectionTest::tinyXmlCompatibilityReferencesDoNotHang`.
It `#include`s `LocalFileMng.cpp` into a test translation unit. The newest line already
has an isolated test target for this converter (`tests/CMakeLists.txt:1347-1350`), so I
extended that target instead.

## Build notes

- The first build failed because ccache could not write to `/run/user/1000`. Only the
  environment variable `CCACHE_TEMPDIR` was changed, pointed at `/tmp`. No file was changed.
- `test-1.log` in the LOG FOLDER is the run against the missing binary (`0% tests passed`,
  EXIT 8). It is not a result and is not cited above.
