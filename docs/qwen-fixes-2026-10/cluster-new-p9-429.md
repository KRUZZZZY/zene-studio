# Cluster new-p9-429 — FIXED

## Finding and change

The finding is real. `SidInstrument::playNoteImpl` is the per-note render callback
at `plugins/Sid/SidInstrument.cpp:283`. Its former sample-count mismatch branch
called `printf`, synchronously writing to stdout on the audio thread. The
callback now calls `sid_fillbuffer` and continues rendering without logging
(`plugins/Sid/SidInstrument.cpp:407`); the unused `<cstdio>` include and count
variable were removed with the diagnostic.

The current tree has no `SidInstrument` ctest: `ctest -N -R '^SidInstrument$'`
reported `Total Tests: 0`. `tests/reference/Sid/SidInstrument.cpp` is a frozen
reference source, so the regression uses the existing `RtSafetySweep` and
`RtSafetySelfTest` tests. The sweep now checks synchronous stdio output, and its
scope declares `SidInstrument::playNoteImpl` with the `stdio-output` rule only
(`tests/rt-safety-scope.txt:72`). That filter keeps the regression focused on
this finding despite the callback's unrelated existing allocation. The
self-test has a printf positive control with that unrelated allocation in the
fixture (`tests/rt_safety_selftest.py:250`).

## Evidence

| Step | Exact command | Result |
|---|---|---|
| Check requested test registration (run from `build/tests`) | `ctest -N -R '^SidInstrument$' > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-429.r1/logs/test-1.log' 2>&1; rc=$?; echo EXIT=$rc` | `EXIT=0`; `Total Tests: 0` |
| Save the product-only patch | `git diff -- plugins/Sid/SidInstrument.cpp > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-429.r1/logs/fix.patch'; rc=$?; echo EXIT=$rc` | `EXIT=0` |
| Restore pre-fix product code for the RED proof | `git apply -R '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-429.r1/logs/fix.patch'; rc=$?; echo EXIT=$rc` | `EXIT=0` |
| Rebuild before RED (from worktree root) | `cmake --build build --target zene sid > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-429.r1/logs/build-7.log' 2>&1; rc=$?; echo EXIT=$rc` | `EXIT=0` |
| **RED**, run from `build/tests` | `ctest -R '^RtSafetySweep$' --output-on-failure > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-429.r1/logs/test-8.log' 2>&1; rc=$?; echo EXIT=$rc` | `EXIT=8`; log names the `stdio-output` hit in `SidInstrument::playNoteImpl` |
| Reapply the product fix | `git apply '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-429.r1/logs/fix.patch'; rc=$?; echo EXIT=$rc` | `EXIT=0` |
| Rebuild before GREEN (from worktree root) | `cmake --build build --target zene sid > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-429.r1/logs/build-8.log' 2>&1; rc=$?; echo EXIT=$rc` | `EXIT=0` |
| **GREEN**, run from `build/tests` | `ctest -R '^RtSafety(Sweep|SelfTest)$' --output-on-failure > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-429.r1/logs/test-9.log' 2>&1; rc=$?; echo EXIT=$rc` | `EXIT=0`; affected family ran **2 tests**, 2/2 passed |
| Complexity gate | `bash tests/complexity-gate.sh --check > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-429.r1/logs/gate-7.log' 2>&1; rc=$?; echo EXIT=$rc` | `EXIT=0` |
| File-length gate | `bash tests/file-length-gate.sh --check > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-429.r1/logs/gate-8.log' 2>&1; rc=$?; echo EXIT=$rc` | `EXIT=0` |

An earlier broad scope attempt found the callback's existing `new reSID::SID()`
allocation after the logging fix. The scope was narrowed to the stdio rule; no
allowlist entry was added. The first complexity check also flagged the updated
scope parser at CCN 16. Parsing was split into helpers, and the final complexity
gate passed. These exploratory outputs remain in `test-3.log` and `gate-1.log`.

The `fff` grep tool was not available in this session (`command -v fff` returned
no path). The log folder was searched with `rg -n -i error`, `rg -n FAILED`, and
`rg -n Assertion`; the expected RED logs contain CTest failure summaries, and
the final build and gate logs contain no matching errors or assertions.

## Files in the commit

- `plugins/Sid/SidInstrument.cpp`
- `tests/rt-safety-scope.txt`
- `tests/rt-safety-sweep.py`
- `tests/rt_safety_lib.py`
- `tests/rt_safety_selftest.py`
- `tests/rt_safety_source.py`
- `tests/upstream-modifications.txt`
- `docs/RT-SAFETY-SWEEP.md`
- `docs/qwen-fixes-2026-10/cluster-new-p9-429.md`
