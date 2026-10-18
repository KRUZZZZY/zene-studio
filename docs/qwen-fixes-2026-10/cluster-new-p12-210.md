# cluster new-p12-210 — BUG 4: a structurally valid message can index missing arguments

**Verdict: FIXED** (round 2, answering Luna's REWORK of round 1)

## The defect

A wire message carries its own argument count (`receiveMessage()` in
`src/common/RemotePluginBase.cpp` pushes exactly that many strings), and the framing check
only validates structure. So a *complete* message can carry fewer arguments than its handler
reads. `message::getInt(p)` / `getString(p)` / `stringArgument(p)` are `data[p]` on a
`std::vector<std::string>` with no bounds check (`include/RemotePluginBase.h:457-478`), so
reading past the end is undefined behaviour. Three host-side handlers read arguments:

| Id | Reads | Pre-fix location |
|---|---|---|
| `IdChangeInputOutputCount` | `getInt(0)`, `getInt(1)` | `src/core/RemotePlugin.cpp:1075-1076` |
| `IdDebugMessage` | `stringArgument(0)`, `getString(0)` | `src/core/RemotePlugin.cpp:1111`, `:1120` |
| `IdIsUIVisible` | `getInt()` (= argument 0) | `src/core/RemotePlugin.cpp:1129` and, separately, the awaited reply read in `isUIVisible()` (`include/RemotePlugin.h:212`) |

Round 1 guarded only `IdChangeInputOutputCount`. Luna's review found the other two IDs still
indexed unchecked, and found the awaited `IdIsUIVisible` reply read in `isUIVisible()` never
goes through `processMessage()` at all (`waitForReplyWithin()` returns a matching reply
directly, `src/core/RemotePlugin.cpp:800-817`). Observed RED: SIGSEGV at a near-null
address (`0x8` / `0x0`) in each case, from reading the empty argument vector (see the evidence table).

## The fix (round 2)

- `include/RemotePlugin.h:382` — `requiredArgumentCount(int)`: the protocol's argument
  table, an inline `static constexpr` in the class: `IdChangeInputOutputCount` → 2,
  `IdDebugMessage` / `IdIsUIVisible` → 1, every other id → 0. Host-only, so the shared
  `include/RemotePluginBase.h` (already over the all-scope file-length baseline) is not grown
  this round.
- `src/core/RemotePlugin.cpp:1009-1014` — one check at the top of `processMessage()` (`:1002`), before
  the switch: a message shorter than `requiredArgumentCount(_m.id)` is refused and
  `processMessage()` returns. This covers every argument read in the function
  (`src/core/RemotePlugin.cpp:1075-1076, 1111, 1120, 1129`), and it replaces round 1's inline
  check in the `IdChangeInputOutputCount` case, so the function's CCN does not rise.
- `include/RemotePlugin.h:217-223` — `isUIVisible()`: the awaited reply is checked the same way,
  under the lock (so `m_inProcessPeriod` is read race-free), before `getInt()`. An incomplete
  reply yields -1, as an unanswered query does.
- `src/core/RemotePlugin.cpp:983` — `refuseIncompleteMessage(const message&)`, generalised
  from round 1's `refuseIncompleteCountMessage(int)`. Nothing is applied and the plugin is failed
  (`m_failed = true`), like the retired pre-#589 ids. On the audio thread
  (`m_inProcessPeriod`) it records the id and count in `m_pendingArityId` /
  `m_pendingArityCount` and sets `m_deferredWork`: no allocation, no mutex, no log. On the main
  thread it logs `qCritical()` directly.
- `src/core/RemotePlugin.cpp:707-714` — `reportDeferredClientRequests()` writes the deferred
  diagnostic on the main thread, with the id and the expected count from `requiredArgumentCount()`.
- `include/RemotePlugin.h:306-307` — the `m_pendingArityId` / `m_pendingArityCount` deferral slots.

Design choice to confirm: an incomplete `IdDebugMessage` or `IdIsUIVisible` fails the plugin,
not merely drops the message. That is the same protocol-failure route Luna accepted for the
arity refusal in round 1 (an arity the protocol does not have means the two builds disagree).

## Regression tests

- `tests/src/core/RemotePluginAudioPortsTest.cpp:824` — `channelCountMessageMissingAnArgumentIsRefused()`
  (round 1, IdChangeInputOutputCount with one argument) — unchanged.
- `tests/src/core/RemotePluginAudioPortsTest.cpp:872` — `zeroArgumentDebugMessageIsRefused()` — a complete
  zero-argument `IdDebugMessage`.
- `tests/src/core/RemotePluginAudioPortsTest.cpp:878` — `zeroArgumentIsUIVisibleMessageIsRefused()` — a complete
  zero-argument `IdIsUIVisible` sent through `processMessage()`.
  Both use the helper `refusesMessageWithoutArguments()` (`:886`): a running host plugin
  (python stand-in client, not failed), one `qCritical` on the taking thread, `failed()` true.
- `tests/src/core/RemotePluginClientE2ETest.cpp:1023` — `emptyQueryAnswerIsRefusedNotRead()` — the awaited
  `IdIsUIVisible` reply carries no argument (the new `emptyquery` mode of the fake client):
  `isUIVisible()` returns -1, `cachedUIVisible()` stays -1, the plugin is failed. This is the
  waiter path that `processMessage()` never sees.
- `tests/src/plugins/FakeRemotePluginClient.cpp:146, 173-177, 410, 629` — the `emptyquery`
  mode: `visibilityAnswer()` returns no arguments in that mode (kept out of `handleMessage()`,
  so that function's CCN is unchanged from HEAD: 125).

No zero-argument `IdChangeInputOutputCount` test was added: it passes on round 1's code as well,
so it would not be a regression for this round, and the one-argument test above already runs
the same `requiredArgumentCount()` check.

## Evidence (exit codes as observed; logs in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-210.r2/logs`)

Build note: the default ccache temp directory `/run/user/1000/ccache-tmp` is outside the
write-jail, so every build used `CCACHE_TEMPDIR=/tmp/zene-ccache-tmp`. The first attempt (no
override) failed with ccache "Permission denied" (`build-1.log`, EXIT 2) before any code was built.

| Step | Command (run from the worktree / `build/tests` as noted) | Exit | Log |
|---|---|---|---|
| Test-target build, product unchanged | `CCACHE_TEMPDIR=/tmp/zene-ccache-tmp cmake --build build --target zene RemotePluginAudioPortsTest RemotePluginClientE2ETest FakeRemotePluginClient -j2` | 0 | `build-2.log` |
| **RED** (product = round 1), each new slot | `./RemotePluginAudioPortsTest zeroArgumentDebugMessageIsRefused` (from `build/tests`) | **139** (SIGSEGV) | `red3-debug.log` |
| **RED** | `./RemotePluginAudioPortsTest zeroArgumentIsUIVisibleMessageIsRefused` | **139** (SIGSEGV) | `red3-uivisible.log` |
| **RED** | `./RemotePluginClientE2ETest emptyQueryAnswerIsRefusedNotRead` | **139** (SIGSEGV) | `red3-e2e.log` |
| **RED** family | `ctest -R '^(RemotePluginAudioPortsTest\|RemotePluginClientE2ETest)$' --output-on-failure` | **8** (both SEGFAULT, 0/2 passed) | `test-red3-family.log` |
| Initial RED on the unfixed branch (earlier fake build) | the same three slots and the AudioPorts ctest | 139 ×3, ctest 8 | `red-debug-1.log`, `red-uivisible-1.log`, `red-e2e-1.log`, `test-red-1.log` |
| Proof the tests catch the bug — product reversed | `git diff -- src/core/RemotePlugin.cpp include/RemotePlugin.h > fix.patch && git apply -R fix.patch` (exit 0), rebuild | 0 | `build-7.log` |
| **RED** with product reversed | the three slots and the family ctest, as above | **139 / 139 / 139**, ctest **8** | `red3-*.log`, `test-red3-family.log` |
| Fix re-applied | `git apply fix.patch` (exit 0); `git diff` equals the saved patch; rebuild | 0 | `build-8.log` |
| **GREEN** with fix, each new slot | the three slots, as above | **0 / 0 / 0**, each `3 passed, 0 failed, 0 skipped` | `green4-*.log` |
| **GREEN** family ctest | `ctest -R '^(RemotePluginAudioPortsTest\|RemotePluginClientE2ETest)$' --output-on-failure` | **0**, 2/2 passed | `test-green4-family.log` |
| Affected-family test count | `ctest -N -R '^(RemotePluginAudioPortsTest\|RemotePluginClientE2ETest)$'` | 2 tests registered | `ctest-count-green4.log` |
| Full binaries, final tree | `./RemotePluginAudioPortsTest`, `./RemotePluginClientE2ETest` (from `build/tests`) | **0 / 0** — AudioPorts **19 passed, 0 failed, 0 skipped**; E2E **26 passed, 0 failed, 0 skipped** | `final-full-audioports-4.log`, `final-full-e2e-4.log` |
| Complexity gate, default scope | `bash tests/complexity-gate.sh --check` | **1** — pre-existing | `gate-complexity-base-1.log` (HEAD), `gate-complexity-final-5.log` (final) |
| File-length gate, default scope | `bash tests/file-length-gate.sh --check` | **1** — pre-existing | `gate-filelen-base-1.log` (HEAD), `gate-filelen-final-5.log` (final) |

### Gate status: both required gates fail on HEAD and fail identically now

Both gates exit 1 on the unmodified HEAD (`gate-*-base-1.log`) and exit 1 on the final tree
(`gate-*-final-5.log`). The `REGRESSION` / `FAIL` / `improved` lines are identical between the
two runs (`diff` empty for both gates). The failing entries are in other files: the default fork-scope
gates measure `tests/fork-sources.txt`, and none of the files this change edits is listed there
(`src/core/RemotePlugin.cpp`, `include/RemotePlugin.h` and the three test files are upstream or
test-side and are not in fork scope). This change adds no violation to either default gate.

Checked at `--scope all` as well, for the files this change touches (`gate-complexity-all-final-4.log`,
`gate-filelen-all-final-4.log`). These are not required and are reported, not fixed:

- `lmms::RemotePlugin::processMessage` CCN **28 on HEAD and 28 now** (lizard on `git show HEAD:…`
  and on the working tree). The all-scope baseline value of 12 predates the current HEAD, so the
  gate reports a rise that this change does not make.
- `FakeRemotePluginClient.cpp handleMessage` CCN **125 on HEAD and 125 now** (the first version of
  this change raised it to 130; moved into `visibilityAnswer()`). Not in the all-scope baseline.
- `RemotePluginBase.h` is byte-identical to HEAD this round (`git diff --quiet HEAD`), and reports 670 lines
  against a 507 all-scope baseline: pre-existing, not this change.
- `src/core/RemotePlugin.cpp`, `tests/src/core/RemotePluginAudioPortsTest.cpp` and
  `tests/src/core/RemotePluginClientE2ETest.cpp` are flagged "new file over 500 lines" in the all-scope
  gate because they have no all-scope baseline entry; they were over 500 code lines at HEAD as well. The
  test additions this round are roughly +50 and +26 lines in the two test files.

## Known limits (not covered by this change)

- The audio-thread deferral branch of `refuseIncompleteMessage()` (`m_inProcessPeriod`) is not
  exercised by a regression for the new ids. The same branch shape is used by the retired-id
  refusal; the regression here runs on the main thread, as round 1's did.
- Two incomplete messages in one period share the single deferred slot (`m_pendingArityId`); the
  report shows the last one. The plugin is already failed by the first, so only the diagnostic is
  affected.
- A zero-argument `IdIsUIVisible` refused by `processMessage()` is not counted as a stale query
  reply (`consumeStaleQuery()` is skipped). The plugin is failed, so the stale count no longer
  matters.
- The client-side handlers (the plugin client reading host messages) were not audited; this report
  covers the host, as the finding does.

## Files committed together

- `src/core/RemotePlugin.cpp` (the check, the refusal, the deferred report)
- `include/RemotePlugin.h` (`requiredArgumentCount()`, `refuseIncompleteMessage()`, the `isUIVisible()` reply check, the deferral slots)
- `tests/src/core/RemotePluginAudioPortsTest.cpp` (the zero-argument regressions)
- `tests/src/core/RemotePluginClientE2ETest.cpp` (the empty-reply regression)
- `tests/src/plugins/FakeRemotePluginClient.cpp` (the `emptyquery` mode)
- `tests/upstream-modifications.txt` (the two inherited-file reasons updated for the round-2 design)
- `docs/qwen-fixes-2026-10/cluster-new-p12-210.md` (this report)

`include/RemotePluginBase.h` is not changed this round (its `message::size()` is round 1's).
