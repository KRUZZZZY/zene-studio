FIXED - cluster ci-v041-1: Qt 5 moc cannot parse a trailing return type in Song.h (linux-x86_64 CI build fails)

Verdict: **REAL**, reproduced on this box with the actual moc of the failing job, fixed, and now held in place by a ctest check.

## Evidence the bug is real

- The hosted linux-x86_64 job runs on `ubuntu-22.04` (`.github/workflows/build.yml:43`) and installs
  the distribution's Qt 5, which is **5.15.3**. I downloaded Ubuntu's
  `qtbase5-dev-tools_5.15.3+dfsg-2ubuntu0.2_amd64.deb` (sha256 `9e1d7278…cda5`) into
  `$TMPDIR/ci-v041-1-moc5153/` and ran its `moc` (`moc 5.15.3`) on the unfixed header:
  `moc -Iinclude -Ibuild -o /dev/null include/Song.h` → **EXIT=1**, output
  `include/Song.h:706: Parse error at ";"`. That is byte-for-byte the CI error
  (log `02-moc5153-song-before.log`).
- The cause is `include/Song.h:708` (pre-fix) `auto prepareConverterModeForInstruments(AudioResampler::Mode mode) noexcept -> bool;`.
  It sits in `public slots:` (`include/Song.h:693`), so moc parses it as a meta-method. Qt 5.15.3's moc
  has no trailing-return-type support there. A trailing return type on an ordinary member is fine on every moc:
  upstream uses it widely (`include/Timeline.h:48`, `include/Song.h:379-382`), and none of those failed.
- The host's own Qt 5 moc (`/usr/bin/moc-qt5`, 5.15.19) and Qt 6's moc both accept it (EXIT=0). That is why
  no build on this box ever noticed.
- A sweep of the real 5.15.3 moc over **all 1127** git-tracked files that declare Q_OBJECT/Q_GADGET/Q_NAMESPACE
  (log `04-moc5153-sweep-before.log`) failed on exactly **2** files:
  `include/Song.h` and `tests/src/plugins/ZynAddSubFxInstrumentTest.cpp:253` (`Parse error at "static"`).
  The second file has three `static auto … -> T` helpers inside `private slots:` (`renderPeriod`, `sameSamples`,
  `firstDifference`; moc stops at the first). CI builds tests, so this would have been the next failure after Song.h.

## The fix (smallest, no behaviour change)

- `include/Song.h:709`: `bool prepareConverterModeForInstruments(AudioResampler::Mode mode) noexcept;`
  (leading return type, with a one-line comment saying why). It has the same signature and ABI, and the slot is unchanged.
- `src/core/Song.cpp:1348`: the definition is spelled the same way, to match its declaration.
- `tests/src/plugins/ZynAddSubFxInstrumentTest.cpp:255-281`: the three slot-section helpers get leading return types.
- No platform guard is needed: leading-return syntax is valid on every compiler and moc. The change uses no Qt-6-only
  API and no Windows-macro identifiers (`small`, `near`, `far`, `interface`).
- Callers checked: `src/core/AudioEngine.cpp:301` (`!song->prepareConverterModeForInstruments(mode)`). Its siblings
  `adoptPreparedConverterModeForInstruments` / `discardPreparedConverterModeForInstruments`
  (`include/Song.h:710-711`) already use `void` leading form. Nothing else references it.

## Regression test

`tests/moc-qt5-trailing-return-check.py`, registered as ctest **`MocQt5TrailingReturnCheck`**
(`tests/CMakeLists.txt`, beside `GoldenAudioSelfTest`, wherever a python3 is). It reads sources only, so it is
deterministic: no moc, no build, no timing. It walks `include/ src/ plugins/ tests/` for Q_OBJECT/Q_GADGET files and
tracks each class body's access section. It reports any `slots:` / `signals:` / `Q_INVOKABLE` / `Q_SCRIPTABLE` /
`Q_SIGNAL` / `Q_SLOT` declaration whose parameter list is followed by `->`. First it runs its own red/green fixtures
(slot, signal, Q_INVOKABLE and a multi-line static slot helper are flagged; ordinary members, inline bodies with `->`,
lambdas, nested helper structs and a non-Q_OBJECT file are not), so a scanner that cannot fail, fails.
Over the unfixed tree it reported exactly the same files as the real 5.15.3 moc sweep, with no false positive.

## Commands and exit codes (logs in `~/.local/share/zene-bunny-fix/qwen/runs/ci-v041-1.r1/logs/`)

| Step | Command | EXIT |
| --- | --- | --- |
| Repro (real CI moc) | `$TMPDIR/ci-v041-1-moc5153/x/usr/lib/qt5/bin/moc -Iinclude -Ibuild -o /dev/null include/Song.h` (unfixed) | 1 (`include/Song.h:706: Parse error at ";"`) |
| Sweep before | 5.15.3 moc over 1127 Q_OBJECT files (`04-moc5153-sweep-before.log`) | 2 files FAIL |
| Build (regen) | `CCACHE_TEMPDIR=$TMPDIR/ci-v041-1-ccache-tmp cmake --build build --target zene -j4` (`10-build-zene.log`) | 0 |
| **RED** | `ctest -R '^MocQt5TrailingReturnCheck$' --output-on-failure` (`09-ctest-red.log`) | 8 (1 test run, 1 failed: `include/Song.h:708` + 3 Zyn helpers) |
| Sweep after | 5.15.3 moc over the same 1127 files (`11-moc5153-sweep-after.log`) | 0 of 1127 FAIL |
| **GREEN** | `ctest -R '^MocQt5TrailingReturnCheck$' --output-on-failure` (`12-ctest-green.log`) | 0 (1/1 passed) |
| Build after fix | `cmake --build build --target zene ControlBounceSupportTest TapTempoTest StartHubTest ZynAddSubFxInstrumentTest AudioResamplerRatioTest -j4` (`13-build-after-fix.log`) | 0 |
| Family | `ctest -R '^(MocQt5TrailingReturnCheck\|ControlBounceSupportTest\|TapTempoTest\|StartHubTest\|ZynAddSubFxInstrumentTest\|AudioResamplerRatioTest)$' --output-on-failure` (`14-ctest-family.log`) | 0 (**6/6** passed) |
| 4b revert | `git diff -- include/Song.h src/core/Song.cpp > logs/fix.patch && git apply -R logs/fix.patch` → rebuild `zene` (`15-…`, EXIT 0) → ctest (`16-ctest-reverted.log`) | apply 0, build 0, **ctest 8** (Song.h:708 flagged) |
| 4b re-apply | `git apply logs/fix.patch` → rebuild `zene` (`17-…`, EXIT 0) → ctest (`18-ctest-reapplied.log`) | apply 0, build 0, **ctest 0** |
| Gates | `feature_check.py <worktree>`, run unchanged through `logs/scratch/run_feature_check.py` (`20-feature-check.log`) | 0 (8/8 PASS) |
| Windows (mingw64 job) | mingw PPA `qt5base-mingw-w64_5.15.6-1` x86_64 moc over the same 1127 files (`21-moc5156-mingw-sweep.log`) | 0 of 1127 FAIL |

Gate table (`20-feature-check.log`): fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace,
no-tautology and doc-figures all PASS (exit 0). Complexity and file-length PASS ("no new item in your files").

## Environment notes (honest)

- `cmake build` (reconfigure) is refused by the lane guard (`06-reconfigure.log`, EXIT 97). The build regenerated
  itself through `cmake --build`.
- The first `cmake --build` failed (`08-build-zene-regen.log`, EXIT 2) because ccache could not create its temp file
  under `~/.local/share/zene-bunny-fix/dsh/home/ccache-tmp-r6` (Permission denied: outside this lane's write-jail).
  This was not a code error. Every later build set `CCACHE_TEMPDIR=$TMPDIR/ci-v041-1-ccache-tmp`.
- `feature_check.py` writes its per-gate logs to `<worktree>/../zene-qwen-ci-v041-1-feature-check/`, which this
  jail cannot create (`19-feature-check.log`, PermissionError, EXIT 1). I ran the identical script through a wrapper
  that only redirects that log folder into the LOG FOLDER. No gate, limit or baseline was touched. For the same
  reason, the `gates` row of `submit_check.py` (which calls `feature_check.py` directly) may fail inside this jail,
  but it will pass when the loop runs it outside.
- Not rebuilt: the hundreds of other targets that include `Song.h`. The edit changes only how the return type is
  spelled (same type, same signature), and `zene` (all of `lmmsobjs`, including `Song.cpp` and `AudioEngine.cpp`)
  plus 5 test targets were rebuilt and run.

## Note for the owner's Windows x64 build

The Windows x64 package comes from the `mingw64` job (`.github/workflows/build.yml`, Qt from tobydox's PPA,
`qt5base-mingw-w64_5.15.6`), and the MSVC job uses Qt 6.8 (`build.yml:1145`). Both mocs accept a trailing return type:
the 5.15.6 mingw moc passes the **unfixed** `Song.h` (EXIT 0) and the whole tree (0 of 1127). So this error stopped
the linux-x86_64 job, not the Windows jobs. If the Windows job also failed in run 40237490965, the cause is elsewhere
and this fix will not change it. Hosted CI on a `ci/*` branch confirms both.

## Self-check

- **The regression test FAILS with the product change reverted and PASSES with it.** Reverting
  `include/Song.h` + `src/core/Song.cpp` (`git apply -R logs/fix.patch`, EXIT 0) and rebuilding `zene` (EXIT 0):
  `ctest -R '^MocQt5TrailingReturnCheck$'` → EXIT 8, flagging `include/Song.h:708`. Re-applying (EXIT 0) and
  rebuilding (EXIT 0): EXIT 0, 1/1 passed.
- **The test is deterministic.** It is a pure source scan with fixed inline fixtures. It uses no moc, no build output,
  no clock, no sleeps and no threads. Its verdict matched the real Qt 5.15.3 moc on the whole tree.
- **The fix is at the cause and complete.** The real 5.15.3 moc was run over every Q_OBJECT/Q_GADGET/Q_NAMESPACE
  file: 2 failing files before, 0 after. The scanner also catches the two Zyn helpers that moc never reached. The only
  caller (`src/core/AudioEngine.cpp:301`) and the two sibling slots (`include/Song.h:710-711`) were checked. The
  `.cpp` definition was changed to match its declaration.
- **No collateral damage.** No behaviour change: the type, signature, slot registration and ABI are identical. No
  project/save data is involved. No limit or check was added to product code; the new check reads sources only. It
  adds nothing on the audio thread, no undo step and no control command (the snapshot canary does not move). The
  family tests pass 6/6.
- **ALREADY-FIXED / FALSE-POSITIVE evidence.** N/A: the verdict is FIXED, and the bug was reproduced with the actual
  Qt 5.15.3 moc (EXIT 1, the identical message).
- **Registration.** `tests/moc-qt5-trailing-return-check.py` was appended to `tests/fork-sources.txt`.
  `include/Song.h`, `src/core/Song.cpp` and `tests/CMakeLists.txt` each got an entry with a reason in
  `tests/upstream-modifications.txt` (ends with a newline). The fork-sources, all-sources-reproduce and
  no-upstream-regression gates pass.
