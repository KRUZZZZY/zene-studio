# Cluster new-p8-183

Verdict: **FIXED**

`isPublicIPv4()` previously accepted reserved benchmarking and documentation addresses as public, which could set `reachable_from_internet` to `true`. It now checks the non-global IPv4 subnets, including `198.18.0.0/15`, the three TEST-NET blocks, and the non-global part of `192.0.0.0/24`. The globally reachable `192.0.0.9` and `.10` anycast exceptions remain public. The ranges follow the [IANA IPv4 Special-Purpose Address Registry](https://www.iana.org/assignments/iana-ipv4-special-registry).

## File and line evidence

- [src/core/PortMappingCodec.cpp:53](../../src/core/PortMappingCodec.cpp:53) declares the explicit non-global subnet table.
- [src/core/PortMappingCodec.cpp:326](../../src/core/PortMappingCodec.cpp:326) applies the table and preserves the `.9` and `.10` exceptions.
- [src/core/PortMappingPmp.cpp:42](../../src/core/PortMappingPmp.cpp:42) derives `reachable_from_internet` from this helper.
- [tests/src/core/PortMappingCodecTest.cpp:249](../../tests/src/core/PortMappingCodecTest.cpp:249) covers the reported addresses, other non-global ranges, boundaries, and reachable exceptions.

## Regression evidence

All CTest commands below ran from `build/tests`. The affected family contains one CTest test; the final run executed `1/1` (`PortMappingCodecTest`, CTest test #303).

| Check | Exact command | Exit | Result |
| --- | --- | ---: | --- |
| RED on original production code (`test-2.log`) | `ctest -R '^PortMappingCodecTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-183.r2/logs/test-2.log 2>&1; result=$?; echo EXIT=$result; exit "$result"` | 8 | Failed at `198.18.0.1`. |
| GREEN with fix (`test-3.log`) | `ctest -R '^PortMappingCodecTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-183.r2/logs/test-3.log 2>&1; result=$?; echo EXIT=$result; exit "$result"` | 0 | Passed, `1/1`. |
| RED after reversing only the production patch (`test-4.log`) | `ctest -R '^PortMappingCodecTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-183.r2/logs/test-4.log 2>&1; result=$?; echo EXIT=$result; exit "$result"` | 8 | Failed again at `198.18.0.1`. |
| GREEN after restoring the patch (`test-5.log`) | `ctest -R '^PortMappingCodecTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-183.r2/logs/test-5.log 2>&1; result=$?; echo EXIT=$result; exit "$result"` | 0 | Passed, `1/1`. |

The build command `cmake --build build --target zene PortMappingCodecTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-183.r2/logs/build-5.log 2>&1; result=$?; echo EXIT=$result; exit "$result"` returned 0 before the first GREEN run. The same command returned 0 for the fix-reversed rebuild (`build-6.log`) and restored rebuild (`build-7.log`).

The proof commands `git diff -- src/core/PortMappingCodec.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-183.r2/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-183.r2/logs/fix.patch` and `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-183.r2/logs/fix.patch` each returned 0.

## Gates

| Gate | Exact command | Exit |
| --- | --- | ---: |
| Complexity (`gate-1.log`) | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-183.r2/logs/gate-1.log 2>&1; result=$?; echo EXIT=$result; exit "$result"` | 0 |
| File length (`gate-2.log`) | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-183.r2/logs/gate-2.log 2>&1; result=$?; echo EXIT=$result; exit "$result"` | 0 |

`fff` was not available in this session; logs were searched with single-term `rg` queries for `error`, `FAILED`, and `Assertion`. The `FAILED` matches were the expected RED runs.

## Files committed

- `src/core/PortMappingCodec.cpp`
- `tests/src/core/PortMappingCodecTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p8-183.md`
