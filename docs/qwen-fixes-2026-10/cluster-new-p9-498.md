# Cluster new-p9-498

Verdict: **FIXED**

## Evidence

`plugins/Vectorscope/VectorView.cpp:232` now keeps `ColorChooser` on the stack in `mouseDoubleClickEvent`. When `exec()` returns, the chooser is destroyed and removed from the view's QObject children. The regression at `tests/src/gui/VcaStripLifetimeTest.cpp:132` opens and rejects three consecutive modal dialogs and checks after each close that the view has no retained `ColorChooser` children.

The suggested `tests/reference/Vectorscope/VectorView.cpp` is a comparison-plugin source, and this configured tree has no `VectorView` CTest target. The regression therefore uses the existing `VcaStripLifetimeTest` target and compiles the production `VectorView.cpp` implementation into that GUI test translation unit; no reconfiguration was needed.

## Verification

The same test source was rebuilt with the production fix removed and restored. With the fix removed, the assertion that `findChildren<ColorChooser*>()` is empty failed. Restoring the fix made the test pass.

| Stage | Exact command | Exit | Evidence |
| --- | --- | ---: | --- |
| Save production patch | `git diff -- plugins/Vectorscope/VectorView.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-498.r1/logs/fix.patch` | 0 | `fix.patch` |
| Remove fix | `git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-498.r1/logs/fix.patch` | 0 | Fix absent for RED run |
| Rebuild for RED | `cmake --build build --target zene vectorscope VcaStripLifetimeTest` | 0 | `build-13.log` |
| RED regression | `ctest -R '^VcaStripLifetimeTest$' --output-on-failure` (run from `build/tests`) | 8 | `test-10.log`: retained-child assertion failed |
| Restore fix | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-498.r1/logs/fix.patch` | 0 | Fix restored |
| Rebuild for GREEN | `cmake --build build --target zene vectorscope VcaStripLifetimeTest` | 0 | `build-14.log` |
| GREEN regression | `ctest -R '^VcaStripLifetimeTest$' --output-on-failure` (run from `build/tests`) | 0 | `test-11.log`: 1/1 CTest target passed |
| Complexity gate | `bash tests/complexity-gate.sh --check` | 0 | `gate-5.log` |
| File-length gate | `bash tests/file-length-gate.sh --check` | 0 | `gate-6.log` |

Affected family count: **1 CTest target** (`VcaStripLifetimeTest`); it ran and passed after the fix.

## Files committed

- `plugins/Vectorscope/VectorView.cpp`
- `tests/src/gui/VcaStripLifetimeTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p9-498.md`
