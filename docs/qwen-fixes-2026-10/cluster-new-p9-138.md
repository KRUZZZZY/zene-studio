# Cluster new-p9-138

Verdict: **FIXED**

`WaveSample::m_interpolate` had no initial value in either constructor. `ExprSynth` passes it to wave registration at [ExprSynth.cpp:721-723](../../plugins/Xpressive/ExprSynth.cpp), so a caller that did not first call `setInterpolate()` could expose an indeterminate `bool`. The in-class initializer at [ExprSynth.h:92](../../plugins/Xpressive/ExprSynth.h) sets the default to `false` for both constructors; callers can still override it with `setInterpolate()`.

The regression at [PluginPortsMigrationTest.cpp:263-289](../../tests/src/plugins/PluginPortsMigrationTest.cpp) seeds the member representation and checks both the integer-length and `graphModel` constructors. It fails on the original header and passes with the initializer.

## Verification evidence

Commands below were run in the stated working directory. `LOGS` in descriptions refers to `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-138.r1/logs`.

| Check | Command | Exit |
|---|---|---:|
| RED, original behavior (`build/tests`) | `./PluginPortsMigrationTest waveSampleDefaultsInterpolationToDisabled > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-138.r1/logs/test-3.log 2>&1` | 1 |
| GREEN, fixed behavior (`build/tests`) | `./PluginPortsMigrationTest waveSampleDefaultsInterpolationToDisabled > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-138.r1/logs/test-8.log 2>&1` | 0 |
| Affected family (`build/tests`) | `ctest -R '^PluginPortsMigrationTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-138.r1/logs/test-9.log 2>&1` | 0 |
| Family count | CTest reported `1/1` passed (`PluginPortsMigrationTest`, test #313); zero tests did not run. | 1/1 |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-138.r1/logs/gate-3.log 2>&1` | 0 |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-138.r1/logs/gate-4.log 2>&1` | 0 |

### Reversal proof

| Command | Exit |
|---|---:|
| `git diff -- plugins/Xpressive/ExprSynth.h > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-138.r1/logs/fix.patch` | 0 |
| `git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-138.r1/logs/fix.patch` | 0 |
| `cmake --build build --target zene PluginPortsMigrationTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-138.r1/logs/build-7.log 2>&1` | 0 |
| `./PluginPortsMigrationTest waveSampleDefaultsInterpolationToDisabled > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-138.r1/logs/test-5.log 2>&1` (`build/tests`) | 1 |
| `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-138.r1/logs/fix.patch` | 0 |
| `cmake --build build --target zene PluginPortsMigrationTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-138.r1/logs/build-8.log 2>&1` | 0 |
| `./PluginPortsMigrationTest waveSampleDefaultsInterpolationToDisabled > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-138.r1/logs/test-6.log 2>&1` (`build/tests`) | 0 |

The final build command was `cmake --build build --target zene PluginPortsMigrationTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-138.r1/logs/build-9.log 2>&1` (exit 0).

Probe calibration: the first build attempts (`build-1.log` and `build-2.log`) exited 2 because GCC's `-Werror=uninitialized` caught the test's direct member-byte probe. That probe was revised to read object representation through no-inline byte helpers. An earlier probe (`test-1.log`) exited 0 and was discarded; it is not used as RED evidence. The final test's RED and reversal RED both fail at the intended assertion.

The environment did not expose an `fff` search tool, so the log scans used `rg` with the requested terms (`error`, `FAILED`, and `Assertion`).

## Files committed

- `plugins/Xpressive/ExprSynth.h`
- `tests/src/plugins/PluginPortsMigrationTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p9-138.md`
