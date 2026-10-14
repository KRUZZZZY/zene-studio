# Cluster new-p10-110: range scanning rescanned document prefixes

**Verdict: FIXED**

`src/core/DocumentIndex.cpp:381` now advances byte and character cursors through the input. `scanSectionRanges()` initializes those cursors once at `:419` and carries them across each selected section's start and end offset conversions at `:442` and `:451`. This preserves the existing UTF-8 lead-byte conversion while making the conversion work linear across all selected sections.

The regression at `tests/src/core/DocumentIndexTest.cpp:586` compares one selected child with 513 selected children after a 1 MiB prefix. Before the fix, the two-scan measurements were 16 ms and 1,930 ms; with the fix, the scaling assertion passes.

## Evidence

| Check | Exact command | Exit / result |
| --- | --- | --- |
| Initial RED build | `cmake --build build --target zene DocumentIndexTest` (log: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-110.r1/logs/build-red-1.log`) | 0 |
| Initial RED | `ctest -R '^DocumentIndexTest$' --output-on-failure` (log: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-110.r1/logs/test-red-1.log`) | 8; scaling assertion failed at 16 ms vs 1,930 ms |
| Initial GREEN build | `cmake --build build --target zene DocumentIndexTest` (log: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-110.r1/logs/build-green-1.log`) | 0 |
| Initial GREEN | `ctest -R '^DocumentIndexTest$' --output-on-failure` (log: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-110.r1/logs/test-green-1.log`) | 0 |
| Remove production fix | `git diff -- src/core/DocumentIndex.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-110.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-110.r1/logs/fix.patch` | 0 |
| Reverse-check build | `cmake --build build --target zene DocumentIndexTest` (log: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-110.r1/logs/build-proof-red-1.log`) | 0 |
| Reverse-check RED | `ctest -R '^DocumentIndexTest$' --output-on-failure` (log: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-110.r1/logs/test-proof-red-1.log`) | 8; same scaling assertion failed at 16 ms vs 1,930 ms |
| Restore production fix | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-110.r1/logs/fix.patch` | 0 |
| Restored build | `cmake --build build --target zene DocumentIndexTest` (log: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-110.r1/logs/build-proof-green-1.log`) | 0 |
| Restored GREEN | `ctest -R '^DocumentIndexTest$' --output-on-failure` (log: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-110.r1/logs/test-proof-green-1.log`) | 0 |
| Affected-family count | `ctest -R '^DocumentIndexTest$' --output-on-failure` | 1/1 CTest target; the RED QtTest run reported 12 passed, 1 failed, 0 skipped (13 total) |
| Complexity gate | `bash tests/complexity-gate.sh --check` (log: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-110.r1/logs/complexity-gate-1.log`) | 0 |
| File-length gate | `bash tests/file-length-gate.sh --check` (log: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-110.r1/logs/file-length-gate-1.log`) | 0 |

The CTest and build commands above redirected their output to the named log. The harness appended the underlying command's exit status as an `EXIT=` line; all log searches used `rg` because the `fff grep` tool was unavailable in this session.

## Files committed

- `src/core/DocumentIndex.cpp`
- `tests/src/core/DocumentIndexTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p10-110.md`
