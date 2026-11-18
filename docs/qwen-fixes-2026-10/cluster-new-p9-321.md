# Cluster new-p9-321

**Verdict: FIXED**

`NesObject` now refreshes its three channel wavelengths when a held note's coarse-detune multiplier changes. The voice caches the multipliers in [Nes.h](../../plugins/Nes/Nes.h:182), initializes them in [Nes.cpp](../../plugins/Nes/Nes.cpp:96), and checks/updates them with the note frequency in [Nes.cpp](../../plugins/Nes/Nes.cpp:456).

The regression in [PluginPortsMigrationTest.cpp](../../tests/src/plugins/PluginPortsMigrationTest.cpp:731) renders a held NES note with vibrato disabled, changes channel 1 coarse detune from 0 to +12, and measures the output oscillator period. The original code measured 100 samples both before and after; the fixed code measures 100 then 50.

## Verification

| Evidence | Exact command | Exit |
| --- | --- | --- |
| RED on original code (`test-5.log`) | `ctest -R '^PluginPortsMigrationTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-321.r1/logs/test-5.log 2>&1; echo EXIT=$?` (cwd: `build/tests`) | 8 |
| Initial GREEN after applying the fix (`test-6.log`) | `ctest -R '^PluginPortsMigrationTest$' --output-on-failure -V > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-321.r1/logs/test-6.log 2>&1; echo EXIT=$?` (cwd: `build/tests`) | 0 |
| RED after removing the production patch (`test-7.log`) | `ctest -R '^PluginPortsMigrationTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-321.r1/logs/test-7.log 2>&1; echo EXIT=$?` (cwd: `build/tests`) | 8 |
| GREEN after restoring the patch (`test-8.log`) | `ctest -R '^PluginPortsMigrationTest$' --output-on-failure -V > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-321.r1/logs/test-8.log 2>&1; echo EXIT=$?` (cwd: `build/tests`) | 0 |
| Rebuild `zene` (`build-10.log`) | `cmake --build build --target zene > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-321.r1/logs/build-10.log 2>&1; echo EXIT=$?` | 0 |
| Rebuild affected test and NES plugin (`build-11.log`) | `cmake --build build --target PluginPortsMigrationTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-321.r1/logs/build-11.log 2>&1; echo EXIT=$?` | 0 |
| Complexity gate (`gate-5.log`) | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-321.r1/logs/gate-5.log 2>&1; echo EXIT=$?` | 0 |
| File-length gate (`gate-6.log`) | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-321.r1/logs/gate-6.log 2>&1; echo EXIT=$?` | 0 |

The affected family ran 1/1 CTest target; its QtTest totals were 9 passed, 0 failed, 0 skipped. The final GREEN output reports oscillator periods 100 before and 50 after detuning. The RED after patch removal reports 100 before and 100 after.

For the removal proof, `git diff -- plugins/Nes/Nes.h plugins/Nes/Nes.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-321.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-321.r1/logs/fix.patch; echo EXIT=$?` returned 0. After rebuilding, `test-7.log` failed as above. `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-321.r1/logs/fix.patch; echo EXIT=$?` returned 0; rebuilding and `test-8.log` then passed.

Earlier discarded regression iterations are retained in `test-1.log` through `test-4.log`: the first sample-difference approach passed on the bug because the independent voices did not begin in phase, and the first period estimator's search range produced a false failure. The final period estimator searches for the unchanged original period as well as the retuned period; its RED result was captured before the production change in `test-5.log`.

Their exact commands and exits were:

- `ctest -R '^PluginPortsMigrationTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-321.r1/logs/test-1.log 2>&1; echo EXIT=$?` — 0.
- `ctest -R '^PluginPortsMigrationTest$' --output-on-failure -V > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-321.r1/logs/test-2.log 2>&1; echo EXIT=$?` — 0.
- `ctest -R '^PluginPortsMigrationTest$' --output-on-failure -V > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-321.r1/logs/test-3.log 2>&1; echo EXIT=$?` — 0.
- `ctest -R '^PluginPortsMigrationTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-321.r1/logs/test-4.log 2>&1; echo EXIT=$?` — 8.

The inherited production files are registered in `tests/upstream-modifications.txt`. Files committed for this cluster:

The session did not expose an `fff` grep tool, so build/test log searches used `rg` with the requested error/failure terms.

- `plugins/Nes/Nes.cpp`
- `plugins/Nes/Nes.h`
- `tests/src/plugins/PluginPortsMigrationTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p9-321.md`
