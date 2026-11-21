FIXED
# cluster new-p16-329: Link tempo clamp let NaN through

Verdict: REAL. A NaN tempo passed through `clampTempo` unchanged, so the timeline carried a NaN tempo. The fix is a local Zene patch to the vendored Ableton Link header.

## Defect and before-fix code

Before the fix, `src/3rdparty/ableton-link/link/include/ableton/link/ClientSessionTimelines.hpp:31-38` was:

```cpp
inline Timeline clampTempo(const Timeline timeline)
{
  const double kMinBpm = 20.0;
  const double kMaxBpm = 999.0;
  return {Tempo{(std::min)((std::max)(timeline.tempo.bpm(), kMinBpm), kMaxBpm)},
          timeline.beatOrigin,
          timeline.timeOrigin};
}
```

The min/max NaN semantics, as implemented by `std::max(a,b) = (a<b)?b:a` and `std::min(a,b) = (b<a)?b:a`:

- NaN: `max(NaN,20)` returns NaN (`NaN<20` is false), and `min(NaN,999)` returns NaN (`999<NaN` is false). The NaN passes through. This is the defect.
- +inf: `max(inf,20)=inf`, then `min(inf,999)=999`. Pre-fix, +inf clamped to 999.
- -inf: `max(-inf,20)=20`, then `min(20,999)=20`. Pre-fix, -inf clamped to 20.

So the only pre-fix failure is NaN. ±inf were already finite after the clamp.

## Fix

```cpp
const double kDefaultBpm = 120.0;
const double bpm = timeline.tempo.bpm();
const double clampedBpm = std::isfinite(bpm)
                              ? (std::min)((std::max)(bpm, kMinBpm), kMaxBpm)
                              : kDefaultBpm;
return {Tempo{clampedBpm}, timeline.beatOrigin, timeline.timeOrigin};
```

Finite tempos use the same min/max expression as before, so finite results are unchanged. Non-finite tempos (NaN, +inf, -inf) now fall back to 120 BPM.

**Behavior change for ±inf (flagged):** pre-fix, +inf gave 999 and -inf gave 20. Post-fix, both give 120. This is the fallback the packet specified. It is a deliberate change, not a side effect.

## Call sites

`clampTempo` is used in these places:

- `src/3rdparty/ableton-link/link/include/ableton/link/Controller.hpp:56`: `initSessionState`
- `Controller.hpp:214`: `setClientState`, thread-safe/blocking path
- `Controller.hpp:277`: `setClientStateRtSafe`, audio-thread path
- `Controller.hpp:343`: `updateSessionTiming`
- `src/3rdparty/ableton-link/link/include/ableton/Link.ipp:205`: `SessionState::setTempo`

## Realtime argument

The new code adds only `std::isfinite` and the existing two comparisons. It does no allocation, takes no lock, does not wait, and does not log. `setClientStateRtSafe` (Controller.hpp:268-277) gets only that `isfinite` check on the audio thread.

## Collateral change

- The finite clamp expression is unchanged.
- No save/load format is touched.
- Only the NaN case and ±inf change behavior. ±inf is covered by the ±inf behavior change above.

## Regression test

Test: `AbletonLinkAllocatorTest::clampTempoStaysWithinTheLinkRange` in `tests/src/core/AbletonLinkAllocatorTest.cpp`, added in this cluster.

Why this target: `AbletonLinkAllocatorTest` is a pure-header Link test target that already links `ableton_link`, so it needs no reconfigure. `AbletonLinkSessionTest` was not used: its pre-existing `joinBoth` QSKIP-in-helper segfaults on a host without multicast (see Other findings).

Frozen test matrix:

| input | expected BPM | notes |
|---|---|---|
| 20.0 | 20.0 | lower bound |
| 120.0 | 120.0 | identity |
| 999.0 | 999.0 | upper bound |
| 19.999 | 20.0 | clamped up |
| 999.001 | 999.0 | clamped down |
| NaN | 120.0 | fallback |
| +inf | 120.0 | fallback |
| -inf | 120.0 | fallback |

Every case also checks that `beatOrigin` stays at 3.5 and `timeOrigin` is preserved. Finite cases check `isfinite`. Non-finite cases check that `toBeats(1 s)` is finite.

## Gate evidence (logs in `state/runs/new-p16-329.r1/logs/`)

Each step below ran as `bash -c '<cmd>; rc=$?; echo EXIT=$rc; exit $rc'`, so the exit code is the last `EXIT=` line inside the named log. The `SHELL_EXIT` from the outer shell matched in every step.

| step | log | EXIT in log | result |
|---|---|---|---|
| reverse fix patch | `patch-reverse-3.log` | 0 | `git apply -R logs/fix.patch` |
| build RED | `build-red-reverse-3.log` | 0 | built with the pre-fix header |
| test RED (slot) | `test-red-reverse-3.log` | 1 | `Totals: 2 passed, 1 failed`; `FAIL! ... clampTempoStaysWithinTheLinkRange()`; `Actual (clamped.tempo.bpm()): nan`, `Expected (120.0)`, at `AbletonLinkAllocatorTest.cpp(159)` |
| restore fix patch | `patch-restore-3.log` | 0 | `git apply logs/fix.patch` |
| build GREEN | `build-green-restore-3.log` | 0 | built with the fixed header |
| test GREEN (slot) | `test-green-restore-3.log` | 0 | `Totals: 3 passed, 0 failed` |
| test family GREEN | `test-family-green-3.log` | 0 | `Totals: 7 passed, 0 failed` |
| ctest | `ctest-green-3.log` | 0 | `100% tests passed out of 1` |
| header check | `cmp` of the header vs `logs/ClientSessionTimelines.fixed.hpp` | 0 | identical (run after the last test, before the report commit) |

The RED step (`test-red-reverse-3.log`, EXIT=1) shows the test catches the defect: restoring the pre-fix header fails it. The GREEN steps show the fixed header passes.

Logs from earlier runs (`*-2`, `*-3` before this packet) are not cited: their exit codes were never recorded inside the logs, so they are not evidence here.

## Gate table

| gate | command | exit | result |
|---|---|---|---|
| feature_check | `feature_check.py <worktree> > logs/feature-check-2.log 2>&1` | 0 | `RESULT: PASS` (complexity, file-length, and the other checks PASS) |
| commit | `git add` (4 paths) and `git commit` | see commit | see COMMITS |
| submit_check (earlier) | `submit_check.py <worktree> new-p16-329 > logs/submit-check-3.log 2>&1` | 0 | `RESULT: PASS - submit`; no FAIL; six `?` asks answered under "submit_check asks" |
| submit_check (final) | `submit_check.py <worktree> new-p16-329 > logs/submit-check-5.log 2>&1` | see final message | run after the report commit; the exit and clean-tree check are in the final message, not in this file |

## Commit chain

On `bugs/qwen-new-p16-329`, oldest first:

- `f461196b76` cluster new-p16-329: clamp Link tempo to a finite range (the fix and test)
- `269ec0ae57` cluster new-p16-329: answer submit_check asks in report
- this report commit: cluster new-p16-329: record RED/GREEN exits in the report (its sha is the HEAD in `git log`)

## Self-check

The submit_check asks, in order:

1. **Clean tree.** Before the final commit, the worktree held only the 4 files this cluster changes. After the report commit, `git status --short` is empty; the final check is `logs/submit-check-5.log` and the final message.
2. **Commits on the branch.** The commit is `cluster new-p16-329: clamp Link tempo to a finite range` on `bugs/qwen-new-p16-329`.
3. **Report verdict in the first lines.** Line 1 is `FIXED`. Line 3 is `Verdict: REAL`.
4. **Self-check section.** This section.
5. **Tests changed.** `tests/src/core/AbletonLinkAllocatorTest.cpp` adds `clampTempoStaysWithinTheLinkRange`, and the ledger entry is in `tests/upstream-modifications.txt`.
6. **feature_check exit 0.** `feature-check-2.log` ends with `RESULT: PASS`, and the run exited 0.

### submit_check asks

Answers to the six `?` asks from `submit_check.py` (log: `logs/submit-check-3.log`, `RESULT: PASS - submit`, exit 0):

1. **Regression test fails with the change reverted and passes with it (exit codes).** With the pre-fix header (`patch-reverse-3.log`, `build-red-reverse-3.log`), `test-red-reverse-3.log` ends `EXIT=1` with `Totals: 2 passed, 1 failed`; the failure is the NaN `QCOMPARE` at `AbletonLinkAllocatorTest.cpp(159)` (`Actual (clamped.tempo.bpm()): nan`). With the fix restored (`patch-restore-3.log`, `build-green-restore-3.log`), `test-green-restore-3.log` ends `EXIT=0` with `Totals: 3 passed, 0 failed`, `test-family-green-3.log` ends `EXIT=0` with `Totals: 7 passed, 0 failed`, and `ctest-green-3.log` ends `EXIT=0`. A `cmp` of the header against `logs/ClientSessionTimelines.fixed.hpp` is identical (`CMP_EXIT=0`).
2. **Deterministic.** The test has no clock, sleep, thread, or I/O. Its inputs are fixed values: the `Tempo` values in the matrix above, `Beats{3.5}`, and `timeOrigin = 250000 µs`. The only arithmetic is `toBeats(1000000 µs)`.
3. **Fix at the cause and complete.** The defect is in `clampTempo` alone. Every caller goes through it: `Controller.hpp:56, 214, 277, 343` and `Link.ipp:205`. A grep for `clampTempo` under `src/3rdparty/` finds only the three Link files (`ClientSessionTimelines.hpp`, `Controller.hpp`, `Link.ipp`). Not checked: other code that builds a `Tempo` without going through `clampTempo`.
4. **No collateral damage.** The finite path is unchanged, so finite inputs give the same results as before. The only changed results are for non-finite tempos: NaN becomes 120 (was NaN), and ±inf becomes 120 (was 999 / 20). No save/load format is touched. No limit or check was added. A saved NaN tempo would now load as 120 BPM; that is the intended effect of the fix.
5. **Verdict is REAL, not ALREADY-FIXED or FALSE-POSITIVE.** The pre-fix code at `ClientSessionTimelines.hpp:31-38` passes NaN through `std::max`/`std::min`, and the pre-fix test run fails on it (see the NaN semantics above).
6. **Sibling commands.** No command files were changed. `git show --stat HEAD` lists only `cluster-new-p16-329.md`, `ClientSessionTimelines.hpp`, `AbletonLinkAllocatorTest.cpp`, and `upstream-modifications.txt`. The `app.*` commands are not affected.

## Other findings (not fixed)

- `AbletonLinkSessionTest`: its pre-existing `joinBoth` QSKIP-in-helper segfaults on a host without multicast. Not fixed, out of scope. Not verified here; taken from the packet.
- Carla copy: `plugins/CarlaBase/carla/source/modules/hylia/link/ableton/link/ClientSessionTimelines.hpp` is a second copy of this header. `git ls-tree HEAD plugins/CarlaBase/carla` shows `160000 commit 66afe24a08`, so `plugins/CarlaBase/carla` is a submodule gitlink, and the header sits inside that submodule. The Carla copy was not built and not fixed by this cluster. Its contents were not inspected here.
- `src/3rdparty/ableton-link/README.lmms:66` says "Local modifications: two, deliberate, both in the ESP32 port". That count is stale for both the existing asio allocator patch and the new `ClientSessionTimelines.hpp` patch from this cluster. Not edited; the lead should decide whether to update it.
