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
  the close already happened. `grep '~setupWidget' include/AudioJack.h` returns nothing (captured in the LOG FOLDER as `01-evidence.log`).
- The header's own comment at `include/AudioJack.h:98` gives the client's purpose: "we have to be our own client to display
  inputs and outputs". Its only use is listing port names during construction, and the callers are at
  `src/core/audio/AudioJack.cpp:540` and `:553`, both before the close at `:565`.
- Opening and closing the dialog repeatedly therefore cannot leave a setup client connected: each instance
  opens and closes its own client inside its constructor (508 → 569).
- `include/AudioJack.h:~71` in the finding points at `supportsMultichannelOutput()` (`include/AudioJack.h:70`), not at the widget.
  The widget class is at `include/AudioJack.h:84`.

**The suggested fix would be harmful.** After the constructor, `m_client` is still non-null: line 569 does not reset it.
A destructor that "closes `m_client` when non-null" would call `jack_client_close` a second time on a client that was already
closed. That is undefined behaviour. Nothing was changed.

## Residual observations (not the reported defect; not fixed, no verified failure)

1. **Dangling pointer after construction.** `m_client` is not set to `nullptr` after `jack_client_close` at line 569.
   Its only remaining reader is `getAudioPortNames` (`src/core/audio/AudioJack.cpp:595`, `:600`), and that helper is called only from
   the constructor (540, 553). So no current code path dereferences it. It is a latent hazard for any future method that does.
   Nulling it would be a hygiene change with no observable behaviour to test, so it is not part of this verdict.
2. **Exception window.** Between `:508` and `:569` nothing is RAII-guarded. An exception that escapes the constructor
   (e.g. `std::bad_alloc` from the widget, `QLineEdit` or the vectors) would skip the close. This is an out-of-memory path only and
   has not been reproduced.

## Regression test

None added. The reported defect does not occur, so no test can fail without a fix. A test that passed with or
without a change would not prove anything. `ctest` was not run because no product file changed.
Existing coverage is `tests/src/core/AudioJackPortRestartTest.cpp`, which was not touched.

## Commands and exit codes

| # | Command (run from the worktree unless noted) | EXIT | Log (LOG FOLDER) |
|---|---|---|---|
| 1 | `grep -n` / `sed -n '565,569p'` / `awk` evidence (see Evidence) | 0 | `01-evidence.log` |
| 2 | `python3 .../qwen/feature_check.py <worktree>` | **1 — blocked** | `02-feature-check.log` |
| 3 | `python3 .../qwen/submit_check.py <worktree> new-p15-261` (before this report existed) | 1 | `03-submit-check-pre.log` |
| 4 | `bash tests/fork-sources-gate.sh` | 0 | `4-fork-sources.log` |
| 5 | `bash tests/all-sources-reproduce.sh` | 0 | `5-all-sources-reproduce.log` |
| 6 | `bash tests/no-upstream-regression-gate.sh` | 0 | `6-no-upstream-regression.log` |
| 7 | `tests/scripted/check-namespace` | 0 | `7-check-namespace.log` |
| 8 | `bash tests/no-tautology-gate.sh` | 0 | `8-no-tautology.log` |
| 9 | `python3 tests/doc-figures-gate.py` | 0 | `9-doc-figures.log` |
| 10 | `bash tests/complexity-gate.sh --check` | 1 | `10-complexity.log` |
| 11 | `bash tests/file-length-gate.sh --check` | 1 | `11-file-length.log` |

**Row 2 is blocked by the environment.** `feature_check.py` puts its logs in `<worktree>/../<name>-feature-check`
(`feature_check.py:70-71`). That is the projects directory, outside the write-jail, so `mkdir` fails:
`PermissionError: [Errno 13] ... zene-qwen-new-p15-261-feature-check`. Nothing was changed to get around it. Rows 4–11 are the same
commands the script runs (its `SIMPLE` and `RATCHET` lists, lines 22–29), run directly from the worktree with the same working directory.

Ratchet rows 10–11 exit 1. Their `REGRESSION` lines (6 and 15) name pre-existing train items, for example
`tests/src/gui/ArrangerTimelineTest.cpp (689)` and an improved `lmms::followActionFromJson` baseline entry. A grep of both logs for
`AudioJack` or `docs/` returns nothing. This worktree changes no product file, so no ratchet item is in a changed file. The
red result is the train's known state, not a result of this work.

## Evidence (file:line)

- `src/core/audio/AudioJack.cpp:508` open; `:565-569` deactivate and close, end of constructor.
- `src/core/audio/AudioJack.cpp:540`, `:553` constructor callers of `getAudioOutputNames` / `getAudioInputNames`; `:595`, `:600` the only `m_client` readers in the helper.
- `include/AudioJack.h:84-103` `setupWidget` (no destructor); `:98` purpose comment; `:99` `m_client` member.
- `include/AudioJack.h:70` `supportsMultichannelOutput()` (what the finding's "~71" points at).

## Self-check

- **RED/GREEN exit codes (revert and re-apply the product change):** not applicable. No product change was made, so there is
  nothing to revert. No test exists for this finding.
- **Test is deterministic (seam or injected values):** not applicable. No test was added.
- **Fix is at the cause and complete (callers and siblings checked):** no fix. The callers are `AudioJack.cpp:540` and `:553` (constructor only),
  and the close is at `:565-569`. The destructor the finding asks for would double-close (see above).
- **No collateral damage:** no product change, so none. Saved projects and JACK behaviour are unchanged.
- **FALSE-POSITIVE evidence (code file:line):** `src/core/audio/AudioJack.cpp:508` and `:565-569`, `include/AudioJack.h:84-103`, `:98`, `:70`.
- **`submit_check.py` rows still FAIL and why:** "tests added or changed" (no test, because the defect does not occur, see above).
  "gates (feature_check.py)" (the script is blocked by the write-jail, row 2; rows 4–11 were run directly instead).
  "commits on the branch" and "report" should now pass once this report is committed.
