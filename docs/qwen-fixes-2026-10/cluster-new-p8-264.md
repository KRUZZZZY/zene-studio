# Cluster new-p8-264

**Verdict: FIXED**

In [Vst3Instrument.cpp](../../plugins/Vst3Instrument/Vst3Instrument.cpp#L338), the unprepared branch now zeros every channel and frame in the output view before returning `Continue`. The fill uses `std::fill` over existing spans and does not allocate. The regression in [Vst3InstrumentIntegrationTest.cpp](../../tests/src/plugins/Vst3InstrumentIntegrationTest.cpp#L394) first renders the fixture's nonzero level, releases its host, and checks the next rendered block is exactly silent.

The integration target now compiles and links the existing VST3 host sources so the test can release the same host instance through `Vst3Instrument::vst3Plugin()`. This target setup is recorded in `tests/upstream-modifications.txt`.

## RED/GREEN evidence

The commands below ran from the worktree root for builds and from `build/tests` for ctest.

| Run | Exact command | Exit |
|---|---|---:|
| RED, original product code (`test-1.log`) | `ctest -R '^Vst3InstrumentIntegrationTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-264.r1/logs/test-1.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 8 |
| GREEN, fixed product code (`test-2.log`) | `ctest -R '^Vst3InstrumentIntegrationTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-264.r1/logs/test-2.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 0 |

The RED assertion was `testAnUnpreparedInstrumentClearsItsOutput()`: the previous rendered samples remained nonzero. The required reversal proof used these exact commands:

```sh
git diff -- plugins/Vst3Instrument/Vst3Instrument.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-264.r1/logs/fix.patch; result=$?; echo EXIT=$result; exit $result
# EXIT=0
git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-264.r1/logs/fix.patch; result=$?; echo EXIT=$result; exit $result
# EXIT=0
cmake --build build --target Vst3InstrumentIntegrationTest zene > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-264.r1/logs/build-8.log 2>&1; result=$?; echo EXIT=$result; exit $result
# EXIT=0
ctest -R '^Vst3InstrumentIntegrationTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-264.r1/logs/test-3.log 2>&1; result=$?; echo EXIT=$result; exit $result
# EXIT=8 (same output assertion failed)
git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-264.r1/logs/fix.patch; result=$?; echo EXIT=$result; exit $result
# EXIT=0
cmake --build build --target Vst3InstrumentIntegrationTest zene > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-264.r1/logs/build-9.log 2>&1; result=$?; echo EXIT=$result; exit $result
# EXIT=0
ctest -R '^Vst3InstrumentIntegrationTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-264.r1/logs/test-4.log 2>&1; result=$?; echo EXIT=$result; exit $result
# EXIT=0
```

Build commands for the integration target and `zene` were run from the worktree root. The initial test-wiring iterations exited 2 while resolving test include/link setup:

```sh
cmake --build build --target Vst3InstrumentIntegrationTest zene > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-264.r1/logs/build-1.log 2>&1; result=$?; echo EXIT=$result; exit $result
# EXIT=2
cmake --build build --target Vst3InstrumentIntegrationTest zene > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-264.r1/logs/build-2.log 2>&1; result=$?; echo EXIT=$result; exit $result
# EXIT=2
cmake --build build --target Vst3InstrumentIntegrationTest zene > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-264.r1/logs/build-3.log 2>&1; result=$?; echo EXIT=$result; exit $result
# EXIT=2
cmake --build build --target Vst3InstrumentIntegrationTest zene > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-264.r1/logs/build-4.log 2>&1; result=$?; echo EXIT=$result; exit $result
# EXIT=2
cmake --build build --target Vst3InstrumentIntegrationTest zene > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-264.r1/logs/build-5.log 2>&1; result=$?; echo EXIT=$result; exit $result
# EXIT=2
```

After fixing the test target wiring, these builds succeeded:

```sh
cmake --build build --target Vst3InstrumentIntegrationTest zene > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-264.r1/logs/build-6.log 2>&1; result=$?; echo EXIT=$result; exit $result
# EXIT=0
cmake --build build --target Vst3InstrumentIntegrationTest zene > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-264.r1/logs/build-7.log 2>&1; result=$?; echo EXIT=$result; exit $result
# EXIT=0
```

## Final affected-family run

Build command, exit 0:

```sh
cmake --build build --target Vst3InstrumentFixtureProbe Vst3InstrumentTest Vst3InstrumentIntegrationTest zene > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-264.r1/logs/build-10.log 2>&1; result=$?; echo EXIT=$result; exit $result
```

Test command, exit 0:

```sh
ctest -R '^Vst3Instrument' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-264.r1/logs/test-5.log 2>&1; result=$?; echo EXIT=$result; exit $result
```

Affected-family count: **3 ctest targets passed** — `Vst3InstrumentFixtureProbe`, `Vst3InstrumentTest`, and `Vst3InstrumentIntegrationTest` (3/3; zero tests skipped).

## Required gates

| Gate | Exact command | Exit |
|---|---|---:|
| Complexity | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-264.r1/logs/gate-1.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 0 |
| File length | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-264.r1/logs/gate-2.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 0 |

## Files committed

- `plugins/Vst3Instrument/Vst3Instrument.cpp`
- `tests/src/plugins/Vst3InstrumentIntegrationTest.cpp`
- `tests/CMakeLists.txt`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p8-264.md`
