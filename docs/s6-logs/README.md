# ARCH-4 S6 (provenance) — lane evidence, 2026-09-22 (branch 040/arch4-s6)

All captures are committed here (never /tmp); files are `.md` because Gate 11/6
classify committed evidence by extension.

## Commands and unpiped exit codes

| Step | Command | Exit |
|---|---|---|
| configure (CI flags, Qt6 deviation noted inside) | `tools/local-ci.sh --configure-only --build-dir build` | 0 |
| reconfigure after CMake edits | `cmake -S . -B build` | 0 |
| targeted build (19 test targets, `-j2`; two compile-error iterations visible at the end of `build.md`) | `cmake --build build --target ProvenanceSectionTest … -j2` | 0 (final; earlier runs exited 2 on `writeTo` member call and `QDomElement::document` — both fixed) |
| single proof test | `cd build/tests && LD_LIBRARY_PATH=… QT_QPA_PLATFORM=offscreen ctest -R '^ProvenanceSectionTest$'` | 0 (`ctest-provenance.md`) |
| regression set (my test + 18 existing save/load/registry tests) | same env, `ctest -R '^(ProvenanceSectionTest\|ProjectRevIdsTest\|…\|SessionModelTest)$'` | 0 — 19/19 passed (`ctest.md`) |
| Gate 3 no-tautology | `bash tests/no-tautology-gate.sh` | 0 |
| Gate 4 complexity (fork + tools) | `bash tests/complexity-gate.sh --check [--scope tools]` | 0 |
| Gate 7 file length (fork + tools) | `bash tests/file-length-gate.sh --check [--scope tools]` | 0 |
| Gate 8 duplication (fork + tools) | `bash tests/duplication-gate.sh [--scope tools]` | 0 |
| Gate 11 evidence + self-test | `bash tests/evidence-gate.sh [--self-test]` | 0 |
| Gate 12 rt-safety | `python3 tests/rt-safety-sweep.py --check` | 0 |
| Gate 6 no-upstream-regression (post-commit, `gates-upstream-post.md`) | `bash tests/no-upstream-regression-gate.sh` | 1 — ONLY the eight pre-existing `docs/train-logs/*.out` captures of the merge train (present at the base tip `4ef3065fa` before this lane's first commit, verified pre-commit in `gates-upstream.md`); every path this lane added or touched classifies allowed/declared |
| Gate 9 fork-sources (post-commit, `gates-fork-sources-post.md`) | `bash tests/fork-sources-gate.sh` | 0 |

## Artefacts

- `configure.md` — CI's exact CMAKE_OPTS + VST3/CLAP provisioning; deviation: no Qt5 dev files → `-DWANT_QT6=ON`.
- `build.md`, `build-testfix.md` — targeted compile of lmmsobjs + 19 test binaries.
- `ctest-provenance.md` — the registered proof (read-back after fresh load, append-only, additive, bound, actor).
- `ctest.md` — 19/19 green regression set.
- `gates-*.md` — each gate's own output.
