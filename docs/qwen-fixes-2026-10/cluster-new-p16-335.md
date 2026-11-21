FIXED

new-p16-335: FIXED

Evidence: REAL residual issue. The branch already checked the project before `file.readAll()` and the host checked before `toBase64()`, but both built frame metadata first and narrowed the `bytes` field to `int`. `src/core/CollabSessionSync.cpp:181-193` now rejects raw files at the line cap before header serialization and keeps `fileSize` as `qint64`; `src/core/CollabHost.cpp:373-383` applies the matching guard and keeps `project.size()` wide. The regression is `CollabTransportTest::snapshotSizesAreCheckedBeforeHeaderSerialization`.

RED: From the worktree, `cmake --build build --target zene CollabTransportTest` EXIT=0. From `build/tests`, `ctest -R '^CollabTransportTest$' --output-on-failure` EXIT=8. Assertion: `hostGuard >= 0` was false.

GREEN: From the worktree, `cmake --build build --target zene CollabTransportTest` EXIT=0. From `build/tests`, `ctest -R '^CollabTransportTest$' --output-on-failure` EXIT=0.

4b: `git diff -- src/core/CollabHost.cpp src/core/CollabSessionSync.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p16-335.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p16-335.r1/logs/fix.patch` EXIT=0; `cmake --build build --target zene CollabTransportTest` EXIT=0; `ctest -R '^CollabTransportTest$' --output-on-failure` from `build/tests` EXIT=8 with the same assertion. Then `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p16-335.r1/logs/fix.patch` EXIT=0; the same build command EXIT=0; the same ctest command from `build/tests` EXIT=0.

Affected family: `cmake --build build --target zene CollabPolicyTest CollabTransportTest CollabPanelTest` EXIT=0; then `ctest -R Collab --output-on-failure` from `build/tests` EXIT=0, 4/4: CollabPolicyTest, CollabTransportTest, CollabPanelTest, ControlCollab.

Gates: `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p16-335` first EXIT=1 because the manifest listed 1,148 excluded vendored sfizz paths. Re-ran the manifest's declared recipe with `manifest=tests/all-sources.txt; regenerated=/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p16-335.r1/logs/all-sources.regenerated; { grep -E '^[[:space:]]*(#|$)' "$manifest"; { git ls-files '*.c' '*.cc' '*.cpp' '*.cxx' '*.h' '*.hpp' | grep -vE '^(src/3rdparty/|tests/reference/|plugins/NeuralAmp/(rtneural|nam|tests)/|plugins/RnnoiseDenoiser/rnnoise/|plugins/SfzPlayer/sfizz/)' ; git ls-files tests/control-detect-commands.py tests/control_detect_fixtures.py ; } | LC_ALL=C sort -u; } > "$regenerated" && mv "$regenerated" "$manifest"` EXIT=0. The same feature-check command then EXIT=0. Table: fork-sources PASS; all-sources-reproduce PASS; no-upstream-regression PASS; check-namespace PASS; no-tautology PASS; doc-figures PASS; complexity PASS; file-length PASS.

Files committed for new-p16-335: `src/core/CollabHost.cpp`, `src/core/CollabSessionSync.cpp`, `tests/src/core/CollabTransportTest.cpp`, `tests/upstream-modifications.txt`, `tests/all-sources.txt`, and this report.

new-p8-291: FIXED

Evidence: REAL. `src/core/CollabSessionSync.cpp:257-260` previously replaced `m_fingerprint` before checking `m_graceUntilMs`, so the later poll could not see a joined peer's edit again. The grace check now precedes the fingerprint update, keeping the edit pending for a later poll. Regression: `CollabTransportTest::snapshotGraceDoesNotConsumeAnUntrackedEdit` verifies this order without sleeps or timestamps.

RED: From the worktree, `cmake --build build --target zene CollabTransportTest` EXIT=0. From `build/tests`, `ctest -R '^CollabTransportTest$' --output-on-failure` EXIT=8. Assertion: `fingerprintUpdate > graceCheck` was false.

GREEN: From the worktree, `cmake --build build --target zene CollabTransportTest` EXIT=0. From `build/tests`, `ctest -R '^CollabTransportTest$' --output-on-failure` EXIT=0.

4b: `git diff -- src/core/CollabSessionSync.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p16-335.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p16-335.r1/logs/fix.patch` EXIT=0; `cmake --build build --target zene CollabTransportTest` EXIT=0; `ctest -R '^CollabTransportTest$' --output-on-failure` from `build/tests` EXIT=8 with the same assertion. Then `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p16-335.r1/logs/fix.patch` EXIT=0; the same build command EXIT=0; the same ctest command from `build/tests` EXIT=0.

Affected family: `cmake --build build --target zene CollabPolicyTest CollabTransportTest CollabPanelTest` EXIT=0; then `ctest -R Collab --output-on-failure` from `build/tests` EXIT=0, 4/4: CollabPolicyTest, CollabTransportTest, CollabPanelTest, ControlCollab.

Gates: `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p16-335` EXIT=0, with all eight rows PASS (see the gate table above).

Files committed for new-p8-291: `src/core/CollabSessionSync.cpp`, `tests/src/core/CollabTransportTest.cpp`, `tests/upstream-modifications.txt`, and this report.

new-p10-8: FIXED

Evidence: REAL. `src/core/CollabSessionSync.cpp:180-213` now reports `QFile::errorString()` when the saved snapshot cannot be reopened, checks `QFileDevice::error()` after `readAll()`, and rejects short reads against the checked file size. Regression: `CollabTransportTest::snapshotReadFailuresPreserveFileReason` verifies both open and read error handling.

RED: From the worktree, `cmake --build build --target zene CollabTransportTest` EXIT=0. From `build/tests`, `ctest -R '^CollabTransportTest$' --output-on-failure` EXIT=8. Assertion: `openReason > openFailure` was false.

GREEN: From the worktree, `cmake --build build --target zene CollabTransportTest` EXIT=0. From `build/tests`, `ctest -R '^CollabTransportTest$' --output-on-failure` EXIT=0.

4b: `git diff -- src/core/CollabSessionSync.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p16-335.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p16-335.r1/logs/fix.patch` EXIT=0; `cmake --build build --target zene CollabTransportTest` EXIT=0; `ctest -R '^CollabTransportTest$' --output-on-failure` from `build/tests` EXIT=8 with the same assertion. Then `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p16-335.r1/logs/fix.patch` EXIT=0; the same build command EXIT=0; the same ctest command from `build/tests` EXIT=0.

Affected family: `cmake --build build --target zene CollabPolicyTest CollabTransportTest CollabPanelTest` EXIT=0; then `ctest -R Collab --output-on-failure` from `build/tests` EXIT=0, 4/4: CollabPolicyTest, CollabTransportTest, CollabPanelTest, ControlCollab.

Gates: final `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p16-335` EXIT=0, all eight rows PASS (see the gate table above).

Files committed for new-p10-8: `src/core/CollabSessionSync.cpp`, `tests/src/core/CollabTransportTest.cpp`, `tests/upstream-modifications.txt`, and this report.

## Self-check

- Regression proof: each source invariant regression failed before its product change and in its own reverse-patch run, then passed after restoration. `snapshotSizesAreCheckedBeforeHeaderSerialization`: RED/reverse EXIT=8, GREEN/restored EXIT=0. `snapshotGraceDoesNotConsumeAnUntrackedEdit`: RED/reverse EXIT=8, GREEN/restored EXIT=0. `snapshotReadFailuresPreserveFileReason`: RED/reverse EXIT=8, GREEN/restored EXIT=0. Exact commands and assertions are recorded in each cluster section above.
- Determinism: all three regressions inspect the relevant source function and assert the guard/update/error-check ordering. They use no clock, sleeps, timer races, or injected timestamps; file paths are supplied by the test's existing compile-time source-path definitions.
- Cause and sibling coverage: snapshot limits and byte counts were checked in both `CollabSession::writeSnapshot` and `CollabHost::snapshotFrame`; `CollabClient` consumes only frames returned by the host, so no second encoder path exists. The grace fix is at `trackUntrackedEdit`, called by `pollJournal`. The read failure handling is at the only saved-file read in `writeSnapshot`; its host provider remains `CollabSession::host`'s `setSnapshotProvider` lambda.
- Collateral: the raw limit applies only to collaboration snapshot framing, where even an unencoded file at the 64 MiB wire-line cap cannot fit. Normal project save/open code and project formats are unchanged. Failed or short snapshot reads now refuse sharing rather than forwarding empty or partial bytes.
- Not-real verdict item: not applicable; all three clusters are FIXED, with no ALREADY-FIXED or FALSE-POSITIVE decision.
- Completion check: `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/submit_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p16-335 new-p16-335` first EXIT=1 while the report lacked a first-line verdict and Self-check; after adding both, the same command EXIT=0 with the working tree clean.
