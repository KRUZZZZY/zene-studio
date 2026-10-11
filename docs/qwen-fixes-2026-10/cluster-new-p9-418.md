# Cluster new-p9-418

**Verdict: FIXED**

The round-one review found unsafe test teardown: `hook` was declared after the two objects, so
the hook could be destroyed before their destructors. The regression now declares `TestHook hook`
before `first` and `second`. The owners are destroyed first, while the hook is still alive. The
reassignment and stale-owner assertions remain unchanged.

## Evidence

- [SerializingObject.cpp:93](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-418/src/core/SerializingObject.cpp:93) detaches the prior owner before moving the hook backlink.
- [SerializingObjectHookTest.cpp:62](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-418/tests/src/core/SerializingObjectHookTest.cpp:62) now declares the hook before both objects; lines 68–73 retain reassignment and ownership assertions.

The reverse check used only the `SerializingObject.cpp` production hunk from commit `657505532`.
With that hunk reversed, the rebuilt regression failed at `first.hook() == nullptr` (CTest EXIT=8).
The test reached the intended assertion and teardown kept the hook alive. The hunk was restored,
the affected tests passed, and the worktree source matches the committed fix.

| Evidence | Exact command | Result |
| --- | --- | --- |
| Focused GREEN, corrected test lifetime | `ctest -R '^SerializingObjectHookTest$' --output-on-failure` (cwd `build/tests`) | EXIT=0; 1/1 passed |
| Reverse production hunk | `git diff 657505532^ 657505532 -- src/core/SerializingObject.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-418.r2/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-418.r2/logs/fix.patch` | EXIT=0 |
| RED with only production hunk reversed | `ctest -R '^SerializingObjectHookTest$' --output-on-failure` (cwd `build/tests`) | EXIT=8; failed at `first.hook() == nullptr` |
| Restore production hunk | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-418.r2/logs/fix.patch && git diff --check` | EXIT=0 |
| Focused GREEN after restoration | `ctest -R '^SerializingObjectHookTest$' --output-on-failure` (cwd `build/tests`) | EXIT=0; 1/1 passed |
| Affected family after restoration | `ctest -R 'SerializingObjectHookTest|ClipSerialisationTest' --output-on-failure` (cwd `build/tests`) | EXIT=0; 2/2 passed |
| Complexity gate | `bash tests/complexity-gate.sh --check` | EXIT=0 |
| File-length gate | `bash tests/file-length-gate.sh --check` | EXIT=0 |

The affected-family count is two tests. Build and test output is in
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-418.r2/logs`:

| Command | Exit | Log |
| --- | ---: | --- |
| `cmake --build build --target zene SerializingObjectHookTest ClipSerialisationTest -j2` (corrected test) | 0 | `build-1.log` |
| Focused GREEN ctest | 0 | `test-1.log` |
| Same build command (production hunk reversed) | 0 | `build-2.log` |
| RED ctest (production hunk reversed) | 8 | `test-2.log` |
| Same build command (production hunk restored) | 0 | `build-3.log` |
| Focused GREEN ctest after restoration | 0 | `test-3.log` |
| Affected-family ctest after restoration | 0; 2 tests passed | `test-4.log` |
| `bash tests/complexity-gate.sh --check` | 0 | `gate-3.log` |
| `bash tests/file-length-gate.sh --check` | 0 | `gate-4.log` |

## Files in this corrective commit

- `tests/src/core/SerializingObjectHookTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p9-418.md`
