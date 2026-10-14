# Cluster new-p9-128: loaded dummy effects have no owning chain

**Verdict: FIXED**

`EffectChain::loadSettings()` constructed the missing-plugin fallback at `src/core/EffectChain.cpp:175`, then inserted it at line 179 without assigning its owning chain. `DummyEffect` initializes its `Effect` base with no chain (`include/DummyEffect.h:95-101`). The user-visible address path returns an empty object when `effectChain()` is null (`src/gui/EffectDeviceMenu.cpp:202-206`).

The loader now calls `e->setEffectChain(this)` at `src/core/EffectChain.cpp:178` before inserting every restored effect. The regression at `tests/src/gui/EffectHostingMenuTest.cpp:96-115` loads a deliberately unavailable effect on a mixer channel and verifies both its owner pointer and device address.

## Evidence

All `ctest` commands below ran from `build/tests`; build and gate commands ran from the worktree root. Each build/test log is a new file under `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-128.r1/logs`.

| Phase | Exact command | Exit |
|---|---|---:|
| Build regression against original code | `cmake --build build --target EffectHostingMenuTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-128.r1/logs/build-1.log 2>&1` | 0 |
| Original code, RED | `ctest -R '^EffectHostingMenuTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-128.r1/logs/test-1.log 2>&1` | 8 |
| Build fix and `zene` | `cmake --build build --target zene EffectHostingMenuTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-128.r1/logs/build-2.log 2>&1` | 0 |
| Fixed code, GREEN | `ctest -R '^EffectHostingMenuTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-128.r1/logs/test-2.log 2>&1` | 0 |
| Reverse production diff for regression proof | `git diff -- src/core/EffectChain.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-128.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-128.r1/logs/fix.patch` | 0 |
| Build with fix reversed | `cmake --build build --target zene EffectHostingMenuTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-128.r1/logs/build-3.log 2>&1` | 0 |
| Reversed fix, RED again | `ctest -R '^EffectHostingMenuTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-128.r1/logs/test-3.log 2>&1` | 8 |
| Reapply production diff | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-128.r1/logs/fix.patch` | 0 |
| Rebuild restored fix and `zene` | `cmake --build build --target zene EffectHostingMenuTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-128.r1/logs/build-4.log 2>&1` | 0 |
| Restored fix, GREEN | `ctest -R '^EffectHostingMenuTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-128.r1/logs/test-4.log 2>&1` | 0 |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-128.r1/logs/complexity-gate-1.log 2>&1` | 0 |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-128.r1/logs/file-length-gate-1.log 2>&1` | 0 |

The affected ctest family ran **1/1 CTest case** (`EffectHostingMenuTest`); it passed after the fix. Both RED runs failed at the owner-pointer comparison, reporting the actual pointer as null. The same test passed after each restoration.

The `fff` grep tool was not exposed in this session; build, test, and gate logs were searched with `rg` for `error`, `FAILED`, and `Assertion`.

## Files in the commit

- `src/core/EffectChain.cpp`
- `tests/src/gui/EffectHostingMenuTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p9-128.md`
