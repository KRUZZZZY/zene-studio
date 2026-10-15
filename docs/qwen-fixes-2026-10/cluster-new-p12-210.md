# cluster new-p12-210 — BUG 4: a structurally valid message can index missing arguments

**Verdict: FIXED**

## The defect

`RemotePlugin::processMessage()` handled `IdChangeInputOutputCount` by reading
`_m.getInt(0)` and `_m.getInt(1)` with no argument-count check (pre-fix
`src/core/RemotePlugin.cpp:1037-1038`, the case starting at 1028). `message::getInt(p)`
is `atoi(data[p].c_str())` (`include/RemotePluginBase.h:475-478` pre-fix) — `data`
is a private `std::vector<std::string>` indexed with `operator[]`, no bounds check.

The wire format carries the argument count with the message (layout comment at
`include/RemotePluginBase.h` `peekNextMessage()`; `receiveMessage()` in
`src/common/RemotePluginBase.cpp:300-314` pushes exactly that many strings), and the
framing check only validates that structure. So a *complete* message — id
`IdChangeInputOutputCount`, count 1, one argument — reaches the handler and
`getInt(1)` reads past the end of the message's arguments: undefined behaviour.
Observed concretely: SIGSEGV (`movsbq (%rdi)` on a null pointer out of the garbage
`std::string` the slot held), stack frame `lmms::RemotePlugin::processMessage`
(`test-red-1.log:36, 284`).

## The fix

- `include/RemotePluginBase.h:487-494` — `message::size()`: the argument count, so
  dispatch can validate before indexing.
- `src/core/RemotePlugin.cpp:1063-1074` — the `IdChangeInputOutputCount` case checks
  `_m.size() < 2` *before* any `getInt()`; an incomplete report is refused by
  `refuseIncompleteCountMessage()`.
- `src/core/RemotePlugin.cpp:982-998` — `refuseIncompleteCountMessage()`: nothing is
  applied, the plugin is failed (`m_failed = true`), like the retired pre-#589 ids
  above it (an arity the protocol does not have means the two builds disagree about
  the protocol, and the client may already lay its own planes out for counts the host
  never applied). Realtime rule: on the audio thread (`m_inProcessPeriod`) it only
  records the count and sets `m_deferredWork` — no allocation, no mutex, no log;
  the main thread writes the `qCritical()` (`src/core/RemotePlugin.cpp:707-713`
  via `reportDeferredClientRequests()`, the same pattern as `m_pendingRetiredId`).
- `include/RemotePlugin.h:299-301, 374-377` — `m_pendingArityCount` deferral slot and
  the `refuseIncompleteCountMessage()` declaration.

Messages that do carry both arguments take exactly the previous path (range check,
deferral, application); the 16 pre-existing tests of the family still pass.

## Regression test

`tests/src/core/RemotePluginAudioPortsTest.cpp:824` —
`channelCountMessageMissingAnArgumentIsRefused()`: brings the plugin into the "not
failed" state with the python client stand-in (the only way out of the constructor's
`m_failed = true`), sends `IdChangeInputOutputCount` with one argument, and pins the
refusal: one `qCritical` on the taking thread, `failed()` flipped, and the counts and
shared block untouched. Before the fix the same call crashes inside
`processMessage()` (RED); after it, the assertions hold (GREEN).

## Evidence (every command exit code as observed; logs in
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-210.r1/logs`)

| Step | Command (run from the worktree / `build/tests` as noted) | Exit | Log |
|---|---|---|---|
| RED build (test only, original product code) | `cmake --build build --target RemotePluginAudioPortsTest` | 0 | `build-1.log` |
| **RED** original code | `ctest -R '^RemotePluginAudioPortsTest$' --output-on-failure` (from `build/tests`) | **8** — `***Exception: SegFault` in `channelCountMessageMissingAnArgumentIsRefused`, frame #6 `lmms::RemotePlugin::processMessage`; 15 other sub-tests passed | `test-red-1.log` |
| GREEN build (all targets including the changed headers: `zene vestige vstbase zynaddsubfx RemoteZynAddSubFx RemoteVstPlugin64 NativeLinuxRemoteVstPlugin64 RemotePluginAudioPortsTest RemotePluginClientE2ETest`) | `cmake --build build --target … -j20` | 0 | `build-3.log` |
| **GREEN** with fix | `ctest -R '^RemotePluginAudioPortsTest$' --output-on-failure` | **0** — 17 passed, 0 failed, 0 skipped (`-V` run: `test-green-2.log`) | `test-green-1.log`, `test-green-2.log` |
| Proof the test catches the bug — fix reversed | `git diff -- src/core/RemotePlugin.cpp include/RemotePlugin.h include/RemotePluginBase.h > fix.patch && git apply -R fix.patch`, rebuild same target list | 0 / 0 | `fix.patch`, `build-4.log` |
| **RED** with fix reverted | `ctest -R '^RemotePluginAudioPortsTest$' --output-on-failure` | **8** — `***Exception: SegFault` again | `test-red-2.log` |
| Fix re-applied | `git apply fix.patch`, rebuild same target list | 0 / 0 | `build-5.log` |
| **GREEN** again | `ctest -R '^RemotePluginAudioPortsTest$' --output-on-failure` | **0** | `test-green-3.log` |
| Affected family | `ctest -R 'RemotePlugin\|FakeRemotePlugin' --output-on-failure` | **0** — 2/2 ctest entries (`RemotePluginAudioPortsTest` 17 sub-tests, `RemotePluginClientE2ETest`); `FakeRemotePluginClient` is the E2E's client binary target, rebuilt, no ctest entry of its own | `test-family-1.log` |
| Complexity gate | `bash tests/complexity-gate.sh --check` | 1 — **pre-existing** (see below) | `gate-pre-complexity.log` (pristine tree), `gate-post-complexity.log` |
| File-length gate | `bash tests/file-length-gate.sh --check` | 1 — **pre-existing** (see below) | `gate-pre-filelen.log` (pristine tree), `gate-post-filelen.log` |

### Gate status: both ratchets fail on the pristine branch, unchanged by this fix

Both gates were run **before any edit** (git status clean, HEAD `b02a0ff2b`): exit 1
each. After the fix they exit 1 with a **byte-identical REGRESSION/FAIL set**
(`diff` of the `REGRESSION|FAIL|PASS` lines of pre vs post is empty — see the four
gate logs). None of the20 violations touches a file this change edits: the changed
sources are upstream-inherited and are outside the fork-scope ratchet
(`tests/fork-sources.txt`); the14 file-length violations are test files that grew past
500 code lines in the 2026-10-14/15 train merges while
`tests/file-length-baseline.tsv` was last refreshed 2026-09-30 (`ba66cdb87`), and the6
complexity regressions are functions in those same merged sources whose baseline
entries are absent. Reconciling that is the train integrator's re-anchor (forbidden
here); this change adds no violation to either ratchet.

## Files committed together

- `src/core/RemotePlugin.cpp` (fix)
- `include/RemotePlugin.h` (deferral slot + declaration; declaration changed with its definition)
- `include/RemotePluginBase.h` (`message::size()`)
- `tests/src/core/RemotePluginAudioPortsTest.cpp` (regression + critical-counting handler)
- `tests/upstream-modifications.txt` (the three inherited files' reasons extended with
  the new-p12-210 clause; file keeps its trailing newline)
- `docs/qwen-fixes-2026-10/cluster-new-p12-210.md` (this report)
