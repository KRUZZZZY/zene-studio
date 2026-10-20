FIXED

Ported by hand from bugs/qwen-new-p8-179-port (commits ce205fe62, 94be171ee, 5eb4b134a) onto the current line.

## Evidence
- RED (product reverted): ctest -R 'PortMappingCodecTest|UpnpMapperFakeRouterTest' exit 8 (0% passed, 2 failed; 5 regression cases failed)
- GREEN: ctest -R 'PortMapping|CollabPolicy|UpnpMapper' exit 0 (4 targets, 100% passed)
- feature_check.py exit 0 RESULT: PASS
- complexity gate exit 1 (pre-existing train-wide items only)
- file-length gate exit 1 (pre-existing train-wide items only)

## Self-check
- Regression test FAILS with product change reverted and PASSES with it (exit codes above).
- Deterministic: uses held replies, fake gateways, processEvents loops; no wall-clock sleeps.
- Cause/complete: PortMapping, PmpMapper, UpnpMapper callers checked and consistent.
- Collateral: no saved data changes; scoped deletions only to reachable protocols/stages.
