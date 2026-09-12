# The telemetry action, the agent surface, and a green obtained honestly

Worktree `zene-pa-foreign` (branch `post-alpha/foreign-merge`, tip `76e3639ea`). One action, one
commit: the Help menu's **"Telemetry - what we send..."** action now resolves to a registered
command, so `tests/agent-surface-gate.py` passes **without** the baseline moving, without a
re-anchor, without an exempt entry and without a weakened gate.

## 1. The failure, exactly as it stood

```
$ python3 tests/agent-surface-gate.py build/zene tests/data/agent-control-fixture.mmp --check
   reflection : 3 registered, 43 unregistered (42 grandfathered, 1 new)
  ratchet    : baseline 42 entries, 0 stale
  reverse    : 72 commands, 72 swept, 0 allowlisted
FAIL: NEW action without a registered command: 'menu:Help/Telemetry - what we send...'
      (surface=menu container=Help)
EXIT=1
```

`tests/integration-logs-3f-fix/01-gate-before.log`. The action arrived with the telemetry lane
(`82d2e1309`, `src/gui/MainWindow.cpp:429`) and declared nothing, so the ratchet was right to call
it a **new** unregistered action.

Neither escape hatch was used, and neither was available:

* `--reanchor` is refused by the gate itself — a re-anchor may only record a surface that is
  *otherwise accounted for*, and this problem is not an ordinary unregistered action.
* `tests/agent-surface-exempt.txt` only matches menus whose **contents are generated at run time**
  (`class:<Name>` / `path:<Menu>`). A fixed consent action is neither.

## 2. What was registered

New file `src/core/ControlCommandsTelemetry.cpp` — the `telemetry.*` group, registered from
`ControlRegistry::registerControlCommands()` like every other group:

| command | `requires` | mutating | what it is |
| --- | --- | --- | --- |
| `telemetry.consent` | `display`, `human` | no | opens the consent screen |
| `telemetry.status` | *(none)* | no | read-only: compiled in? consent on? which groups? the exact payload `submit()` would send |

Both are always registered, whatever `-DZENE_TELEMETRY` says. With the kill switch off the handlers
answer a typed "not in this build" instead of disappearing from the registry, so a client never has
to guess whether an empty `telemetry.*` group means "off" or "not compiled in".

> **SUPERSEDED 2026-09-12 — this is what the file did, and it is why the OFF build stopped
> building.** `src/core/ControlCommandsTelemetry.cpp` names `TelemetryConsent`, `Telemetry`,
> `TelemetryPayload` and `TelemetryHardware`, none of which exist in a
> `-DZENE_TELEMETRY=OFF` build; its OFF path compiled only because the client's *header* was still
> there, and the moment `Telemetry.cpp` was compiled out the file failed on its own signatures
> (unused `consentState`, fatal under `-Werror`) — see `docs/TELEMETRY-V1.md` §5.1. The group is now
> guarded by `ZENE_TELEMETRY_ENABLED` and is **absent from the registry** in that configuration
> (72 commands, not 74): a command id that describes a feature the binary does not contain is worse
> than an absent one, because the agent-surface gate then has to sweep or allowlist it.
> `docs/TELEMETRY-KILL-SWITCH.md` is the repair's report. The design argument in the rest of this
> document — the `display, human` split, the read-only half being swept — stands unchanged.

**Why the split.** Consent is a human act, visibility is not:

* `telemetry.consent` is the modal screen, so it declares `requires: display, human`.
  `ControlRegistry::checkRequires()` refuses it **before the handler runs** for every automated
  caller, and `include/UnattendedRun.h` is why that matters: a `QDialog::exec()` in an unattended
  run is a wait on a click that never comes (task #625). An agent calling it gets a typed `requires`
  refusal naming `telemetry.status` as the alternative it does have. It therefore cannot consent on
  the user's behalf and cannot block an unattended run.
* `telemetry.status` needs no display, no device and no human, so the headless sweep **runs** it
  rather than the allowlist excusing it. It reports the consent record plus
  `Telemetry::buildPayload()` — the same object the consent screen previews and `submit()` sends, so
  there is no second list here that could drift.

**How it was declared.** Through the **dynamic property `controlCommand`** on the `QAction`:

```cpp
telemetryAction->setProperty( "controlCommand", QStringLiteral( "telemetry.consent" ) );
connect( telemetryAction, &QAction::triggered, this, [] { openTelemetryConsentScreen(); } );
```

Not `objectName()` (load-bearing for this action elsewhere) and not `data()` (the gate's own words:
"the weakest channel ... it counts only when it resolves to a live command"). Live proof, from the
running binary (`tests/integration-logs-3f-fix/04-evidence.log`):

```
  text='Telemetry - what we send...' object_name='' command='telemetry.consent' declared_unknown=False
EVIDENCE OK: the Help menu consent action resolves to telemetry.consent
```

**One action, one implementation (A11).** The menu slot no longer builds its own dialog: it calls
`lmms::openTelemetryConsentScreen()`, the same function `telemetry.consent`'s handler calls, so the
menu item and the agent surface cannot drift into two screens. The function is the one place that
checks `isUnattendedRun()` and the one place that constructs `TelemetryConsentDialog`.

Supporting edits, all additive: the two rows the anti-drift contract requires in
`ControlReversibilityTable.cpp` (both `NotMutating`, with their reasons), the declaration + the shared
entry point in `include/ControlRegistry.h`, the one registration call in `ControlRegistry.cpp`, the
file in `src/core/CMakeLists.txt`, and two assertions plus one test slot in
`tests/src/core/ControlRegistryTest.cpp` (registration, the `human` declaration, the absence of
`requires` on `status`, and the typed `requires` refusal).

## 3. Both directions

**FORWARD — the action resolves, so the baseline is untouched.**

```
$ git diff HEAD --stat tests/agent-surface-baseline.txt      # (no output)
$ git diff HEAD -- tests/agent-surface-baseline.txt | wc -c  # 0
$ sha256sum tests/agent-surface-baseline.txt
4c6380b4c45ddb909e36566a7f5a8bcf22a6ad835e7502ec04f3aabe2fe5e398
$ git show HEAD:tests/agent-surface-baseline.txt | sha256sum
4c6380b4c45ddb909e36566a7f5a8bcf22a6ad835e7502ec04f3aabe2fe5e398
```

Byte-identical. No re-anchor, no new baseline line, `tests/agent-surface-exempt.txt` untouched
(`git diff HEAD` on it is empty). The reflection count moved exactly as it should — one action out of
the unregistered column and into the registered one:

| | before | after |
| --- | --- | --- |
| reflected actions | 46 | 46 |
| registered | 3 | **4** |
| unregistered | 43 (42 grandfathered, 1 new) | 42 (42 grandfathered, **0 new**) |
| baseline entries / stale | 42 / 0 | 42 / 0 |
| **registered commands** | **72** | **74** |
| swept / allowlisted | 72 / 0 | 73 / **1** |

**REVERSE — every declared command is accounted for.** `telemetry.status` is caught by the headless
sweep:

```
   telemetry.status             ok            0.0s  args={}
```

`telemetry.consent` is the first entry `tests/agent-surface-allowlist.txt` has ever carried, and it
is there for the reason that file exists: the command genuinely needs a display and a human, so the
sweep could only ever measure the typed refusal, never exercise the screen. The entry is 13 added
lines (the reasoned line plus the header paragraph that says why the file is no longer empty), and
the gate's rule is satisfied because the command **declares** a `requires` value.

## 4. The four exit codes

Every command below was run from the worktree root with its exit code read **unpiped**.

| command | exit | where |
| --- | --- | --- |
| `JOBS=2 bash tools/local-ci.sh --build-dir build --jobs 4` | **0** | `05-local-ci.log` |
| `python3 tests/agent-surface-gate.py build/zene tests/data/agent-control-fixture.mmp --check` | **0** | `03-gate-after.log` |
| `ctest --test-dir build/tests --output-on-failure` | **0** — `100% tests passed, 0 tests failed out of 86` | `07-ctest.log` (and `build/ctest.log`, from `local-ci.sh`'s own run) |
| `bash tests/release-honesty-gate.sh --header build/lmmsversion.h --artifacts build` | **0** — `6/6 PASS` | `06-honesty.log` |

`agent_surface` itself: `83/86 ... Test #83: agent_surface ... Passed 2.86 sec`, up from 85/86.
The honesty gate still reports all six documented features (`vst3-hosting`, `vst3-instrument-hosting`,
`clap-hosting`, `session-view`, `wasm-sandbox`, `stem-separation`) as what this build contains.

Ancillary gates that this change could have moved, all re-run and green
(`tests/integration-logs-3f-fix/gates/`):

| gate | exit | note |
| --- | --- | --- |
| `fork-sources-gate.sh` | 0 | 242 fork-NEW (was 241), 1036 inherited, 18 tooling, 0 stale |
| `unregistered-tests-gate.sh` | 0 | no test source left unregistered |
| `file-length-gate.sh --check` | 0 | new file is ~250 lines, under the 500-line ratchet |
| `complexity-gate.sh --check` | 0 | no regression; baseline not written |
| `duplication-gate.sh` | 0 | 1.44 % duplicated lines (budget 5 %) |

## 5. The render did not move

Nothing in this change is on an audio path, and it was measured rather than assumed:

```
$ bash tools/mmpz-git/render-recipe.sh data/projects/shorties/sv-DnB-Startup.mmpz \
      -o tests/integration-logs-3f-fix/render/render-1.wav --build-dir build
RENDER EXIT=0
943e323864cb5c07bb8da264736978be9294513b93c1b6b81421cb6d8a890526  render-1.wav
  chunk LIST   INFO ISFT = 'Zene Studio (libsndfile-1.2.2)'
  chunk data   size=2177024  payload sha256=b37cefc5a97e2d4664bbb0087a187935cdb9421030492f3b21972c3a7e3e59ca
```

`943e3238…` with `data` chunk `b37cefc5…` — the values `docs/INTEGRATION-MERGES-3B.md` says a train
after 3B should expect. No finding to report.

## 6. Manifests

`tests/fork-sources.txt` and `tests/all-sources.txt` each gained exactly one line,
`src/core/ControlCommandsTelemetry.cpp`, regenerated (not hand-edited) and re-verified with the
previous train's `tests/integration-logs-3f/tools/regen.py`: both print `REPRODUCES` with
`+0 -0`. `tests/upstream-modifications.txt` carries the amended reason for `src/gui/MainWindow.cpp`
in the same commit, as the ledger requires.

## 7. What was NOT done

* No `--reanchor`; `tests/agent-surface-baseline.txt` is byte-identical and
  `tests/agent-surface-exempt.txt` is untouched.
* No gate was weakened: no threshold moved, no test expectation edited, no allowlist entry without a
  declared `requires`, no new baseline line.
* No behaviour change to the telemetry feature itself: the consent dialog is the same dialog, the
  payload builder is the same builder, and `-DZENE_TELEMETRY=OFF` still builds the client out
  entirely (the command group answers a typed refusal instead of vanishing).
* Nothing pushed, nothing tagged, no remote touched, no rebase, no `git add -A`.

## 8. Residual notes (not blockers)

* **Coverage gate.** `tests/coverage-gate.sh` needs its own instrumented `build-coverage` tree, which
  this change did not build, so its ratchet was not re-measured here. The new file is exercised by
  `ControlRegistryTest` (registration + `telemetry.status`'s handler + the `requires` refusal) and by
  the gate's own sweep in `agent_surface`, so it should enter above the 50 % floor; that is a claim
  for whoever next refreshes the coverage baseline, not a measurement made here.
* **A `telemetry-off` configure is worth a future run.** Both handlers' `#else` branches compile only
  with `-DZENE_TELEMETRY=OFF`, and no CI job sets that flag today. The branches are covered by
  inspection, not by a build.
