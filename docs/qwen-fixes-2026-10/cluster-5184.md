FIXED

# Cluster 5184 — a route's address that is not a number loads as 0

Verdict: **FIXED** on `bugs/qwen-5184`. A route whose `channel`, `chain`, `effect` or `track` is present and is not a
number now refuses its modulator. Before the fix the same block loaded, and the modulator was routed to channel,
chain or effect 0, an address nobody authored.

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

- `intAttribute` (`src/core/ModulationLayer.cpp:139`) returns false when the attribute is present and not a number,
  and gives the fallback when it is absent. It mirrors the existing `boolAttribute` and `floatAttribute` helpers.
- `readRoute` (`:190`) validates the four address fields with `intAttribute` and returns false when any is malformed.
  Absent fields keep their documented defaults: channel 0, chain 0, effect 0, track -1. The track clamp
  `std::max(-1, ...)` (`:198`) is kept.
- `readRoutes` (`:206`) refuses the whole modulator (`return false`) when `readRoute` fails, before the route flags
  are read (`:214`).
- A nameless route still returns true from `readRoute` (`:193`), and `readRoutes` drops it at `:215` without
  inspecting its other fields. A nameless route with `channel="bad"` therefore still loads.

## Callers and siblings checked

Grep of `src/` for `readRoute(` and `readRoutes(`/`loadModulator(`:

- `readRoute` is called only from `readRoutes` (`:214`).
- `readRoutes` is called only from `loadModulator` (`:249`).
- `loadModulator` is called only from `loadSettings` (`:601`).

**Changed:** the four address fields (channel, chain, effect, track).

**Deliberately unchanged:**

- `depth` (`:199`) still uses `boundedValue`, as before.
- The phase and the flags already refuse a malformed value (`readSourcePhase`, `readSourceFlags`, `readRouteFlags`).
- `trigger` (`readSource`, `:181`) still uses `std::max(-1, ...toInt())`. It has the same unchecked pattern. It is
  not in this cluster and is listed under Other findings.

## Tests

`tests/src/core/ModulationLayerTest.cpp`:

- `aMalformedRouteAddressIsRefused_data` (`:524`) — four in-memory rows: `channel`, `chain`, `effect`, `track`.
- `aMalformedRouteAddressIsRefused` (`:533`) — builds a `QDomDocument` in memory with one modulator whose route has
  the row's field set to `"bad"`. It asserts `loadSettings` returns false and `modulatorCount()` is 0.
- `aRouteAddressAbsentKeepsItsDefault` (`:560`) — an absent address keeps 0/0/0/-1, and a nameless route with a bad
  channel is dropped while the valid modulator still loads.

Determinism: all data are in-memory `QDomDocument` rows. There are no sleeps, no `qWait`, no threads and no
randomness in lines 520-586 (checked with grep; no matches).

## Evidence

Logs are outside the repository, in `/home/kruzzzzy/.config/kads/zene/state/runs/5184.r1/`. The quiet-run output is in
`.../duet/10-worker/tmp/`. The fix patch is `.../5184.r1/logs/fix.patch`.

| Step | Command (run from `build/tests` for ctest) | Exit | Result |
|---|---|---|---|
| GREEN (fix present) | `cmake --build build --target ModulationLayerTest -j8`; `ctest -R '^ModulationLayerTest$' --output-on-failure` | 0; 0 | 100% of 1 passed (`002-ctest-green.log`) |
| Patch | `git diff -- src/core/ModulationLayer.cpp > logs/fix.patch`; `test -s logs/fix.patch` | 0; 0 | non-empty |
| RED (reverse the fix) | `git apply -R logs/fix.patch`; build; ctest | 0; 0; **8** | `aMalformedRouteAddressIsRefused(channel/chain/effect/track)` FAIL, `Actual (layer.loadSettings(root)): 1` against expected false; 16 passed, 4 failed (`005-ctest-red.log`) |
| Reapply | `git apply logs/fix.patch`; build; ctest | 0; 0; **0** | 100% of 1 passed (`007-ctest-reapplied.log`) |

Note on RED: a first ctest attempt from the worktree root was refused by the bunny guard with exit 97. It ran no test.
The ctest was rerun from `build/tests` and gave exit 8. The RED run and the "reversed" run in the packet are the same run.

Gates:

- `python3 feature_check.py <worktree>` → `RESULT: PASS`, exit 0 (`008-feature-check.log`).
- `ctest -R Modulation` from `build/tests` → 100% passed, 5 of 5, exit 0 (`009-family-modulation.log`). The five are
  ModulationLayerValueTest, ModulationLayerTest, ModulationAudioViewTest, ModulationLayerProjectRoundTripTest and
  ModulationRateTest.

## Realtime and undo

The change is on the load path only (`loadSettings` → `loadModulator` → `readRoutes` → `readRoute`). It adds no
audio-thread work, no commands and no undo change.

## Registration

`tests/fork-sources.txt:2130` lists `src/core/ModulationLayer.cpp`. The change is to an existing source file and the
existing test file. No new source and no CMake change.

## Other findings

- `trigger` (`src/core/ModulationLayer.cpp:181`, `readSource`) is still read with `std::max(-1, ...toInt())`. A
  malformed `trigger` loads as track 0. This is the same defect class as this cluster, outside its scope. It needs its
  own cluster and its own RED test.

## Self-check

- **RED evidence:** the reversed fix gives ctest exit 8. All four rows fail at the `loadSettings` assertion, with
  `Actual 1` against expected false. See the table above and `005-ctest-red.log`.
- **GREEN evidence:** ctest exit 0 with the fix present (`002-ctest-green.log`) and again after reapply
  (`007-ctest-reapplied.log`).
- **4b (patch / apply -R / reapply):** `git diff` patch created exit 0, `test -s` exit 0, `git apply -R` exit 0, reversed
  ctest exit 8, `git apply` exit 0, reapplied ctest exit 0.
- **Determinism:** in-memory `QDomDocument` data rows. No sleeps, waits, threads or random data in the new tests
  (grep of test lines 520-586, no match).
- **Cause:** `toInt()` returns 0 for a non-number, and the four address fields used it unchecked (HEAD `:177-182`).
- **Siblings:** `readRoute` is called only from `readRoutes`, which is called only from `loadModulator`, called only from
  `loadSettings`. The `trigger` field has the same pattern and is listed under Other findings.
- **Collateral, absent defaults:** an absent address keeps 0/0/0/-1. Test `aRouteAddressAbsentKeepsItsDefault` (`:560`)
  asserts this, and it passes in GREEN.
- **Collateral, nameless route:** a nameless route with `channel="bad"` is dropped without inspection and the valid
  modulator still loads. Same test, `:560`.
- **Collateral, round-trip family:** the five-test modulation family passes 5/5 (`009-family-modulation.log`), including
  `ModulationLayerProjectRoundTripTest`.
- **FIXED verdict:** this report says FIXED on the first line, citing the RED exit 8 and GREEN exit 0 above and
  `docs/MODULATION.md` §6, where the contract is updated with the new sentence.
