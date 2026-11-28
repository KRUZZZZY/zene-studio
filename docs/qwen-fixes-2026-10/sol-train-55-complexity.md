# Train 55 complexity repair

The complexity ratchet reported three new over-target test functions: the control socket drain test (CCN 15), the fractional impulse helper (11), and the sub-sample clamp test (13). Extracting the existing EOF receive loop, silent-channel check, and history seeding preserves their inputs and assertions while reducing each caller's complexity. No baseline or gate is changed.

- `PYTHONPATH="$PWD/../ci-python" ctest -R '^NoteCanvasComplexityGate$' --output-on-failure --no-tests=error` from `build/tests`: failed before the extraction with the three regressions above.
The following checks were recorded by the original pending repair; the source changes are identical. This replacement resolves its provenance ledger conflict against the selected train.

- `PYTHONPATH="$PWD/build/ci-python" bash tests/complexity-gate.sh --check`: passed after extraction (lizard 1.26.1).
- `PYTHONPATH="$PWD/../ci-python" ctest -R '^NoteCanvasComplexityGate$' --output-on-failure --no-tests=error` from `build/tests`: passed after extraction (1/1).
- `PYTHONPATH="$PWD/build/ci-python" bash tests/no-upstream-regression-gate.sh`: passed.
- `PYTHONPATH="$PWD/build/ci-python" bash tests/evidence-gate.sh`: passed.
- `CCACHE_DIR="$PWD/build/ccache" CCACHE_TEMPDIR="$PWD/build/ccache-tmp" cmake --build build --target zene ControlServerDrainTest DeviceDspTest -j12`: passed.
- `QT_QPA_PLATFORM=offscreen ctest -R '^(ControlServerDrainTest|DeviceDspTest)$' --output-on-failure --no-tests=error` from `build/tests`: passed (2/2). Without the offscreen setting, DeviceDspTest aborted during Qt platform initialization; gdb located the abort in `QGuiApplicationPrivate::createPlatformIntegration`.

Replacement verification in this checkout: `CCACHE_DISABLE=1 cmake --build build --target zene ControlServerDrainTest DeviceDspTest -j12` passed; `QT_QPA_PLATFORM=offscreen ctest -R '^(ControlServerDrainTest|DeviceDspTest)$' --no-tests=error --output-on-failure` passed 2/2 from build/tests. The complexity ctest passed 1/1 with lizard 1.26.1 and both provenance and evidence gates passed.
