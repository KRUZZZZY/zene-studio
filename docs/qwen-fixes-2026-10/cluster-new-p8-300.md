# Cluster new-p8-300

**Verdict: FIXED**

The line-size check in `CollabLink::onReadyRead()` previously ran only after complete lines had already been extracted and dispatched. It now checks the first newline offset against `m_maxLineBytes` before `takeLine()` copies the line or JSON parsing begins. An oversized complete line closes the connection with the existing size-limit reason. The existing check for oversized unterminated data remains in place.

## Evidence

- `src/core/CollabLink.cpp:186-195`: checks the complete line length before extraction and dispatch.
- `tests/src/core/CollabTransportTest.cpp:233-251`: sends a five-byte line to a link capped at four bytes and verifies the link closes without emitting the line.
- Baseline RED: the new test failed because `closed.size()` was 0 (actual) instead of 1 (expected); the Qt test run reported 11 passed and 1 failed.
- Patch reversal proof: with only the production patch reversed, the same test failed again. Restoring it and rebuilding made the test pass.

Affected family: 1 CTest target (`CollabTransportTest`) selected and executed; the baseline RED run reported 12 Qt test cases (11 passed, 1 failed). The green CTest runs passed the selected target.

## Commands and results

Commands below ran from the repository root unless marked `build/tests`. Build and test output was redirected to the named log under `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-300.r1/logs`; each log records its exit code.

| Phase | Exact command | Exit |
|---|---|---:|
| Baseline test build | `cmake --build build --target CollabTransportTest` (`build-1.log`) | 0 |
| Baseline RED, from `build/tests` | `ctest -R '^CollabTransportTest$' --output-on-failure` (`test-1.log`) | 8 |
| Fixed build | `cmake --build build --target zene CollabTransportTest` (`build-2.log`) | 0 |
| Fixed GREEN, from `build/tests` | `ctest -R '^CollabTransportTest$' --output-on-failure` (`test-2.log`) | 0 |
| Save product patch | `git diff -- src/core/CollabLink.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-300.r1/logs/fix.patch` | 0 |
| Reverse product patch | `git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-300.r1/logs/fix.patch` | 0 |
| Reversed build | `cmake --build build --target zene CollabTransportTest` (`build-3.log`) | 0 |
| Reversed RED, from `build/tests` | `ctest -R '^CollabTransportTest$' --output-on-failure` (`test-3.log`) | 8 |
| Restore product patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-300.r1/logs/fix.patch` | 0 |
| Restored build | `cmake --build build --target zene CollabTransportTest` (`build-4.log`) | 0 |
| Restored GREEN, from `build/tests` | `ctest -R '^CollabTransportTest$' --output-on-failure` (`test-4.log`) | 0 |
| Complexity gate | `bash tests/complexity-gate.sh --check` (`gate-1.log`) | 0 |
| File-length gate | `bash tests/file-length-gate.sh --check` (`gate-2.log`) | 0 |

Files committed:

- `src/core/CollabLink.cpp`
- `tests/src/core/CollabTransportTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p8-300.md`
