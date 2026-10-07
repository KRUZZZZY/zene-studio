# Cluster new-p8-181: Truncated NAT-PMP replies

## Verdict: FIXED

`decodePmpReply()` now rejects a NAT-PMP address reply shorter than 12 bytes and a TCP mapping reply shorter than 16 bytes. It still decodes the common 8-byte header first, preserving the result code on short error replies. The payload checks are in [PortMappingCodec.cpp](../../src/core/PortMappingCodec.cpp#L85) and validity is set only after they pass at [PortMappingCodec.cpp](../../src/core/PortMappingCodec.cpp#L171). Regression assertions cover 8-byte success replies for both opcodes at [PortMappingCodecTest.cpp](../../tests/src/core/PortMappingCodecTest.cpp#L149).

The affected ctest family contains one test, `PortMappingCodecTest`; the final run passed 1/1. The added assertion failed on the original decoder and failed again with the final production patch temporarily removed.

## Evidence and command results

Commands were run in the worktree unless the working directory is noted. Build and test output is in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-181.r1/logs`.

### Build invocations

All build commands below exited 0. Odd numbered logs built `zene`; even numbered logs built `PortMappingCodecTest`.

```text
cmake --build build --target zene > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-181.r1/logs/build-1.log 2>&1; echo EXIT=$?  EXIT=0
cmake --build build --target PortMappingCodecTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-181.r1/logs/build-2.log 2>&1; echo EXIT=$?  EXIT=0
cmake --build build --target zene > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-181.r1/logs/build-3.log 2>&1; echo EXIT=$?  EXIT=0
cmake --build build --target PortMappingCodecTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-181.r1/logs/build-4.log 2>&1; echo EXIT=$?  EXIT=0
cmake --build build --target zene > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-181.r1/logs/build-5.log 2>&1; echo EXIT=$?  EXIT=0
cmake --build build --target PortMappingCodecTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-181.r1/logs/build-6.log 2>&1; echo EXIT=$?  EXIT=0
cmake --build build --target zene > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-181.r1/logs/build-7.log 2>&1; echo EXIT=$?  EXIT=0
cmake --build build --target PortMappingCodecTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-181.r1/logs/build-8.log 2>&1; echo EXIT=$?  EXIT=0
cmake --build build --target zene > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-181.r1/logs/build-9.log 2>&1; echo EXIT=$?  EXIT=0
cmake --build build --target PortMappingCodecTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-181.r1/logs/build-10.log 2>&1; echo EXIT=$?  EXIT=0
cmake --build build --target zene > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-181.r1/logs/build-11.log 2>&1; echo EXIT=$?  EXIT=0
cmake --build build --target PortMappingCodecTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-181.r1/logs/build-12.log 2>&1; echo EXIT=$?  EXIT=0
cmake --build build --target zene > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-181.r1/logs/build-13.log 2>&1; echo EXIT=$?  EXIT=0
cmake --build build --target PortMappingCodecTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-181.r1/logs/build-14.log 2>&1; echo EXIT=$?  EXIT=0
cmake --build build --target zene > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-181.r1/logs/build-15.log 2>&1; echo EXIT=$?  EXIT=0
cmake --build build --target PortMappingCodecTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-181.r1/logs/build-16.log 2>&1; echo EXIT=$?  EXIT=0
```

### CTest runs

Run from `/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p8-181/build/tests`. Each command selected exactly one ctest test (`PortMappingCodecTest`). The initial RED and final GREEN are the required regression comparison; test-7/test-8 are the final reverse-patch proof.

```text
ctest -R '^PortMappingCodecTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-181.r1/logs/test-1.log 2>&1; echo EXIT=$?  EXIT=8 (original decoder; new map assertion failed)
ctest -R '^PortMappingCodecTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-181.r1/logs/test-2.log 2>&1; echo EXIT=$?  EXIT=8 (intermediate attempt also dropped the short error reply result code)
ctest -R '^PortMappingCodecTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-181.r1/logs/test-3.log 2>&1; echo EXIT=$?  EXIT=0
ctest -R '^PortMappingCodecTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-181.r1/logs/test-4.log 2>&1; echo EXIT=$?  EXIT=8 (intermediate reverse-patch proof)
ctest -R '^PortMappingCodecTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-181.r1/logs/test-5.log 2>&1; echo EXIT=$?  EXIT=0
ctest -R '^PortMappingCodecTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-181.r1/logs/test-6.log 2>&1; echo EXIT=$?  EXIT=0
ctest -R '^PortMappingCodecTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-181.r1/logs/test-7.log 2>&1; echo EXIT=$?  EXIT=8 (final patch reversed; new map assertion failed)
ctest -R '^PortMappingCodecTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-181.r1/logs/test-8.log 2>&1; echo EXIT=$?  EXIT=0 (final fix restored; 1/1 passed)
```

### Ratchet gates

The first complexity check was on an intermediate implementation and exited 1 because `decodePmpReply` reached CCN 12. Splitting the payload checks into `decodePmpPayload` brought the final check under the limit.

```text
bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-181.r1/logs/gate-1.log 2>&1; echo EXIT=$?  EXIT=1 (intermediate CCN 12)
bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-181.r1/logs/gate-2.log 2>&1; echo EXIT=$?  EXIT=0
bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-181.r1/logs/gate-3.log 2>&1; echo EXIT=$?  EXIT=0
```

## Files in the fix commit

- `src/core/PortMappingCodec.cpp`
- `tests/src/core/PortMappingCodecTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p8-181.md`
