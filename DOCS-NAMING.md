# Naming decision — Zene Studio

**Status:** repo rename DONE 2026-09-09 · **Owner decision date:** 2026-09-09

## Decision

- The product name is **Zene Studio**.
- The product repository is **`KRUZZZZY/zene-studio`** (public since 2026-09-09,
  standalone, default branch `main`; renamed 2026-09-09).
- The upstream-PR fork (formerly `KRUZZZZY/lmms`) was **deleted 2026-09-09**;
  its unique refs live on in the product repo as branches `standards/quality-gates`
  and `archive/fork-readme`, and upstream PR #8548 (HiDPI) was closed as a
  consequence (diff/body/CI archive: `submissions/pr8548/`).
- Code strings, CMake `project()` name, binary names, and directories change only
  in **wave R** (below).

## Deferred rename checklist — wave R of `ableton-gap/PLAN-zene-studio.md`

Execute these together (the repo rename is done; these are the remaining items):

1. ~~Rename the repo to `KRUZZZZY/zene-studio`~~ **DONE 2026-09-09**.
2. `CMakeLists.txt`: change `PROJECT(lmms)` → `PROJECT(zene)` and update
   `PROJECT_AUTHOR`, `PROJECT_URL`, and `PROJECT_DESCRIPTION` to match.
3. Update desktop entries (`.desktop` files) and application metadata.
4. Update window titles.
5. Update the `--version` string.
6. Update the README.
7. Verification: a code search must then show **zero references to the old
   product repo name**.

## Explicitly out of scope

- No pre-emptive renames of code strings, CMake project names, binaries, or
  directories before wave R.

## Rationale

Documentation moved to the product name immediately because it is
product-facing and does not affect builds, packaging, or CI. The technical
rename touches build metadata, install paths, and distribution artefacts, so it
is batched into wave R with an explicit verification step.
