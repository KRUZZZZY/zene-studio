FALSE-POSITIVE (the regression test is BLOCKED: the lane build tree cannot register a new test target, see Blockers)

# Cluster 1447 — "The multicast loopback probe always targets an unset address"

## Verdict

The finding says `configure()` calls `measureLoopback()` before it assigns `m_group`. On this branch
(`bugs/qwen-1447`, HEAD `37eaf07557`) the code does the reverse: `m_group` is assigned first, and the
probe runs after. The defect as described is not present.

## Evidence (`src/core/LinkUdpTransport.cpp`)

| Step | Line | Code |
| --- | --- | --- |
| Socket option `IP_MULTICAST_LOOP = 1` set on the sending socket | 298 | `(void)::setsockopt(fd, IPPROTO_IP, IP_MULTICAST_LOOP, &loop, sizeof(loop));` |
| Group address built | 304–307 | `group.sin_family`, `group.sin_addr.s_addr = ::inet_addr(link::DefaultGroup)`, `group.sin_port = htons(link::DefaultPort)` |
| Group stored in the member | 308 | `m_group = group;` |
| Probe run, after the store | 310 | `if (!measureLoopback(fd))` |
| Probe sends to the stored group | 262–263 | `sendto(fd, LoopbackProbePayload, ..., reinterpret_cast<const sockaddr*>(&m_group), sizeof(m_group));` |

The finding's own line numbers match these lines exactly, so the order it cites is the order it misread.

History (`git blame`): line 308 (`m_group = group;`) is from `c1fb542b11` (2026-09-13, the original link feature).
The probe call at line 310 was added after it in `ebe3e16dd8` (2026-09-14). The file has never had the probe
before the assignment on this branch.

## What I did not verify

- I did not run the probe on this host. The existing tests that would exercise it cannot be run in this lane
  (see Blockers). Whether multicast loopback works on this machine is unknown from this lane.

## Gap found while checking (not fixed here)

`tests/src/core/AbletonLinkSessionTest.cpp:102-110` and `tests/src/core/AbletonLinkInteropTest.cpp:186-192`
read `loopbackProbe()`, but when `delivered` is false they set `m_multicastReason` and skip. A regression of
the ordering the finding describes would therefore show up as SKIPPED, not FAILED. A direct regression test
(`tests/src/core/LinkUdpTransportTest.cpp`, proposed, not registered) would close that gap. It is not in the commit.

## Commands run and exit codes

| Command | EXIT | Log |
| --- | --- | --- |
| `cmake -S <wt> -B <wt>/build` (to register the new test target) | 97 | `logs/configure-1.log` — `bunny-guard: configuring is the parent's job; the lane build tree is already configured.` |
| `cmake --build build --target LinkUdpTransportTest -j4` | 2 | `logs/build-1-green-baseline.log` — `make: *** No rule to make target 'LinkUdpTransportTest'.` |
| `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <wt>` | 1 | `logs/feature-check-1.log` — `PermissionError` creating `<wt>-feature-check` (outside the write-jail; no check ran) |

No RED or GREEN run was executed, so no RED/GREEN result is claimed. The affected-family test count is 0.

## Blockers

1. **Build tree cannot register a new test target.** `tests/CMakeLists.txt` needs a `LIST(APPEND LMMS_TESTS
   src/core/LinkUdpTransportTest.cpp)` line, which only takes effect after a configure. The lane shim refuses
   configure (exit 97). I did not use `mcp__lmms-lab__lmms_build` because it configures the worktree, which is the
   same action the guard reserves for the parent.
2. **feature_check cannot run in the lane**: it writes logs to a sibling directory outside the write-jail.

## What the parent needs to do

- Configure `zene-qwen-1447/build` once, then build and run `LinkUdpTransportTest`.
- The proposed test source is in `logs/LinkUdpTransportTest.cpp.proposed`; the registration edits are in
  `logs/test-registration-unbuilt.patch` (`git apply` to re-apply, then add the test to `tests/fork-sources.txt`
  and `tests/all-sources.txt` after `tests/src/core/LinkRealtimeProbe.h`).
- Mutation check to prove the test is meaningful: apply the finding's ordering to `LinkUdpTransport.cpp` (move the
  `measureLoopback` block above lines 304–308), rebuild, expect `startMeasuresDeliveryOnThisHost` to FAIL; revert
  and expect PASS.
- Run `feature_check.py` from outside the jail, where its log directory can be written.

## Self-check

Pending: `submit_check.py` is run after this commit; its output is recorded below.
