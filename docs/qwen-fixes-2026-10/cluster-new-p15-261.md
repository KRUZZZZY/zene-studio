# Cluster new-p15-261 — The JACK setup widget leaks its JACK client

**Verdict: FALSE-POSITIVE**

## Finding (as reported)

`include/AudioJack.h:~71` — `AudioJack::setupWidget` stores a raw `jack_client_t*` opened by its
constructor, the class declares no destructor, so destroying the widget "loses the only handle while
the JACK client remains registered". Suggested fix: close `m_client` in the widget destructor when non-null.

## Why it is not real on this branch

The handle never outlives the constructor. The constructor opens the client and closes it again before it returns,
so there is nothing left for a destructor to release.

- `src/core/audio/AudioJack.cpp:508` — `m_client = jack_client_open("Zene Studio Setup Dialog", JackNoStartServer, &status);`
- `src/core/audio/AudioJack.cpp:565-569` — at the end of the same constructor, on every normal return path:
  `if (m_client != nullptr) { jack_deactivate(m_client); jack_client_close(m_client); }`
- `include/AudioJack.h:84-103` — `setupWidget` declares no destructor. None is needed for the client, because
  the close already happened. `grep '~setupWidget' include/AudioJack.h` returns nothing (captured in the round-1 LOG FOLDER as `01-evidence.log`).
- The header's own comment at `include/AudioJack.h:98` gives the client's purpose: "we have to be our own client to display
  inputs and outputs". Its only use is listing port names during construction, and the callers are at
  `src/core/audio/AudioJack.cpp:540` and `:553`, both before the close at `:565`.
- Opening and closing the dialog repeatedly therefore cannot leave a setup client connected: each instance
  opens and closes its own client inside its constructor (508 → 569).
- `include/AudioJack.h:~71` in the finding points at `supportsMultichannelOutput()` (`include/AudioJack.h:70`), not at the widget.
  The widget class is at `include/AudioJack.h:84`.

**The suggested fix would be harmful.** After the constructor, `m_client` is still non-null: line 569 does not reset it.
A destructor that "closes `m_client` when non-null" would call `jack_client_close` a second time on a client that was already
closed. That is undefined behaviour. No product code was changed.

## Residual observations (not the reported defect; not fixed, no verified failure)

1. **Dangling pointer after construction.** `m_client` is not set to `nullptr` after `jack_client_close` at line 569.
   Its only remaining reader is `getAudioPortNames` (`src/core/audio/AudioJack.cpp:595`, `:600`), and that helper is called only from
   the constructor (540, 553). So no current code path dereferences it. It is a latent hazard for any future method that does.
   Nulling it would be a hygiene change with no observable behaviour to test, so it is not part of this verdict.
2. **Exception window.** Between `:508` and `:569` nothing is RAII-guarded. An exception that escapes the constructor
   (e.g. `std::bad_alloc` from the widget, `QLineEdit` or the vectors) would skip the close. This is an out-of-memory path only and
   has not been reproduced.

## Regression test (round 2)

Added to the existing registered file `tests/src/core/AudioJackPortRestartTest.cpp` (no CMake or registration change):

- `setupWidgetOpensAndClosesItsClientEachTime` — constructs and destroys `AudioJack::setupWidget` three times under a parent
  `QWidget` and asserts the widget's client-name `QLineEdit` child exists. The main macro changed from `QTEST_GUILESS_MAIN` to
  `QTEST_MAIN` because a widget needs a `QApplication`; `QT_QPA_PLATFORM=offscreen` is already set for this test in `tests/CMakeLists.txt:5509`.
- **Determinism:** no timing, no sleeps, no races. The fixed loop count is 3. `JackNoStartServer` never spawns a server, so the widget
  takes the null-client branch when no server is running (`:508`). If a real JACK server is running, it takes the open-and-close branch
  (`:565-569`). Both branches pass the same assertions, so the outcome does not depend on the machine's JACK state.
- **What it proves:** the construct/destroy cycle the finding describes runs cleanly, including the null-client path.
- **What it does NOT prove:** the test is a contract and smoke check, not a regression proof. The reported defect does not exist,
  so there is no product change to revert.

### Mutation experiment (round 2, to test whether the test has teeth)

I applied a temporary mutation: a new destructor that calls `jack_client_close(m_client)` unconditionally. This is the finding's
suggested fix with its null check removed. I built and ran it:

- The test **still passes**. `test-2-red-mutation.log` ctest EXIT=0, and direct run `test-3-direct-mutation.log` EXIT=0
  with `Totals: 4 passed, 0 failed`. The message `jack_client_close called with a NULL client` appears 3 times in that log, so the mutated destructor did run.
- Reason: libjack's `jack_client_close(NULL)` does not crash. It prints the message and returns. Probe: `probe-compile.log` EXIT=0,
  `probe-run.log` EXIT=0, output "jack_client_close called with a NULL client" then "after-null-close".
- So on the no-server path this test cannot detect the double-close hazard. The double close needs a live JACK server and a
  non-null dangling `m_client`, which cannot be reproduced deterministically here (it would need a racy live server or a test seam
  in production code). That limit is reported here and not hidden.

The mutation was reverted with `git apply -R` on `mutation.patch` (EXIT=0). `git diff` of the two product files is empty again,
and the rebuild (`build-4-restored.log`, EXIT=0) plus the GREEN run (below) confirm the restored tree.

## Commands and exit codes

Round 2 (LOG FOLDER `new-p15-261.r2/logs`). Run from the worktree unless noted.

| # | Command | EXIT | Log |
|---|---|---|---|
| 1 | `cmake --build build --target zene AudioJackPortRestartTest -j4` (first try, ccache temp in `/run/user/1000` denied) | 2 | `build-1.log` (environment error, not code) |
| 2 | `env CCACHE_TEMPDIR=/tmp/ccache-tmp-p15 cmake --build build --target zene AudioJackPortRestartTest -j4` | 0 | `build-2.log` |
| 3 | `ctest -R '^AudioJackPortRestartTest$' --output-on-failure` (from `build/tests`, with the new test) | 0 | `test-1-green.log` — 1 ctest test, passed |
| 4 | Mutation applied, `git diff` saved to `mutation.patch`, then the same build | 0 | `build-3-mutation.log` |
| 5 | Same ctest on the mutation | 0 (test still passes — see experiment) | `test-2-red-mutation.log` |
| 6 | `./AudioJackPortRestartTest` (direct, mutation) | 0 — `4 passed, 0 failed` | `test-3-direct-mutation.log` |
| 7 | `git apply -R mutation.patch` (revert) | 0 | — |
| 8 | Same build after revert | 0 | `build-4-restored.log` |
| 9 | `ctest -R '^AudioJackPortRestartTest$' --output-on-failure` (restored) | 0 — GREEN, 1 ctest test, passed | `test-4-green.log` |
| 10 | `./AudioJackPortRestartTest` (direct, restored) | 0 — `4 passed, 0 failed`, 0 `NULL client` lines | `test-4-green-direct.log` |
| 11 | `bash tests/fork-sources-gate.sh` | 0 | `20-fork-sources.log` |
| 12 | `bash tests/all-sources-reproduce.sh` | 0 | `21-all-sources-reproduce.log` |
| 13 | `bash tests/no-upstream-regression-gate.sh` | 0 | `22-no-upstream-regression.log` |
| 14 | `tests/scripted/check-namespace` | 0 | `23-check-namespace.log` |
| 15 | `bash tests/no-tautology-gate.sh` | 0 | `24-no-tautology.log` |
| 16 | `python3 tests/doc-figures-gate.py` | 0 | `25-doc-figures.log` |
| 17 | `bash tests/complexity-gate.sh --check` | 1 (ratchet, see below) | `26-complexity.log` |
| 18 | `bash tests/file-length-gate.sh --check` | 1 (ratchet, see below) | `27-file-length.log` |
| 19 | `python3 .../qwen/feature_check.py <worktree>` | **1 — blocked** (`PermissionError`, see below) | `28-feature-check.log` |
| 20 | `python3 .../qwen/feature_check.py <worktree>` (re-check, round 2 final) | **1 — blocked** (`PermissionError: ... zene-qwen-new-p15-261-feature-check/fork-sources.log`) | `30-feature-check-recheck.log` |
| 21 | `python3 .../qwen/submit_check.py <worktree> new-p15-261` (round 2 final) | 1 — "gates" row FAIL (it runs row 20), "tests added or changed" PASS | `29-submit-check.log` |

**Rows 19–21 are blocked by the environment.** `feature_check.py` writes its logs to `<worktree>/../<name>-feature-check`
(`feature_check.py:70-71`). That is the projects directory, outside the write-jail. Round 1 failed creating that directory;
round 2 finds it present but not writable from here (`PermissionError` writing `fork-sources.log`). Nothing was changed to get around it.
Rows 11–18 are the same commands its `SIMPLE` and `RATCHET` lists run (lines 22–29), run directly from the worktree.

Ratchet rows 17–18 exit 1. Their `REGRESSION` lines (6 and 15) name pre-existing train items, and neither log mentions `AudioJack`
(`grep -c AudioJack` → 0 for both). This worktree changes only `tests/src/core/AudioJackPortRestartTest.cpp` and the report, so no
ratchet item is in a file I changed. The red result is the train's known state.

Round 1 (LOG FOLDER `new-p15-261.r1/logs`): the evidence grep (`01-evidence.log`, EXIT 0), the first `feature_check.py` attempt
(`02-feature-check.log`, EXIT 1, blocked), and the first `submit_check.py` run (`03-submit-check-pre.log`, EXIT 1) are kept there.

## Evidence (file:line)

- `src/core/audio/AudioJack.cpp:508` open; `:565-569` deactivate and close, end of constructor.
- `src/core/audio/AudioJack.cpp:540`, `:553` constructor callers of `getAudioOutputNames` / `getAudioInputNames`; `:595`, `:600` the only `m_client` readers in the helper.
- `include/AudioJack.h:84-103` `setupWidget` (no destructor); `:98` purpose comment; `:99` `m_client` member.
- `include/AudioJack.h:70` `supportsMultichannelOutput()` (what the finding's "~71" points at).
- `tests/src/core/AudioJackPortRestartTest.cpp` (new `setupWidgetOpensAndClosesItsClientEachTime`); `tests/CMakeLists.txt:1244` and `:5509` (registration, unchanged).

## Self-check

- **The regression test FAILS with the product change reverted and PASSES with it (exit codes):** not applicable, because
  there is no product change and the reported defect does not occur. The one fail-check I could run was the mutation experiment above:
  the test **passed** with the unconditional-close mutation (ctest EXIT 0, direct EXIT 0), so it does not fail for that hazard.
  Exit codes: mutation build 0, mutation run 0, revert 0, restored build 0, restored run 0. The test therefore is not a regression proof.
- **The test is deterministic (seam or injected values):** yes. No timing, sleeps or races. It uses no JACK server
  (`JackNoStartServer`), a fixed loop of 3, and a parent `QWidget`. Both possible outcomes (null client or open-and-close) pass the same assertions.
- **The fix is at the cause and complete (callers and siblings checked):** no fix. Callers of the port-name helpers are at
  `src/core/audio/AudioJack.cpp:540` and `:553`, both in the constructor, before the close at `:565`. The destructor the finding asks for
  would double-close (see above). No sibling changed.
- **No collateral damage (old projects, saved data, limits):** no product code changed, so no runtime or saved-data behaviour changed.
  The existing slot `newClientStateDropsPortsFromTheDeadClient` still passes (direct run `4 passed, 0 failed`). The main-macro change
  (`QTEST_GUILESS_MAIN` → `QTEST_MAIN`) affects only this test file.
- **FALSE-POSITIVE evidence (code file:line or contract):** `src/core/audio/AudioJack.cpp:508` and `:565-569`;
  `include/AudioJack.h:84-103`, `:98`, `:70`. Contract: the purpose comment at `include/AudioJack.h:98` (the client exists only to list port names during construction).
- **`submit_check.py` and build notes:** "tests added or changed" is now satisfied by the test file change (`tests/src/core/AudioJackPortRestartTest.cpp`).
  The "gates (feature_check.py)" row still FAILs in my run, but only because `feature_check.py` cannot write its logs from my write-jail
  (rows 19–21, `PermissionError`). Every gate command it runs passes or shows only the known train ratchet reds (rows 11–18).
  The gate row needs to be re-run from outside the jail to confirm. The build needed `CCACHE_TEMPDIR=/tmp/ccache-tmp-p15`
  because ccache could not write under `/run/user/1000` (row 1).
