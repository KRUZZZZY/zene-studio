IMPLEMENTED

# Q050-334.2 - Versioned public TypeScript session SDK (stage 2 of 2 of Q050-334)

Branch `feat/050-Q050-334.2-oc` from `050/m0-train-26` (`af010bc3cc`). The package is in `tools/zene-session-sdk/`
(`@zene-studio/session-sdk` 0.1.0), and it uses stage 1's event protocol unchanged. This unit changes no C++, adds or
changes no command, and changes no project format. The feature record is `docs/050-features/Q050-334.2.md`. Logs are
in `~/.local/share/zene-bunny-fix/qwen/runs/Q050-334.2.r1/logs/`.

## Acceptance

All runs below were from `build/tests` with `QT_QPA_PLATFORM=offscreen`, no `LMMS_PLUGIN_DIR`, and Node v22.23.1.

| Criterion | Test | Command | Exit |
|---|---|---|---|
| A versioned package exports public types, includes a usage example that typechecks, and documents connection, read/edit calls, subscriptions, errors, and resync | `ZeneSessionSdkTypecheck`: strict `tsc` over `src/`, `examples/live-view.ts` (imported by package name) and `test/`, then the `dist` build emitting `index.js` + `index.d.ts`. `ControlSessionSdk`: `SDK_VERSION` equals the `package.json` semver, and the example runs against a real instance. The docs are `tools/zene-session-sdk/README.md` (sections Connect, Read and edit, Subscribe to changes, Errors, Resync) and `CHANGELOG.md` | `ctest -R "SessionSdk" --output-on-failure` (`ctest-9-restored-final.log`) | 0 |
| An integration test uses the SDK against a real Zene instance to connect, read state, edit, receive the corresponding event, unsubscribe, and exercise recovery after disconnect or overflow | `ControlSessionSdk`: `tests/control-session-sdk.py` starts a headless `zene` and runs `tools/zene-session-sdk/test/integration.ts`, 42 PASS lines through the SDK's own exports, covering connect, read, the SDK's `track.add` arriving as its event, unsubscribe stopping events, overflow (held 4 of 4, notice detected, auto-resync, missed count, state complete) and disconnect (stale, retries, `missed === 2`, nothing replayed, next change arrives) | same run; also `ctest -R "^ControlSessionSdk$" --repeat until-fail:5` (`ctest-2-repeat.log`): 5 of 5 passed | 0 |

## Revert proof

| Change | Command | Exit |
|---|---|---|
| Product reverted: `tools/zene-session-sdk/src/` moved away (example and test unchanged) | `ctest -R "SessionSdk" --output-on-failure` (`ctest-7-reverted-final.log`): typecheck fails with TS2307 "Cannot find module '@zene-studio/session-sdk'", and ControlSessionSdk fails with ERR_MODULE_NOT_FOUND | 8 (both failed) |
| Behavioural mutation: `LiveSession` does not call `recover()` | `ctest -R "^ControlSessionSdk$"` (`ctest-8-mutated-final.log`): "timed out waiting for the resync after the overflow" | 8 |
| Restored | `ctest -R "SessionSdk" --output-on-failure` (`ctest-9-restored-final.log`) | 0 |

## Gates

`feature_check.py` could not create its log directory beside the worktree, because this session's write jail refused
it (PermissionError). I ran a copy of the script, `/tmp/q3342-feature_check.py`, that changes only that log path, to the
run folder. Every row passed: fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace,
no-tautology, doc-figures, complexity and file-length. Exit 0 (`feature-check-4.log`). The first run, before the
refactor commit, failed complexity on `connect` (CCN 14) and on a test function (CCN 11); both were split.
`submit_check.py` reported its gates row as FAIL for the same PermissionError, not for a gate result.

## Self-check

- **Each new test fails with the product change reverted, then passes restored.** Yes. With `src/` moved away,
  `ZeneSessionSdkTypecheck` and `ControlSessionSdk` both failed (ctest exit 8). A behavioural mutation, removing
  `LiveSession`'s recovery, also failed `ControlSessionSdk` (exit 8). Restored, both passed (exit 0). See the
  revert table above. This unit has no C++ change, so no rebuild applies; `zene` was rebuilt once after the
  CMakeLists change (`build-2.log`, EXIT=0).
- **Every acceptance criterion is proven by a test I ran.** Yes. Criterion 1 is `ZeneSessionSdkTypecheck` plus
  `ControlSessionSdk`, and criterion 2 is `ControlSessionSdk`; both exit 0, and `ControlSessionSdk` passed 5 of 5
  repeats.
- **One undo step per edit; honest reversibility classes.** No command is new or changed. The SDK's edits are the
  existing registered commands (`track.add`, `track.rename`, `control.undo`/`redo`), each with its unchanged row.
  Subscribing uses stage 1's `events.*`, which are `not_mutating`. The snapshot is not affected, so it was not
  regenerated.
- **Nothing on the audio thread.** This unit adds no C++ to Zene. The SDK is an external Node process that talks to
  the control socket, which is served on Zene's UI thread (stage 1's design). TSan does not run on this box; this
  is reasoned, not measured.
- **Old projects load and render as before; new data round-trips.** Nothing in Zene's load, save or render path
  changed, and the SDK stores nothing in a project (subscriptions are runtime state). No render comparison was
  run, because no rendering code changed.
- **Qt 5.15 / MSVC / macOS < 10.15 portability.** No C++ or Qt API was added. The CMake change only adds
  `find_program(node)` and two `add_test` entries behind it. `ControlSessionSdk` is registered with the UNIX
  control suite only, like `ControlProjectEvents`. The Windows named-pipe path of the SDK (`net.connect` with a
  `\\.\pipe\` name) was **not run here**: there is no Windows toolchain on this box.
- **GUI and agent through the same command (WARN rows).** No DAW-side user action is added: the brief puts a GUI
  panel out of scope, and the package is for external programs. Every edit it makes is an existing registered
  command, the same one the menus use, so there is no new GUI entry point to test. The manual describes the
  package (`#session-sdk`).
- **Not verified:** the typecheck on CI. It needs `npm ci` in the package directory (or `$ZENE_TSC`); without one it
  reports Skipped (77) and does not pass. I ran it locally with the pinned TypeScript 5.9.3 and `@types/node`
  22.20.5. `ControlSessionSdk` needs Node >= 22.6; it reports Skipped (77) on older Node and is not registered
  without node.
