NEEDS-CONTEXT

# Cluster ci-f10986b0b93b91c1-port — "Port: CI regression: build / mingw64"

ci-f10986b0b93b91c1-port: NEEDS-CONTEXT

## Why no verdict can be given

The finding is only a title: `{"cluster": "ci-f10986b0b93b91c1-port", "title": "Port: CI regression: build / mingw64"}`.
It has no job log, no failing step, no compiler or linker message, no file, no run id and no
commit range. A CI regression can only be judged REAL, ALREADY-FIXED or FALSE-POSITIVE from the
symptom, and this job gives none. I also could not get the symptom myself:

| What I tried (this session) | Result |
|---|---|
| fff search of the worktree for `f10986b0b93b91c1` | only submodule `.git` gitdir lines (worktree path); no note, log or doc about this cluster |
| `ls ~/.config/kads/zene/state/runs/ci-f10986b0b93b91c1-port.r2/` | brief.md, prepare.log (local Linux `zene` build, ends `Built target zene`), empty logs/ ; no CI log |
| `gh run list ...` | `timeout: failed to run command 'gh': No such file or directory` |
| `which x86_64-w64-mingw32-g++ x86_64-w64-mingw32-gcc` | `not found` (both); `/usr/x86_64-w64-mingw32` does not exist |
| `docker images` | `failed to connect to the docker API at unix:///var/run/docker.sock ... no such file or directory` |

The repository already records that this job cannot be reproduced on this machine:
`docs/LOCAL-CI.md:125`: "`mingw64` | ubuntu-latest + vcpkg | **NOT-REPRODUCIBLE-HERE** | no
`x86_64-w64-mingw32-g++` on this box; the job needs the vcpkg manifest and
`cmake/toolchains/x64-mingw-vcpkg.cmake`." The job definition is `.github/workflows/build.yml`
(`mingw:` / `name: mingw64`, cross build with `-DUSE_WERROR=ON -Werror=dev`, vcpkg deps).

Past mingw64 failures are known (`docs/LOCAL-CI.md:144` ExprSynth `file too big`;
`docs/CI-TAG-FAILURES.md:130` CLAP headers not found under the MinGW find-root). I can't tell
whether this cluster is either of them, so a fix based on either would be a guess. The brief
says not to guess a fix.

## What is needed to continue

One of:
1. The failing mingw64 job log (at least the first `error:` line with file:line, or the failing
   step name) and the commit/run it came from; or
2. A host with the mingw-w64 cross toolchain and the vcpkg mingw dependency set
   (`build/vcpkg_installed`), so that the job can be reproduced locally.

## Commands run (EXIT codes)
- `which x86_64-w64-mingw32-g++ x86_64-w64-mingw32-gcc docker podman`: compilers and podman not found
- `timeout 20 docker images`: daemon socket missing (output above)
- `timeout 30 gh run list -L 15 --workflow build.yml`: gh not installed
No build or test was run: there is no fix to test. No product file was changed, so there is no
regression test, fix.patch or proof manifest (NEEDS-CONTEXT reports need no manifest).

## Self-check
- Verdict backed by evidence? Yes, as NEEDS-CONTEXT: the missing inputs and the failed attempts
  to obtain them are listed above with their output.
- Regression test? Not applicable: no fix was made.
- Gates: no product or test file changed, only this report.
- git status clean after commit: yes (only this report is committed).

Delegation: none. The work was five short read-only checks (searches, `which`, `docker`, `gh`, two
doc excerpts). Writing a subagent request for each would have cost more than doing them, and
no build, test or code change was needed.

## Other findings
- None.
