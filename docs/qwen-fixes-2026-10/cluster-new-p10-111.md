# Cluster new-p10-111: document index metadata

Verdict: **FIXED**

`parseDocumentIndex()` previously accepted a named `<z:section>` with an empty
`entry` or `digest`, and `QString::toInt()` silently converted a missing or
invalid `v` to zero. The parser now rejects the whole index if any named row is
missing required metadata or has a non-integer version. The documented
nameless-row skip remains unchanged.

Evidence:

- `src/core/DocumentIndex.cpp:331-341` reads the required fields, checks the
  version conversion result, and rejects a malformed named row by returning an
  empty index.
- `tests/src/core/DocumentIndexTest.cpp:389-410` tests missing and empty
  `entry`/`digest`, missing `v`, and non-integer `v`. Each malformed row is
  paired with a valid row and must invalidate the entire index.

## Verification

All test commands ran from `build/tests`; all build and test output is in the
numbered files under `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-111.r1/logs`.

| Check | Exact command | Exit | Evidence |
|---|---|---:|---|
| Initial RED before the production change | `ctest -R '^DocumentIndexTest$' --output-on-failure` | 8 | `test-1.log`; 1/1 test failed on a named row missing `entry`. |
| Final regression RED with the product patch reversed | `ctest -R '^DocumentIndexTest$' --output-on-failure` | 8 | `test-5.log`; 1/1 test failed with the final valid-sibling fixture. |
| Final GREEN after restoring the product patch | `ctest -R '^DocumentIndexTest$' --output-on-failure` | 0 | `test-6.log`; 1/1 test passed. |
| Affected test family | `DocumentIndexTest` | 0 | 1/1 test executed; zero tests run would be an error. |
| Complexity gate | `bash tests/complexity-gate.sh --check` | 0 | `gate-3.log`: PASS. |
| File-length gate | `bash tests/file-length-gate.sh --check` | 0 | `gate-4.log`: PASS. |

The initial regression build (`build-1.log`) ran `cmake --build build --target
DocumentIndexTest` and exited 0. Builds in `build-2.log` through `build-6.log`
ran `cmake --build build --target zene DocumentIndexTest` and each exited 0.
The proof patch was created with `git diff -- src/core/DocumentIndex.cpp >
/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-111.r1/logs/fix.patch`
and reversed with `git apply -R
/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-111.r1/logs/fix.patch`;
both commands exited 0. The final proof restored it with `git apply
/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-111.r1/logs/fix.patch`,
which also exited 0.

The fff grep tool was not available in this session, so numbered logs were
searched with `rg` for `error`, `FAILED`, and `Assertion`.

## Files committed

- `src/core/DocumentIndex.cpp`
- `tests/src/core/DocumentIndexTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p10-111.md`
