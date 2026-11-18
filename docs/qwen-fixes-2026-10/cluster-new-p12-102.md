# Cluster new-p12-102 — FIXED

The finding is real. `GrooveTemplate::loadXml` now checks whether the timing
attribute parsed. If it is unreadable, the loader skips that step before its
velocity can be applied. A missing timing attribute still defaults to zero.

Evidence:

- `src/core/GrooveTemplate.cpp:271-275` checks the parse result and skips an
  unreadable timing value.
- `tests/src/core/GrooveTemplateTest.cpp:416-436` loads
  `<step slot="0" timing="bad" velocity="100">` and verifies the slot stays
  neutral while the template geometry loads.

## Build and regression evidence

Commands ran in the worktree root unless the working directory is stated.
Build and test output is in
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-102.r1/logs/`.
No `fff grep` tool or executable was available in this session, so I searched
the logs with targeted `rg -n` terms instead.

| Stage | Exact command | Exit | Evidence |
| --- | --- | ---: | --- |
| Build regression target on original code | `cmake --build build --target GrooveTemplateTest` | 0 | `build-1.log` |
| Initial RED | `ctest -R '^GrooveTemplateTest$' --output-on-failure` (cwd: `build/tests`) | 8 | `test-1.log`; 1/1 CTest failed at `aMalformedTimingDoesNotLoadTheRestOfTheStep` |
| Build fixed code and dependent targets | `cmake --build build --target zene GrooveTemplateTest` | 0 | `build-2.log` |
| Initial GREEN | `ctest -R '^GrooveTemplateTest$' --output-on-failure` (cwd: `build/tests`) | 0 | `test-2.log`; 1/1 CTest passed |

## Rollback proof (required regression check)

| Step | Exact command | Exit | Evidence |
| --- | --- | ---: | --- |
| Remove only the production fix | `git diff -- src/core/GrooveTemplate.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-102.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-102.r1/logs/fix.patch` | 0 | `proof-1.log` |
| Rebuild with fix removed | `cmake --build build --target zene GrooveTemplateTest` | 0 | `build-3.log` |
| RED with fix removed | `ctest -R '^GrooveTemplateTest$' --output-on-failure` (cwd: `build/tests`) | 8 | `test-3.log`; 1/1 CTest failed at the new assertion |
| Restore the production fix | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-102.r1/logs/fix.patch` | 0 | `proof-2.log` |
| Rebuild with fix restored | `cmake --build build --target zene GrooveTemplateTest` | 0 | `build-4.log` |
| GREEN with fix restored | `ctest -R '^GrooveTemplateTest$' --output-on-failure` (cwd: `build/tests`) | 0 | `test-4.log`; 1/1 CTest passed |

## Gates

| Exact command | Exit | Evidence |
| --- | ---: | --- |
| `bash tests/complexity-gate.sh --check` | 0 | `complexity-gate-1.log` |
| `bash tests/file-length-gate.sh --check` | 0 | `file-length-gate-1.log` |

## Files committed

- `src/core/GrooveTemplate.cpp`
- `tests/src/core/GrooveTemplateTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p12-102.md`
