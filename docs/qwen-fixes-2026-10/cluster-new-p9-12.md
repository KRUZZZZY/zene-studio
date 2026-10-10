# Cluster new-p9-12

Verdict: **FIXED**

`ComboBoxModel::currentData()` indexed `m_items[value()]` with no items in the model. It now returns `nullptr` when `m_items` is empty.

## File and line evidence

- `include/ComboBoxModel.h:64-67` — `currentData()` guards the empty vector before indexing.
- `tests/src/core/AutomatableModelTest.cpp:77-87` — regression checks both a newly constructed model and a model after `clear()`.
- `tests/upstream-modifications.txt:1054-1055` — inherited header and test changes are registered with reasons.

## Red/green and reversal proof

Affected-family count: **1 CTest test** (`AutomatableModelTest`); the fixed run passed 1/1.

All test commands below ran from `build/tests`.

| Phase | Exact command | EXIT | Result |
|---|---|---:|---|
| Initial RED | `ctest -R '^AutomatableModelTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-12.r1/logs/test-1.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit "$rc"` | 8 | 1/1 failed with SIGSEGV before the fix. |
| Initial GREEN | `ctest -R '^AutomatableModelTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-12.r1/logs/test-2.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit "$rc"` | 0 | 1/1 passed with the fix. |
| Reversal RED | `ctest -R '^AutomatableModelTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-12.r1/logs/test-3.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit "$rc"` | 8 | After reversing the header patch and rebuilding, 1/1 failed with SIGSEGV. |
| Restored GREEN | `ctest -R '^AutomatableModelTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-12.r1/logs/test-4.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit "$rc"` | 0 | After restoring the patch and rebuilding, 1/1 passed. |

The reversal was performed using these commands; each exited 0:

```sh
git diff -- include/ComboBoxModel.h > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-12.r1/logs/fix.patch
git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-12.r1/logs/fix.patch
git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-12.r1/logs/fix.patch
```

The `git diff` and both `git apply` commands exited 0. Before each test run, the following build command exited 0:

| Phase | Exact build command | EXIT |
|---|---|---:|
| Initial RED setup | `cmake --build build --target zene AutomatableModelTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-12.r1/logs/build-1.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit "$rc"` | 0 |
| Initial GREEN | `cmake --build build --target zene AutomatableModelTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-12.r1/logs/build-2.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit "$rc"` | 0 |
| Reversal RED | `cmake --build build --target zene AutomatableModelTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-12.r1/logs/build-3.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit "$rc"` | 0 |
| Restored GREEN | `cmake --build build --target zene AutomatableModelTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-12.r1/logs/build-4.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit "$rc"` | 0 |

## Required gates

| Gate | Exact command | EXIT |
|---|---|---:|
| Complexity | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-12.r1/logs/gate-complexity-2.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit "$rc"` | 0 |
| File length | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-12.r1/logs/gate-file-length-2.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit "$rc"` | 0 |

## Files committed

- `include/ComboBoxModel.h`
- `tests/src/core/AutomatableModelTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p9-12.md`
