new-p21-235: FIXED

Evidence: REAL. `f_cnt_t` is `std::uint64_t` (`include/LmmsTypes.h:43`), so a negative signed argument previously converted to a huge unsigned count before `feed`'s null/zero-only guard (`src/core/MasterLoudnessTap.cpp:118-120`). The signed integral overload rejects null and nonpositive counts before conversion; the regression also confirms a positive signed count still feeds one frame (`include/MasterLoudnessTap.h:170-176`).

Commands and results:

- Original RED build: `cmake --build build --target zene MeterTapTest -j2 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p21-235.r1/logs/build-1.log 2>&1; echo EXIT=$?` — EXIT=0.
- RED: `ctest -R '^MeterTapTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p21-235.r1/logs/test-1.log 2>&1; echo EXIT=$?` from `build/tests` — EXIT=8; the new assertion failed because no signed overload existed (11 QtTest cases passed, 1 failed).
- GREEN build: `cmake --build build --target zene MeterTapTest -j4 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p21-235.r1/logs/build-3.log 2>&1; echo EXIT=$?` — EXIT=0.
- GREEN: `ctest -R '^MeterTapTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p21-235.r1/logs/test-2.log 2>&1; echo EXIT=$?` from `build/tests` — EXIT=0; 1/1 ctest target passed.
- 4b reverse: `git diff -- include/MasterLoudnessTap.h > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p21-235.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p21-235.r1/logs/fix.patch; echo EXIT=$?` — EXIT=0.
- 4b RED build: `cmake --build build --target zene MeterTapTest -j4 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p21-235.r1/logs/build-4.log 2>&1; echo EXIT=$?` — EXIT=0. 4b RED: `ctest -R '^MeterTapTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p21-235.r1/logs/test-3.log 2>&1; echo EXIT=$?` from `build/tests` — EXIT=8; the same assertion failed.
- 4b restore: `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p21-235.r1/logs/fix.patch; echo EXIT=$?` — EXIT=0. Restore build: `cmake --build build --target zene MeterTapTest -j4 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p21-235.r1/logs/build-5.log 2>&1; echo EXIT=$?` — EXIT=0. Restore GREEN: `ctest -R '^MeterTapTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p21-235.r1/logs/test-4.log 2>&1; echo EXIT=$?` from `build/tests` — EXIT=0.
- Feature gate after regenerating stale `tests/all-sources.txt` entries with its documented recipe: `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p21-235 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p21-235.r1/logs/gate-2.log 2>&1; echo EXIT=$?` — EXIT=0, all gates PASS.

Affected family: `MeterTapTest`, 1/1 ctest target; its pre-fix run reported 11 passed and the new regression failed. The initial `-j2` rebuild was interrupted to increase parallelism (EXIT=130); the completed `-j4` builds above succeeded.

Files committed for this bug: `include/MasterLoudnessTap.h`, `tests/src/core/MeterTapTest.cpp`, this report.
