# Cluster new-p10-137

Verdict: **FIXED**

## Finding and fix

At `src/core/DrumSynth.cpp:255` and `:269`, the integer and float getters now use their supplied default when `sscanf` does not parse one value. Before this change, malformed nonempty values left each result at its zero initialization.

The regression at `tests/src/core/ImportDetectionTest.cpp:314` renders temporary DrumSynth files and checks that malformed `Tone.Level=oops` and `Tone.F1=oops` produce the same samples as the documented defaults (`128` and `200`). The initial run failed on the Level comparison. The reverse-patch run failed on the same assertion; restoring the production patch passed.

## Evidence

| Check | Command | Result |
| --- | --- | --- |
| Original behavior, regression RED | `ctest -R ImportDetectionTest --output-on-failure` from `build/tests` | EXIT=8; 1/1 selected test failed at `malformedDrumSynthNumbersUseDefaults`, `invalidLevel == validLevel` |
| Fix applied, GREEN | `ctest -R ImportDetectionTest --output-on-failure` from `build/tests` | EXIT=0; 1/1 passed |
| Reverse-patch proof, fix removed | `ctest -R ImportDetectionTest --output-on-failure` from `build/tests` | EXIT=8; same assertion failed |
| Reverse-patch proof, fix restored | `ctest -R ImportDetectionTest --output-on-failure` from `build/tests` | EXIT=0; 1/1 passed |
| Final affected-family run | `ctest -R ImportDetectionTest --output-on-failure` from `build/tests` | EXIT=0; 1/1 passed |
| Complexity gate | `bash tests/complexity-gate.sh --check` | EXIT=0 |
| File-length gate | `bash tests/file-length-gate.sh --check` | EXIT=0 |

The final test build command was `cmake --build build --target zene ImportDetectionTest`; captured EXIT=0. Build and test logs are under `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-137.r1/logs` (`build-5.log`, `test-5.log`, `gate-3.log`, `gate-4.log`). The RED/GREEN reverse-patch records are `test-3.log` and `test-4.log`; each test log includes the captured ctest exit code.

`fff` grep was unavailable in this session (`type fff` returned not found), so each log was searched with `rg` using separate `error`, `FAILED`, and `Assertion` terms, plus the test summary and captured exit line.

## Exact command record

Each build, ctest, and gate command below wrote its output to a new numbered log. `rc` is the captured command exit code appended to that log; the wrapper shell itself exits 0 after recording it.

Original-code build (captured EXIT=0):

```sh
cmake --build build --target ImportDetectionTest > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-137.r1/logs/build-1.log' 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-137.r1/logs/build-1.log'; exit 0
```

Original-code RED run (captured ctest EXIT=8):

```sh
ctest -R ImportDetectionTest --output-on-failure > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-137.r1/logs/test-1.log' 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-137.r1/logs/test-1.log'; exit 0
```

First fixed build and GREEN run (captured exits 0 and 0):

```sh
cmake --build build --target zene ImportDetectionTest > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-137.r1/logs/build-2.log' 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-137.r1/logs/build-2.log'; exit 0
ctest -R ImportDetectionTest --output-on-failure > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-137.r1/logs/test-2.log' 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-137.r1/logs/test-2.log'; exit 0
```

Reverse-patch save and removal (both EXIT=0):

```sh
git diff -- src/core/DrumSynth.cpp > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-137.r1/logs/fix.patch'; rc=$?; printf 'EXIT=%s\n' "$rc"; exit 0
git apply -R '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-137.r1/logs/fix.patch'; rc=$?; printf 'EXIT=%s\n' "$rc"; exit 0
```

Build with the fix removed and reverse-proof RED run (captured exits 0 and 8):

```sh
cmake --build build --target zene ImportDetectionTest > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-137.r1/logs/build-3.log' 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-137.r1/logs/build-3.log'; exit 0
ctest -R ImportDetectionTest --output-on-failure > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-137.r1/logs/test-3.log' 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-137.r1/logs/test-3.log'; exit 0
```

Patch restoration (EXIT=0):

```sh
git apply '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-137.r1/logs/fix.patch'; rc=$?; printf 'EXIT=%s\n' "$rc"; exit 0
```

Build and GREEN run after restoration (captured exits 0 and 0):

```sh
cmake --build build --target zene ImportDetectionTest > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-137.r1/logs/build-4.log' 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-137.r1/logs/build-4.log'; exit 0
ctest -R ImportDetectionTest --output-on-failure > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-137.r1/logs/test-4.log' 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-137.r1/logs/test-4.log'; exit 0
```

Final build and test after the last source edit (captured exits 0 and 0):

```sh
cmake --build build --target zene ImportDetectionTest > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-137.r1/logs/build-5.log' 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-137.r1/logs/build-5.log'; exit 0
ctest -R ImportDetectionTest --output-on-failure > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-137.r1/logs/test-5.log' 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-137.r1/logs/test-5.log'; exit 0
```

Final gates (captured exits 0 and 0):

```sh
bash tests/complexity-gate.sh --check > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-137.r1/logs/gate-3.log' 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-137.r1/logs/gate-3.log'; exit 0
bash tests/file-length-gate.sh --check > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-137.r1/logs/gate-4.log' 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-137.r1/logs/gate-4.log'; exit 0
```

## Files committed

- `src/core/DrumSynth.cpp`
- `tests/src/core/ImportDetectionTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p10-137.md`
