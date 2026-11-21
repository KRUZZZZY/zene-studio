FIXED — cluster new-p14-173: MIDI clock counters become negative in state JSON

# Cluster new-p14-173 — MIDI clock counters become negative in state JSON

**Verdict: FIXED** (Luna patrol finding "BUG 9", `src/core/MidiClockState.cpp:59`, confidence high, reported 3 times).

## Finding

The clock state reply (`clock.get_state`) narrowed unsigned 32-bit session counters to
`int`. Above `INT_MAX` (2,147,483,647) a count came out negative. The finding named
line 59 (per-message counts) and line 73 (`emitted_total`).

## Evidence (file:line, pre-fix tree at HEAD `2da2ed356`)

- `src/core/MidiClockState.cpp:59` — `messageCountsJson()` did `static_cast<int>(counter(message))`. It is one template used for BOTH sides: the master's `emitted` (`:75`, `emittedCount`) and the follower's `messages` (`:112`, `tracker.messageCount`).
- `src/core/MidiClockState.cpp:73` — `master.emitted_total` did `static_cast<int>(emittedTotal())`.
- `src/core/MidiClockState.cpp:98` — `slave.ignored` did `static_cast<int>(slaveIgnoredCount())`. Same defect class, not named in the finding; fixed as a sibling.
- The narrowed values are unsigned: `include/MidiClock.h:427` `std::atomic<quint32> m_emittedTotal`, `:429` `m_emittedCounts`, `:463` `m_ignoredTotal`, and `:193` (tracker) `m_messageCounts`. All are `quint32`.
- Grep for every consumer of these counters (`src`, `include`): the only narrowing sites are `MidiClockState.cpp:59`, `:73`, `:98`. `stateJson()` is called only by `src/core/ControlCommandsClock.cpp:75,80,85` (command reads). Nothing persists it, so saved projects are not affected.
- Mechanism, shown by the RED run: `static_cast<int>(3000000000u)` gives `-1294967296` (= 3000000000 − 2³²). The reply reports exactly that.

## Fix (the cause, all three narrowing sites)

`src/core/MidiClockState.cpp`:

1. `:59` `static_cast<int>(counter(message))` → `static_cast<qint64>(counter(message))` (with a comment on why).
2. `:73` `static_cast<int>(emittedTotal())` → `static_cast<qint64>(emittedTotal())`.
3. `:98` `static_cast<int>(slaveIgnoredCount())` → `static_cast<qint64>(slaveIgnoredCount())`.

Why this is behaviour-preserving for every in-range value: `include/qt6/QtCore/qjsonvalue.h:53-55` gives `QJsonValue` only `double`, `int` and `qint64` constructors, and its `enum Type` (`:32`) has `Double = 0x2` and no integer type. An in-range number already serializes through the same Double, so the reply text is unchanged for values below `INT_MAX`.

## Test (regression, deterministic)

`tests/src/core/MidiClockSourceTest.cpp` — new slot `stateCountersAboveInt32MaxAreNotNegative()`, placed beside the other state-reply test (its item 7 already covers `slave.ignored`). It sets the value 3,000,000,000u (above `INT_MAX`, below `UINT_MAX`) into every slot of each counter array and into `m_emittedTotal` / `m_ignoredTotal`, calls `stateJson()`, serializes it to text and parses it back (the form a client reads), then restores the singleton's counters BEFORE any `QCOMPARE`, so an early return cannot leak state into later slots. Assertions: `emitted_total`, all six `emitted` entries, `slave.ignored`, and all six `messages` entries equal 3e9.

Seam: `include/MidiClock.h` adds `friend class ::MidiClockSourceTest;` under `#ifdef LMMS_TESTING` in `MidiClock` and `MidiClockTracker`, with a forward declaration. This is the same pattern as `include/AutomatableModel.h:42` and `:615`. A friend declaration does not change layout, and no public API changes. Deterministic: no sleeps, no clocks, no threads; the audio device is stopped in `initTestCase` as in the other slots of this file.

Why this file and not a new one: `MidiClockTest.cpp` is at the file-length ratchet (see Self-check). A first version in a new file needed a CMake change, which requires a reconfigure, and the lane build tree is configured by the parent (`bunny-guard`). The test therefore lives in the existing registered `MidiClockSourceTest`, which needs no CMake or manifest change.

## Commands run and exit codes

Logs are in the LOG FOLDER `new-p14-173.r1/logs/`. "EXIT" is the unpiped exit status.

| Step | Command (from the worktree / `build/`) | Log | EXIT |
| --- | --- | --- | --- |
| build attempt, failed in ccache (temp dir under `/run/user/1000` denied) | `cmake --build build --target MidiClockTest -j2` | `build-1-red.log` | 2 |
| build (ccache temp dir redirected to /tmp) | `CCACHE_TEMPDIR=/tmp/ccache-tmp-p14-173 cmake --build build --target MidiClockTest -j2` | `build-2-red.log` | 0 |
| RED at first location (test in `MidiClockTest.cpp`, no fix): `Totals: 20 passed, 1 failed` | `cd build/tests && ctest -R MidiClockTest --output-on-failure` | `test-1-red.log` | 8 |
| RED message: `Actual -1294967296`, `Expected 3000000000`, `Loc: MidiClockTest.cpp(683)` | (same run) | `test-1-red.log` | |
| step 4b: reverse the fix only (`fix.patch` = `git diff -- src/core/MidiClockState.cpp`) | `git apply -R <logs>/fix.patch` | — | 0 |
| step 4b build, unfixed | `cmake --build build --target MidiClockSourceTest -j2` | `build-9-4b-unfixed.log` | 0 |
| **RED, final location** (unfixed) | `cd build/tests && ./MidiClockSourceTest stateCountersAboveInt32MaxAreNotNegative` | `test-11-4b-red.log` | **1** |
| RED message, final location | (same run) `FAIL!: stateCountersAboveInt32MaxAreNotNegative()`, `Actual -1294967296`, `Loc: MidiClockSourceTest.cpp(524)` | `test-11-4b-red.log` | |
| step 4b: reapply the fix | `git apply <logs>/fix.patch` | — | 0 |
| step 4b build, fixed | `cmake --build build --target MidiClockSourceTest -j2` | `build-10-4b-refixed.log` | 0 |
| **GREEN, final location** (fixed) | `cd build/tests && ./MidiClockSourceTest stateCountersAboveInt32MaxAreNotNegative` | `test-12-4b-green.log` | **0** (`PASS : ...stateCountersAboveInt32MaxAreNotNegative()`) |
| GREEN at first location, for the record | `./MidiClockTest stateCounters...` (before the move) | `test-5-refixed-slot.log` | 0 |
| build of every includer after the header seam and move | `cmake --build build --target zene MidiClockTest MidiClockSourceTest MidiClockMenuTest -j2` | `build-8-fixed-in-sourcetest.log` | 0 |
| family: `MidiClockTest` (#106), `MidiClockSourceTest` (#107), `MidiClockMenuTest` (#418) | `cd build/tests && ctest -R MidiClock --output-on-failure` | `test-10-green-family.log` | **0**, `100% tests passed, out of 3` |
| live consumer: real binary over the control socket, reads `emitted_total` / `ignored` | `ctest -R "^ControlClockCommands$" --output-on-failure` (after `cmake --build build --target zene -j2`, `build-11-zene-final.log` EXIT 0) | `test-13-controlclock.log` | **0**, `1/1` |

Tests executed: the family runs 3 ctest entries (3 of 3 passed). `ctest -R MidiClock` runs every QTest slot
of `MidiClockSourceTest`, which includes the new slot; the single-slot run above is the targeted proof.

## Gates

`feature_check.py` could not run as written. It writes its per-gate logs to
`<worktree>/../zene-qwen-new-p14-173-feature-check/`, which is outside the write-jail, so it stops
with `PermissionError: [Errno 13]` before any gate (the traceback is in `gate-feature-1.log`). I ran the same eight gate commands from the worktree, logged to the
LOG FOLDER, with unpiped exit codes (`gates-direct-2.txt`):

| gate | command | EXIT |
| --- | --- | --- |
| fork-sources | `bash tests/fork-sources-gate.sh` | 0 |
| all-sources-reproduce | `bash tests/all-sources-reproduce.sh` | 0 |
| no-upstream-regression | `bash tests/no-upstream-regression-gate.sh` | 0 |
| check-namespace | `tests/scripted/check-namespace` | 0 |
| no-tautology | `bash tests/no-tautology-gate.sh` | 0 |
| doc-figures | `python3 tests/doc-figures-gate.py` | 0 |
| complexity (ratchet) | `bash tests/complexity-gate.sh --check` | 1 — see below |
| file-length (ratchet) | `bash tests/file-length-gate.sh --check` | 1 — see below |

Both ratchets are red on the train, on purpose. Their REGRESSION lines (`gate2-complexity.log`,
`gate2-file-length.log`) name other files (`XyPadPairTest.cpp`, `DawProjectReadTracks.cpp`, `TakeLane.cpp`, …).
`grep MidiClock` over both logs returns nothing, and this branch changed three files: `include/MidiClock.h`,
`src/core/MidiClockState.cpp`, `tests/src/core/MidiClockSourceTest.cpp`. So no new item is in a file this branch
changed, which is the condition `feature_check` uses for PASS. The baseline comparison itself (which needs a
checkout of the base outside the jail) I could not run; this is a reading of the output, not the script's table.

The first attempt put the test in `MidiClockTest.cpp`, and `file-length` then reported
`new file over 500 lines: tests/src/core/MidiClockTest.cpp (536)`, a real regression in a changed file (by my
local count, its base was about 487 code lines). That is why the test moved to `MidiClockSourceTest.cpp`.

## Not changed (residuals, for the owner)

1. **The unsigned counters still wrap at 2³².** `m_emittedTotal` and the count arrays are `quint32`
   (`include/MidiClock.h:427-429,463`, tracker `:193`). The fix reports every value up to 2³²−1 correctly, but
   above that the stored count wraps to 0. At 48 clock messages per second (120 BPM, 24 pulses per quarter) that is
   about 2³²/48 s ≈ 2.8 years of continuous clock. Widening the storage changes public accessor types
   (`emittedCount`, `messageCount`, `slaveIgnoredCount` return `quint32`), which the tests compare with `1u`, so it is a
   larger change than this finding's stated fix. Not done here.
2. **`pulses` is a signed `int`.** `MidiClockState.cpp:106` reports `tracker.pulseCount()`, which is
   `int` (`include/MidiClock.h:160`, storage `m_pulseCount` `std::atomic<int>` at `:187`). It is cleared by
   `MidiClockTracker::reset()` (`src/core/MidiClockTracker.cpp:109-116`), which runs from `MidiClock.cpp:287` and `:328`. It is
   per-follow-run, not session-long. Reaching INT_MAX takes about 518 days of unbroken pulses at 48 per second. The
   tests compare it as `int` (`MidiClockTest.cpp:245`, `MidiClockSourceTest.cpp:206`), so widening it is also an API change. Not
   changed.

## Build note

Every build in this lane ran with `CCACHE_TEMPDIR=/tmp/ccache-tmp-p14-173`, because ccache's default temp dir
under `/run/user/1000` is not writable from this lane (`build-1-red.log`: `ccache: error: failed to create temporary
file ... Permission denied`). No repository file changed for this.

## Self-check

`submit_check.py` was run twice. Before the commit (`submit-check-1.log`, EXIT 1) it failed six items. After
the commit with this report (`submit-check-2.log`, EXIT 1) five pass: commits on the branch, working tree clean,
report present, tests changed (`MidiClockSourceTest.cpp`). The one FAIL is `gates (feature_check.py)`, which is
blocked by the write-jail (see Gates) and not by a gate result. The items it asks for are answered below:

- **Regression test FAILS with the product change reverted and PASSES with it.** Yes. Reverted: `test-11-4b-red.log`, EXIT 1, `FAIL!` with `Actual -1294967296`. Fixed: `test-12-4b-green.log`, EXIT 0. The reversal patch is `fix.patch` = `git diff -- src/core/MidiClockState.cpp` (the seam header is kept, because removing it stops the test compiling, which proves nothing). The first-location RED is `test-1-red.log` (EXIT 8).
- **Deterministic: no wall-clock timing, sleeps or races.** Yes. It uses the `LMMS_TESTING` friend seam to write the counters, computes `stateJson()` on the test thread, and runs no writer thread. The audio device is stopped in `initTestCase`, so no period runs during the probe. The singleton is restored before any assertion.
- **The fix is at the cause and complete; every caller and sibling checked.** The three narrowing sites are all of them (grep above). `messageCountsJson` is one template for both the master and follower sides, so both are fixed by one change; `ignored` is fixed as a sibling. Residuals 1 and 2 are named and left alone with the reason.
- **No collateral damage.** No limit or check was added; values below `INT_MAX` serialize identically (`qjsonvalue.h:32-55`, no integer JSON type); `stateJson` is not persisted (only the command reads at `ControlCommandsClock.cpp:75,80,85`); no control command was added; no audio-thread path changed (`stateJson` runs on the control thread). `ControlClockCommands` (the live socket consumer) and the whole `MidiClock` family pass.
- **Verdict FIXED, not ALREADY-FIXED or FALSE-POSITIVE:** the defect is present at HEAD (`MidiClockState.cpp:59,73,98`) and reproduced by the RED run.
- **Registration.** No new file was created. The changed files are already in the manifests: `include/MidiClock.h` (`tests/fork-sources.txt:897`), `src/core/MidiClockState.cpp` (`:1722`), `tests/src/core/MidiClockSourceTest.cpp` (`:2348`, and CMake `tests/CMakeLists.txt`). No `upstream-modifications.txt` entry is needed because these are fork files.
- **Gates.** See the Gates section. `feature_check.py` could not run from this lane (log-folder permission); the direct run is red only on the train's own ratchet items, none in a changed file.
