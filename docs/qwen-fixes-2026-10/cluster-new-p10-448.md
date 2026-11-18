# Cluster new-p10-448

**Verdict: FALSE-POSITIVE**

The reported code structure exists, but the claimed missing-label behavior did not reproduce on this configured Qt 6 build. In a temporary GUI test linked directly to the production Vectorscope sources, the footer had 0 full-intensity label-color pixels before wheel zoom and 152 after zoom, while production code was unchanged. The baseline comparison passed, so there was no justified production fix or RED-to-GREEN transition. The temporary test and its CMake registration were removed because they passed on the original behavior.

## File evidence

- [VectorView.cpp](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-448/plugins/Vectorscope/VectorView.cpp:70) starts the paint-event painter; line 202 calls `drawZoomInfo()` while it remains in scope.
- [VectorView.cpp](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-448/plugins/Vectorscope/VectorView.cpp:265) constructs the second painter. The temporary runtime check observed the zoom label in the footer on this build despite that structure.

## Evidence table

| Check | Exact command and result |
|---|---|
| RED | No RED result: the original-code label-presence check passed. Exact command: `ctest -R '^VectorViewTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-448.r1/logs/test-9-vectorview-red.log 2>&1; echo EXIT=$?` — `EXIT=0`. |
| GREEN | Not applicable: no production fix was made because the finding was not reproduced. |
| Affected family | `VectorViewTest`: 1 test executed, 1 passed in the original-code baseline comparison (`test-9-vectorview-red.log`). |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-448.r1/logs/gate-12-complexity.log 2>&1; echo EXIT=$?` — `EXIT=0`. |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-448.r1/logs/gate-13-file-length.log 2>&1; echo EXIT=$?` — `EXIT=0`. |
| Files committed | `docs/qwen-fixes-2026-10/cluster-new-p10-448.md` only. |

## Build and test record

Each command below returned the stated unpiped exit code. The ctest commands ran from `build/tests`.

| Log | Exact command | Exit |
|---|---|---:|
| `build-1-zene-red.log` | `cmake --build build --target zene -- -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-448.r1/logs/build-1-zene-red.log 2>&1; echo EXIT=$?` | 0 |
| `build-2-vectorview-red.log` | `cmake --build build --target VectorViewTest -- -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-448.r1/logs/build-2-vectorview-red.log 2>&1; echo EXIT=$?` | 0 |
| `test-3-vectorview-red.log` | `ctest -R '^VectorViewTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-448.r1/logs/test-3-vectorview-red.log 2>&1; echo EXIT=$?` | 0 |
| `build-4-vectorview-red.log` | `cmake --build build --target VectorViewTest -- -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-448.r1/logs/build-4-vectorview-red.log 2>&1; echo EXIT=$?` | 0 |
| `test-5-vectorview-red.log` | `ctest -R '^VectorViewTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-448.r1/logs/test-5-vectorview-red.log 2>&1; echo EXIT=$?` | 0 |
| `build-6-vectorview-diagnostic.log` | `cmake --build build --target VectorViewTest -- -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-448.r1/logs/build-6-vectorview-diagnostic.log 2>&1; echo EXIT=$?` | 0 |
| `test-7-vectorview-diagnostic.log` | `ctest -R '^VectorViewTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-448.r1/logs/test-7-vectorview-diagnostic.log 2>&1; echo EXIT=$?` | 8 — the deliberately inverted assertion reported 152 full-intensity footer pixels. |
| `build-8-vectorview-red.log` | `cmake --build build --target VectorViewTest -- -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-448.r1/logs/build-8-vectorview-red.log 2>&1; echo EXIT=$?` | 0 |
| `test-9-vectorview-red.log` | `ctest -R '^VectorViewTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-448.r1/logs/test-9-vectorview-red.log 2>&1; echo EXIT=$?` | 0 — 1/1 passed; footer count changed from 0 to 152. |
| `build-10-vectorview-count.log` | `cmake --build build --target VectorViewTest -- -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-448.r1/logs/build-10-vectorview-count.log 2>&1; echo EXIT=$?` | 0 |
| `test-11-vectorview-count.log` | `ctest -R '^VectorViewTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-448.r1/logs/test-11-vectorview-count.log 2>&1; echo EXIT=$?` | 8 — the deliberately inverted comparison reported `before: 0, after: 152`. |

No product or test source changes are committed for this finding.
