# Cluster new-p8-179-port-port

FIXED

Ported by hand from bugs/qwen-new-p8-179-port (commits ce205fe62, 94be171ee, 5eb4b134a) onto the current line.

## Evidence
- RED: ctest -R 'PortMappingCodecTest|UpnpMapperFakeRouterTest' exit 8 (2 failed)
- GREEN: same command exit 0 (4 tests, 100% passed)
- feature_check.py exit 0 RESULT: PASS

## Files
- include/PortMapping.h
- src/core/PortMapping.cpp
- src/core/PortMappingPmp.cpp
- src/core/PortMappingUpnp.cpp
- tests/src/core/PortMappingCodecTest.cpp
- tests/src/core/UpnpMapperFakeRouterTest.cpp
- docs/qwen-fixes-2026-10/cluster-new-p8-179-port-port.md

## Self-check
- Regression test RED/GREEN: reverted product fix, ctest exit 8 (2 failed); restored, ctest exit 0 (4 passed). Evidence logs test-red.log, test-green3.log.
- Determinism: tests use held replies (m_hold), fake gateways, injected lifetimes; sleeps are bounded waits for deterministic completion, no wall-clock races.
- Cause/complete: checked PortMapping, PmpMapper, UpnpMapper callers; consistent.
- Collateral: no saved data changes; no limits added affecting other callers.
