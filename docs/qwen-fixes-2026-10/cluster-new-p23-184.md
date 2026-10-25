new-p23-184: FIXED

Evidence: `plugins/NeuralAmp/nam/NamModelLoader.cpp:158-165` now raises `ParseError` unless a declared non-null `sample_rate` is finite and positive. `tests/src/core/NamModelLoaderTest.cpp:137-167,170-195` covers zero, negative, exponent overflow, valid positive, and null rates; the existing absent-rate and file-size-limit fixtures continue to load. The test writes rejected then valid models to the same path. This check runs during loading; no render/process path changes.

Commands and exit codes:

- `cmake --build build --target NamModelLoaderTest -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p23-184.r1/logs/004-red-sample-rate-final-build.log 2>&1` — EXIT 0.
- `ctest -R '^NamModelLoaderTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p23-184.r1/logs/005-red-sample-rate-final-test.log 2>&1` (from `build/tests`) — EXIT 8; assertion output: `loader accepted or misreported invalid sample_rate: zero ()`.
- `cmake --build build --target zene NamModelLoaderTest -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p23-184.r1/logs/006-green-sample-rate-build.log 2>&1` — EXIT 0.
- `ctest -R '^NamModelLoaderTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p23-184.r1/logs/007-green-sample-rate-test.log 2>&1` (from `build/tests`) — EXIT 0; 1/1 test passed.
- `cmake --build build --target zene NamModelLoaderTest -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p23-184.r1/logs/013-final-sample-rate-build.log 2>&1` — EXIT 0.
- `ctest -R '^NamModelLoaderTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p23-184.r1/logs/014-final-sample-rate-test.log 2>&1` (from `build/tests`) — EXIT 0; 1/1 test passed with the final helper layout.
- `git diff -- plugins/NeuralAmp/nam/NamModelLoader.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p23-184.r1/logs/fix.patch` — EXIT 0.
- `git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p23-184.r1/logs/fix.patch` — EXIT 0.
- `cmake --build build --target zene NamModelLoaderTest -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p23-184.r1/logs/015-final-reverse-sample-build.log 2>&1` — EXIT 0.
- `ctest -R '^NamModelLoaderTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p23-184.r1/logs/016-final-reverse-sample-test.log 2>&1` (from `build/tests`) — EXIT 8; the zero-rate assertion failed again.
- `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p23-184.r1/logs/fix.patch` — EXIT 0.
- `cmake --build build --target zene NamModelLoaderTest -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p23-184.r1/logs/017-final-restored-sample-build.log 2>&1` — EXIT 0.
- `ctest -R '^NamModelLoaderTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p23-184.r1/logs/018-final-restored-sample-test.log 2>&1` (from `build/tests`) — EXIT 0; 1/1 test passed.

Regression matrix: ordinary finite weights with omitted, null, and positive sample rates load; zero and negative declared rates refuse with a `sample_rate` error; the bundled JSON parser rejects `1e400` as an overflowing JSON number. Existing malformed-dimension and parse-size cases are retained. Loading remains off the audio thread; the test target is Qt-free C++20 on the configured Linux build.

Feature gate command: `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p23-184 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p23-184.r1/logs/019-feature-check-bug1-final.log 2>&1` — EXIT 1. `complexity` and `file-length` pass with no new items; no-upstream-regression, namespace, tautology, and doc-figures pass. `fork-sources` and `all-sources-reproduce` fail because the unchanged `tests/all-sources.txt` contains excluded `plugins/SfzPlayer/sfizz/external/abseil-cpp` paths. `git diff --exit-code -- tests/fork-sources.txt tests/all-sources.txt` — EXIT 0, confirming those gate manifests were not changed by this fix.

Files in this bug's commit: `plugins/NeuralAmp/nam/NamModelLoader.cpp`, `tests/src/core/NamModelLoaderTest.cpp`, and this report. The test source was already registered in `tests/fork-sources.txt` as fork-added.
