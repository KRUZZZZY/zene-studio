# Cluster new-p11-11 — collaboration panel recovery from a failed connection

Verdict: **FALSE-POSITIVE**

## Finding
"The collaboration panel cannot recover from a failed connection" — claims that after joining an
offline host and the connection fails, Host, Join, and Leave are all disabled, leaving no panel
action to retry or start hosting. Proposed fix: "Treat the failed state as recoverable: enable
Host and Join, and keep the invite field enabled."

## Why it is a false-positive

The panel's `refresh()` reads the **session** mode, not the client's own state. The two are
different enums:

- `CollabSession::Mode` = { Idle, Hosting, Joined } — `include/CollabSession.h:72-77`, with the
  comment on `Joined`: "connecting, joined, or failed and not yet left".
- `CollabClient::State` = { Idle, Connecting, Joined, Failed } — `include/CollabClient.h:60-66`.

Trace of the failed-connection scenario:

1. `CollabSession::join()` sets `m_mode = Mode::Joined` (`src/core/CollabSession.cpp:233`) and then
   starts the client connection (`m_client->join(...)`, line 236).
2. On a failed connection the client calls `setState(State::Failed, ...)`
   (`src/core/CollabClient.cpp:116` timeout, `:161` socket error, `:180` disconnect, `:273` host
   ended). **Nothing on any failure path changes `m_mode`** — the only `m_mode` assignments in
   `CollabSession.cpp` are `resetToIdle()` (Idle), `host()` (Hosting), `join()` (Joined).
3. `CollabSession::state()` therefore reports `mode = modeName(m_mode) = "joined"`
   (`src/core/CollabSession.cpp:283`); the client's `Failed` state is exposed separately under
   `peer.state` (`peerState()`, line 275).

In `CollabPanel::refresh()` (`src/gui/CollabPanel.cpp:165-182`), `mode` is that session mode:

- `m_leave->setEnabled(mode == QLatin1String("joined"))` (line 178) → **Leave is ENABLED** in the
  failed sub-state. The finding's claim that "Leave is enabled only for `joined`" and that Leave is
  disabled after failure is wrong: the session mode stays `joined` through the failed sub-state.
- `collab.leave` → `CollabSession::leave()` → `resetToIdle()` (`src/core/CollabSession.cpp:242-243`)
  sets `m_mode = Mode::Idle`, which re-enables Host, Join, and the invite field on the next
  `refresh()`.

So the recovery path already exists: click **Leave** (enabled), which resets to idle, then Host/Join
become enabled. The panel is not stuck.

## Why the suggested fix would not work either

Enabling the Host and Join buttons while the session is in the failed sub-state would not let the
user retry, because the underlying commands guard on the session mode:

- `collab.host`: `if (m_mode != Mode::Idle) return refused(...)` (`src/core/CollabSession.cpp:151`)
- `collab.join`:  `if (m_mode != Mode::Idle) return refused(...)` (`src/core/CollabSession.cpp:218`)

With `m_mode == Joined` (the failed sub-state), an enabled Host/Join button would only produce a
refusal error, not a retry. The real recovery is Leave, which is already correctly enabled.

## Evidence (file:line)
- `src/gui/CollabPanel.cpp:178` — `m_leave->setEnabled(mode == "joined")`, `mode` = session mode.
- `src/core/CollabSession.cpp:233` — `m_mode = Mode::Joined` on join; no failure path resets it.
- `src/core/CollabSession.cpp:283` — `state()` reports session mode ("joined"), not client state.
- `src/core/CollabSession.cpp:275` — client `Failed` state lives under `peer.state`.
- `src/core/CollabSession.cpp:151` / `:218` — host/join refuse when `m_mode != Idle`.

## Commands run
No product code changed and no test was added (the behaviour is already correct), so no build/test
ratchet was required. The two gate scripts were run for the record:

- `bash tests/complexity-gate.sh --check` → EXIT 0
- `bash tests/file-length-gate.sh --check` → EXIT 0

## Action
No code change. Report committed only.
