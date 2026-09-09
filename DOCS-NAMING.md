# Naming decision — Zene Studio

**Status:** decided · **Owner decision date:** 2026-09-09

## Decision

- The product name is **Zene Studio**.
- The product repository is currently `KRUZZZZY/lmms-complete`. Its rename to
  `KRUZZZZY/zene-studio` is **deferred to the end of the naming transition**
  (owner decision, 2026-09-09).
- Until that rename happens, **no code strings, CMake `project()` name, binary
  names, or directories change**. Only product-facing documentation (this file
  and the README) uses the new name.

## Deferred rename checklist

Execute these together at the end of the naming transition:

1. Rename the repo `KRUZZZZY/lmms-complete` → `KRUZZZZY/zene-studio`.
2. `CMakeLists.txt`: change `PROJECT(lmms)` → `PROJECT(zene)` and update
   `PROJECT_AUTHOR`, `PROJECT_URL`, and `PROJECT_DESCRIPTION` to match.
3. Update desktop entries (`.desktop` files) and application metadata.
4. Update window titles.
5. Update the `--version` string.
6. Verification: a code search must then show **zero `lmms-complete` product
   references**.

## Explicitly out of scope

- The public fork `KRUZZZZY/lmms` is **NOT renamed**. It is the upstream-PR
  fork and must keep LMMS branding for upstream pull requests.
- No pre-emptive renames of code strings, CMake project names, binaries, or
  directories before the deferred rename step above.

## Rationale

Documentation can move to the product name immediately because it is
product-facing and does not affect builds, packaging, or CI. The technical
rename touches build metadata, install paths, and distribution artefacts, so it
is batched into a single deferred change with an explicit verification step.
