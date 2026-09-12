# The MCP bridge meets the tools-scope bar, and Gate 6's `tools/` rule is fixed

Lane: `post-alpha/foreign-merge`. Worktree: `projects/lmms-fl-research/zene-pa-foreign`.
Commits: `8131b1ac9` (the bridge meets the tools-scope bar), `c4879b440` (Gate 6's `tools/**`
rule), `56000c514` (this report and the evidence logs), plus a fourth commit carrying the
committed-tree re-runs of §7. Nothing is pushed; no tag exists.

Evidence: **`tests/integration-logs-3f-bridge/`** (committed, never `/tmp`). Every exit code
in this document is from an unpiped run — `cmd > log 2>&1; echo EXIT=$?` — and the log is
named after the run.

## 0. Why this lane exists

The agent-control MCP bridge was copied into the product at `tools/mcp-zene-control/`
(16 files) and registered in `tests/tools-sources.txt` (18 → 31 entries; the manifest's own
`REPRODUCES` check passes). Three gates that measure `tools/` then failed over it:

```
tests/integration-logs-3f-bridge/before-complexity-tools.log
  complexity-gate: 463 functions in the tools scope; 17 exceed CCN 10
  REGRESSION: new function over target: request@159-222@…/zene_control/protocol.py (CCN 18)
  REGRESSION: new function over target: test_01_tools_list_set_equals_daw_commands_list@175-216@…/tests/test_mcp_e2e.py (CCN 17)
  REGRESSION: new function over target: parse_args@193-256@…/zene_control/config.py (CCN 12)
  REGRESSION: new function over target: commands_payload@168-219@…/zene_control/tools.py (CCN 12)
  FAIL: complexity ratchet regressed (check mode: baseline not written)

tests/integration-logs-3f-bridge/before-file-length-tools.log
  file-length-gate: 31 tools-scope sources measured; 4 exceed 500 lines
  REGRESSION: new file over 500 lines: tools/mcp-zene-control/tests/test_mcp_e2e.py (610)
  REGRESSION: new file over 500 lines: tools/mcp-zene-control/tests/test_units.py (513)

tests/integration-logs-3f-bridge/before-gate6.log
  tools/mcp-zene-control/.gitignore                         VIOLATION: undeclared change to upstream-inherited code
  tools/mcp-zene-control/tests/data/agent-control-fixture.mmp VIOLATION: undeclared change to upstream-inherited code
  tools/mcp-zene-control/zene_control/commands_snapshot.json  VIOLATION: undeclared change to upstream-inherited code
```

Neither escape was available and neither was sought: `--reanchor` is refused here by design
(the ratchet's whole point is that NEW code meets the bar), and grandfathering a thing imported
the day before is what this project has spent the night refusing. So the code was fixed, and
the one genuinely wrong gate rule was fixed.

## 1. Task 1 — the split, by concern

### 1.1 The five files (line counts from `wc -l`)

| file | before | after | what it holds now |
|---|---|---|---|
| `tests/test_mcp_e2e.py` | 610 | **310** | the **live instance's contract**: the generated tool list (test_01), the full flow (test_02), resources (test_03), the DAW's typed errors (test_04), the protocol check (test_09), transactions + undo (test_11) |
| `tests/test_mcp_errors.py` | — | **247** | the **bounded typed failures** (test_05 no instance, test_06 stale socket, test_07 killed mid-call, test_08 wedged stub, test_10 the readiness gate) — the tests that drive `harness.StubDaw` instead of the live flow |
| `tests/mcp_fixture.py` | — | **175** | the **shared module fixture**: the one real headless instance, `STATE_DIR` (where the live command-list cache that test_05 reads back is written), the verbatim transcript, and the MCP helpers |
| `tests/test_units.py` | 513 | **385** | naming, generation, configuration, the taxonomy, the offline surfaces |
| `tests/test_wire_client.py` | — | **168** | the **wire client's own concern**: real `AF_UNIX` sockets, the lifecycle cases, no DAW binary |

Why this split and not "cut each file at 500 lines": a split that leaves two files each over
500 is not a fix, and a split by size alone would have put the live-instance flow and the
stub-DAW failures in the same module while giving the fixture away twice. The seam was chosen
where the **fixture needs** already differ:

* `test_mcp_e2e.py` and `test_mcp_errors.py` both need the streamed MCP client and one real
  instance, so the instance, the state directory and the transcript moved to
  `tests/mcp_fixture.py` — **one copy**, not two (a copy would also be a Gate 8 clone risk);
* `test_wire_client.py` needs no instance, no MCP client and no fixture: it is `ControlClient`
  against real sockets, which is the one place the four lifecycle cases belong;
* `test_units.py` and `test_wire_client.py` share nothing (`CONTROL_ENTRY` and `config_for()`
  stay in `test_units.py`, which is where the generation tests live).

`tests/mcp_fixture.py` is deliberately **not** named `test_*`, so `unittest`/`pytest` discovery
cannot collect it as a suite. Its `ensure()` is **idempotent** (the second e2e module reuses the
first's instance rather than starting a second one, which would also overwrite `STATE_DIR`) and
its cleanup — `control.quit`, then write and print the transcript — is registered **once with
`atexit`**, so no module's `tearDownModule` can pull the instance out from under a module that
runs later. One visible consequence, stated rather than hidden: the transcript dump now prints
*after* the test summary instead of before it, because it happens at interpreter exit.

### 1.2 What was *not* changed

* No test deleted, skipped, renamed or weakened. `python3 -m pytest tests/ -q` collects
  **64** tests (39 + 14 + 6 + 5, i.e. exactly the 53 units + 11 e2e of the original two files)
  and every method name from the original suites (`test_01_…` … `test_11_…`, `TestNaming`, …)
  is unchanged, so the README's own references (`test_01`, `test_05`, `test_07`, `test_08`,
  `test_10`) still point at the same tests.
* The only textual edits inside a moved test are `call(…` → `F.call(…)`, `record(` →
  `F.record(`, `run_bridge(` → `F.run_bridge(`, `error_kind(` → `F.error_kind(`,
  `daw_tool_name(` → `F.daw_tool_name(`, `FULL_FLOW_PHASE` → `F.FULL_FLOW_PHASE`, and
  `instance = INSTANCE; assert instance is not None` → `instance = F.live_instance()` (the same
  assertion, now a named fixture accessor). Every assertion, message and comment is otherwise
  byte-identical.
* `tests/tools-sources.txt` gained the three new files so gates 4, 7 and 8 measure them (one
  file, one home), and the modified bridge README records the split and its counts.

## 2. The four decompositions (target CCN ≤ 10)

Measured with the same tool the gate uses: `python3 -m lizard --csv <files>`.

| function | file | CCN before | CCN after | what was extracted |
|---|---|---|---|---|
| `request` | `zene_control/protocol.py` | 18 | **3** | `_send` (frame + send, its three typed send failures), `_read_reply` (read, parse, id check), `_reply_result` (ok → result, else the typed error with the DAW-kind downgrade) |
| `test_01_tools_list_set_equals_daw_commands_list` | `tests/test_mcp_e2e.py` | 17 | **4** | `_listed_tools` (initialize, list, record the exchange) and `_assert_daw_args_survive` (the per-command schema loop) |
| `parse_args` | `zene_control/config.py` | 12 | **1** | `_build_parser`, `_resolve_workdir`, `_resolve_socket_path`, `_resolve_state_dir`, `_config_from_args`, `_cli_or_env_int`, `_cli_or_env_float` |
| `commands_payload` | `zene_control/tools.py` | 12 | **5** | `_commands_arguments`, `_commands_source_or_error`, `_compact_commands` |

Every one is an **extraction**, not a rewrite, and the invariants that matter are unchanged:

* `request` still fails `"request before connect"` on a disconnected client, still raises send
  errors *before* anything is read, still raises the same kinds/messages/details for a
  non-JSON reply, a non-object reply, an id mismatch and every DAW error kind, and still
  downgrades an unknown kind to `bridge_error`. The `_send` guard is defensive only — `request`
  checks first, exactly as before — and it exists so the send path is typed and statically
  checkable on its own.
* `parse_args` keeps the documented precedence (CLI > `ZENE_CONTROL_*` > default) for every
  field, the same defaults, the same `clamp_timeout`/`MIN_READY_TIMEOUT` handling, the same
  `os.path.abspath` on socket/workdir/state-dir and the same typed error from a bad env number;
  the parser's flags, destinations and help strings are unchanged.
* `commands_payload` returns the same payload, the same `INVALID_ARGS` message for an unknown
  `source`, the same `NOT_FOUND` payload when the snapshot is missing, and the same compacted
  command objects for `include_schemas: false`. The one added line is an `assert resolved is
  not None` that states the invariant of the (source, error) pair for reader and type checker.
* `test_01` asserts exactly what it asserted, in the same order.

After the change the highest CCN in the bridge's own files is **9** (`status_payload`,
`resolve`, `handshake`, `_read_line`), all pre-existing and all under the target. No baseline
entry was added, moved or removed; the gate reports the same 13 grandfathered functions it
reported before (all of them `tools/mmpz-git/*`, none of them the bridge's).

## 3. Task 2 — Gate 6's `tools/` rule was wrong, and now is not

### 3.1 The defect

`tests/no-upstream-regression-gate.sh` classified a path as allowed tooling **only** if the path
appeared in `tests/tools-sources.txt` (old rule, at the `tools/*` branch of its `case`). That
manifest's own documented candidate command admits **source extensions only**:

```bash
git diff --name-only --diff-filter=A 4e677cb6c6ab HEAD -- tools \
  | grep -E '\.(py|sh|cpp|c|h|hpp|cc|cxx)$' | LC_ALL=C sort
```

So a non-source file under `tools/` — a fixture, a JSON snapshot, a `.gitignore` — **can never be
listed in it**, could never be classified as allowed, and was reported as *"VIOLATION:
undeclared change to upstream-inherited code"*: a false statement about a file upstream has never
had. Three such files were the known victims, all three from the bridge (quoted at the top of
this document). The recorded hole is the class, not those three paths.

### 3.2 Why the rule is wrong — the verification the code now quotes

`tools/` does not exist upstream at all, in either the upstream commit this fork is based on or
in upstream master today. Both commands print nothing:

```
$ git ls-tree -r --name-only 4e677cb6c6ab -- tools | wc -l
0
$ git ls-tree -r --name-only origin/master -- tools | wc -l
0
$ git show -s --format='%H %ci %s' 4e677cb6c6ab
4e677cb6c6ab7ac8bec221097053753c1eb273e3 2026-08-30 12:45:12 +0900 Don't compress man page in during build (#8494)
```

Every `tools/**` path is therefore fork-authored **by construction**, whatever its file type, and
there is nothing to declare. That is now the rule, in the `case`:

```bash
tools/*) verdict="fork tooling (allowed by construction: tools/ does not exist upstream)" ;;
```

The old manifest read (`TOOL_SOURCES`) is gone from the gate, with a comment saying why.
`tests/tools-sources.txt` keeps its real jobs and loses the one it never had: it is the **scope
manifest** for gates 4, 7 and 8 (`--scope tools`, its own baselines) and one of Gate 9's homes for
a tools/ source — it is not, and structurally cannot be, a declaration Gate 6 may demand. Nothing
else in Gate 6 changed: the ledger, the fail-closed blank-reason check, the `tests/**`,
build-config, CI-config, docs, fork-NEW and declared-divergence categories, and the
`base..HEAD` scope are all as they were.

### 3.3 The negative control — both directions

Gate 6 examines `git diff --name-only <base>..HEAD`, i.e. **committed** changes, so a working-tree
edit would prove nothing. The control is therefore a scratch commit on a throwaway branch, and it
is reverted by deleting that branch:

```
tests/integration-logs-3f-bridge/gate6-negative-control-branch.log
  git switch -c scratch/gate6-negative-control                                   EXIT=0
  # appended a 4-line "this is a scratch change, do not merge" comment to
  # src/core/AudioResampler.cpp — an INHERITED file, in the base commit, and
  # verified absent from tests/upstream-modifications.txt
  git add src/core/AudioResampler.cpp; git commit                                  EXIT=0  (f2f80d987)
```

**Direction 1 — a genuine violation outside `tools/` still fails** (after the fix):

```
$ bash tests/no-upstream-regression-gate.sh > tests/integration-logs-3f-bridge/gate6-negative-control-fails.log 2>&1; echo EXIT=$?
EXIT=1
$ grep AudioResampler tests/integration-logs-3f-bridge/gate6-negative-control-fails.log
src/core/AudioResampler.cpp                              VIOLATION: undeclared change to upstream-inherited code
FAIL: Gate 6 violation(s) above — either revert the change, or declare it in
      tests/upstream-modifications.txt with a reason and ship a regression test.
```

**Direction 2 — the real tree passes, and the scratch branch is gone**:

```
$ git switch post-alpha/foreign-merge        EXIT=0
$ git branch -D scratch/gate6-negative-control   EXIT=0
$ git diff --exit-code 4e677cb6c6ab -- src/core/AudioResampler.cpp   EXIT=0   # byte-identical to base
$ git rev-parse --short HEAD
c4879b440
$ bash tests/no-upstream-regression-gate.sh > tests/integration-logs-3f-bridge/gate6-real-tree-passes.log 2>&1; echo EXIT=$?
EXIT=0
PASS: every change to upstream-inherited code since 01148947ea4d8bdb05c237942758d61acc867223 is declared
      (435 changed path(s) declared; the ledger holds 447 entries)
```

and the three bridge files that used to be violations are now classified, in the same run:

```
tools/mcp-zene-control/.gitignore                          fork tooling (allowed by construction: tools/ does not exist upstream)
tools/mcp-zene-control/tests/data/agent-control-fixture.mmp  fork tooling (allowed by construction: tools/ does not exist upstream)
tools/mcp-zene-control/zene_control/commands_snapshot.json   fork tooling (allowed by construction: tools/ does not exist upstream)
```

So the gate has been **seen to fail** on a real violation with the fix in place, and seen to pass
on the real tree. Neither direction is inferred from the code.

## 4. Every exit code (unpiped, committed under `tests/integration-logs-3f-bridge/`)

| # | command | before | after | after-log |
|---|---|---|---|---|
| 1 | `bash tests/complexity-gate.sh --scope tools --check` | **1** | **0** | `after-complexity-tools.log` |
| 2 | `bash tests/file-length-gate.sh --scope tools --check` | **1** | **0** | `after-file-length-tools.log` |
| 3 | `bash tests/duplication-gate.sh --scope tools --check` | 0 | **0** (0.00% of 34 sources) | `after-duplication-tools.log` |
| 4 | `bash tests/no-upstream-regression-gate.sh` (Gate 6) | **1** (3 violations) | **0** | `gate6-real-tree-passes.log` |
| 5 | `bash tests/fork-sources-gate.sh` (Gate 9) | 0 | **0** (34 tools-sources, 0 stale) | `after-gate9.log` |
| 6 | `bash tests/run-all-gates.sh` | — | **3 = PASS-WITH-SKIPS** (9 of 10 gates PASS; gate 2 SKIP: `--with-coverage` not passed) | `run-all-gates.log`, `run-all-gates.exit` |
| 7 | the manifest still reproduces (the manifest's own command) | REPRODUCES | **REPRODUCES** | `manifest-reproduces.log` |
| 8 | the bridge's own tests (`tests/mcp-zene-control`) | 53/53 units, 10/11 e2e | see §5 | `bridge-test-*.log` |
| 9 | `ctest` from `build/tests` | 86/86 | **86/86 (EXIT 0)** | `run-all-gates.log` (Gate 1) |

Reproduce the manifest check exactly as it is written in the hint (it prints `REPRODUCES` and
nothing else):

```
$ diff <(grep -vE '^[[:space:]]*(#|$)' tests/tools-sources.txt | LC_ALL=C sort) \
    <(git diff --name-only --diff-filter=A 4e677cb6c6ab HEAD -- tools \
      | grep -E '\.(py|sh|cpp|c|h|hpp|cc|cxx)$' \
      | grep -vE '^(tools/local-ci\.sh|tools/mmpz-git/scratch/(qtesc|qtroundtrip|qtsave|qtsave2)\.cpp|tools/ncpu-shim\.c|tools/wasm/wat2wasm\.cpp)$' \
      | LC_ALL=C sort) && echo REPRODUCES
REPRODUCES   EXIT=0
```

### 4.1 `run-all-gates.sh` and `ctest`

```
$ bash tests/run-all-gates.sh > tests/integration-logs-3f-bridge/run-all-gates.log 2>&1; echo EXIT=$?
EXIT=3

================ SUMMARY ================
gate   name                     result
1      ctest                    PASS
2      coverage                 SKIP
3      no-tautology             PASS
4      complexity               PASS
5      mutation                 PASS
6      upstream-regression      PASS
7      file-length              PASS
8      duplication              PASS
9      fork-sources             PASS
10     unregistered-tests       PASS

skipped: 1 of 10 gates did not run
  gate 2 (coverage): --with-coverage was not passed — to run it: pass --with-coverage
RESULT: PASS-WITH-SKIPS (exit 3) — 1 of 10 gates did not run;
100% tests passed, 0 tests failed out of 86
Total Test time (real) = 125.04 sec
```

Exit 3 is the honest result, not a pass: every gate that ran passed, and Gate 2 (coverage) did
not run because `--with-coverage` was not passed — coverage needs a separate instrumented build.
Gate 1's `ctest` is the 86/86 above, run from `build/tests` (the only directory with a
`CTestTestfile.cmake`; running it from the top-level build dir reports 0 tests, which is an
error, not a pass). No C++ was touched by this lane, so the 86/86 baseline is unchanged; the only
rebuilt targets are the ones the mutation gate at Gate 5 rebuilds for itself.

## 5. The bridge's own tests, and the one failure this lane did not fix

With `ZENE_CONTROL_BINARY=<worktree>/build/zene` (the built binary of this very tree — the
`zene-pa-agentctl` build the README's original run used no longer exists on this box; the harness
fails loudly when there is no binary, by design):

| module | tests | result | exit | log |
|---|---|---|---|---|
| `tests.test_units` | 39 | OK | **0** | `bridge-test-units.log` |
| `tests.test_wire_client` | 14 | OK | **0** | `bridge-test-wire-client.log` |
| `tests.test_mcp_errors` | 5 | OK | **0** | `bridge-test-mcp-errors.log` |
| `tests.test_mcp_e2e` | 6 | 1 error (`test_11`) | **1** | `bridge-test-mcp-e2e.log` |
| `python3 -m pytest tests/ -q` (all four) | 64 | **63 passed, 1 failed** | **1** | `bridge-test-pytest.log` |

`test_11_mutating_command_undo_and_transactions` fails because **the DAW crashes**, and it did so
before this lane touched anything: the baseline run of the unsplit suite against the same binary
is 10/11 with the identical failure (`baseline-bridge-e2e.log`), and the after run fails in the
same test, at the same call, with the same typed error. That is a pre-existing defect, not a
regression from the split or the decompositions.

The root cause, with the bridge out of the picture:

```
tests/integration-logs-3f-bridge/probe-undo.py     (raw RawDawClient, no MCP, no server.py)
tests/integration-logs-3f-bridge/probe-undo.log
  project.open              ok
  transport.set_tempo       ok (tempo 128)
  control.transactions      ok (2 transactions; top: transport.set_tempo, reversible, inverse {op: transport.set_tempo, bpm: 140})
  control.undo              ConnectionError: the instance closed the connection
  process_alive_after_undo  true
  same_connection_ping      BrokenPipeError
  process_alive_after_1s    false
  process_returncode        -11        <-- SIGSEGV
  fresh_connection          ConnectionRefusedError
```

`tests/integration-logs-3f-bridge/gdb-wrapper.sh` runs the instance under gdb to capture the
stack (`gdb-undo-bt.log`), which localises it entirely on the DAW side:

```
Thread 1 "zene" received SIGSEGV
#0  PatternStore::updateComboBox            src/core/PatternStore.cpp:203      (pt->name(), pt == nullptr)
#1  PatternTrack::PatternTrack              include/Engine.h:76
#2  Track::create                           src/core/Track.cpp:140
#4  TrackContainer::loadSettings            src/core/TrackContainer.cpp:146
#6  JournallingObject::restoreState         src/core/JournallingObject.cpp:92
#7  ProjectJournal::restoreState            src/core/ProjectJournal.cpp:87
#8  ProjectJournal::restoreStep             src/core/ProjectJournal.cpp:110
#9  ProjectJournal::undo                    src/core/ProjectJournal.cpp:126
#10 undoThroughJournal                      src/core/ControlCommandsControl.cpp:252
#11 undoLastCommand                         src/core/ControlCommandsControl.cpp:308
```

i.e. `control.undo` → the engine journal unwinds the `transport.set_tempo` checkpoint → the Song's
track container is re-created → a fresh `PatternTrack` calls `updateComboBox()` → the loop
`for (i = 0; i < numOfPatterns(); ++i)` dereferences `PatternTrack::findPatternTrack(i)`, which
returns `nullptr` for a pattern index with no matching entry in its static map. The same path is
the GUI's Ctrl+Z unwind, so it is a product defect, not a bridge artefact — and the bridge itself
behaved correctly: it reported the closed connection as the typed, retryable `disconnected`,
inside its budget, instead of hanging.

**It was not fixed here, deliberately.** A fix would be a change to inherited upstream code
(`src/core/PatternStore.cpp`, `src/tracks/PatternTrack.cpp`), which Gate 6 requires to be declared
in `tests/upstream-modifications.txt` with a reason *and* shipped with a regression test, plus a
`-j4` rebuild and a fresh ctest run — a lane of its own, not a side effect of a test-file split.
Weakening, skipping or deleting `test_11` to make the suite green was not an option: the assertion
is the contract that caught the crash.

## 6. What this lane could not do

* **`test_11` cannot pass in this tree** until the DAW crash in §5 is fixed by whoever owns
  `ProjectJournal`/`PatternStore`. Everything else in the bridge's suites passes, and the failure
  is byte-identical before and after this lane's changes.
* No other deviation. Nothing was pushed, no tag exists, `origin` (LMMS/lmms) and `messmerd` were
  never written to, and no C++ file was modified — the scratch branch that held the negative-control
  commit was deleted, and `src/core/AudioResampler.cpp` is byte-identical to the upstream base
  (`git diff --exit-code 4e677cb6c6ab`, EXIT=0).

## 7. The committed-tree re-runs

The runs in §4 were taken with the work committed (Gate 6 and Gate 9 read `git`, not the working
tree), and re-running them after `56000c514` — i.e. with the evidence and the report themselves now
tracked under `tests/**` — changes nothing, which is the point of taking them twice:

```
$ bash tests/no-upstream-regression-gate.sh              > tests/integration-logs-3f-bridge/committed-tree-gate6.log                 2>&1; echo EXIT=$?
EXIT=0
$ bash tests/fork-sources-gate.sh                        > tests/integration-logs-3f-bridge/committed-tree-gate9.log                 2>&1; echo EXIT=$?
EXIT=0
$ bash tests/complexity-gate.sh --scope tools --check    > tests/integration-logs-3f-bridge/committed-tree-complexity-tools.log      2>&1; echo EXIT=$?
EXIT=0
$ bash tests/file-length-gate.sh --scope tools --check   > tests/integration-logs-3f-bridge/committed-tree-file-length-tools.log     2>&1; echo EXIT=$?
EXIT=0
$ bash tests/duplication-gate.sh --scope tools --check   > tests/integration-logs-3f-bridge/committed-tree-duplication-tools.log     2>&1; echo EXIT=$?
EXIT=0
$ diff <(…tools-sources.txt…) <(…git diff --diff-filter=A 4e677cb6c6ab HEAD -- tools…) && echo REPRODUCES   # the manifest's own command
REPRODUCES                                                                                                    EXIT=0
```

The bridge's own modules are recorded in §5 and were run at `8131b1ac9`; `pytest tests/ -q` at the
same commit collected 64 tests in one process and reported 63 passed / 1 failed.
