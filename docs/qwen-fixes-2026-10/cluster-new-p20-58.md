FIXED

# Cluster new-p20-58: fallback Link sync beat resets on re-enable

## Verdict

FIXED. Disabling and re-enabling the fallback Link sync model restarted the shared beat at 0. It now keeps the current beat and advances the declaration revision.

## Evidence

- **Old behaviour.** `git show HEAD:src/core/LinkSync.cpp` lines 203-206 zeroed the anchor on every enable, unconditionally: `m_anchorUs = monotonicMicros();`, `m_anchorBeat = 0.0;`, then `declareTempo(base, m_anchorUs);`. A re-enable therefore discarded the beat.
- **Contract.** `docs/LINK-SYNC.md:150-156` says `anchorUs` is re-anchored at every tempo change *before* the change takes effect, so beats are continuous across it and no peer's phase jumps. A re-enable is a rejoin, not a fresh session, so the same continuity applies.
- **Red run.** `test-2-red.log` (EXIT=8): `reEnablingKeepsTheSharedBeat` FAILS with `'afterBeat > beforeBeat - 0.5' returned FALSE (beat before re-enable was 8.0024, after re-enable is 0.000296)`. `test-4-red-reversed.log` (EXIT=8) shows the same failure with the fix reversed (beat 0.000282).
- **Green runs.** `test-4-green-before-reverse.log` (EXIT=0), `test-4-green-restored.log` (EXIT=0), and `test-5-family.log` (EXIT=0) show `ControlLinkCommandsTest` passing, including `reEnablingKeepsTheSharedBeat`.

## The fix

`src/core/LinkSync.cpp:196-209` (in `LinkSyncEngine::setEnabled`, enable branch):

- The unconditional `m_anchorUs = monotonicMicros(); m_anchorBeat = 0.0;` is removed.
- The anchor is left alone; `declareTempo(base, now)` rebases it to now, so the beat carries across the rejoin.
- Only a first join (`m_tempo == 0`, `m_anchorUs == 0`) begins at beat 0. That path is the existing `declareTempo` behaviour on a fresh engine.
- `m_appliedTempo = -1.0` is kept, so the declaration still writes through.
- `declareTempo` already bumps the revision (`m_revision = highestSeenRevision() + 1`), so a re-enable advances the revision.

No audio-thread code is touched. The change is inside a UI-thread control path (`setEnabled`), and it adds no new work to the audio path.

## Regression test

`tests/src/core/ControlLinkCommandsTest.cpp`, `reEnablingKeepsTheSharedBeat()` (line 386).

Why it is deterministic:
- The peer packet is injected through `observe()` → `observePacket`, with `packet.sentUs = LinkSyncEngine::monotonicMicros() - 1000`, so the peer's beat is stamped from the same monotonic clock as the read. Its beat is about 8.0 one millisecond ago.
- It uses the registry surface only (`setEnabledForTest`, `run("link.set_enabled")`, `state()`), so no socket or multicast is needed.
- There are no sleeps. Assertions are on values read through `state()`: beat before > 7.0, beat after > before − 0.5, revision after > before.

## Test matrix

| Behaviour | Covered by | Result in this run |
|---|---|---|
| Disable and re-enable preserves ~8 beats and advances the revision | `reEnablingKeepsTheSharedBeat` | PASS (`test-4-green-restored-verbose.log`, `test-5-family.log`); FAIL without fix (`test-2-red.log`, `test-4-red-reversed.log`) |
| Adopting a peer's shared-beat phase | `adoptionPlacesTheSharedBeatPhase` | PASS |
| First enable on a fresh engine starts at beat 0 | No named test passes for this. `enablingJoinsAndDisablingLeaves` covers join and leave, and it is SKIPPED on this host (see below). | **Not verified by a passing test in this run** |
| Disable only drops peers (does not touch the beat) | No named test | **Not covered by a test in this run** |
| UI-thread only, no new audio work | Diff review: only `setEnabled` changed; no audio code | Verified by diff, not by a test |
| Fallback backend pinned by ctest ENV | Not checked in this run | **Unverified** |

Within `ControlLinkCommandsTest`, `enablingJoinsAndDisablingLeaves` is SKIPPED (not passed). The skip reason is: "this platform cannot carry announcements; the model is still covered here" (`test-4-green-restored-verbose.log`, `test-3-green.log`). The run reports `15 passed, 0 failed, 1 skipped`.

## Commands and exit codes

Every EXIT code below appears in the named log.

| Step | Command | Log | EXIT |
|---|---|---|---|
| build-1 | `cmake --build build --target ControlLinkCommandsTest` | `build-1.log` | 0 |
| test-1 | `ctest -R '^ControlLinkCommandsTest$'` (from `build/tests`) | `test-1.log` | 0 |
| test-2-red | ctest with the fix reverted | `test-2-red.log` | 8 (expected red) |
| build-3 | build (packet label; the log does not say what state it built) | `build-3.log` | **no EXIT line in log** (ends "Built target ControlLinkCommandsTest") |
| test-3 | `ctest` green (`test-3-ctest-green.log`), `test-3-green.log`, `test-3-family.log` | (as named) | **no EXIT line in these logs**. Results: 100% passed for ctest; `15 passed, 0 failed, 1 skipped`; ControlLinkSync skipped |
| p4 green | build | `build-4-green.log` | 0 |
| p4 green | test | `test-4-green-before-reverse.log` | 0 |
| p4 reversed | build | `build-4-reversed.log` | 0 |
| p4 reversed | test | `test-4-red-reversed.log` | 8 (expected red) |
| p4 restored | build | `build-4-restored.log` | 0 |
| p4 restored | test | `test-4-green-restored.log`, `test-4-green-restored-verbose.log` | 0 |
| p4 family | `ctest -R '^(ControlLinkCommandsTest\|ControlLinkSync)$'` | `test-4-family-verbose.log` | 0 |
| p5 gates | `feature_check.py <worktree>` | `gates-1.log` | 0 (`RESULT: PASS`) |
| p5 family | `cd build/tests && ctest -R '^(ControlLinkCommandsTest\|ControlLinkSync)$' --output-on-failure` | `test-5-family.log` | 0 |

**p4 `git apply --check` EXIT=1.** The p4 check of `logs/fix.patch` returned 1 because the patch is already applied. The error is `patch failed: src/core/LinkSync.cpp:196`. The fix is present in the tree, so a forward check cannot apply. This check was not in a p4 log. It was re-run in p5 and saved to `logs/apply-check-p5.log`:
- `git apply --check logs/fix.patch` → EXIT=1 (already applied)
- `git apply -R --check logs/fix.patch` → EXIT=0 (reverse applies, so the fix is present)

**Gates (`gates-1.log`).** `fork-sources`, `all-sources-reproduce`, `no-upstream-regression`, `check-namespace`, `no-tautology`, `doc-figures`, `complexity` (no new item in your files), and `file-length` (no new item in your files) all PASS. `RESULT: PASS`, EXIT=0.

**Measured ControlLinkSync skip.** `test-4-family-verbose.log:58` (and `test-5-family.log`):
`908: SKIP: A-leader cannot carry announcements: 224.76.78.75 cannot be received on this host: a datagram sent to 224.76.78.75:20808 was not read by a SECOND socket here that bound the port and join...`
This is a host multicast limitation, not a test failure. ControlLinkSync is **skipped, not passed**.

## Self-check

- Product change limited to `src/core/LinkSync.cpp` (setEnabled enable branch). Verified by `git diff --stat` (2 files plus the test).
- Test file change is one new test function, `reEnablingKeepsTheSharedBeat`, with no other test edited. Verified by `git diff`.
- No `src/gui` change, so no screenshot is required.
- No control-command addition, so the `ControlCommandsSnapshot` canary is not moved.
- `LinkSync.cpp` and the test file are already registered in `tests/fork-sources.txt`. `fork-sources` gate PASS in `gates-1.log`.
- Gates complexity and file-length: PASS (no new items), `gates-1.log`.
- The fix was verified red→green (`test-2-red.log` EXIT=8, `test-4-green-restored.log` EXIT=0).
- Open items, not verified in this run: first-enable-at-zero has no passing test; disable-only-drops-peers has no test; the fallback ENV pin was not checked; ControlLinkSync skipped on this host (multicast).
