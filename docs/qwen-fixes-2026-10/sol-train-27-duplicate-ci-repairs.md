# Train 27: supersede CI repairs already retained by recovery

Base examined: `4d10173b58` (Merge recovery/terra-20261111).
The train aborted before building when it tried to replay `c99455b15c`.
Its AudioEngine fix, regression scanner and issue report already match this
checkout byte for byte. Its CMake and registration additions are also present;
the old append contexts conflict with the cumulative recovery changes.

The other six pending CI repairs are likewise retained. This is a metadata-only
supersession: no product source, test assertion, gate executable or baseline is
changed. The replacement delta is this report and its source-ledger registration.
The runner must retain the recovery candidate when applying this delta.

## Repair records replaced

| Pending ID | Preserved commit | Retention check |
| --- | --- | --- |
| ci-repair-ci-512849fa221097db-c6addd986190 | c99455b15c | AudioEngine header/source, TLS scanner and report identical |
| ci-repair-ci-12f4249981b085d6-d96907bf0ca9 | 7b80302db8 | Darwin scanner/report identical; combined fixture retains Darwin feature macro |
| ci-repair-ci-c487f7d0c9f0a1ac-7bbdafe11505 | ac17b5dddd | NAM probe, loader test and report identical |
| ci-repair-ci-0ff72c5279856e5d-e60aa807e1d8 | 492177a2f0 | Root CMake, EnsureThreads module, regression driver and report identical |
| ci-repair-ci-748342d46d7e9dd9-e37492cefca5 | 9b0dcb1b24 | Threads scope regression and report identical |
| ci-repair-ci-d14f7617bae6160d-326e36c3d405 | 8a0311b2ee | Fixture CMake, chunk check, Darwin feature check and report identical |
| ci-repair-ci-642b4d09f4421388-e0ba3346027b | b540436f42 | Every listed file identical, including product and regression sources |

Compared every path in each pending repair record using `git show COMMIT:PATH`
against the working file. Apart from the shared CMake/manifests and the combined
VST fixture, all files are identical. For the shared CMake/manifests, every added
nonblank, noncomment line from each original repair is retained (zero missing).
The combined fixture preserves POSIX nanosleep visibility and explicitly enables
Darwin's full feature level; neither accepted scanner was dropped.

`git show c99455b15c | git apply --reverse --check` exits 1 only for
tests/CMakeLists.txt, tests/fork-sources.txt and tests/upstream-modifications.txt;
the retained product and regression-file changes match. No cherry-pick, commit,
queue edit or source mutation outside this private checkout was performed.

## Verification in this checkout

Each command below exited 0:

```
python3 tests/exported-class-tls-check.py .
python3 tests/threads-target-windows-configure.py /tmp/zene-sol-21-threads
bash tests/cmake-threads-scope.sh
python3 tests/vst2-fixture-darwin-headers-check.py /usr/bin/cc tests/data/vst2-test-instrument/vst2-test-instrument.c
python3 tests/data/vst2-test-instrument/darwin-feature-level-check.py /usr/bin/cc GNU
bash tests/no-upstream-regression-gate.sh
bash tests/evidence-gate.sh
bash tests/evidence-gate.sh --tree docs/qwen-fixes-2026-10
git diff --check
```

The TLS check's own red/green fixtures pass. Both Windows-shaped CMake configure
regressions pass, as do native/simulated Darwin fixture compilation and both
feature-level probes. These are existing regression drivers, not native Windows
or macOS verification. Gate 6 accepts the provenance ledger and Gate 11 refuses
zero files under its unchanged 1 MiB cap and existing seven exemptions.
The explicit documentation-tree scan includes this new, untracked Markdown
report. Staging the two explicit paths was refused by the shared repository
object database permissions (git add exit 128); files are left unstaged for the
runner to commit as instructed. No permission or repository-owner changes were
attempted.

No new DAW build or ctest run is claimed: this replacement changes metadata
only and retains the independently reviewed source repairs. The full train,
remaining gates and native hosted builds still have to pass after duplicate
repair replay is removed. In particular, this supersession does not certify the
recovery candidate for promotion or waive its remaining verification.
