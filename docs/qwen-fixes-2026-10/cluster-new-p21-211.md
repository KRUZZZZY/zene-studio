FALSE-POSITIVE

# Cluster new-p21-211 - LaunchBindings::release dereferences a possibly-null song

Finding: `src/core/LaunchBindings.cpp:78` (historical line 78; the current tree has it at the same line) reads
`Song* song = Engine::getSong();` and dereferences it in `release()` without a null check. The stated trigger is "a
slot binding's note is released with no project/song loaded".

## Why the trigger cannot produce a null song

1. **The song pointer is non-null for the whole engine lifetime.** `Engine::getSong()` returns `s_song`
   (`include/Engine.h:71-73`). `s_song` is set only in `Engine::init`, at `src/core/Engine.cpp:72`
   (`s_song = new Song;`), and cleared only by `deleteHelper( &s_song )` in `Engine::destroy`, `src/core/Engine.cpp:123`.
   "No project loaded" is a state of the `Song` object, not a null `Song*`: project new/close goes through
   `Song::clearProject()` (`src/core/Song.cpp:1684`), which resets the song's contents and keeps the object.
   `Engine::destroy` calls `s_song->clearProject()` at `src/core/Engine.cpp:106` just before deleting it.

2. **`release()` is reachable only through the UI-thread drain of a live port.** `release()` is called only from
   `LaunchBindings::applyNote` (`src/core/LaunchBindings.cpp:180`). `applyNote` is the `apply()` of
   `MidiSurfaceInput`, which runs from `drain()` on a `QTimer` on the UI thread (`include/MidiSurfaceInput.h:53`,
   `:110-114`). `drain()` pops only items pushed by `processInEvent`, which needs a `MidiPort` created in `open()`
   (`include/MidiSurfaceInput.h:57-65`). `open()` dereferences `Engine::audioEngine()` (`:61`), so a port exists only
   once the audio engine exists.

3. **The two init steps that create the port and the song are adjacent and not interleaved with event processing.**
   `s_audioEngine` is created at `src/core/Engine.cpp:71` and `s_song` at `:72`. No `initProgress` emit or
   `processEvents` falls between them (the emits are at `Engine.cpp:63` and `:69` before, and later ones after `:72`),
   so no queued control or MIDI event can reach `drain()` while the audio engine exists and the song does not.

4. **Shutdown does not leave a window either.** `main` calls `Engine::destroy()` at `src/core/main.cpp:1862`, after
   `delete app` (`:1858`). With the event loop gone, `drain()` cannot run, so `release()` cannot run after
   `s_song` is deleted at `Engine.cpp:123`.

5. **The launch path has the same precondition, and the code already relies on it.** `launch()`
   (`LaunchBindings.cpp:109`) goes through the registry, and `ControlRegistry::isEngineReady()` requires `Engine::getSong() != nullptr`
   (`src/core/ControlRegistry.cpp:205-207`, gate at `:283`) for every engine command. A press with no song is refused,
   not crashed.

So the finding's precondition does not occur. A null check at line 78 would be dead code, and a regression test for it
could not fail on the current tree without first constructing a state the production code cannot reach. No product code
was changed for this cluster.

## Adjacent hazard (not this finding; not fixed here; not reproduced by a run)

`session.bindings_connect` runs before the engine is ready, and that path does dereference a null audio engine.

- `src/core/ControlCommandsLaunchBindings.cpp:144-147`: the `add()` helper passes `false` as the last argument of
  `defineCommandWithSchema`, which is `requiresEngine` (`src/core/ControlCommandDef.cpp:34-45`). So every `session.*`
  launcher command, including `session.bindings_connect` (`:159-161`), has `requiresEngine == false`. The
  `isEngineReady()` gate at `ControlRegistry.cpp:283` does not apply to them.
- `src/core/ControlCommandsLaunchBindings.cpp:129-131`: `connectBindings` calls `LaunchBindings::connect()`, which calls
  `open()` (`include/MidiSurfaceInput.h:57-65`).
- `include/MidiSurfaceInput.h:61`: `open()` calls `Engine::audioEngine()->midiClient()` with no null check.
  `Engine::audioEngine()` is null until `Engine.cpp:71` (`include/Engine.h:61-64`).
- The control socket is started before the engine (`src/core/main.cpp:1378-1382` comment), and the UI thread processes
  events before `Engine::init(false)` (`src/gui/GuiApplication.cpp:205` precedes `:211`).

So a control client that sends `session.bindings_connect` during startup should be able to dereference a null
`AudioEngine`. I read this from the code; I did not build a reproduction. It is a different defect (the port setup, not
the song in `release`), and it belongs in its own cluster. It needs a design decision: either gate the `session.bindings_*`
commands on the engine (a contract change for a command), or guard `open()`. I did not make that choice here.

## Commands run

No product change, so no RED/GREEN regression was run (there is no reachable failure to pin). Logs are in the run folder
`logs/`, named as below. The worktree path is `zene-qwen-new-p21-211`.

| Step | Command | Exit | Log |
| --- | --- | --- | --- |
| Official gate runner | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | 1 | `feature-check-1.log` |
| Official completion check, before commit | `python3 ~/.local/share/zene-bunny-fix/qwen/submit_check.py <worktree> new-p21-211` | 1 | `submit-check-1.log` |
| Gate command 1/8 | `bash tests/fork-sources-gate.sh` (cwd: worktree) | 0 | `gate-fork-sources-2.log` |
| Gate command 2/8 | `bash tests/all-sources-reproduce.sh` | 0 | `gate-all-sources-2.log` |
| Gate command 3/8 | `bash tests/no-upstream-regression-gate.sh` | 0 | `gate-no-upstream-2.log` |
| Gate command 4/8 | `tests/scripted/check-namespace` | 0 | `gate-check-namespace-2.log` |
| Gate command 5/8 | `bash tests/no-tautology-gate.sh` | 0 | `gate-no-tautology-2.log` |
| Gate command 6/8 | `python3 tests/doc-figures-gate.py` | 0 | `gate-doc-figures-2.log` |
| Gate command 7/8 (ratchet) | `bash tests/complexity-gate.sh --check` | 1 | `gate-complexity-2.log` |
| Gate command 8/8 (ratchet) | `bash tests/file-length-gate.sh --check` | 1 | `gate-file-length-2.log` |

Why `feature_check.py` exited 1 without running its checks: it creates `<worktree>-feature-check/` as a sibling of the
worktree (`feature_check.py` `main()`, `logs = wt.parent / f"{wt.name}-feature-check"`), and that path is outside the
directories this lane may write. The log `feature-check-1.log` ends in `PermissionError: [Errno 13]`. I did not edit the
shared gate script. Rows 2-9 above are the same commands it runs, run by hand from the worktree with the logs moved to the
run folder. That is not the runner's table, and it is labelled as manual.

The two ratchets (rows 7-8) exit 1 here. The runner's docstring says they are red on the train on purpose, and I did
not re-check the base. The
runner fails a ratchet only on a regression item that is both new and in a file the branch changed. The branch changes
one file, this doc. `grep` of `gate-complexity-2.log` (7 `REGRESSION` lines) and `gate-file-length-2.log` (19) finds no
line naming `docs/qwen-fixes-2026-10/` or any other changed path. So both pass under the runner's rule.

## Self-check

- **RED/GREEN regression:** not applicable. No product change, because the precondition is unreachable (see the
  evidence above). A test for an unreachable state could not fail on this tree.
- **Deterministic test:** not applicable. No test added or changed.
- **Fix at the cause and complete:** not applicable. The callers and siblings checked are `release` (sole caller
  `LaunchBindings.cpp:180`), `launch` (`:109`, guarded by `ControlRegistry.cpp:205-207` for registry commands), and
  `open()` / `connectBindings` (`MidiSurfaceInput.h:57-65`, `ControlCommandsLaunchBindings.cpp:129-131`). The one hazard
  found on that sibling path is reported above, not fixed here.
- **No collateral damage:** nothing changed in product code or saved data.
- **FALSE-POSITIVE evidence:** file:line citations in the sections above. The key ones are `include/Engine.h:71-73`,
  `src/core/Engine.cpp:71-72` and `:123`, `src/core/LaunchBindings.cpp:180`, `include/MidiSurfaceInput.h:53` and
  `:110-114`, and `src/core/main.cpp:1858-1862`.
- **Submit check:** re-run after this commit. Its output is in the run folder as `submit-check-2.log` and in the lane's
  final message, not in this file, because this file was committed first.
