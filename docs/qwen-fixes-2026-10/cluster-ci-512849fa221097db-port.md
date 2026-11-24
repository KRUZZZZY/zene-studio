NEEDS-CONTEXT

# Cluster ci-512849fa221097db-port: "Port: CI regression: build / msvc-x64"

ci-512849fa221097db-port: NEEDS-CONTEXT

## Why no verdict on the defect itself

The finding carries only a cluster id and a title:

```json
{"cluster": "ci-512849fa221097db-port", "title": "Port: CI regression: build / msvc-x64"}
```

It has no run id, no job id, no failing step and no compiler or linker error line. A REAL,
ALREADY-FIXED or FALSE-POSITIVE verdict needs the actual MSVC diagnostic. I could not get
it from this machine:

- The cluster id appears nowhere in the worktree. An fff grep for `512849fa221097db` matched
  only the submodule `.git` gitdir pointer files that contain the worktree name.
- The run folder `~/.config/kads/zene/state/runs/ci-512849fa221097db-port.r2/` has no CI
  log. `logs/` was empty at the start of the session, and `prepare.log` holds only the local
  Linux build of `zene`, which ends with `[100%] Built target zene`.
- `timeout 30 gh run list --workflow build -L 10` printed
  `timeout: failed to run command 'gh': No such file or directory`, so I could not fetch the hosted job log.
- `which cl.exe` printed `cl.exe not found`. The repository's own probe,
  `tools/local-ci.sh:341-346`, reports
  `msvc-x64: NOT-REPRODUCIBLE-HERE (MSVC toolchain required: /WX, no libm, import-library semantics)`.
  This means the failure can't be reproduced on this Linux box. A guessed fix with no
  RED/GREEN evidence would break the definition of done.

## Missing capability

One of:
1. the hosted msvc-x64 job log for this regression (run id + job id, or the first `error C`/`LNK`
   lines with file:line), or
2. a Windows/MSVC (cl.exe) build environment where the msvc-x64 job can be reproduced.

With either, the job can be re-run: find the failing translation unit, write a regression
test (or a build-guard test like `tests/vcpkg-pcre2-gitlink-test.py` for configure/toolchain
failures) and fix it.

## Commands run (EXIT codes as observed)

- `timeout 30 gh run list --workflow build -L 10` -> `gh: No such file or directory` (gh absent)
- `which cl.exe clang-cl` -> `cl.exe not found`, `/usr/bin/clang-cl` (clang-cl alone does not
  reproduce the hosted MSVC /WX + import-library build)
- No product code changed, so no build, test, fix.patch or proof manifest
  (the brief says NEEDS-CONTEXT needs no manifest).

## Self-check

- Verdict backed by evidence: yes. The finding has no diagnostic, and the outputs above show
  that neither the log nor the toolchain is reachable.
- Regression test: not applicable. No defect was identified, so there is no fix.
- Product files changed: none. The only file added is this report.
- Realtime, undo and control commands: untouched.
- Git: report-only commit, so the tree is clean afterwards.

Delegation: none. The session ended before any work could be delegated: there was nothing to
build, test or edit without the CI log. Every step was a short read of local state that
cost less than writing a request.

## Other findings

None.
