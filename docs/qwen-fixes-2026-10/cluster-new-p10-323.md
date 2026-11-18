# Cluster new-p10-323

**Verdict: FIXED**

`parseChange()` previously accepted a decimal sequence such as `01` as the
integer `1`, while `writeTo()` emits `QString::number(1)` as `1`. The loader
now rejects sequence text unless it exactly matches the writer's canonical
decimal spelling. The regression test verifies that the rejected section does
not replace the in-memory journal.

Evidence: [ProvenanceSection.cpp:220](../../src/core/ProvenanceSection.cpp#L220)
and [ProvenanceSectionTest.cpp:547](../../tests/src/core/ProvenanceSectionTest.cpp#L547).

## Validation evidence

All commands below ran in the worktree. CTest commands ran from `build/tests`;
the build and gate commands ran from the worktree root. Test and gate commands
redirected output to new files under
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-323.r1/logs`.
The displayed `EXIT` is the command's printed result.

| Evidence | Exact command | Result |
| --- | --- | --- |
| Original-code regression build | `cmake --build build --target ProvenanceSectionTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-323.r1/logs/build-1.log 2>&1; echo EXIT=$?` | `EXIT=0` |
| RED on original code | `ctest -R '^ProvenanceSectionTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-323.r1/logs/test-1.log 2>&1; echo EXIT=$?` | `EXIT=8`; the new assertion failed because `Section::load()` accepted `seq="01"`. |
| GREEN after fix | `cmake --build build --target ProvenanceSectionTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-323.r1/logs/build-2.log 2>&1; echo EXIT=$?` then `ctest -R '^ProvenanceSectionTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-323.r1/logs/test-2.log 2>&1; echo EXIT=$?` | Both `EXIT=0`; CTest `1/1` passed. |
| Reverse-fix proof, remove production patch | `git diff -- src/core/ProvenanceSection.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-323.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-323.r1/logs/fix.patch; echo EXIT=$?` | `EXIT=0` |
| Reverse-fix proof, rebuild and RED | `cmake --build build --target zene ProvenanceSectionTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-323.r1/logs/build-3.log 2>&1; echo EXIT=$?` then `ctest -R '^ProvenanceSectionTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-323.r1/logs/test-3.log 2>&1; echo EXIT=$?` | Build `EXIT=0`; CTest `EXIT=8`, failing at the same assertion. |
| Reverse-fix proof, restore production patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-323.r1/logs/fix.patch; echo EXIT=$?` | `EXIT=0` |
| Reverse-fix proof, rebuild and GREEN | `cmake --build build --target zene ProvenanceSectionTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-323.r1/logs/build-4.log 2>&1; echo EXIT=$?` then `ctest -R '^ProvenanceSectionTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-323.r1/logs/test-4.log 2>&1; echo EXIT=$?` | Both `EXIT=0`; CTest `1/1` passed. |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-323.r1/logs/gate-3.log 2>&1; echo EXIT=$?` | `EXIT=0` |
| File length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-323.r1/logs/gate-4.log 2>&1; echo EXIT=$?` | `EXIT=0` |
| Diff whitespace check | `git diff --check` | `EXIT=0` |

The affected CTest family executed one test target (`ProvenanceSectionTest`),
which passed. Its RED run reported 12 passed and 1 failed, including the new
regression.

## Files committed

- `src/core/ProvenanceSection.cpp`
- `tests/src/core/ProvenanceSectionTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p10-323.md`

The source and test files are already registered in `tests/fork-sources.txt`.
