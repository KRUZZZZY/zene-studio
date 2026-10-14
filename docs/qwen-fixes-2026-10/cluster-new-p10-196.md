# Cluster new-p10-196

**Verdict: FIXED**

`LadspaControl::setValue()` treated every nonzero float as on for toggled
ports. It now sets the model from `_value > 0.0f`, so negative and zero
values are off.

## Evidence

- [src/core/LadspaControl.cpp:163](../../src/core/LadspaControl.cpp#L163) applies
  the positive-value condition in the `Toggled` branch.
- [tests/src/core/AutomatableModelTest.cpp:60](../../tests/src/core/AutomatableModelTest.cpp#L60)
  exercises the live `LadspaControl`: positive turns on, then `-1.0f` and
  `0.0f` turn it off.
- The affected ctest family is `AutomatableModelTest`: one registered CTest
  target. The RED run reports 6 passed and 1 failed QtTest cases; the GREEN
  run passes the target.

## Regression and fix proof

All CTest commands below ran from `build/tests`. Build and test output was
captured under
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-196.r1/logs`.
For test commands, `EXIT` is the captured CTest status; the shell wrapper
returns zero so the expected RED result does not stop the proof sequence.

| Phase | Exact command | Exit |
| --- | --- | ---: |
| RED build | `cmake --build build --target AutomatableModelTest zene > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-196.r1/logs/build-1.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit "$rc"` | 0 |
| RED test | `ctest -R '^AutomatableModelTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-196.r1/logs/test-1.log 2>&1; rc=$?; echo EXIT=$rc; exit 0` | 8 |
| GREEN build | `cmake --build build --target AutomatableModelTest zene > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-196.r1/logs/build-2.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc` | 0 |
| GREEN test | `ctest -R '^AutomatableModelTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-196.r1/logs/test-2.log 2>&1; rc=$?; echo EXIT=$rc; exit 0` | 0 |
| Remove-fix proof | Reverse-apply saved source-only patch | 0 |
| Reverted build | `cmake --build build --target AutomatableModelTest zene > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-196.r1/logs/build-3.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc` | 0 |
| Reverted test | `ctest -R '^AutomatableModelTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-196.r1/logs/test-3.log 2>&1; rc=$?; echo EXIT=$rc; exit 0` | 8 |
| Reapply fix | Reapply saved source-only patch | 0 |
| Reapplied build | `cmake --build build --target AutomatableModelTest zene > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-196.r1/logs/build-4.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc` | 0 |
| Reapplied test | `ctest -R '^AutomatableModelTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-196.r1/logs/test-4.log 2>&1; rc=$?; echo EXIT=$rc; exit 0` | 0 |

The exact patch-proof commands were:

```sh
git diff -- src/core/LadspaControl.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-196.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-196.r1/logs/fix.patch > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-196.r1/logs/proof-1.log 2>&1; rc=$?; echo EXIT=$rc | tee -a /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-196.r1/logs/proof-1.log; exit $rc
git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-196.r1/logs/fix.patch > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-196.r1/logs/proof-2.log 2>&1; rc=$?; echo EXIT=$rc | tee -a /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-196.r1/logs/proof-2.log; exit $rc
```

Both patch-proof commands exited 0.

The RED and reverted runs fail at
`LadspaToggleValuesUsePositiveAsOn()` because the toggle remains on after
`setValue(-1.0f)`. The GREEN and reapplied runs pass.

## Gates

Both required gates passed (exit 0):

- `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-196.r1/logs/gate-1.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc`
- `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-196.r1/logs/gate-2.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc`

## Files committed

- `src/core/LadspaControl.cpp`
- `tests/src/core/AutomatableModelTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p10-196.md`
