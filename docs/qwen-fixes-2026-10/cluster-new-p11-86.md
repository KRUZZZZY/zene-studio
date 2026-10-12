# Cluster new-p11-86: empty DAWproject mixer import

Verdict: **FIXED**.

When a DAWproject has no mixer channels, `applyDawProjectModel` now resets the
implicit master to its default name, unity volume, unmuted state, and unsoloed
state. The reset helper is at `src/core/DawProjectSession.cpp:206` and is called
from the import flow at line 515. The regression at
`tests/src/core/DawProjectInterchangeRoundTripTest.cpp:489` writes and reads a
project with zero mixer channels, imports it through `dawproject.import`, and
checks all four master values.

## Evidence

The affected CTest family is `DawProjectInterchangeRoundTripTest`. Each run
executed 1/1 CTest targets. The corrected RED run failed on the previous master
name; the GREEN run passed. Reversing only the product patch reproduced that
failure, and reapplying it restored the pass.

All build commands below ran from the worktree root and exited 0:

1. `cmake --build build --target zene DawProjectInterchangeRoundTripTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-86.r1/logs/build-1.log 2>&1; echo EXIT=$?`
2. `cmake --build build --target zene DawProjectInterchangeRoundTripTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-86.r1/logs/build-2.log 2>&1; echo EXIT=$?`
3. `cmake --build build --target zene DawProjectInterchangeRoundTripTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-86.r1/logs/build-3.log 2>&1; echo EXIT=$?`
4. `cmake --build build --target zene DawProjectInterchangeRoundTripTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-86.r1/logs/build-4.log 2>&1; echo EXIT=$?`
5. `cmake --build build --target zene DawProjectInterchangeRoundTripTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-86.r1/logs/build-5.log 2>&1; echo EXIT=$?`
6. `cmake --build build --target zene DawProjectInterchangeRoundTripTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-86.r1/logs/build-6.log 2>&1; echo EXIT=$?`

CTest commands ran from `build/tests`:

| Run | Exact command | Exit | Result |
| --- | --- | ---: | --- |
| Initial fixture attempt | `ctest -R DawProjectInterchangeRoundTripTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-86.r1/logs/test-1.log 2>&1; echo EXIT=$?` | 8 | Fixture lacked required application name; not accepted as RED evidence. |
| Corrected metadata attempt | `ctest -R DawProjectInterchangeRoundTripTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-86.r1/logs/test-2.log 2>&1; echo EXIT=$?` | 8 | Incorrectly expected the post-import payload to report zero; it reports the implicit master. The test was corrected to assert the file reader's zero count. |
| RED on original code | `ctest -R DawProjectInterchangeRoundTripTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-86.r1/logs/test-3.log 2>&1; echo EXIT=$?` | 8 | 1/1 target failed: actual master name `Previous Master`, expected `Master`. |
| GREEN with fix | `ctest -R DawProjectInterchangeRoundTripTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-86.r1/logs/test-4.log 2>&1; echo EXIT=$?` | 0 | 1/1 target passed. |
| RED with product patch reversed | `ctest -R DawProjectInterchangeRoundTripTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-86.r1/logs/test-5.log 2>&1; echo EXIT=$?` | 8 | Same master-name assertion failed. |
| GREEN after patch reapplication | `ctest -R DawProjectInterchangeRoundTripTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-86.r1/logs/test-6.log 2>&1; echo EXIT=$?` | 0 | 1/1 target passed. |

Patch reversal proof commands, run from the worktree root, each exited 0:

- `git diff -- src/core/DawProjectSession.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-86.r1/logs/fix.patch; echo EXIT=$?`
- `git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-86.r1/logs/fix.patch; echo EXIT=$?`
- `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-86.r1/logs/fix.patch; echo EXIT=$?`

Both required gates ran from the worktree root and exited 0:

- `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-86.r1/logs/gate-complexity-1.log 2>&1; echo EXIT=$?`
- `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-86.r1/logs/gate-file-length-1.log 2>&1; echo EXIT=$?`

`git diff --check` exited 0. The commit contains:

- `src/core/DawProjectSession.cpp`
- `tests/src/core/DawProjectInterchangeRoundTripTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p11-86.md`
