# Cluster new-p10-126

**Verdict: FIXED.** `ExprFront::add_cyclic_vector()` now rejects length zero before allocating or registering either callback. This prevents the non-interpolating callback from indexing an empty vector and the interpolating callback from taking modulo zero.

## Evidence

| Location | Evidence |
| --- | --- |
| `plugins/Xpressive/ExprSynth.cpp:635-640` | Shared zero-length rejection before the interpolation branch. |
| `tests/src/plugins/AudioPluginTest.cpp:452-463` | Separate regression cases check rejection with and without interpolation. |
| `tests/exprtk.hpp:1-2` | Test-only forwarding include lets the existing host test compile the production ExprSynth implementation. |
| `tests/fork-sources.txt:17,1872` | Registers the test forwarding header and includes it in the manifest recipe. |
| `tests/upstream-modifications.txt:662` | Records the inherited production source change and reason. |

The affected CTest family is `AudioPluginTest`: **1 CTest test** per run. The two new QtTest slots both failed before the guard and both failed again after the reverse-patch check. Each failure reported that `add_cyclic_vector()` returned true for length zero.

| Evidence | Exact command or result |
| --- | --- |
| RED | `ctest -R '^AudioPluginTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-126.r1/logs/test-1.log 2>&1; echo EXIT=$?` — `EXIT=8` |
| GREEN | `ctest -R '^AudioPluginTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-126.r1/logs/test-10.log 2>&1; echo EXIT=$?` — `EXIT=0` |
| Affected-family count | `AudioPluginTest`: 1 CTest test; 2 new QtTest slots |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-126.r1/logs/gate-5.log 2>&1; echo EXIT=$?` — `EXIT=0` |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-126.r1/logs/gate-6.log 2>&1; echo EXIT=$?` — `EXIT=0` |
| Files committed | `plugins/Xpressive/ExprSynth.cpp`; `tests/src/plugins/AudioPluginTest.cpp`; `tests/exprtk.hpp`; `tests/fork-sources.txt`; `tests/upstream-modifications.txt`; `docs/qwen-fixes-2026-10/cluster-new-p10-126.md` |

## RED and GREEN commands

All CTest commands ran from `build/tests`. RED before the fix:

```sh
ctest -R '^AudioPluginTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-126.r1/logs/test-1.log 2>&1; echo EXIT=$?
```

`EXIT=8` (1 CTest test failed; both new assertions failed).

GREEN after the fix was restored and rebuilt:

```sh
ctest -R '^AudioPluginTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-126.r1/logs/test-10.log 2>&1; echo EXIT=$?
```

`EXIT=0` (1/1 CTest tests passed).

## Reverse-patch proof

The production-only patch was reversed, rebuilt, and tested; restoring it was followed by another build and test:

```sh
git diff -- plugins/Xpressive/ExprSynth.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-126.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-126.r1/logs/fix.patch; echo EXIT=$?
```

`EXIT=0`.

```sh
cmake --build build --target zene xpressive AudioPluginTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-126.r1/logs/build-11.log 2>&1; echo EXIT=$?
```

`EXIT=0`.

```sh
ctest -R '^AudioPluginTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-126.r1/logs/test-9.log 2>&1; echo EXIT=$?
```

`EXIT=8`; both new assertions failed again.

```sh
git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-126.r1/logs/fix.patch; echo EXIT=$?
```

`EXIT=0`.

```sh
cmake --build build --target zene xpressive AudioPluginTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-126.r1/logs/build-12.log 2>&1; echo EXIT=$?
```

`EXIT=0`.

The same final target build command exited 0 in `build-3.log`, `build-4.log`, `build-5.log`, and `build-7.log` through `build-12.log`. Two earlier harness setup attempts are also recorded: `cmake --build build --target zene AudioPluginTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-126.r1/logs/build-1.log 2>&1; echo EXIT=$?` exited 2 because the test target initially lacked the ExprTk include bridge; the same command to `build-2.log` exited 0 after adding it. `cmake --build build --target zene xpressive AudioPluginTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-126.r1/logs/build-6.log 2>&1; echo EXIT=$?` exited 2 while the temporary bridge was outside the target's include path; the final test bridge resolved that setup issue.

## Gates

```sh
bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-126.r1/logs/gate-5.log 2>&1; echo EXIT=$?
```

`EXIT=0`.

```sh
bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-126.r1/logs/gate-6.log 2>&1; echo EXIT=$?
```

`EXIT=0`. The fork-source registration gate and no-upstream-regression gate also exited 0 (`gate-7.log`, `gate-8.log`).

```sh
bash tests/fork-sources-gate.sh > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-126.r1/logs/gate-7.log 2>&1; echo EXIT=$?
```

`EXIT=0`.

```sh
bash tests/no-upstream-regression-gate.sh > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-126.r1/logs/gate-8.log 2>&1; echo EXIT=$?
```

`EXIT=0`.

The `fff` search tool was not exposed in this session; build and test logs were searched with `rg -n` for `error`, `FAILED`, and `Assertion`.

## Files committed

- `plugins/Xpressive/ExprSynth.cpp`
- `tests/src/plugins/AudioPluginTest.cpp`
- `tests/exprtk.hpp`
- `tests/fork-sources.txt`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p10-126.md`
