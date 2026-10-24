new-p16-335: FIXED

Evidence: REAL residual issue. The branch already checked the project before `file.readAll()` and the host checked before `toBase64()`, but both built frame metadata first and narrowed the `bytes` field to `int`. `src/core/CollabSessionSync.cpp:181-193` now rejects raw files at the line cap before header serialization and keeps `fileSize` as `qint64`; `src/core/CollabHost.cpp:373-383` applies the matching guard and keeps `project.size()` wide. The regression is `CollabTransportTest::snapshotSizesAreCheckedBeforeHeaderSerialization`.

RED: `cmake --build build --target zene CollabTransportTest` (worktree; EXIT=0), then `ctest -R '^CollabTransportTest$' --output-on-failure` (cwd `build/tests`; EXIT=8). Assertion: `hostGuard >= 0` was false.

GREEN: same build command (EXIT=0), then same ctest command (cwd `build/tests`; EXIT=0).

4b: `git diff -- src/core/CollabHost.cpp src/core/CollabSessionSync.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p16-335.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p16-335.r1/logs/fix.patch` (EXIT=0); rebuild (EXIT=0); same ctest command (cwd `build/tests`; EXIT=8, same assertion). Reapplied with `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p16-335.r1/logs/fix.patch` (EXIT=0), rebuilt (EXIT=0), and reran the same ctest command (cwd `build/tests`; EXIT=0).

Affected family: `ctest -R Collab --output-on-failure` (cwd `build/tests`; EXIT=0, 4/4: CollabPolicyTest, CollabTransportTest, CollabPanelTest, ControlCollab).

Gates: first `feature_check.py` run EXIT=1 because `tests/all-sources.txt` contained 1,148 vendored sfizz entries excluded by its declared recipe. Regenerated the entry list from that recipe (EXIT=0); final `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p16-335` EXIT=0. Table: fork-sources PASS; all-sources-reproduce PASS; no-upstream-regression PASS; check-namespace PASS; no-tautology PASS; doc-figures PASS; complexity PASS; file-length PASS.

Files committed: `src/core/CollabHost.cpp`, `src/core/CollabSessionSync.cpp`, `tests/src/core/CollabTransportTest.cpp`, `tests/upstream-modifications.txt`, `tests/all-sources.txt`, and this report.
