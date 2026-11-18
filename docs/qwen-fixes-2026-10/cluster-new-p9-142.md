# Cluster new-p9-142

Verdict: **FIXED**

`positiveFraction` was in `plugins/Xpressive/ExprSynth.h:124`, while the reported
`plugins/Xpressive/ExprSynth.cpp:124` was a historical call-site reference. The
helper's `static_cast<int>` operations could receive finite values outside the
`int` range. `plugins/Xpressive/ExprSynth.cpp:414` calls the helper from the
`sinew` implementation, registered at line 541.

Regression assertions are in `tests/src/plugins/PluginPortsMigrationTest.cpp`
at lines 225, 233, and 241; inherited-file tracking is at
`tests/upstream-modifications.txt:658`.

The helper now uses `std::fmod` for the fractional part, wraps negative values
into `[0, 1)`, keeps zero for non-finite inputs, and avoids returning `1.0f` when
adding one to a very small negative fraction rounds up. The test adds large
positive and negative finite inputs and checks ordinary positive, negative, and
integer inputs for behavior preservation.

The reference files under `tests/reference/Xpressive/` are frozen plugin source
copies in this checkout. Regression coverage was added to the registered
`PluginPortsMigrationTest`, which builds and exercises the Xpressive module.

## Evidence

| Check | Exact command | Exit | Result |
|---|---|---:|---|
| Initial RED, original implementation | `cd build/tests && ctest -R '^PluginPortsMigrationTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-142.r1/logs/test-1.log 2>&1; rc=$?; echo EXIT=$rc` | 8 | `1/1` ctest target; positive case failed `result < 1.0f`, negative case failed `result >= 0.0f`. |
| Initial GREEN, fixed implementation | `cd build/tests && ctest -R '^PluginPortsMigrationTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-142.r1/logs/test-2.log 2>&1; rc=$?; echo EXIT=$rc` | 0 | `1/1` ctest target passed. |
| RED without production patch | `cd build/tests && ctest -R '^PluginPortsMigrationTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-142.r1/logs/test-3.log 2>&1; rc=$?; echo EXIT=$rc` | 8 | Repeated the same two failing assertions after reverse-applying only the production patch. |
| GREEN after restoring production patch | `cd build/tests && ctest -R '^PluginPortsMigrationTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-142.r1/logs/test-4.log 2>&1; rc=$?; echo EXIT=$rc` | 0 | `1/1` ctest target passed after reapplying the identical patch. |

Affected-family count: one CTest target, `PluginPortsMigrationTest`, executed
(`1/1`) on each run. The target includes three new QtTest cases for large
positive, large negative, and ordinary values.

### Build and proof commands

Each build command targeted `zene`, `xpressive`, and `PluginPortsMigrationTest`
before the corresponding test run. All four builds exited 0:

```text
cmake --build build --target zene xpressive PluginPortsMigrationTest -- -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-142.r1/logs/build-1.log 2>&1; rc=$?; echo EXIT=$rc  # EXIT=0
cmake --build build --target zene xpressive PluginPortsMigrationTest -- -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-142.r1/logs/build-2.log 2>&1; rc=$?; echo EXIT=$rc  # EXIT=0
cmake --build build --target zene xpressive PluginPortsMigrationTest -- -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-142.r1/logs/build-3.log 2>&1; rc=$?; echo EXIT=$rc  # EXIT=0
cmake --build build --target zene xpressive PluginPortsMigrationTest -- -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-142.r1/logs/build-4.log 2>&1; rc=$?; echo EXIT=$rc  # EXIT=0
```

The proof patch was captured and reversed, then restored:

```text
git diff -- plugins/Xpressive/ExprSynth.h > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-142.r1/logs/fix.patch  # EXIT=0
git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-142.r1/logs/fix.patch  # EXIT=0
git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-142.r1/logs/fix.patch  # EXIT=0
```

### Required gates

| Gate | Exact command | Exit |
|---|---|---:|
| Complexity | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-142.r1/logs/gate-1.log 2>&1; rc=$?; echo EXIT=$rc` | 0 |
| File length | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-142.r1/logs/gate-2.log 2>&1; rc=$?; echo EXIT=$rc` | 0 |

## Files committed

- `plugins/Xpressive/ExprSynth.h`
- `tests/src/plugins/PluginPortsMigrationTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p9-142.md`
