FIXED
new-p23-184: FIXED

Evidence: `plugins/NeuralAmp/nam/NamModelLoader.cpp:158-165` now raises `ParseError` unless a declared non-null `sample_rate` is finite and positive. `tests/src/core/NamModelLoaderTest.cpp:163-193,208-230` covers zero, negative, exponent overflow, valid positive, and null rates; the existing absent-rate and file-size-limit fixtures continue to load. The test writes rejected then valid models to the same path. This check runs during loading; no render/process path changes.

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

Files in commit `4fc6b5086` (`Validate NAM sample rate (new-p23-184)`): `plugins/NeuralAmp/nam/NamModelLoader.cpp`, `tests/src/core/NamModelLoaderTest.cpp`, and this report. The test source was already registered in `tests/fork-sources.txt` as fork-added.

new-p9-312: FIXED

Evidence: `plugins/NeuralAmp/nam/NamModelLoader.cpp:366-374` checks every JSON weight after conversion to `float` and throws `ParseError` before engine construction if it is non-finite. `head_scale` is the final element of that same checked weight vector (`NamModelLoader.cpp:464-465`). `tests/src/core/NamModelLoaderTest.cpp:196-204` supplies `1e100` once at tensor index 0 and once at the final head-scale index; both conversions must be refused with a weight error.

Commands and exit codes:

- `cmake --build build --target zene NamModelLoaderTest -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p23-184.r1/logs/020-red-weight-build.log 2>&1` — EXIT 0.
- `ctest -R '^NamModelLoaderTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p23-184.r1/logs/021-red-weight-test.log 2>&1` (from `build/tests`) — EXIT 8; assertion output: `loader accepted or misreported invalid model: tensor weight overflow ()`.
- `cmake --build build --target zene NamModelLoaderTest -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p23-184.r1/logs/039-final-weight-build.log 2>&1` — EXIT 0.
- `ctest -R '^NamModelLoaderTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p23-184.r1/logs/040-final-weight-test.log 2>&1` (from `build/tests`) — EXIT 0; 1/1 test passed.
- `git diff -- plugins/NeuralAmp/nam/NamModelLoader.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p23-184.r1/logs/fix.patch` — EXIT 0.
- `git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p23-184.r1/logs/fix.patch` — EXIT 0.
- `cmake --build build --target zene NamModelLoaderTest -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p23-184.r1/logs/041-final-reverse-weight-build.log 2>&1` — EXIT 0.
- `ctest -R '^NamModelLoaderTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p23-184.r1/logs/042-final-reverse-weight-test.log 2>&1` (from `build/tests`) — EXIT 8; both `tensor weight overflow` and `head_scale overflow` were accepted without the fix.
- `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p23-184.r1/logs/fix.patch` — EXIT 0.
- `cmake --build build --target zene NamModelLoaderTest -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p23-184.r1/logs/043-final-restored-weight-build.log 2>&1` — EXIT 0.
- `ctest -R '^NamModelLoaderTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p23-184.r1/logs/044-final-restored-weight-test.log 2>&1` (from `build/tests`) — EXIT 0; 1/1 test passed.

Files in commit `f431b7489` (`Reject overflowing NAM weights (new-p9-312)`): `plugins/NeuralAmp/nam/NamModelLoader.cpp`, `tests/src/core/NamModelLoaderTest.cpp`, and this report.

Gate repair and final gate results:

`tests/all-sources.txt` had 1,148 excluded SfzPlayer vendor entries and failed its own header recipe. The entry list was regenerated from that recipe and committed as `76b963438` (`Reproduce source manifest recipe (new-p23-184)`), together with the reason in `tests/upstream-modifications.txt`.

- `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p23-184 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p23-184.r1/logs/028-feature-check-final.log 2>&1` — EXIT 1 before the manifest repair; the two source-manifest gates failed on the excluded vendor entries.
- The exact recipe used to generate `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p23-184.r1/logs/029-all-sources-expected.txt` — EXIT 0:

  ```sh
  set -o pipefail
  { git ls-files '*.c' '*.cc' '*.cpp' '*.cxx' '*.h' '*.hpp' | grep -vE '^(src/3rdparty/|tests/reference/|plugins/NeuralAmp/(rtneural|nam|tests)/|plugins/RnnoiseDenoiser/rnnoise/|plugins/SfzPlayer/sfizz/)' ; git ls-files tests/control-detect-commands.py tests/control_detect_fixtures.py ; } | LC_ALL=C sort -u > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p23-184.r1/logs/029-all-sources-expected.txt
  ```
- The documented verification `diff <(grep -vE '^[[:space:]]*(#|$)' tests/all-sources.txt) <({ git ls-files '*.c' '*.cc' '*.cpp' '*.cxx' '*.h' '*.hpp' | grep -vE '^(src/3rdparty/|tests/reference/|plugins/NeuralAmp/(rtneural|nam|tests)/|plugins/RnnoiseDenoiser/rnnoise/|plugins/SfzPlayer/sfizz/)' ; git ls-files tests/control-detect-commands.py tests/control_detect_fixtures.py ; } | LC_ALL=C sort -u) > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p23-184.r1/logs/030-all-sources-reproduce.log 2>&1` — EXIT 0.
- `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p23-184 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p23-184.r1/logs/049-feature-check-final-report.log 2>&1` — EXIT 0.

| Gate | Result | Exit |
|---|---|---:|
| fork-sources | PASS | 0 |
| all-sources-reproduce | PASS | 0 |
| no-upstream-regression | PASS | 0 |
| check-namespace | PASS | 0 |
| no-tautology | PASS | 0 |
| doc-figures | PASS | 0 |
| complexity (no new item) | PASS | 0 |
| file-length (no new item) | PASS | 0 |
| feature_check total | PASS | 0 |
| submit_check | PASS | 0 |

`python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/submit_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p23-184 new-p23-184 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p23-184.r1/logs/046-submit-check-after-commit.log 2>&1` — EXIT 0 (`RESULT: PASS - submit`).

Files in separate gate-repair commit `76b963438` (`Reproduce source manifest recipe (new-p23-184)`): `tests/all-sources.txt` and `tests/upstream-modifications.txt`.

## Self-check

- RED/GREEN proof: with the sample-rate product patch reversed, `NamModelLoaderTest` failed at `sample_rate: 0` (exit 8, log 016); after restoring the patch it passed 1/1 (exit 0, log 018). With the weight patch reversed, the same test reported both the tensor and `head_scale` fixtures accepted (exit 8, log 042); after restoring it passed 1/1 (exit 0, log 044). Each reversal rebuilt `zene` and the test target first.
- Determinism: the assertions use fixed model dimensions and literal JSON values (`0`, `-1`, `1e400`, `48000`, `null`, and `1e100`). The fixture directory uses the process ID so concurrent test processes have separate paths. No wall-clock value, sleep, timing threshold, or race affects the assertions.
- Cause/completeness: `loadFromFile()` call sites in `NeuralAmpEffect.cpp` and the NAM test/harness/profile tools all use this loader. The `sample_rate` check follows the JSON-to-double conversion. The one `get<float>()` in `NamModelLoader.cpp` is followed by the finiteness check for every weight before `NamModel` construction; `head_scale` is read from the final element of that checked vector. No sibling weight conversion path in this loader remains unchecked.
- Collateral behavior: omitted and null rates keep the documented `-1.0` sentinel, a declared `48000` rate and finite weights load, and existing dimension/file-size fixtures still pass. The fix adds no upper bound for legitimate finite rates or weights; it rejects only non-finite/non-positive rates and non-finite float conversions.
- Not-real verdict prompt: not applicable; both bug rows are `FIXED`, supported by the source lines and RED/GREEN evidence above.
