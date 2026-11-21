FIXED

# Cluster 5184 — a route's address that is not a number loads as 0

Verdict: **FIXED** on `bugs/qwen-5184`. A route whose `channel`, `chain`, `effect` or `track` is present and is not a
number now refuses its modulator. Before the fix the same block loaded, and the modulator was routed to channel,
chain or effect 0, an address nobody authored. The file-length compression in the follow-up commit changes no
validation order; its RED status is stated under Evidence.

## The real decision

Before the fix, `readRoute` (pre-fix `src/core/ModulationLayer.cpp`, HEAD) read the address with unchecked `toInt()`:

- `route.channel = element.attribute("channel").toInt();` (HEAD `:177`)
- `route.chain = ...toInt();` (HEAD `:178`), `route.effect = ...toInt();` (HEAD `:179`)
- `route.track = std::max(-1, ...toInt());` (HEAD `:182`)

`QString::toInt()` returns 0 for `"bad"`, so a corrupt address became a legal-looking address. `saveRoute` writes
integers, so a file with `channel="bad"` was never written by the engine.

The documented contract says a corrupt block must not be repaired. `docs/MODULATION.md:162-168` states that a corrupt
or empty block loads as an EMPTY layer, and that nothing is guessed. The address case broke that contract silently.

## The fix

- `intAttribute` (`src/core/ModulationLayer.cpp:139`) returns the result of `toInt(&ok)` when the attribute is present,
  and gives the fallback when it is absent. It mirrors the existing `boolAttribute` and `floatAttribute` helpers.
- `readRoute` (`:182`) validates the four address fields with `intAttribute` (`:186-189`) and returns false when any is
  malformed. Absent fields keep their documented defaults: channel 0, chain 0, effect 0, track -1. The track clamp
  `std::max(-1, ...)` (`:191`) is kept.
- The route's per-sample flag is checked in `readRoute` by one `boolAttribute` line (`:190`), after the address checks.
  It is reached only for a named route. A nameless route returns true earlier (`:185`).
- `readRoutes` (`:199`) refuses the whole modulator (`return false`) when `readRoute` fails (`:207`).
- A nameless route still returns true from `readRoute` (`:185`), and `readRoutes` drops it at `:208` without
  inspecting its other fields. A nameless route with `channel="bad"` or `persample="bad"` therefore still loads.

## Compression (follow-up commit)

The file-length gate counts code lines, and the fix pushed `ModulationLayer.cpp` over 500. Two reductions, with no
change to any validation order:

- `intAttribute` body reduced from 9 to 7 code lines. The failure path now writes `toInt`'s return (0) into `*out`
  before returning false. The only caller is `readRoute`, which returns false at once and discards the route, so no
  written value is observed.
- The route's per-sample flag had its own 4-line helper and a call in `readRoutes`. Both are deleted, and the flag is
  checked in `readRoute` by one line (`:190`).

The gate's `--check` log lists only the four files over 500 (`file-length-check.log`); `ModulationLayer.cpp` is not in
that list. The gate does not print a per-file count.

## Callers and siblings checked

Grep of `src/` for `readRoute(` and `readRoutes(`/`loadModulator(`:

- `readRoute` is called only from `readRoutes` (`:207`).
- `readRoutes` is called only from `loadModulator` (`:241`).
- `loadModulator` is called only from `loadSettings` (`:593`).
- `intAttribute` in `ModulationLayer.cpp` is called only from `readRoute` (`:186-189`). The other `intAttribute`
  functions in `src/core/SessionModelPrivate.h` and `DawProjectRead.cpp` are different helpers and are not touched.

**Changed:** the four address fields (channel, chain, effect, track), and the route's per-sample flag, which is now
checked in the same function.

**Deliberately unchanged:**

- `depth` (`:192`) still uses `boundedValue`, as before.
- The phase and the source flags already refuse a malformed value (`readSourcePhase` `:149`, `readSourceFlags` `:156`).
- `trigger` (`readSource`, `:173`) still uses `std::max(-1, ...toInt())`. It has the same unchecked pattern. It is
  not in this cluster and is listed under Other findings.

## Tests

`tests/src/core/ModulationLayerTest.cpp` (unchanged by this commit):

- `aMalformedRouteAddressIsRefused_data` (`:524`) — four in-memory rows: `channel`, `chain`, `effect`, `track`.
- `aMalformedRouteAddressIsRefused` (`:533`) — builds a `QDomDocument` in memory with one modulator whose route has
  the row's field set to `"bad"`. It asserts `loadSettings` returns false and `modulatorCount()` is 0.
- `aRouteAddressAbsentKeepsItsDefault` (`:560`) — an absent address keeps 0/0/0/-1, and a nameless route with a bad
  channel is dropped while the valid modulator still loads.

Determinism: all data are in-memory `QDomDocument` rows. There are no sleeps, no `qWait`, no threads and no
randomness in lines 520-586 (checked with grep; no matches).

## Evidence

Logs are outside the repository, in `/home/kruzzzzy/.config/kads/zene/state/runs/5184.r1/logs/`. The quiet-run wrapper
output for these steps is in `/home/kruzzzzy/.config/kads/zene/state/runs/5184.r1/duet/12-worker/tmp/`. The fix patch is
`logs/fix.patch`.

| Step | Command (run from `build/tests` for ctest) | Exit | Result |
|---|---|---|---|
| File length | `bash tests/file-length-gate.sh --check` → `file-length-check.log` | 0 | `ModulationLayer.cpp` not in the exceed list; PASS |
| GREEN (compressed fix) | `cmake --build build --target zene ModulationLayerTest`; `ctest -R '^ModulationLayerTest$'` → `build-5-green.log`, `test-5-green.log` | 0; 0 | 100% of 1 passed |
| Patch | `git diff -- src/core/ModulationLayer.cpp > logs/fix.patch`; `test -s logs/fix.patch` | 0; 0 | non-empty, 11 insertions, 19 deletions |
| Reversed (compression only) | `git apply -R logs/fix.patch`; build → `build-6-reversed.log`; ctest → `test-6-reversed.log` | 0; 0; 0 | 100% of 1 passed. **Not a RED for the address check** (see note) |
| Reapply | `git apply logs/fix.patch`; build → `build-7-reapplied.log`; ctest → `test-7-reapplied.log` | 0; 0; 0 | 100% of 1 passed |
| Feature check | `FEATURE_CHECK_LOG_DIR=logs/feature-check-2 python3 feature_check.py <worktree>` → `feature-check-2.log` | 0 | `RESULT: PASS` |
| Family | `ctest -R '^Modulation'` → `test-8-modulation-family.log` | 0 | 100% passed, 5 of 5 |

Note on RED: the reversal in this run undoes only the uncommitted compression. It leaves the committed `b85f94a` address
check in place, so the four address rows are still checked and pass. These logs therefore do not show the address check
failing. The earlier address-reversal RED is not cited here, because it is not in this run's logs.

Gates:

- `python3 feature_check.py <worktree>` → `RESULT: PASS`, exit 0 (`feature-check-2.log`).
- `ctest -R Modulation` from `build/tests` → 100% passed, 5 of 5, exit 0 (`test-8-modulation-family.log`). The five are
  ModulationLayerValueTest, ModulationLayerTest, ModulationAudioViewTest, ModulationLayerProjectRoundTripTest and
  ModulationRateTest.

## Realtime and undo

The change is on the load path only (`loadSettings` → `loadModulator` → `readRoutes` → `readRoute`). It adds no
audio-thread work, no commands and no undo change.

## Registration

`tests/fork-sources.txt:2130` lists `src/core/ModulationLayer.cpp`. The change is to an existing source file and the
existing test file. No new source and no CMake change.

## Other findings

- `trigger` (`src/core/ModulationLayer.cpp:173`, `readSource`) is still read with `std::max(-1, ...toInt())`. A
  malformed `trigger` loads as track 0. This is the same defect class as this cluster, outside its scope. It needs its
  own cluster and its own RED test.

## Self-check

- **RED evidence:** not shown by the logs in this commit's run. The packet's reversal removes only the compression,
  so the address check is still present and the reversed ctest exits 0. A RED for the address check needs a reversal
  of the `b85f94a` change itself; the lead should decide whether to run that.
- **GREEN evidence:** ctest exit 0 with the compressed fix (`test-5-green.log`) and again after reapply
  (`test-7-reapplied.log`).
- **Patch / apply -R / reapply:** `git diff` patch created exit 0, `test -s` exit 0, `git apply -R` exit 0, reversed
  build and ctest exit 0 (not a RED, see above), `git apply` exit 0, reapplied build and ctest exit 0.
- **Determinism:** in-memory `QDomDocument` data rows. No sleeps, waits, threads or random data in the new tests
  (grep of test lines 520-586, no match).
- **Cause:** `toInt()` returns 0 for a non-number, and the four address fields used it unchecked (HEAD `:177-182`).
- **Siblings:** `readRoute` is called only from `readRoutes`, which is called only from `loadModulator`, called only from
  `loadSettings`. The `trigger` field has the same pattern and is listed under Other findings.
- **Collateral, absent defaults:** an absent address keeps 0/0/0/-1. Test `aRouteAddressAbsentKeepsItsDefault` (`:560`)
  asserts this; it passes in the GREEN runs.
- **Collateral, nameless route:** a nameless route with `channel="bad"` is dropped without inspection and the valid
  modulator still loads. Same test, `:560`.
- **Collateral, round-trip family:** the five-test modulation family passes 5/5 (`test-8-modulation-family.log`),
  including `ModulationLayerProjectRoundTripTest`.
