# Cluster new-p8-182

Verdict: **FIXED**

The finding is real. `src/core/PortMappingCodec.cpp:105-110` now accepts only a valid HTTP control URL whose scheme and host match the description location. `src/core/PortMappingCodec.cpp:279-282` validates the resolved URL before returning the service. The regression at `tests/src/core/PortMappingCodecTest.cpp:227-231` covers a loopback `URLBase` and an absolute loopback `controlURL`.

## Verification evidence

The original implementation failed at `PortMappingCodecTest::ssdpAndDescription()` on the `hostileBase` assertion. Removing the production patch reproduced the same failure. With the patch restored, the affected CTest family passed: **1 CTest test executed, 1/1 passed**.

| Evidence | Exact command, count, or files | Exit |
|---|---|---:|
| Initial RED | `ctest -R '^PortMappingCodecTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-182.r1/logs/test-1.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-182.r1/logs/test-1.log; echo EXIT=$rc; exit $rc` | 8 |
| Fixed GREEN | `ctest -R '^PortMappingCodecTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-182.r1/logs/test-2.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-182.r1/logs/test-2.log; echo EXIT=$rc; exit $rc` | 0 |
| Patch removed RED | `ctest -R '^PortMappingCodecTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-182.r1/logs/test-3.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-182.r1/logs/test-3.log; echo EXIT=$rc; exit $rc` | 8 |
| Patch restored GREEN | `ctest -R '^PortMappingCodecTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-182.r1/logs/test-4.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-182.r1/logs/test-4.log; echo EXIT=$rc; exit $rc` | 0 |
| Affected-family test count | `PortMappingCodecTest` (CTest #303): 1 test executed, 1/1 passed on each GREEN run | — |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-182.r1/logs/gate-1.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-182.r1/logs/gate-1.log; echo EXIT=$rc; exit $rc` | 0 |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-182.r1/logs/gate-2.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-182.r1/logs/gate-2.log; echo EXIT=$rc; exit $rc` | 0 |
| Files in the fix commit | `src/core/PortMappingCodec.cpp`; `tests/src/core/PortMappingCodecTest.cpp`; `docs/qwen-fixes-2026-10/cluster-new-p8-182.md` | — |

All four build invocations exited 0:

- `cmake --build build --target zene PortMappingCodecTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-182.r1/logs/build-1.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-182.r1/logs/build-1.log; echo EXIT=$rc; exit $rc`
- `cmake --build build --target zene PortMappingCodecTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-182.r1/logs/build-2.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-182.r1/logs/build-2.log; echo EXIT=$rc; exit $rc`
- `cmake --build build --target zene PortMappingCodecTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-182.r1/logs/build-3.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-182.r1/logs/build-3.log; echo EXIT=$rc; exit $rc`
- `cmake --build build --target zene PortMappingCodecTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-182.r1/logs/build-4.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-182.r1/logs/build-4.log; echo EXIT=$rc; exit $rc`

Patch-removal proof commands exited 0:

- `git diff -- src/core/PortMappingCodec.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-182.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-182.r1/logs/fix.patch`
- `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-182.r1/logs/fix.patch`

- `git diff --check` — exit 0.

## Files in the commit

- `src/core/PortMappingCodec.cpp`
- `tests/src/core/PortMappingCodecTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p8-182.md`
