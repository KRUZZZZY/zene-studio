# Cluster new-p10-1

**Verdict: FALSE-POSITIVE**

## Finding evidence

The narrowing in `plugins/ClapInstrument/ClapInstrument.cpp:100-104` receives a
`clap::PortLayout`, whose plugin-reported counts are produced by
`HostedPlugin::Impl::scanAudioPorts()` in `plugins/ClapEffect/ClapHost.cpp:107-129`.
That writer converts the CLAP port count to `int` and passes the descriptors to
`mapPorts()`. In `plugins/ClapEffect/ClapBusMap.cpp:34-60`, `mapPorts()` limits
each direction to `MaxChannelsPerAudioBuffer` before storing the totals in the
layout. `include/lmms_constants.h:39` sets that limit to 128.

For the reported 65,536-channel output, the CLAP `uint32_t` count is representable
as `int`; `mapPorts()` caps the layout output count at 128. The cast in
`channelCountsOf()` therefore yields 128, not zero. The same cap applies to
inputs. The host's documented behavior is to drop channels above its 128-channel
budget, so rejecting this layout is not needed to prevent the reported wrap.

The existing `ClapBusMapTest` family was built and run. Its channel-budget case
checks that large port counts are clamped, and all nine QtTest slots passed.
No production change or regression test was appropriate for this unreachable
state. Consequently, RED/GREEN and inverse-patch proof runs are not applicable.

## Evidence table

| Check | Exact command (run from) | Result |
|---|---|---|
| RED regression | N/A — finding is unreachable through the production layout path | N/A |
| GREEN regression | N/A — no fix or regression test was added | N/A |
| Affected-family build | Worktree root: `cmake --build build --target ClapBusMapTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-1.r1/logs/build-1.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"` | `EXIT=0` |
| Affected-family test | `build/tests`: `ctest -R ClapBusMapTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-1.r1/logs/test-1.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"` | `EXIT=0`; 1/1 registered CTest target passed (9 QtTest slots) |
| Complexity gate | Worktree root: `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-1.r1/logs/gate-3.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"` | `EXIT=0` |
| File-length gate | Worktree root: `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-1.r1/logs/gate-4.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"` | `EXIT=0` |

## Files committed

- `docs/qwen-fixes-2026-10/cluster-new-p10-1.md` (this report only)
