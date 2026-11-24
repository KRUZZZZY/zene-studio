NEEDS-CONTEXT

# Cluster ci-64efb7585b33898b-port: "Port: CI regression: build at af139394af21"

ci-64efb7585b33898b-port: NEEDS-CONTEXT

## What is missing
The finding names only the workflow (`build`) and the commit (`af139394af21`). It does not give the
job (linux-x86_64, linux-arm64, macos, mingw, msvc, msys2 or release-gate;
`.github/workflows/build.yml:8-1590`), the failing step (Build, Run tests, Release-honesty
guard and so on), a run id, or a single error line. Without one of these there is no defect to
reproduce, so I cannot write a test for it or make a fix.

The places I looked, all this session:
- `gh` is not installed: `timeout 30 gh run list --commit af139394...` printed
  `timeout: failed to run command 'gh': No such file or directory`. The hosted run log cannot be read
  from this machine, and this session sends nothing off it.
- The pipeline's CI records do not mention the cluster or the commit. A Haiku search covered
  `CI-BUG-QUEUE.jsonl`, `CI-ACCEPTED.tsv`, `TRAIN-CI-STATUS.json`, `train-ci/` and `logs/train-ci.log`
  under `~/.local/share/zene-bunny-fix/qwen`. I then scanned `BUG-INVENTORY.jsonl`, `CI-BUG-QUEUE.jsonl`,
  `ESCALATIONS.json`, `BUG-ESCALATIONS.json`, `logs/merge-train.log` and `logs/fix.log` for
  `64efb7585b33898b|af139394af21`. Every search came back empty.
- The fff search for `af139394` / `64efb7585b33898b` in the worktree found nothing either.

## What I checked locally (Linux, USE_WERROR=ON, RelWithDebInfo, WANT_VST3/WANT_CLAP=ON)
- `git merge-base --is-ancestor af139394af21 HEAD` returned EXIT=0, so the commit is on this
  branch, 328 commits back.
- `af139394af21` merges cluster new-p15-270 (`8e9db6944c`). Its product and test delta against its
  first parent `1a7ac7354b` is:
  `src/gui/editors/PatternEditor.cpp:266-277` (updatePixelsPerBar clamps to >= 1),
  `src/gui/tracks/AutomationTrackView.cpp:63-75` (float pixels-per-bar in dropEvent),
  `tests/src/gui/AutomationLaneChildGuiTest.cpp:494-519` (aZeroWidthPatternEditorKeepsOnePixelPerBar),
  and two rows in `tests/upstream-modifications.txt`. Since then, no commit has touched these three
  source files (`git log af139394af21..HEAD -- <files>` printed nothing).
- The new test reads `PatternEditorWindow::m_editor`. That member is public
  (`include/PatternEditor.h:86-92`), so the delta has no access error that would only show up on
  some compilers.
- `cmake --build build --target zene AutomationLaneChildGuiTest AutomationLaneChildrenTest -j4 > logs/build-2.log 2>&1`
  returned EXIT=0.
- `cd build/tests && ctest -R 'AutomationLaneChild' --output-on-failure > logs/test-1.log 2>&1` returned
  EXIT=0 (`100% tests passed out of 2`: AutomationLaneChildrenTest and AutomationLaneChildGuiTest).
- The whole hosted Build step (every target) could not be reproduced here. The cmake shim refuses
  builds that name no target:
  `cmake --build build -j4 > logs/build-1.log 2>&1` returned EXIT=97, and the log says
  `bunny-guard: build a named target: cmake --build build --target <zene|SomeTest> (full builds are the parent's job)`.

## What would unblock this
Any one of these: the failing job name with the first compiler or test error lines, the GitHub
Actions run id for `af139394af21b8ca490207d4e2aa9cd1f4273e47`, or a CI-BUG-QUEUE record for
`ci-64efb7585b33898b`. A port lane should get the original failing log line, as cluster
ci-00e6f05f41fa4f1a-port did (`tests/src/plugins/SfzDistoBuffersTest.cpp:60` in its report).

No product or test code was changed, so there is no regression test, fix.patch or proof manifest.
The brief says NEEDS-CONTEXT reports need no manifest.

Delegation: Haiku (1) tried the full build and reported the shim refusal (EXIT=97). Haiku (2)
searched the pipeline's CI records for the cluster and commit and found nothing. Haiku (3) built
zene and the two AutomationLaneChild test targets and ran their ctests (both EXIT=0). Sonnet and
DeepSeek were not used, because nothing was implemented: no fix or test could be designed without
the failure.

## Self-check
I ran `python3 .../qwen/submit_check.py <worktree> ci-64efb7585b33898b-port > logs/submit-1.log 2>&1`
and it returned EXIT=0 (`RESULT: PASS - submit`). Every PASS row passed, including the gates
(feature_check.py). The ? items:
- **Red/green:** none, because no fix was made. The verdict is NEEDS-CONTEXT: no failure was ever
  observed, so nothing exists to turn red.
- **Deterministic:** no test was added, for the same reason. The only tests I ran were the existing
  AutomationLaneChild ctests (EXIT=0, logs/test-1.log).
- **Cause and completeness:** no product code was changed, so no caller or sibling is affected.
- **No collateral damage:** no product code, data format or limit was changed. This commit adds the
  report file only.
- **ALREADY-FIXED / FALSE-POSITIVE evidence:** neither applies; the verdict is NEEDS-CONTEXT.
- **Sibling commands (app.*, arrangement.*):** this commit changes no command file. The check lists
  test files and commands that earlier commits on this branch brought in relative to master
  (tests/control-*.py), and none of them is touched here.

## Other findings
None.
