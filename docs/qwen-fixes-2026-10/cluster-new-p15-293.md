new-p15-293: FIXED
Evidence: `tests/data/clap-test-plugin/clap-test-instrument.c:415` now rejects non-finite gains and values outside the parameter range declared at line 214 before assigning `self->gain`. The regression test at `tests/src/plugins/ClapTestInstrumentUnitTest.c:51` accepts valid endpoints and rejects NaN, infinities, below-range and above-range gains while preserving the current value; it also covers truncated and bad-magic state.
Commands and evidence (logs: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p15-293.r1/logs`):
- `cmake --build build --target zene` — EXIT=0 (`002-build-zene-red-state.log`).
- `cmake --build build/tests --target ClapTestInstrumentUnitTest` — EXIT=0 (`004-build-red-state-test.log`, `007-build-green-state.log`, `009-build-red-proof-state.log`, `011-build-green-proof-state.log`).
- RED, cwd `build/tests`: `ctest -R '^ClapTestInstrumentStateTest$' --output-on-failure` — EXIT=8, 1 test failed with `FAIL: invalid gain state was accepted` (`005-test-red-state.log`).
- GREEN, cwd `build/tests`: same ctest command — EXIT=0, 1 test passed (`008-test-green-state.log`).
- 4b remove: `git diff -- tests/data/clap-test-plugin/clap-test-instrument.c > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p15-293.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p15-293.r1/logs/fix.patch` — EXIT=0.
- 4b RED, cwd `build/tests`: same build command — EXIT=0; same ctest command — EXIT=8, 1 test failed with the same assertion (`010-test-red-proof-state.log`).
- 4b restore: `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p15-293.r1/logs/fix.patch` — EXIT=0; same build command — EXIT=0; same ctest command — EXIT=0, 1 test passed (`012-test-green-proof-state.log`).
