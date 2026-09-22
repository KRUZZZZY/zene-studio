fork-sources.txt registration checks (040/ui-precond, 2026-09-22):
1. entry order is LC_ALL=C sorted: OK
2. my entries' sort positions:
484:include/Accessibility.h
636:include/WcagContrast.h
3. Gate 9 (tests/fork-sources-gate.sh) covers registered->exists + all-sources reproduce: see 040-ui-precond-gate9.log, EXIT=0.
