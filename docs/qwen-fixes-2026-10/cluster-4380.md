4380: FIXED

Decision: REAL. `src/core/RenderQueue.cpp:312-338` sent renderer stderr to the null device and finished with a generic process error. `TaskCenterTest::failedRenderSurfacesChildDiagnostics` now launches the queue's fake CLI child, which emits “encoder refused the requested output” and exits 3; its job report includes that text. `appendDiagnosticTail` retains at most 512 bytes. The test failed on the original production code and failed again when the production patch was removed, then passed after restoration.

Regression commands and exit codes (run from the stated directories):

- RED build: `cmake --build build --target zene TaskCenterTest` — EXIT=0 (`build-6.log`).
- RED test from `build/tests`: `ctest -R '^TaskCenterTest$' --output-on-failure` — EXIT=8 (`test-6.log`), missing-diagnostic assertion.
- GREEN build: `cmake --build build --target zene TaskCenterTest` — EXIT=0 (`build-7.log`).
- GREEN test from `build/tests`: `ctest -R '^TaskCenterTest$' --output-on-failure` — EXIT=0 (`test-7.log`), 1/1 CTest target passed.
- 4b removal command: `git diff -- src/core/RenderQueue.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/4380.r1/logs/fix-4380.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/4380.r1/logs/fix-4380.patch`; rebuild — EXIT=0 (`build-8.log`); test from `build/tests` — EXIT=8 (`test-8.log`), same missing-diagnostic assertion.
- 4b restore command: `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/4380.r1/logs/fix-4380.patch && cmake --build build --target zene TaskCenterTest`; rebuild — EXIT=0 (`build-9.log`); test from `build/tests` — EXIT=0 (`test-9.log`), 1/1 CTest target passed.

Gates: `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-4380` initially EXIT=1 (`feature-check-1.log`): the changed test exceeded the file-length ratchet and the existing whole-tree manifest did not match its declared recipe. The regression was moved to the smaller registered `TaskCenterTest`; `tests/all-sources.txt` was regenerated with its own header recipe, removing stale vendored sfizz entries. `bash tests/all-sources-reproduce.sh` then printed REPRODUCES, and the same feature-check command exited 0 (`feature-check-2.log`); all eight rows passed, including complexity and file-length with no new items. The renderer and worker run outside the audio thread; no audio callback changed.

Files in the 4380 fix commit: `src/core/RenderQueue.cpp`, `tests/src/core/TaskCenterTest.cpp`, `tests/all-sources.txt`, and this report.
