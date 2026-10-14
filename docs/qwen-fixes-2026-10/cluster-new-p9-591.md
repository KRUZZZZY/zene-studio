# Cluster new-p9-591

Verdict: **FIXED**

`BasicFilters` owned its double-filter subfilter through a raw pointer and inherited shallow copy operations. [include/BasicFilters.h:279](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-591/include/BasicFilters.h:279) now creates it with `std::make_unique`; [include/BasicFilters.h:295](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-591/include/BasicFilters.h:295) defaults the destructor, and [include/BasicFilters.h:908](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-591/include/BasicFilters.h:908) stores it as a `std::unique_ptr`. This makes copying unavailable and retains automatic cleanup.

The regression is [PluginPortsMigrationTest.cpp:223](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-591/tests/src/plugins/PluginPortsMigrationTest.cpp:223). It checks that construction and assignment copies are both disabled. On the original code the construction check failed; after temporarily removing the production patch it failed again; with the patch restored the family passed.

All build and test logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-591.r1/logs`.

## Evidence

| Stage | Exact command | Exit / result |
|---|---|---|
| Original-code RED build | `cmake --build build --target PluginPortsMigrationTest > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-591.r1/logs/build-1.log' 2>&1` | 0 |
| Original-code RED test | `ctest -R '^PluginPortsMigrationTest$' --output-on-failure > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-591.r1/logs/test-1.log' 2>&1` | 8; `basicFiltersCannotBeCopied` failed as expected; QtTest totals: 8 passed, 1 failed. CTest ran 1/1 family target. |
| Fixed build | `cmake --build build --target zene PluginPortsMigrationTest > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-591.r1/logs/build-2.log' 2>&1` | 0 |
| Fixed GREEN test | `ctest -R '^PluginPortsMigrationTest$' --output-on-failure > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-591.r1/logs/test-2.log' 2>&1` | 0; CTest passed 1/1. |
| Remove product patch for proof | `git diff -- include/BasicFilters.h > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-591.r1/logs/fix.patch' && git apply -R '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-591.r1/logs/fix.patch'` | 0 |
| Reverted-code rebuild | `cmake --build build --target zene PluginPortsMigrationTest > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-591.r1/logs/build-3.log' 2>&1` | 0 |
| Proof RED test | `ctest -R '^PluginPortsMigrationTest$' --output-on-failure > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-591.r1/logs/test-3.log' 2>&1` | 8; same assertion failed again; QtTest totals: 8 passed, 1 failed. |
| Restore product patch | `git apply '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-591.r1/logs/fix.patch'` | 0 |
| Restored-code rebuild | `cmake --build build --target zene PluginPortsMigrationTest > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-591.r1/logs/build-4.log' 2>&1` | 0 |
| Proof GREEN test | `ctest -R '^PluginPortsMigrationTest$' --output-on-failure > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-591.r1/logs/test-4.log' 2>&1` | 0; CTest passed 1/1. |
| Complexity gate | `bash tests/complexity-gate.sh --check > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-591.r1/logs/gate-1.log' 2>&1` | 0 |
| File-length gate | `bash tests/file-length-gate.sh --check > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-591.r1/logs/gate-2.log' 2>&1` | 0 |
| All-target build attempt | `cmake --build build > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-591.r1/logs/build-5.log' 2>&1` | 97; workspace `bunny-guard` requires a named target and says full builds are the parent’s job. The scoped `zene` and `PluginPortsMigrationTest` builds passed. |

## Files committed

- `include/BasicFilters.h`
- `tests/src/plugins/PluginPortsMigrationTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p9-591.md`
