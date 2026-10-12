# Cluster new-p10-322: provenance change child content

**Verdict: FIXED**

`parseChange()` now rejects any `<z:change>` with child nodes. The writer emits empty change elements, so the loader refuses a nested element or text and lets `Song::loadProject()` preserve the whole section through its unclaimed-element path.

## File and line evidence

- [`src/core/ProvenanceSection.cpp:213`](../../src/core/ProvenanceSection.cpp#L213) defines `parseChange()`; line 217 rejects `element.hasChildNodes()` before reading the entry.
- [`tests/src/core/ProvenanceSectionTest.cpp:608`](../../tests/src/core/ProvenanceSectionTest.cpp#L608) loads a project containing a nested `<z:future/>` and text inside change entries, saves it, and checks both pieces of content survive.
- `src/core/Song.cpp:1698-1700` captures a refused section as unclaimed; `src/core/Song.cpp:2005` re-emits it when saving.

The changed product and test files were already registered in `tests/fork-sources.txt` at lines 1522 and 2001; no source-manifest edit was needed.

## Evidence table

The affected CTest family is `ProvenanceSectionTest`: one CTest target ran per invocation. The original and reverse-patch RED runs each failed the new preservation assertion; the fixed runs passed 1/1.

| Check | Exact command | Exit | CTest count/result |
|---|---|---:|---|
| Initial RED, before the product fix | `cd /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-322/build/tests; QT_QPA_PLATFORM=offscreen ctest -R '^ProvenanceSectionTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-322.r1/logs/test-1.log 2>&1; test_status=$?; echo EXIT=$test_status; exit $test_status` | 8 | 0/1 passed; preservation assertion failed |
| GREEN, product fix applied | `cd /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-322/build/tests; QT_QPA_PLATFORM=offscreen ctest -R '^ProvenanceSectionTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-322.r1/logs/test-2.log 2>&1; test_status=$?; echo EXIT=$test_status; exit $test_status` | 0 | 1/1 passed |
| Reverse-patch RED, product fix removed | `cd /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-322/build/tests; QT_QPA_PLATFORM=offscreen ctest -R '^ProvenanceSectionTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-322.r1/logs/test-3.log 2>&1; test_status=$?; echo EXIT=$test_status; exit $test_status` | 8 | 0/1 passed; same assertion failed |
| GREEN, product fix restored | `cd /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-322/build/tests; QT_QPA_PLATFORM=offscreen ctest -R '^ProvenanceSectionTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-322.r1/logs/test-4.log 2>&1; test_status=$?; echo EXIT=$test_status; exit $test_status` | 0 | 1/1 passed |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-322.r1/logs/gate-complexity-2.log 2>&1; gate_status=$?; echo EXIT=$gate_status; exit $gate_status` | 0 | — |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-322.r1/logs/gate-file-length-2.log 2>&1; gate_status=$?; echo EXIT=$gate_status; exit $gate_status` | 0 | — |

The product was rebuilt with `zene` and `ProvenanceSectionTest` before every CTest run:

- Baseline build: `cmake --build build --target zene ProvenanceSectionTest -- -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-322.r1/logs/build-1.log 2>&1; build_status=$?; echo EXIT=$build_status; exit $build_status` — exit 0.
- Fixed build: `cmake --build build --target zene ProvenanceSectionTest -- -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-322.r1/logs/build-2.log 2>&1; build_status=$?; echo EXIT=$build_status; exit $build_status` — exit 0.
- Reverse-patch build: `cmake --build build --target zene ProvenanceSectionTest -- -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-322.r1/logs/build-3.log 2>&1; build_status=$?; echo EXIT=$build_status; exit $build_status` — exit 0.
- Restored build: `cmake --build build --target zene ProvenanceSectionTest -- -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-322.r1/logs/build-4.log 2>&1; build_status=$?; echo EXIT=$build_status; exit $build_status` — exit 0.

The reverse-patch steps both exited 0:

- `git diff -- src/core/ProvenanceSection.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-322.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-322.r1/logs/fix.patch; patch_status=$?; echo EXIT=$patch_status; exit $patch_status`
- `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-322.r1/logs/fix.patch; patch_status=$?; echo EXIT=$patch_status; exit $patch_status`

## Files committed

- `src/core/ProvenanceSection.cpp`
- `tests/src/core/ProvenanceSectionTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p10-322.md`
