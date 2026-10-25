FALSE-POSITIVE
new-p23-200: FALSE-POSITIVE

Evidence: `src/core/ParameterAliases.cpp:141` passes the original string to `DeviceOptions::setAlias`, but `src/core/DeviceOptions.cpp:64-68` trims the value before storing it and removes it when the trimmed value equals the key. The reported input `" Foo "` therefore stores as `"Foo"`, not with surrounding whitespace. `DeviceOptions::load` also routes saved aliases through this setter at `src/core/DeviceOptions.cpp:101`.

No regression test or RED/GREEN run was added for this finding: the described behavior is already prevented by the existing storage contract, so a test for surrounding whitespace would pass before any change. Procedure step 2 directs that a non-real finding skip to the report with code evidence.

Commands and results:

- `nl -ba src/core/DeviceOptions.cpp | sed -n '60,72p'` — EXIT=0.
- `nl -ba src/core/ParameterAliases.cpp | sed -n '135,166p'` — EXIT=0.
- `rg -n 'void DeviceOptions::load|setAlias\(it.key' src/core/DeviceOptions.cpp` — EXIT=0.
- `rg -n '^src/core/ParameterAliases.cpp$|^tests/src/core/DeviceToolsTest.cpp$' tests/fork-sources.txt tests/all-sources.txt` — EXIT=0.

new-p23-201: FIXED

Evidence: `src/core/ParameterAliases.cpp:155-159` now records a display name in `moved` only if the stable-key destination is empty and `DeviceOptions::setAlias` leaves a nonempty value at that key. The second check matters because `DeviceOptions::setAlias` removes an entry when its alias equals its own key (`src/core/DeviceOptions.cpp:66-68`). The source display-name entry is cleared only for names in `moved` (`ParameterAliases.cpp:161`).

Test matrix:

| Case | Check | Result |
| --- | --- | --- |
| Successful legacy migration | Existing `anAliasFollowsTheParameterNotItsName` verifies a free key adopts the old alias while an unrelated legacy alias remains. | Passed in the DeviceTools family. |
| Stable key already occupied | `aLegacyAliasSurvivesWhenItsStableKeyIsAlreadyOccupied` asserts the current keyed alias and legacy name alias both remain, and migration reports false. | Fails without the fix; passes with it. |
| Destination write rejected | `aLegacyAliasSurvivesWhenItsValueCannotBeStoredUnderItsKey` uses the stable key as the alias value; `DeviceOptions::setAlias` rejects the destination, and the old entry must remain. | Fails without the fix; passes with it. |
| Exhaustion and reconfiguration | The routine has no retry, capacity, or reconfiguration state; there is no applicable exhaustion/reconfiguration path. | Not applicable. |
| Realtime and undo | Production callers are project loading (`Effect::loadSettings`, `Effect.cpp:233`) and the existing `paramRename` options edit (`ControlCommandsDeviceParams.cpp:150`), which remains inside `editOptions` and its existing undo step (`ControlCommandsDeviceParams.cpp:139`). No audio-thread path or command was added. | No realtime or undo changes. |
| Supported platforms | Built `zene`, `DeviceToolsTest`, and `DeviceToolsChainTest` with this configured C++20/Qt build. The change uses existing Qt types and changes no header or platform-specific API. | No separate Windows/macOS build was available or run. |

RED/GREEN and reversal proof:

- Revert only the product hunk: `git diff -- src/core/ParameterAliases.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p23-200.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p23-200.r1/logs/fix.patch` — EXIT=0.
- RED build with product reverted: `cmake --build build --target zene DeviceToolsTest` — EXIT=0; log `build-022-red-data-loss-tests.log`.
- RED test from `build/tests`: `ctest -R '^DeviceToolsTest$' --output-on-failure` — EXIT=8; log `test-020-red-data-loss-tests.log`. The suite registered one CTest test; both new Qt test methods fail on data-loss assertions (`""` instead of `"Legacy"`, and an empty source instead of the key string).
- Restore product: `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p23-200.r1/logs/fix.patch` — EXIT=0.
- GREEN build: `cmake --build build --target zene DeviceToolsTest DeviceToolsChainTest` — EXIT=0; log `build-023-green-data-loss-tests.log`.
- GREEN family from `build/tests`: `ctest -R '^DeviceTools(Chain)?Test$' --output-on-failure` — EXIT=0; log `test-021-green-final-family.log`, 2/2 CTest tests passed.

Gate registration repair: the first `feature_check.py` run returned EXIT=1 because the derived `tests/all-sources.txt` still contained vendored SFizz/Abseil paths that its own header recipe excludes. The changed C++ files were already present in both source manifests. I re-generated the entry list with the recipe from the manifest header (3,067 entries; EXIT=0): `bash -c "{ git ls-files '*.c' '*.cc' '*.cpp' '*.cxx' '*.h' '*.hpp' | grep -vE '^(src/3rdparty/|tests/reference/|plugins/NeuralAmp/(rtneural|nam|tests)/|plugins/RnnoiseDenoiser/rnnoise/|plugins/SfzPlayer/sfizz/)' ; git ls-files tests/control-detect-commands.py tests/control_detect_fixtures.py ; } | LC_ALL=C sort -u"`. No product source registration changed.

Files in the new-p23-201 commit: `src/core/ParameterAliases.cpp`, `tests/src/core/DeviceToolsTest.cpp`, `tests/all-sources.txt`, and this report.

## Self-check

- Regression proof: new-p23-200 is a false positive, so its described whitespace case cannot fail against the current `DeviceOptions::setAlias` contract. For new-p23-201, the reversed-product RED run returned EXIT=8 and the restored GREEN family run returned EXIT=0 (2/2 CTest tests).
- Determinism: the two regression cases use stack-local effects and fixed alias strings; they use no clock, sleep, race, or external state.
- Cause and callers: searched every `adoptLegacyAliases` caller (`ParameterAliases::setAlias` and `Effect::loadSettings`) and the only production `parameteraliases::setAlias` caller (`paramRename`). Both paths retain the same key and migration policy; no new control command was added.
- Collateral and saved projects: the pre-existing successful migration and unrelated legacy-alias preservation test still pass. `DeviceOptions::load` continues to normalize aliases through its existing setter. Only a source alias that was not copied is now retained.
- Audio, undo, and portability: the changed migration code is only reached on project load or the existing control-options edit path; it adds no audio-thread work. The options edit keeps its existing undo step. No header or Qt-version-specific API changed; only the configured platform build was run.
- Gates: `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p23-200`, logged as `gate-007-post-report.log` — EXIT=0 (all eight rows PASS).
- Completion gate: `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/submit_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p23-200 new-p23-200`, logged as `submit-001-final.log` — EXIT=0 (`RESULT: PASS - submit`).
