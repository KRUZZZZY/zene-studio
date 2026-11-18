# Cluster new-p9-355: piano view focus with no model

**Verdict: FIXED**

`PianoView` initializes `m_piano` to null. `focusInEvent()` previously dereferenced it unconditionally; it now returns for a null model, matching `focusOutEvent()`. The guard is at [PianoView.cpp:686](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-355/src/gui/instrument/PianoView.cpp:686). The regression in `tests/src/gui/PianoViewFocusTest.cpp` directly sends a focus-in event to a default, unmodeled view.

## Evidence

| Check | Exact command | Result |
|---|---|---|
| Initial regression build (target was not yet in generated build files) | `cmake --build build --target PianoViewFocusTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-355.r1/logs/build-1.log 2>&1; echo EXIT=$?` | `EXIT=2`; no rule for the new target. |
| Build existing target to regenerate, then build the regression target | `cmake --build build --target zene > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-355.r1/logs/build-2.log 2>&1; echo EXIT=$?`<br>`cmake --build build --target PianoViewFocusTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-355.r1/logs/build-3.log 2>&1; echo EXIT=$?` | Both `EXIT=0`. |
| RED, original handler | From `build/tests`: `ctest -R PianoViewFocusTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-355.r1/logs/test-1.log 2>&1; echo EXIT=$?` | `EXIT=8`; 1 test ran and segfaulted. |
| Build fixed product and affected test | `cmake --build build --target zene PianoViewFocusTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-355.r1/logs/build-4.log 2>&1; echo EXIT=$?` | `EXIT=0`. |
| GREEN | From `build/tests`: `ctest -R PianoViewFocusTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-355.r1/logs/test-2.log 2>&1; echo EXIT=$?` | `EXIT=0`; 1/1 passed. |
| Reverse only the production fix | `git diff -- src/gui/instrument/PianoView.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-355.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-355.r1/logs/fix.patch; echo EXIT=$?` | `EXIT=0`. |
| Rebuild without fix | `cmake --build build --target zene PianoViewFocusTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-355.r1/logs/build-5.log 2>&1; echo EXIT=$?` | `EXIT=0`. |
| Regression proof without fix | From `build/tests`: `ctest -R PianoViewFocusTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-355.r1/logs/test-3.log 2>&1; echo EXIT=$?` | `EXIT=8`; 1 test ran and segfaulted again. |
| Restore fix | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-355.r1/logs/fix.patch; echo EXIT=$?` | `EXIT=0`. |
| Final rebuild | `cmake --build build --target zene PianoViewFocusTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-355.r1/logs/build-6.log 2>&1; echo EXIT=$?` | `EXIT=0`. |
| Final GREEN | From `build/tests`: `ctest -R PianoViewFocusTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-355.r1/logs/test-4.log 2>&1; echo EXIT=$?` | `EXIT=0`; 1/1 passed. |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-355.r1/logs/gate-1.log 2>&1; echo EXIT=$?` | `EXIT=0`. |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-355.r1/logs/gate-2.log 2>&1; echo EXIT=$?` | `EXIT=0`. |

Affected-family count: `PianoViewFocusTest`, 1 registered ctest and 1 executed; final result 1/1 passed. The reverse-patch run confirms the test fails without the production guard.

The fff grep tool was not available in this session, so log checks used `rg` on failure markers and ctest summaries. All build/test output is in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-355.r1/logs`.

## Files committed

- `src/gui/instrument/PianoView.cpp`
- `tests/src/gui/PianoViewFocusTest.cpp`
- `tests/CMakeLists.txt`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p9-355.md`

## QA follow-up (round 2: fork-source registration)

The product fix and regression were accepted in round 1. This follow-up registers the new GUI test in `tests/fork-sources.txt`, adds its exact path to both the regenerate and verify recipes, and places it in sorted order. The focused entry check passes. The full documented recipe still exits 1 because of unrelated source-list drift already present before this correction; its diagnostic was byte-identical before and after the change, and the new test path does not appear among those differences.

| Check | Exact command | Result |
|---|---|---|
| Full recipe before correction | `bash -c 'sed -n "45,76p" tests/fork-sources.txt \| sed -E "s/^# ?//" \| bash' > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-355.r2/logs/manifest-verify-1.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit 0` | Captured verifier `EXIT=1`; unrelated list differences. |
| Exact new entry appears in the fork manifest | `bash -c 'test "$(git diff --name-only --diff-filter=A 4e677cb6c6ab HEAD -- tests/src/gui/PianoViewFocusTest.cpp)" = "$(rg -x "tests/src/gui/PianoViewFocusTest.cpp" tests/fork-sources.txt)' > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-355.r2/logs/manifest-entry-check-1.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit 0` | `EXIT=0`. |
| Full recipe after correction | `bash -c 'sed -n "45,76p" tests/fork-sources.txt \| sed -E "s/^# ?//" \| bash' > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-355.r2/logs/manifest-verify-2.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit 0` | Captured verifier `EXIT=1`; same pre-existing list differences as before. |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-355.r2/logs/complexity-gate-4.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-355.r2/logs/complexity-gate-4.log; printf 'EXIT=%s\n' "$rc"; exit 0` | `EXIT=0`. |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-355.r2/logs/file-length-gate-4.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-355.r2/logs/file-length-gate-4.log; printf 'EXIT=%s\n' "$rc"; exit 0` | `EXIT=0`. |

No build or ctest rerun was needed for this manifest-only correction; round 1's regression build, RED/GREEN run, and reverse-patch proof remain documented above.

Files in the bookkeeping follow-up commit:

- `tests/fork-sources.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p9-355.md`
