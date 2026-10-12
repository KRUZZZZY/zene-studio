# Cluster new-p12-14

**Verdict: NEEDS-CONTEXT**

The reported stale-loop-frame behavior is not what the current replacement path does. `clip.process` calls `SampleClip::replaceSample` at `src/core/ControlCommandsClipProcess.cpp:136`; `SampleClip::replaceSample` assigns a new `Sample` at `src/core/SampleClip.cpp:174`. The `Sample` buffer constructor initializes loop start/end to `0` and the new buffer size at `src/core/Sample.cpp:40-45`. `installWindow` only updates the sample start/end frames at `src/core/SampleClip.cpp:483-491`.

The loop-point API exists (`include/Sample.h:94-104`), and `Sample::render` reads those points only for `Loop::On` and `Loop::PingPong` (`src/core/Sample.cpp:163-178`). The sample-clip play handle passes `Sample::Loop::Off` (`src/core/SamplePlayHandle.cpp:419`). A repository search found no production call site that sets custom loop frames. Therefore, there is no current sample-clip playback path that can reproduce the finding’s claimed shifted loop. Whether `clip.process` should preserve and rescale loop metadata supplied through the public `Sample` API is an unclear contract; changing it without that context would invent behavior.

## Verification evidence

| Item | Command/result |
|---|---|
| RED regression | Not run; NEEDS-CONTEXT because the current sample-clip path does not use loop points. Exit: N/A. |
| GREEN regression | Not run for the same reason. Exit: N/A. |
| Affected-family test count | N/A; no test was run. |
| Complexity gate | `if bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-14.r1/logs/gate-2.log 2>&1; then status=0; else status=$?; fi; printf 'EXIT=%s\n' "$status" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-14.r1/logs/gate-2.log; printf 'EXIT=%s\n' "$status"` — exit 0. |
| File-length gate | `if bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-14.r1/logs/gate-3.log 2>&1; then status=0; else status=$?; fi; printf 'EXIT=%s\n' "$status" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-14.r1/logs/gate-3.log; printf 'EXIT=%s\n' "$status"` — exit 0. |
| Build / ctest | Not run; no production or test change was justified. |
| Log search | The `fff grep` tool is unavailable in this environment (`command -v fff` returned no path). `rg -n` searches of the log folder for `error`, `FAILED`, and `Assertion` returned no matches. |
| Files committed | `docs/qwen-fixes-2026-10/cluster-new-p12-14.md` only. |
