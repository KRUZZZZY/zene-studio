FALSE-POSITIVE

# Cluster new-p21-19: link.audio_set_enabled "disables the stream when 'enabled' is omitted"

Finding: `src/core/ControlCommandsLinkAudio.cpp:105` (`mergeArgs` sets `c.enabled` from `args.value("enabled")`, which is false when the key is absent).

## Decision

The reported behaviour cannot happen through the control surface, so there is no defect to fix and no product code changed.

1. **Validation refuses the call before the handler.** `ControlRegistry::invoke` runs `validateArgs(cmd->argsSchema, args)` and returns `InvalidArgs` on failure (`src/core/ControlRegistry.cpp:277-281`). The handler is only taken from the command afterwards (`:293`) and called at `:449` (`return handler(args);`). That is the only call of the handler in `src/`.
2. **The schema requires `enabled`.** `src/core/ControlCommandsLinkAudio.cpp:180` passes `{QStringLiteral("enabled")}` as the required list of `link.audio_set_enabled`. `src/core/ControlSchema.cpp:154-161` rejects an object that lacks a required member with `missing required property '<name>'`.
3. **The docs agree.** `docs/LINK-AUDIO.md:80` lists `enabled` without the `?` that marks the optional arguments (`peer?`, `send?`, ...).

So a call with only `peer` and `latency_ms` is refused with `missing required property 'enabled'`. `mergeArgs` (`ControlCommandsLinkAudio.cpp:102-115`) never runs, and the running stream is not touched. The regression test below pins this.

**Open wording conflict (not changed, owner decision):** the command description (`src/core/ControlCommandsLinkAudio.cpp:168`) and `docs/LINK-AUDIO.md:84` say "Arguments not given keep their (current) value", which read literally covers `enabled`. The schema and the signature say `enabled` is required. The finding's proposed fix (assign `enabled` only when present) only has an effect if `enabled` is made optional. That is a contract change: it alters `args_schema` for `link.audio_set_enabled` in `ControlCommandsSnapshot` and the docs, so it is not made here. If the owner wants `enabled` optional, the fix is the one the finding proposes plus the schema and doc change, and the RED test would then be possible.

## Regression test (characterization, not RED)

No RED is possible for the reported symptom, because the path is unreachable without changing the contract. Rather than invent a behaviour change, I added a guard that pins the refusal:

- `tests/src/core/LinkAudioStreamTest.cpp:602-620`, `LinkAudioStreamTest::omittedEnabledIsRefusedAndTheRunningStreamKeepsRunning`. It starts the stream, sends `peer` and `latency_ms` without `enabled`, asserts `!ok`, asserts the error contains `missing required property 'enabled'`, asserts the stream is still `running`, then stops it. It uses no timing or sleeps (listen_port 0, loopback as the existing tests do).
- The test is in an existing file, so no registration in `tests/fork-sources.txt` or `tests/upstream-modifications.txt` is needed.

## Commands and exit codes

| # | Command (run in the worktree) | EXIT | Log |
| --- | --- | --- | --- |
| 1 | `cmake --build build --target zene LinkAudioStreamTest -j4` | 2 | `build-1.log` (ccache: `failed to create temporary file ... Permission denied` in the shared ccache temp dir; not a compile error) |
| 2 | `CCACHE_DISABLE=1 cmake --build build --target zene LinkAudioStreamTest -j4` | 0 | `build-2.log` |
| 3 | `cd build/tests && ctest -R LinkAudioStreamTest --output-on-failure` | 0 | `test-1.log` (`100% tests passed out of 1`) |
| 4 | `cd build/tests && ./LinkAudioStreamTest omittedEnabledIsRefusedAndTheRunningStreamKeepsRunning -v2` | 0 | `test-2.log` (`PASS` for the new slot; `Totals: 3 passed, 0 failed`) |
| 5 | `bash tests/fork-sources-gate.sh` | 0 | `gate-fork-sources.log` |
| 6 | `bash tests/all-sources-reproduce.sh` | 0 | `gate-all-sources-reproduce.log` |
| 7 | `bash tests/no-upstream-regression-gate.sh` | 0 | `gate-no-upstream-regression.log` |
| 8 | `tests/scripted/check-namespace` | 0 | `gate-check-namespace.log` |
| 9 | `bash tests/no-tautology-gate.sh` | 0 | `gate-no-tautology.log` |
| 10 | `python3 tests/doc-figures-gate.py` | 0 | `gate-doc-figures.log` |
| 11 | `bash tests/complexity-gate.sh --check` | 1 | `gate-complexity.log` (red on the train on purpose; no `REGRESSION` line names `LinkAudioStreamTest.cpp`) |
| 12 | `bash tests/file-length-gate.sh --check` | 1 | `gate-file-length.log` (red on the train on purpose; no `REGRESSION` line names `LinkAudioStreamTest.cpp`) |
| 13 | `python3 .../qwen/feature_check.py <worktree>` | 1 | `gates-1.log`: `PermissionError` creating `<worktree>-feature-check` outside the write-jail, so the script did not run its checks |

Commands 5-12 are the commands `feature_check.py` runs, executed one by one because the script could not run. Its own PASS/FAIL table was not produced. Its ratchet rule (a red item only fails if it is new and lies in a changed file) was applied by hand: neither ratchet log names the changed file, so there is no new item in changed files. I did not compare against the cached base listing.

Affected-family count: `ctest -R LinkAudioStreamTest` executed 1 test, 1 passed. The product files are unchanged, so no other family is affected.

## Files committed

- `tests/src/core/LinkAudioStreamTest.cpp` (new characterization slot)
- `docs/qwen-fixes-2026-10/cluster-new-p21-19.md` (this report)

No product source changed. No control command was added and the `ControlCommandsSnapshot` canary is untouched. The audio thread is not involved (the change is in a test file only).

## Notes

- The GUI entry "Link Audio Peer..." (`src/gui/LibraryCommandMenus.cpp:218`) runs `link.audio_set_enabled` with no fixed arguments and `ask=true`. Whether its form includes `enabled` was not checked, so this is unverified. If the form omits it, the user would see the same `missing required property 'enabled'` refusal.
