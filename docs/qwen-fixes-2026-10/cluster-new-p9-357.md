# Cluster new-p9-357

Verdict: **FIXED**

`src/gui/PinConnector.cpp:463-469` now ignores every mouse press except the left button before resolving a pin. `tests/src/gui/MixerModesTest.cpp:283-321` clicks both visible pin matrices with the right button and checks that the input and output routes remain unchanged.

| Evidence | Exact command and working directory | Exit | Result |
|---|---|---:|---|
| RED, original handler | `ctest -R '^MixerModesTest$' --output-on-failure` (`build/tests`) | 8 | `pinsUnchanged` failed in `test-4.log`; 0/1 CTest tests passed. |
| Reverse-patch proof | `ctest -R '^MixerModesTest$' --output-on-failure` (`build/tests`, with `PinConnector.cpp` fix reversed) | 8 | The same `pinsUnchanged` assertion failed in `test-6.log`. |
| GREEN, restored fix | `ctest -R '^MixerModesTest$' --output-on-failure` (`build/tests`) | 0 | 1/1 CTest tests passed in `test-7.log`. |
| Affected family | `MixerModesTest` | — | 1 registered CTest test; 12 behavior slots (11 existing and the new regression) pass with the fix, in addition to init and cleanup. |
| Build | `cmake --build build --target zene MixerModesTest` | 0 | Rebuilt before GREEN and again after restoring the fix; see `build-6.log` and `build-8.log`. |
| Complexity gate | `bash tests/complexity-gate.sh --check` | 0 | `gate-5.log`. |
| File length gate | `bash tests/file-length-gate.sh --check` | 0 | `gate-6.log`. |

The sensitivity check used `git diff -- src/gui/PinConnector.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-357.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-357.r1/logs/fix.patch` (exit 0), rebuilt `zene` and `MixerModesTest` (exit 0), and got the RED result above. The fix was restored with `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-357.r1/logs/fix.patch` (exit 0), followed by the successful rebuild and GREEN run.

Files in the commit:

- `src/gui/PinConnector.cpp`
- `tests/src/gui/MixerModesTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p9-357.md`
