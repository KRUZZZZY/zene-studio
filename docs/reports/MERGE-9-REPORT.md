# MERGE-9 REPORT — 030/capabilities into release/0.3.0 (+ the gate-6 follow-up fix)

**Date:** 2026-09-19. **Worktree:** `zene-030`. The last lane of the 0.3.0 cycle to merge.
**Status at writing:** not yet pushed as its own commit — rides the next release-line push
(release/0.3.0 currently = `a4b21cfa6`, run #11 in flight).

## What merged

- `cef07cadc` = `--no-ff` merge of **030/capabilities** (`ae8a4add0`, based on `b89d10a62`) into the
  then-tip `6ad59cbd3` (= `b89d10a62` + docs-agree + crashbot). **Conflicts: none** (auto-merge; the
  docs edits sat in distinct regions). Both sides verified to have survived: docs-agree's corrections,
  the capabilities corrigendum (KNOWN-LIMITATIONS) and the UNDO-BOUNDS `chord.set` row, plus the
  complete spec — `docs/CAPABILITIES-0.3.0.md`, 1,383 lines, Appendix A generated live
  (616 lines; capture at `d408e35`, surface 340 ids / 53 groups / 205 mutating).
- `a4b21cfa6` = **fix(gates):** the verification record renamed
  `.txt -> .md` (`docs/CAPABILITIES-0.3.0-VERIFICATION.md`) + its 3 references updated. Reason: the
  bar's gate 6 classifies documentation **by extension** (`*.md` allowed); the `.txt` read as
  "undeclared change to upstream-inherited code" and **would have reddened CI's static-gates on the
  next push**. The local bar caught it pre-push; the same lesson is recorded in the zene-studio-ci
  skill.

## The bar on the merged tip

```
focused ctest (ZeneApiBoundary | ReversibilityContractTest | ControlMcpGroupCoverage)  3/3 pass
full ctest -j4 --output-on-failure on cef07cadc                                        214/214, EXIT=0 (145.64 s)
gate 6 standalone on a4b21cfa6                                                         PASS (422 paths declared; ledger 460)
run-all-gates.sh on a4b21cfa6    EXIT=3 PASS-WITH-SKIPS — 12 of 13 PASS
                                 (incl. gate 1 ctest, gate 5 mutation, gate 6, gate 13);
                                 gate 2 coverage skipped by policy; tree clean
```

**Flake observed during the bar (evidence kept):** gates run 1's gate-1 ctest hung on
`WasmWorkerPoolTest` (75 min, futex waits; killed by PID). Standalone probe: 10/10 pass, 215 ms each
— a suite-level flake, not reproducible in isolation. Hung log + per-thread snapshot in
`zene-030/.dlog-fire-0917/`. Post-tag candidates: a ctest `TIMEOUT` property on that test; a crashbot
scenario for the pool's park/wake path.

## CI context at this merge

- **Run #10 (`6ad59cbd3`):** 6/7 build jobs success with both guards PASS; checks 3/3; doxygen and
  quality-gates success. `linux-arm64` red on **one flaked control test** (`#201
  ControlSocketPathSafety` — a control-socket ping deadline; 99% of 207 passed; the job's own
  failing-test auto-rerun passed 1/1 in 115.66 s). Same tree, `main`'s arm64 passed. Job-level
  rerun of #10's arm64 queued for record-cleanliness. **Confirmed flake class, not a product break.**
- **Push #11 = `a4b21cfa6`** (this tip, carrying the gate fix) — run #11: build `35454876489`,
  checks `35454876562`, doxygen `35454876460`, quality-gates `35454876406`. Its build-triggered
  `release.yml` fitness run is the **"may this be tagged?"** verdict for the 0.3.0 tag.

## Where 0.3.0 stands after this merge

Everything planned for the 0.3.0 line is now on release/0.3.0: the version bump (CI-proven green on
run #9), the docs corrections (docs-agree), the crashbot tooling + pilot (crashbot), the capability
spec + verification record (capabilities), and the title/banner/claim corrections across the four
user-facing documents. Remaining before a published 0.3.0: run #11 green (incl. its release-fitness
verdict) → re-advance `main` to the final tip → tag (owner's act) → publish via `zene-release`.
