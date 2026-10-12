# Cluster new-p10-375: fixed

**Verdict: FIXED.** The finding is real. Right-click deletion and dragging both accepted the first or last slice index when interior points existed. The handlers now allow edits only for interior indices; the endpoints remain at 0 and 1.

## Evidence

- [SlicerTWaveform.cpp](../../plugins/SlicerT/SlicerTWaveform.cpp):380 checks that a deletion index is interior; :443 checks the same for dragging.
- [SlicerTWaveformTest.cpp](../../tests/src/plugins/SlicerTWaveformTest.cpp):94, :110, :128 and :142 cover deleting and moving each endpoint. :156 verifies an interior slice remains movable.
- The original-code RED run reported two endpoint deletions leaving 2 points instead of 3, and endpoint drags changing 0 to 0.25 and 1 to 0.75.
- The required inverse-patch RED run reproduced those four failures while `interiorSliceRemainsMovable()` passed.
- Final GREEN ctest result: 1/1 registered test ran and passed; the test contains five behavior slots.

All build/test logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-375.r1/logs`.

| Evidence | Exact command | EXIT | Log |
| --- | --- | ---: | --- |
| Original-code RED | `ctest -R '^SlicerTWaveformTest$' --output-on-failure` (from `build/tests`) | 8 | `test-1.log` |
| Reverse-patch RED | `ctest -R '^SlicerTWaveformTest$' --output-on-failure` (from `build/tests`) | 8 | `test-6.log` |
| Restored-patch GREEN | `ctest -R '^SlicerTWaveformTest$' --output-on-failure` (from `build/tests`) | 0 | `test-7.log` |
| Complexity gate | `bash tests/complexity-gate.sh --check` | 0 | `gate-5.log` |
| File length gate | `bash tests/file-length-gate.sh --check` | 0 | `gate-6.log` |

The inverse-patch proof commands both exited 0:

```sh
git diff -- plugins/SlicerT/SlicerTWaveform.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-375.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-375.r1/logs/fix.patch
git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-375.r1/logs/fix.patch
```

The final restored-patch build commands each exited 0:

```sh
cmake --build build --target zene -j2
cmake --build build/plugins/SlicerT --target slicert -j2
cmake --build build/tests --target SlicerTWaveformTest -j2
```

The same three commands exited 0 before the inverse-patch RED run. Their final logs are `build-18.log`, `build-19.log` and `build-20.log`; inverse-proof logs are `build-15.log`, `build-16.log` and `build-17.log`.

Initial build setup retries were also recorded: `cmake --build build --target zene SlicerTWaveformTest -j2` exited 2 because the test target is exposed from `build/tests`; the first `cmake --build build/tests --target SlicerTWaveformTest -j2` exited 2 because the plugin's generated `plugin_export.h` include directory was not yet on that target. After adding the include directory, that test-target build exited 0 (`build-4.log`).

## Files committed

- `plugins/SlicerT/SlicerTWaveform.cpp`
- `tests/CMakeLists.txt`
- `tests/src/plugins/SlicerTWaveformTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p10-375.md`
