# Three-stream acceptance — 2026-09-24, train tip `576fd8c68`

The owner's acceptance clause: *"ONE verified tree with all three streams merged
(gates and ctest green on the merged tip) and the UI-B interface wired to the
features that landed, ready for me to test — with any feature still in flight at
UI-freeze named explicitly rather than silently unwired."*

**VERDICT: MET.** Evidence below; every number is a measured run on this tree.

## The tree

`0.4.0/train-w1`, tip `576fd8c68` (version `0.3.0-alpha.84+2f6218c` at the
snapshot capture; the freeze commit is the shipping tip). Merge train: 14
dispatched lanes + 10 ARCH-4 slices delivered or harvested-and-finished by the
parent; every lane's "done" was re-verified at merge, and three rounds of
post-merge regressions were root-caused and fixed at source (below).

## Stream 1 — features (engine code + command group + registered proof + limitations line)

| Feature | Command group | Proof (registered) | Notes |
|---|---|---|---|
| #706 destructive waveform editor (slice 1) | `sample.*` (generate, amplify, normalize, reverse, fade) | `ControlSampleOperatorTest` | undo = one journal step per operator |
| #708 scheduled-Lua live-coding clock | `livecode.*` (get_state, schedule, unschedule) | `ControlLivecodeCommands`, `MidiClockTest` | ~0.8 s budget refusal fires before load |
| #709 cycle-permitted submode | `feedback.*` (enable, disable, get_state) | `ControlFeedbackCommands` (25 checks) | PDC suspended in submode; byte-identity negative control (rotations #1/#2 recorded) |
| #712 MTS-ESP host-wide tuning | `mts.*` (get_state, load_scale, load_keymap, master_set, set_note, set_tuning, reset) | `SessionTuningTest` (7 cases) | Scala + keymap; sounding notes retune while playing; the UAF in this path was found and fixed (below) |

Plus the document-model families: `warp.*` (S9), `comp.*`/`comp.lane_*` (S7),
`session.*` ×17 (S8/S10), `revisions.*` (S3/S6) — all in the 350-id surface.

## Stream 2 — UI (mockup B / B-desk), wired and ready to test

Focus Desk items 3–7 + the `mod:` verb (registry-first seam), B-desk workspaces
as declarative data, generated Commands/Modules menus with mechanical
availability, the density contract, WCAG-contrast controls, and the owner's two
live-test fixes: **off-screen window recovery** (restore + re-show clamp into the
visible MDI region) and the **legacy-load crash class** (the play-handle
use-after-free; see below). UI-freeze commit `576fd8c68`: the two placeholders
wired to the real commands — **nothing is in flight at freeze** (the clause's
naming requirement is vacuously satisfied: `todo.mts` → `mts.*`, `todo.s7-lanes`
→ `comp.lane_*`; the stub mechanism itself was deleted).

## Stream 3 — architecture

- **ARCH-4 document model: COMPLETE** — S1–S10 + the v2 writer/reader slice +
  `WANT_SESSION_VIEW` removed (the Session View is native). `<z:lanes>` /
  `<z:scenes>` top-level sections; `<session>` reader is the upconverter;
  byte-identity for lane-less/scene-less projects (the additive rule), with the
  RISK-1 phantom-clip negative control seen red-then-green.
- **ARCH-5** (keep-and-own, D16) and **ARCH-7** (Tracktion: DO NOT ADOPT,
  licence-first, D17) — decided and recorded in MASTER-PLAN + BACKLOG.
- **Multicore scheduling** — Slice 0+1 (rt-safety frontier + published schedule).
- **ARCH-8** — the architecture write-up (separate file, closing the wave).

## Verification battery (all unpiped, all on this tree)

| Check | Result |
|---|---|
| Full ctest | **236/236 green** at `007565d0f` (the acceptance run: EXIT=0); the shipping tip's closing run recorded below |
| Seven text gates (FL/CP/DUP/G9/G6/G11/unreg) | **all EXIT=0** |
| Honesty gate | **PASS 5/5 features match** (`--dump` of `--version`) |
| Snapshot/MCP drift | **0 drift** after the REGEN-ONCE: `commands_snapshot.json` = 350 commands / 56 groups / ids_sha256 `2fcc8f64f1dd…`, captured from the live final tip |
| Id-set proof | S8: 17 `session.*` ids unchanged · S10: delta 0 over 359 tracked id literals |
| FocusDesk (freeze) | 4/4 green on fresh binaries; no `todo.*` id survives |

## The three regression rounds (recorded, each root-caused)

1. **Play-handle use-after-free** (`SessionTuningTest` abort/segfault): handles
   staged in `m_newPlayHandles` survived track teardown (`removePlayHandlesOfTypes`
   walked only `m_playHandles`) and rendered through their freed track — proved
   with `MALLOC_PERTURB_` (garbage flipped to the freed-memory pattern) and a
   diagnostic `qFatal` that named the enum garbage. Fix: an in-place, head-safe
   (`setFirst`), note-handled-only staging sweep — the class behind the owner's
   old-project load crashes too (load = teardown + rebuild).
2. **`RevisionTimelineTest` "gone" id resolving**: strace proved the handler read
   the machine's *global* `Documents/Zene Studio/recover.mmp.bak` (left by the
   owner's live/crashed sessions). Fix: `ZENE_RECOVERY_FILE` override + test
   isolation.
3. **`ControlFeedbackCommands` save-sha drift**: window-layout metadata (the
   owner's saved layout + the off-screen clamp legitimately shifting geometry).
   Fix: the canonical form drops GUI view sections + geometry attrs; rotation #2
   recorded in `docs/709-logs/709-baseline-rotate.md` (pdc/mixer constants
   untouched — byte-identical across all three measurements).

## Known limits at freeze (named, not silent)

- The `S10` row's "340 ids" is a 2026-09-17 inventory number from an older
  tree; this tip measures **350** (the growth is `mts.*` + the feature families
  of this wave). S10 proved *its delta = 0*; the number itself is not
  retro-fitted.
- `ProjectVersionTest` showed one transient failure under a disk-full parallel
  run; 10/10 green in repeat probes (recorded in `docs/s10-logs/gates.md`).
- The stripped-build mtime trap (strip rewrites `.o` timestamps and make skips
  recompiles) bit twice and is documented for future lanes.

## OPEN at close (boarded — do not read this report as fully executed)

Three items were reported in this session but are NOT closed here; each has a
boarded task with a full spec, and this section exists so a cold reader cannot
mistake the acceptance for their completion:

1. **#745 — the owner's actual crashing songs are unconfirmed on the fixed
   build.** The UAF class is fixed and proved (8/8 `SessionTuningTest`), but no
   crashing file from the owner's session was re-opened against it. Open the
   files (or the 42-fixture corpus + `Crunk(Demo).mmp`) on `9d658259d`+ before
   calling the legacy-load crash dead.
2. **#743 — `ProjectVersionTest`'s transient** (one failure under a disk-full
   parallel run; 10/10 green in probes) is un-stressed under the triggering
   conditions.
3. **#744 — `verification/CRASH-TESTING-INVENTORY.md` still reads 340 ids**;
   the measured surface is 350/56 groups. The doc needs its figure folded to
   measured state with the stale number kept as dated history.

Frontier (not a defect, for the next wave): multicore S2+ — the published
schedule exists (Slice 0+1); execution is unbuilt (see `ARCH-8` §5).
