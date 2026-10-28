FALSE-POSITIVE

# Cluster new-p21-59 — remote.connect dereferences RemoteControls::connect result without null check

Finding: `src/core/ControlCommandsRemote.cpp:54` (historical line 54; current tree line 54 is the same call),
`remoteConnect` calls `remote->state()` on the pointer `RemoteControls::connect(...)` returned, and the finding says
this is null when "connect fails to create/bind the remote controls".

## Verdict: the null return cannot happen

`RemoteControls::connect` never returns `nullptr`. It returns the object it has just stored in the owning pointer:

- `src/core/RemoteControls.cpp:91` — `s_remote.reset();` (the old controls are released first)
- `src/core/RemoteControls.cpp:92` — `auto remote = std::make_unique<RemoteControls>();` (`make_unique` never yields null; allocation failure throws, it does not return null)
- `src/core/RemoteControls.cpp:93-96` — `remote->m_channel`, `m_firstCc`, `open(...)`, `setTarget(...)` on the local object
- `src/core/RemoteControls.cpp:97` — `s_remote = std::move(remote);` (the owning pointer now holds the non-null object)
- `src/core/RemoteControls.cpp:98` — `return s_remote.get();` (nothing runs between 97 and 98 that could reset it)

The only code that runs between line 92 and line 97 is `open` and `setTarget`. Neither can leave `s_remote` null at
line 98, because line 97 assigns it unconditionally after them. If a nested call reset `s_remote` in the meantime, line 97
would still assign a non-null object. So the return value is non-null on every path.

The only caller is the command handler (`grep -rn "RemoteControls::connect\b"`, excluding `build/`):
`src/core/ControlCommandsRemote.cpp:51`, and the definition at `src/core/RemoteControls.cpp:85`.
Declaration: `include/RemoteControls.h:62` (no nullable contract is declared).

## What "connect fails" actually does (the by-design path)

A failure to open the MIDI port is not reported as a null pointer. `MidiSurfaceInput::open` (`include/MidiSurfaceInput.h:57-67`)
returns early when there is no audio engine (`:62`), leaving `m_port` null; `connect` still returns the live object.
The failure is reported through the state the handler returns:

- `src/core/RemoteControls.cpp:189` — `state()` reports `connected` (`isOpen()`) and `subscribed` (the port is subscribed).
- The command schema documents exactly this, `src/core/ControlCommandsRemote.cpp:106-108`:
  "'subscribed' says whether the input is a port the MIDI client has (false: nothing will arrive)."

So `remote.connect` with no engine or an unknown port returns success with `connected`/`subscribed` describing the
failure. That is the intended contract, not a null dereference. The finding's "connect fails → nullptr" premise is not
true in this tree.

## Why no code change and no RED/GREEN test

Procedure step 2 says a not-real finding skips to the report, and the completion check accepts a claim-only closure for
FALSE-POSITIVE. There is no defect to fix. A test added here would pass both before and after, so it would not prove
anything about the finding. No `src/`, `include/` or `tests/` file changed.

A defensive `if (remote == nullptr)` in `remoteConnect` would be dead code for the current `connect`. It would only matter
if a future `connect` could return null, and the right place for that change would be the contract in `connect`, not this
handler. Not added.

## Commands run (EXIT codes)

| # | Command | EXIT | Log |
| --- | --- | --- | --- |
| 1 | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | 1 | `logs/gates-1.log` — `PermissionError` creating `<worktree>/../zene-qwen-new-p21-59-feature-check` (outside the write-jail); the gates never ran |
| 2 | `python3 -I logs/runner/feature_check_logdir.py <worktree>` (copy of the same runner; only the log folder and the cache root are redirected to the LOG FOLDER, gate code untouched) | 0 | `logs/gates-2.log` — all 8 rows PASS, `RESULT: PASS`, base `37eaf0755` (cached baseline) |

Diff between the runner and the copy: `ROOT` and `logs` lines only (`logs/runner/feature_check_logdir.py`).

No build or ctest was run: nothing in `src/` or `tests/` changed, so no binary or test result depends on this branch.

## Self-check

- Regression test FAILS with the product change reverted and PASSES with it: **not applicable** — there is no product change. Nothing to revert.
- The test is deterministic: **not applicable** (no test added).
- The fix is at the cause and complete: **not applicable** (no fix). The cause is absent: see the `RemoteControls.cpp:91-98` chain above.
- No collateral damage: nothing changed; old projects and saved data are untouched.
- Verdict FALSE-POSITIVE evidence: `src/core/RemoteControls.cpp:85-98` (non-null return by construction), `src/core/ControlCommandsRemote.cpp:51-54` (the only caller), `include/MidiSurfaceInput.h:57-67` and `src/core/RemoteControls.cpp:189` (failure reported through state, not null), `src/core/ControlCommandsRemote.cpp:106-108` (documented contract).
- Gates: PASS via the log-redirected runner copy (`gates-2.log`, EXIT 0). The stock `feature_check.py` cannot run from this jail (`gates-1.log`, PermissionError on the projects folder). This is an environment limit, not a gate result.
- Audio-thread, undo and command constraints: no code added; no new command (the command snapshot is not touched).
