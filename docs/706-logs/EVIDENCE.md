# Board card #706, lane `040/feat-706` — lane evidence (2026-09-22)

Proof for the destructive waveform editor's first slice (`sample.*`). Every
command below was run unpiped in this worktree; the exit codes are the
commands' own, never a downstream pipe's (`cmd > log 2>&1; echo EXIT=$?`).
ctest ran from `build/tests` (0 tests would be an ERROR, never a pass).
This one `.md` IS the `docs/706-logs` evidence: Gate 6's declared docs class
is `*.md`, and raw `.log`/`.txt` files under `docs/` are not a declared class
— measured by this lane's first Gate 6 run (EXIT=1, 16 undeclared paths, all
of them this lane's log files), fixed by consolidating here rather than by
widening the ledger for logs.

## Disk before the build (`df -h /home`, rule: free < 12 GB → STOP)

```
/dev/nvme0n1p2  286G  230G   42G  85% /
```

## Configure

CI's linux-x86_64 CMAKE_OPTS + `-DWANT_QT6=ON` (first attempt without
WANT_QT6: EXIT=1, Qt5 not found on this box — every sibling lane configures
with `-DWANT_QT6=ON` too; second attempt: EXIT=1, no CLAP headers — the CLAP
and VST3 SDK checkouts were copied READ-ONLY from the sibling lane's build
tree into this build tree, no sibling file touched; final:)

```


-- Configuring done (1.5s)
-- Generating done (2.5s)
-- Build files have been written to: /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-706/build
EXIT=0
```

## Build, targeted only (`cmake --build build --target <targets> -- -j2`)

Attempt 1 — EXIT=2: `undefined reference to lmms::registerSampleEditCommands`
(the four baker registrations existed, the umbrella that calls them did not).
Attempt 2 — EXIT=2: `ControlSampleOperatorTest.cpp` compile errors
(`std::vector::isEmpty`; address-of-rvalue `&control::ClipRef{}`) — fixed.
Attempt 3 (the current tree:)

```
[100%] Building CXX object tests/CMakeFiles/ControlSurfaceReferenceTest.dir/ControlSurfaceReferenceTest_autogen/mocs_compilation.cpp.o
[100%] Linking CXX executable ControlSurfaceReferenceTest
[100%] Built target ControlSurfaceReferenceTest
EXIT=0
[100%] Linking CXX executable ControlRegistryTest
[100%] Built target ControlRegistryTest
```

## The registered proof — ctest from `build/tests`

```
Test project /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-706/build/tests
    Start 42: ControlSampleOperatorTest
1/1 Test #42: ControlSampleOperatorTest ........   Passed    1.68 sec

100% tests passed, 0 tests failed out of 1

Total Test time (real) =   1.69 sec

--- contract / regression batch ---

Test project /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-706/build/tests
    Start  36: ControlRegistryTest
1/5 Test  #36: ControlRegistryTest ..............   Passed    1.43 sec
    Start  37: ControlSurfaceReferenceTest
2/5 Test  #37: ControlSurfaceReferenceTest ......   Passed    1.41 sec
    Start  41: ControlVerbInverseTest
3/5 Test  #41: ControlVerbInverseTest ...........   Passed    1.40 sec
    Start  44: ControlWarpCommandsTest
4/5 Test  #44: ControlWarpCommandsTest ..........   Passed    1.42 sec
    Start 107: ReversibilityContractTest
5/5 Test #107: ReversibilityContractTest ........   Passed    1.44 sec

100% tests passed, 0 tests failed out of 5

Total Test time (real) =   7.09 sec

--- rebuilt unit batch after the last comment-only edit ---

Test project /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-706/build/tests
    Start 36: ControlRegistryTest
1/2 Test #36: ControlRegistryTest ..............   Passed    1.42 sec
    Start 42: ControlSampleOperatorTest
2/2 Test #42: ControlSampleOperatorTest ........   Passed    2.60 sec

100% tests passed, 0 tests failed out of 2

Total Test time (real) =   4.02 sec

--- snapshot, agent surface, API boundary, MCP coverage ---

Test project /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-706/build/tests
    Start 170: LuaApiSurface
1/5 Test #170: LuaApiSurface ....................   Passed    0.06 sec
    Start 211: agent_surface
2/5 Test #211: agent_surface ....................   Passed    3.14 sec
    Start 218: ControlCommandsSnapshot
3/5 Test #218: ControlCommandsSnapshot ..........   Passed    3.00 sec
    Start 221: ZeneApiBoundary
4/5 Test #221: ZeneApiBoundary ..................   Passed    5.53 sec
    Start 222: ControlMcpGroupCoverage
5/5 Test #222: ControlMcpGroupCoverage ..........   Passed    5.67 sec

100% tests passed, 0 tests failed out of 5

Total Test time (real) =  17.41 sec
```

## Snapshot regeneration from a live instance

```
wrote /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-706/tools/mcp-zene-control/zene_control/commands_snapshot.json: 337 commands, proto 1, version 0.3.0-alpha.26+ab9462d, lane head ab9462d56142cf342ab327f6bb737dea03e42f77
surface: 337 id(s) across 53 group(s), ids_sha256 9c311ffc7c08921553eadd7a09dc53b399c0350b4987ed335f4ecb09189c84e0
```

(ids delta measured against the committed base: 332 → 337; the added set is
exactly `sample.amplify, sample.fade, sample.generate, sample.normalize,
sample.reverse`, removed = ∅.)

## Gates (the brief's set: 4 / 6 / 7 / 8 / 9 + the unregistered-tests check)

```
$ bash tests/unregistered-tests-gate.sh
test sources scanned: 182 (registered: 180, declared-not-built: 2, helpers: 4)
PASS: every test source under tests/src/ is registered, or declared with a reason
EXIT=0

$ bash tests/complexity-gate.sh --check   # gate 4
mavis-trash: moved to trash: '/tmp/tmp.FtfjrWllcq.over'
EXIT=0   (first run EXIT=1: new lmms::parseGenerate measured CCN 13; split into three helpers, max CCN now 5 — see the lizard block below)

$ bash tests/file-length-gate.sh --check  # gate 7
0
0 regressions
mavis-trash: moved to trash: '/tmp/tmp.eXx8IJd0jj'
EXIT=0

$ bash tests/duplication-gate.sh --check  # gate 8
PASS: duplicated lines 0.51% (budget 5%)
EXIT=0

$ bash tests/fork-sources-gate.sh         # gate 9 (incl. all-sources-reproduce)

PASS: every tracked source in scope is registered (693 fork-NEW, 1107 inherited, 46 tooling).
EXIT=0

$ bash tests/no-upstream-regression-gate.sh  # gate 6
16
      tests/upstream-modifications.txt with a reason and ship a regression test.

first run of gate 6 (raw .log evidence): EXIT=1, 16 paths, every one this lane's docs/706-logs file; the two real contended files (tests/CMakeLists.txt, src/core/CMakeLists.txt) classified 'build/config (allowed)' with their ledger reasons appended in-line by this lane. The re-run after this file replaced the logs is quoted above.
```

## New-file cyclomatic complexity (lizard, six new files, max first)

```
5	sameFrames@88-97@tests/src/core/ControlSampleOperatorTest.cpp
5	lmms::sizeFrames@138-166@src/core/ControlCommandsSample.cpp
5	lmms::sampleops::generate@127-142@src/core/SampleOperators.cpp
5	lmms::resolveBakeTarget@80-113@src/core/ControlCommandsSampleEdit.cpp
5	lmms::registerSampleNormalize@195-256@src/core/ControlCommandsSampleEdit.cpp
5	lmms::parseLevel@116-136@src/core/ControlCommandsSample.cpp
```

(target CCN ≤ 10; gate 4's own `--check` exit is quoted above.)

## A16 measurement constants, marked LANE-LOCAL

- `docs/RELEASE-NOTES-v0.3.0-alpha.md` A16-HISTOGRAM block: `rows=345
  true_inverse=163 snapshot=32 irreversible=13 not_mutating=137` — asserted
  by the registered `ReversibilityContractTest` (EXIT=0 above) against this
  tree's five new rows; the merge tip re-takes the probe.
- `tests/src/core/ControlRegistryTest.cpp:499-500`: `85 + 7 + 5 + 5 + 5`
  with the LANE-LOCAL marker, line count kept at the grandfathered 505.
  Measured, not assumed: that QCOMPARE lives in the telemetry-OFF slot
  (`#ifdef ZENE_TELEMETRY_ENABLED` at :103) and this build is telemetry-ON,
  so the lane did not execute it; the live surface measured 337 ids ON
  (snapshot above) while the committed base already carried 332 — the line's
  BASE predates the registry's growth, which is exactly why the marker says
  the merge tip must re-measure instead of the lane claiming it did.
- `include/ControlRegistryGroups.h` file-length re-anchored 696 → 714 (fork
  and all scope) by `tests/file-length-gate.sh --reanchor-file`, reason
  recorded in `tests/file-length-baseline.tsv` / `-all.tsv`.
