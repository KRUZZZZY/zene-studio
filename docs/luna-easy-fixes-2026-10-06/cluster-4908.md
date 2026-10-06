# Clef cluster 4908

- **Verdict: FIXED.** The finding applies: before the fix, `vca.create` redo recreated the group with an empty name.
- **Source:** [ControlCommandsVca.cpp](../../src/core/ControlCommandsVca.cpp#L137) reads the supplied name; its redo closure now captures and reuses it at lines 158–160.
- **Regression:** [ControlVcaCommandsTest.cpp](../../tests/src/core/ControlVcaCommandsTest.cpp#L282) creates a named group, undoes it, redoes it, and checks the exact restored name.
- **RED:** `cmake --build build --target zene ControlVcaCommandsTest` EXIT=0; from `build/tests`, `ctest -R '^ControlVcaCommandsTest$' --output-on-failure` EXIT=8 (1/1 selected; actual `VCA 0`, expected `Redo preserves this name`).
- **GREEN:** the same targeted build EXIT=0; the same CTest EXIT=0 (1/1); `ctest -V -R '^ControlVcaCommandsTest$'` EXIT=0 (10 passed, 0 failed, 0 skipped).
- **Required ratchets:** `bash tests/complexity-gate.sh --check` EXIT=0; `bash tests/file-length-gate.sh --check` EXIT=0.
- **Registration:** product source is in `tests/fork-sources.txt`; the existing test source is in `tests/all-sources.txt`. No new source path was added, so the all-sources manifest needed no regeneration.
- **Evidence:** [RED transcript](cluster-4908-red.md), [GREEN transcript](cluster-4908-green.md), [ratchet transcript](cluster-4908-gates.md). Fix commit: `e0ae11ff2`.
- **Unverified:** full suite and full gate sweep were intentionally not run; other operating systems and toolchains were not checked. No shared header changed, so plugin modules were not affected.
