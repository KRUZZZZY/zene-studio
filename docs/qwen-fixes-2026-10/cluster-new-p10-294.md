# Cluster new-p10-294: PlayHandle audio bus pointer

**Verdict: FIXED**

`PlayHandle::audioBusHandle()` returned `m_audioBusHandle` ([include/PlayHandle.h:143-145](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-294/include/PlayHandle.h:143)), but the member had no initializer ([include/PlayHandle.h:165](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-294/include/PlayHandle.h:165)) and the base constructor did not assign it ([src/core/PlayHandle.cpp:37-43](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-294/src/core/PlayHandle.cpp:37)). The member now initializes to `nullptr`.

The regression ([tests/src/core/AudioBusHandleTest.cpp:134-145](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-294/tests/src/core/AudioBusHandleTest.cpp:134)) poisons handle storage before construction and verifies the public getter returns null when no bus is assigned. It failed on the original code and failed again when the production patch was reversed.

## Evidence

All ctest commands below ran from `build/tests`. Each selected one affected-family test: `1/1 AudioBusHandleTest`.

| Phase | Exact test command | Exit | Evidence |
|---|---|---:|---|
| Original code (RED) | `ctest -R '^AudioBusHandleTest$' --output-on-failure` | 8 | `audioBusHandleDefaultsToNull`: `hasNoBusHandle` returned FALSE. Log: `test-1-red.log`. |
| Fix applied (GREEN) | `ctest -R '^AudioBusHandleTest$' --output-on-failure` | 0 | `1/1` passed. Log: `test-2-green.log`. |
| Production patch reversed (RED) | `ctest -R '^AudioBusHandleTest$' --output-on-failure` | 8 | Same assertion failed; Qt reported 5 passed and 1 failed. Log: `test-3-reverse-red.log`. |
| Patch restored (GREEN) | `ctest -R '^AudioBusHandleTest$' --output-on-failure` | 0 | `1/1` passed. Log: `test-4-restored-green.log`. |

Build command for each phase: `cmake --build build --target zene AudioBusHandleTest` — exit 0 for the original test build (`build-1.log`), fixed build (`build-2.log`), reverse-patch build (`build-3-reverse.log`), and restored build (`build-4-restore.log`).

Patch proof commands:

- `git diff -- include/PlayHandle.h > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-294.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-294.r1/logs/fix.patch` — exit 0.
- `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-294.r1/logs/fix.patch` — exit 0.

Required ratchets:

- `bash tests/complexity-gate.sh --check` — exit 0 (`gate-1-complexity.log`).
- `bash tests/file-length-gate.sh --check` — exit 0 (`gate-2-file-length.log`).

## Files committed

- `include/PlayHandle.h`
- `tests/src/core/AudioBusHandleTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p10-294.md`
