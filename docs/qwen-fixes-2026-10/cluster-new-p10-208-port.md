# Cluster new-p10-208-port: `--tail-bars` action scope

**Verdict: FIXED**

The finding is real on this line. `--tail-bars` was parsed for every action, but
its value is consumed only by `exportStems()`. `zene render project.mmp
--tail-bars 4` therefore exited 0 and wrote a whole-project render without the
requested tail (measured below). The fix refuses the option unless
`exportstems` was selected, before a project is loaded or an output opened.

## Evidence

| Location | Evidence |
| --- | --- |
| `src/core/main.cpp:274-276` | Help already documents `--tail-bars` as `"exportstems" only`, but the parser did not enforce it. |
| `src/core/main.cpp:482` | `stemTailBarsSpecified` records that the option was supplied, so an explicit `--tail-bars 0` is refused too. |
| `src/core/main.cpp:851-853` | The flag is set only after the value passes the existing integer/non-negative validation. |
| `src/core/main.cpp:1186-1193` | Refusal (`--tail-bars is only valid for exportstems`) runs before `fileCheck()` and before any render/export starts. |
| `src/core/main.cpp:1446-1450` | The only consumer is the `renderStems` branch (`stemOptions.tailBars = stemTailBars`); no other action reads it. |
| `tests/mmpz-v2-container-refusal.py:202-247` | Regression: `render --tail-bars 4` and `render --tail-bars 0` must fail without creating output; the control proves `exportstems --tail-bars 4` still succeeds and writes stems. |

## Regression proof

The product change is only `src/core/main.cpp`; the test is the Python CTest
`MmpzV2CliRefusal`. All test runs were from `build/tests`.

| Run | Exact command | Exit | Result |
| --- | --- | ---: | --- |
| RED, original code | `ctest -R '^MmpzV2CliRefusal$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-208-port.r1/logs/test-1-red.log 2>&1` | 8 | 6 failed: `--tail-bars 4`/`0` rendered with exit 0, created output, and printed no refusal. |
| Build, fix applied | `cmake --build build --target zene > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-208-port.r1/logs/build-1-green.log 2>&1` | 0 | `zene` relinked. |
| GREEN, fix applied | `cd build/tests && ctest -R '^MmpzV2CliRefusal$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-208-port.r1/logs/test-3-green.log 2>&1` | 0 | `100% tests passed out of 1`. |

Patch-removal proof (step 4b), with the product patch saved and reversed:

| Step | Exact command | Exit |
| --- | --- | ---: |
| Save and reverse product patch | `git diff -- src/core/main.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-208-port.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-208-port.r1/logs/fix.patch` | 0 |
| Build without the fix | `cmake --build build --target zene > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-208-port.r1/logs/build-2-nofix.log 2>&1` | 0 |
| RED without the fix | `cd build/tests && ctest -R '^MmpzV2CliRefusal$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-208-port.r1/logs/test-4-nofix.log 2>&1` | 8 |
| Restore the product patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-208-port.r1/logs/fix.patch` | 0 |
| Rebuild with the fix | `cmake --build build --target zene > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-208-port.r1/logs/build-3-refix.log 2>&1` | 0 |
| GREEN with the fix | `cd build/tests && ctest -R '^MmpzV2CliRefusal$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-208-port.r1/logs/test-5-refix.log 2>&1` | 0 |

The affected CTest family is `MmpzV2CliRefusal`; the run executed 1/1 test
(`100% tests passed out of 1`). Zero tests did not run. The test's own counter
reports 25 cases on the no-fix run (`25 case(s), 4 failed`); with the fix the
CTest exits 0 and every case passes.

## Gates

| Gate | Exact command | Exit | Result |
| --- | --- | ---: | --- |
| Feature check | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-208-port > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-208-port.r1/logs/feature-check-1.log 2>&1` | 0 | `RESULT: PASS`; complexity and file-length both `no new item in your files`. |
| Complexity (raw) | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-208-port.r1/logs/gate-complexity.log 2>&1` | 1 | Pre-existing over-target functions; neither `src/core/main.cpp` nor the test file appears in the report. |
| File length (raw) | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-208-port.r1/logs/gate-file-length.log 2>&1` | 1 | Pre-existing unbaselined files; neither changed file appears in the report. |

No gate limit or baseline was changed.

## Files in this commit

- `src/core/main.cpp` (fix)
- `tests/mmpz-v2-container-refusal.py` (regression)
- `tests/upstream-modifications.txt` (register the inherited file change)
- `tests/fork-sources.txt` (register this report)
- `docs/qwen-fixes-2026-10/cluster-new-p10-208-port.md`

## Self-check

- **Regression RED/GREEN:** RED exit 8 (`test-1-red.log`, and again with the
  product patch reversed at `test-4-nofix.log`, exit 8), GREEN exit 0
  (`test-5-refix.log`). The failing assertions were
  `render refuses the exportstems-only --tail-bars 4` (`exit 0`) and
  `a render refused for --tail-bars 4 creates no output` (`output exists=True`),
  plus the explicit-zero pair.
- **Deterministic:** no sleeps or wall-clock checks. The test runs the real
  `zene` binary synchronously via `subprocess.run` with a fixed fixture, a
  temporary scratch HOME/XDG, and injected option values (`4`, `0`). The
  control runs `exportstems` on the same fixture and checks the resulting stem
  directory; no race or timing value is observed.
- **Cause and complete:** `stemTailBars` is read only by the `renderStems`
  branch (`src/core/main.cpp:1446-1450`), so every other action (`render`,
  `rendertracks`, `master`, GUI/no action) now refuses the option; the
  `exportstems` path keeps the value. Existing parsing (missing value, invalid
  or negative length) is unchanged, and the presence flag distinguishes an
  explicit zero from the default without changing any default. The control case
  proves the option was scoped, not removed.
- **No collateral damage:** with the option absent, `stemTailBarsSpecified`
  stays false and every action follows the previous code path; `exportstems`
  without the option still uses its default of 1. No project format, save/load
  path, audio-thread path or command surface was touched.
- **Not ALREADY-FIXED/FALSE-POSITIVE:** the measured RED run on the original
  code exited 0 and wrote `render-with-tail-bars-4.wav`; the verdict is FIXED,
  with the location evidence above.
