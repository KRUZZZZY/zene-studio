# Control cluster (linux-arm64 + macOS) — the measurements behind d9185514f

Run: commit `ec07dd3a0` (= v0.2.1-alpha), jobs **103724228374** (linux-arm64) and
**103724228405** (macos-arm64). Every line quoted below is the failing test's OWN
message, read out of those job logs before any theory was formed:

```
zene-gh-token bash -c "gh api repos/KRUZZZZY/zene-studio/actions/jobs/103724228374/logs \
    --allow-escape-sequences" | sed -E 's/\x1b\[[0-9;]*[mGKHF]//g'
```

## Cause per test

| test | job | its own message | measured cause |
|---|---|---|---|
| ControlSocketIntegration | macos-arm64 | 8 pings + `control.version` answered, then EOF on `control.commands_list` ("the server closed the connection") | reply (~46 KiB) larger than macOS's 8 KiB AF_UNIX buffer; `writeAll` treated EAGAIN as failure and dropped the client |
| agent_surface | macos-arm64 | `ConnectionError: the server closed the connection` (+ stack, no transcript) | same command, same defect, one call later |
| ControlHeadlessNoAudioDevice | macos-arm64 | "the instance proceeded but never said so: no 'audio-device-setup' line in the app log" | SDL **succeeds** on macOS (CoreAudio opens), so the test's premise was false there, not the product |
| ControlAbsentSocket | macos-arm64 | "the instance holds AF_UNIX socket(s) with a path ... `['0x3c2e1a4de449532a', '0xc31ed38bef8fd348']`" | lsof's kernel address for an UNNAMED socket read as a held path |
| ControlReadiness | linux-arm64 | "not ready 0.006s after connect: ... 'state': 'engine_missing'" then "no response line inside 30.0s (socket timed out)" | the engine start blocks the thread that serves the socket; the pre-readiness request was bounded by one socket read (30s) instead of the declared 120s budget |
| ControlNoAudioDevice | linux-arm64 | "no response line inside 30.0s (socket timed out)" + "stderr never mentions the failed device 'SDL ...'" | same window; the stderr line is a *consequence* (the app had not reached MainWindow yet) |
| ControlAbsentSocket | linux-arm64 | "method control: the probe sees a real control socket FAILED (1): no response line inside 30.0s" | same window on its pre-readiness ping |
| ControlSocketPathSafety | linux-arm64 | `case_request_line_cap`: `sendall` of 2 MiB → TimeoutError | same window: the server was not draining the socket at all |
| ControlHeadlessProjectOpen | linux-arm64 | `AssertionError: reply id 0 != 1` | a late readiness ping's reply read as the next request's answer (stream shifted by one) |

## The cost that made the bounds expire (measured, still open)

Linux-arm64 adds a **fixed ~34s per engine-constructing process**; linux-x86_64 does not:

| test | x86_64 | arm64 | ratio |
|---|---|---|---|
| TimelineTest | 1.36s | 35.97s | 26x |
| AutomatableModelTest | 1.35s | 35.94s | 27x |
| ControlSocketIntegration | 4.71s | 173.16s (5 instances) | 37x |
| ControlNegativeControl (no engine in-process) | 12.19s | 12.18s | 1.0x |
| MathTest (pure CPU) | 0.02s | 0.01s | 0.5x |
| ControlAbsentSocket: instance SIGINT → exit | 0.17s (macOS arm64) | 28.26s | — |

Tests that never construct the engine are unchanged or faster on arm64, so the machine is
not slow: the +34s lands inside the engine start (single-threaded and burning one core
when the diagnosis sampled it, i.e. before the device exists). This is the item
9d15bd7e9 recorded as STILL OPEN; this lane fixes its effect on the suite's bounds, not
the cost.

## Local proof (linux-x86_64, binary sha256 2ed8c92f9546...)

```
# the macOS defect, reproduced here (burst of 8 pipelined commands_list requests)
python3 tests/control-socket-integration.py build/zene tests/data/agent-control-fixture.mmp
  fix toggled back to the old short-write semantics: EXIT=1
    FAIL: the server closed the connection after 4 of 8 pipelined replies (226864 bytes)
  with the fix:                                     EXIT=0
    PASS: 8 pipelined 74-command replies arrive whole over a 4 KiB receive buffer
          (372 KiB through a socket nobody was draining)

# the linux-arm64 condition, simulated (Engine::init start stretched; toggle not committed)
ZENE_SLOW_ENGINE_START_MS=35000 ctest -R "ControlReadiness|ControlHeadlessProjectOpen" -V -j1   -> EXIT=0 (37.2s / 37.7s)
ZENE_SLOW_ENGINE_START_MS=35000 ctest -R "ControlNoAudioDevice|ControlAbsentSocket" -V -j1      -> EXIT=0 (37.1s / 74.2s)
ZENE_SLOW_ENGINE_START_MS=130000 ctest -R "ControlReadiness" -V -j1                              -> EXIT=8, "***Failed 121.34 sec"
```
The last line is the negative control: past the declared 120s budget the test still FAILS,
so the bound was not weakened into vacuity. The 35s run's pre-readiness answer is a typed
`busy` error carrying the `engine_starting` reason, not a timeout.

## Suite and gates (the commands, unpiped exit codes)

```
cd build/tests && ctest -j2 --output-on-failure   -> CTEST2_EXIT=0, 92/92 passed, 79.84s
bash tests/run-all-gates.sh                       -> GATES4_EXIT=3 (the run of this tip)
```
Both logs are the run of THESE sources; the log files themselves are read by no test and
no gate row, which is why a log cannot be inside the run it records.
Exit 3 = every gate that ran PASSed (ctest, no-tautology, complexity, mutation,
upstream-regression, file-length, duplication, fork-sources, unregistered-tests) and gate 2
(coverage) SKIPPED for want of `--with-coverage` — the documented pass-with-skips code.

The logs themselves are committed beside the tests rather than under docs/ because Gate 6
(no-upstream-regression) only allows `*.md` and `tests/**`, and a run log is not
documentation: `tests/control-arm64-cluster-logs/{ctest-92.log,run-all-gates.log,
slow-engine-simulation/,big-reply-prefix-semantics.txt,big-reply-with-fix.txt}`.

## Only CI can confirm

* engine_ready on a host with no audio device, and the `audio-device-setup` line there;
* the lsof probe against a real Darwin host (its positive control still finds the socket);
* `control.commands_list` over macOS's 8 KiB AF_UNIX buffer — this box's send buffer is
  ~208 KiB, which is why a SINGLE big reply passes pre-fix here and a pipelined burst is
  what reproduces it.
* and the ~34s engine start is untouched: the arm64 job is the only witness for it.
