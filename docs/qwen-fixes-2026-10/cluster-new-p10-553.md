# Cluster new-p10-553

**Verdict: FALSE-POSITIVE**

The report says a finite tail of `INT32_MAX` frames is being misreported as infinite. The CLAP API contract in `build/clap/include/clap/ext/tail.h:13` says that *any* value greater than or equal to `INT32_MAX` implies an infinite tail. `HostedPlugin::tail()` at `plugins/ClapEffect/ClapHost.cpp:140` maps exactly that range to `-1`, which is the host's infinite-tail value. A finite tail at `INT32_MAX` is therefore not representable under this CLAP contract, and saturating that value to a finite host value would contradict the plug-in API.

## Evidence and validation

| Item | Command or evidence | Result |
|---|---|---|
| Finding evidence | `plugins/ClapEffect/ClapHost.cpp:135-140`; `build/clap/include/clap/ext/tail.h:11-14` | The conversion matches the CLAP boundary contract. |
| RED regression | Not applicable: the finding is false-positive, so no regression test was added or run. | N/A (no command / exit code) |
| GREEN regression | Not applicable: no code or test change was made. | N/A (no command / exit code) |
| Affected-family test count | `ClapHostTest` was not invoked. | 0 tests executed; no ctest run was attempted. |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-553.r1/logs/ratchet-3.log 2>&1; gate_rc=$?; printf 'EXIT=%s\n' "$gate_rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-553.r1/logs/ratchet-3.log; exit "$gate_rc"` | Exit 0 |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-553.r1/logs/ratchet-4.log 2>&1; gate_rc=$?; printf 'EXIT=%s\n' "$gate_rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-553.r1/logs/ratchet-4.log; exit "$gate_rc"` | Exit 0 |

## Committed files

- `docs/qwen-fixes-2026-10/cluster-new-p10-553.md` (this report)
