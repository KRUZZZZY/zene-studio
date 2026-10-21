FIXED

# Cluster new-p16-81: malformed saved booleans silently disable default-on behaviour

Finding (Luna patrol, BUG 10): `prefader` on a cue send and `listen` on a cue master were read with
`toInt() != 0` and no conversion check. A malformed value such as `"abc"` converts to 0 and overrode the
default of `true`, so the send became post-fader or the cue stopped following listen.

## Verdict and evidence

**REAL, fixed.** Before the fix (base `2da2ed356`, branch `bugs/qwen-new-p16-81`):

- `src/core/HardwareIo.cpp:96` (base): `send.preFader = cue.attribute("prefader", "1").toInt() != 0;`
- `src/core/HardwareIo.cpp:292` (base): `cue.followListen = element.attribute("listen", "1").toInt() != 0;`

The fix adds `savedFlag()` in the anonymous namespace next to `readDestination()` (`src/core/HardwareIo.cpp:67-73`).
It returns `toInt(&valid) != 0` when the text converts, otherwise the caller's default. Both call sites now use it
with the attribute's own default `true`:

- `src/core/HardwareIo.cpp:104` (`loadCueSends`): `send.preFader = savedFlag(cue, "prefader", true);`
- `src/core/HardwareIo.cpp:300` (`loadMixer`): `cue.followListen = savedFlag(element, "listen", true);`

A missing attribute still gives `true`, because the empty string does not convert and falls back to the same
default. Well-formed `"0"` and `"1"` read exactly as before, and the writers only ever emit `0`/`1`, so saved
projects round-trip unchanged.

Regression tests are in `tests/src/core/HardwareIoTest.cpp`: `malformedPrefaderKeepsItsDefault` (line 690) and
`malformedListenKeepsItsDefault` (line 715). They drive the real `ChannelHardwareIo::load` and
`hardwareio::loadMixer` paths. They were split into two slots so each half has its own RED evidence, because
`QVERIFY` stops a function at its first failure.

Sibling readers checked and left alone: `src/core/HardwareInsert.cpp:220` (`on`, default false) and
`src/core/Mixer.cpp:2695` (`prefader`, default false). A malformed value gives false there, the same as an
absent attribute, so neither overrides a default-on value. `loadCueSends` index and `loadMixer` index already
use `validIndex`.

## Commands and exit codes

Log folder: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p16-81.r1/logs`.

| Step | Command (from `build/` unless noted) | EXIT | Log |
| --- | --- | --- | --- |
| build attempt 1 | `cmake --build . --target zene HardwareIoTest -j4` | 2 | `build-1-red.log` (ccache could not write to `/run/user/1000`; environmental, no code error) |
| build attempt 2 | same, with `CCACHE_TEMPDIR=/tmp/zene-ccache-tmp` | 0 | `build-2-red.log` |
| RED build | `CCACHE_TEMPDIR=/tmp/zene-ccache-tmp cmake --build . --target HardwareIoTest -j4` (split tests, fix not applied) | 0 | `build-3-red.log` |
| **RED test** | `cd tests && ctest -R '^HardwareIoTest$' --output-on-failure` | **8** | `test-2-red.log`: `FAIL! : malformedPrefaderKeepsItsDefault() 'loaded.cues[0].preFader' returned FALSE`; `FAIL! : malformedListenKeepsItsDefault() 'hardwareio::cueMaster(0).followListen' returned FALSE`; `Totals: 24 passed, 2 failed` |
| GREEN build | `cmake --build . --target zene HardwareIoTest -j4` (fix applied) | 0 | `build-4-green.log` |
| **GREEN test** | `cd tests && ctest -R '^HardwareIoTest$' --output-on-failure` | **0** | `test-3-green.log`: 100% passed |
| family | `cd tests && ctest -R 'Hardware\|CueMix\|Cue' --output-on-failure` | 0 | `test-4-family-green.log`: 2/2 passed (`HardwareIoTest`, `ControlHardwareIo`) |
| verbose | `cd tests && ctest -R '^HardwareIoTest$' -V` | 0 | `test-5-green-verbose.log`: `Totals: 26 passed, 0 failed`; both new tests PASS |

Affected-family test count: 2 ctest tests executed (`HardwareIoTest` with 26 test functions, `ControlHardwareIo`).
Zero-tests check: both non-zero.

### Step 4b: proof the test catches the bug

| Step | Command | EXIT | Log |
| --- | --- | --- | --- |
| save product diff | `git diff -- src/core/HardwareIo.cpp > .../logs/fix.patch` | 0 | `fix.patch` |
| revert fix | `git apply -R .../logs/fix.patch` | 0 | |
| rebuild unfixed | `cmake --build . --target zene HardwareIoTest -j4` | 0 | `build-5-unfixed.log` |
| **test unfixed** | `cd tests && ctest -R '^HardwareIoTest$' --output-on-failure` | **8** (FAIL, as required) | `test-6-proof-unfixed.log`: both new tests FAIL, same assertions as RED |
| reapply fix | `git apply .../logs/fix.patch` | 0 | |
| rebuild fixed | `cmake --build . --target zene HardwareIoTest -j4` | 0 | `build-6-refixed.log` |
| **test fixed** | `cd tests && ctest -R '^HardwareIoTest$' --output-on-failure` | **0** (PASS) | `test-7-proof-refixed.log`: 26/26 |

The test FAILS without the fix and PASSES with it.

No header changed, so no dependent targets needed a rebuild. No new source files, so `tests/fork-sources.txt`
and `tests/upstream-modifications.txt` are unchanged (the existing `HardwareIo.cpp` and `HardwareIoTest.cpp`
are already registered).

### Gates

`feature_check.py` could not run as specified. It writes its logs to `<worktree>/../<name>-feature-check`, which is
`.../zene-studio-program/zene-qwen-new-p16-81-feature-check`, outside the writable jail:

- `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>`: **EXIT 1**, `PermissionError`
  on `logs.mkdir` (`gates-1-feature-check.log`). No gate ran.

I did not edit the script. I ran its own `SIMPLE` and `RATCHET` lists, `run()`, `key()`, `item_file()` and
`base_listings()` from a driver (`/tmp/zene-fc-driver/fc_driver.py`, imported unchanged with `python3 -I`), with
logs in the LOG folder:

- `python3 -I /tmp/zene-fc-driver/fc_driver.py <worktree> .../logs/gates-feature-check`: **EXIT 0**, `RESULT: PASS`
  (`gates-2-feature-check-driver.log`)

| Gate | Result |
| --- | --- |
| fork-sources | PASS (exit 0) |
| all-sources-reproduce | PASS (exit 0) |
| no-upstream-regression | PASS (exit 0) |
| check-namespace | PASS (exit 0) |
| no-tautology | PASS (exit 0) |
| doc-figures | PASS (exit 0) |
| complexity | PASS: ratchet exit 1 on the train, but no new item in a changed file |
| file-length | PASS: ratchet exit 1 on the train, but no new item in a changed file |

The two ratchet exits are the expected red train state. The only file-length item in a changed file is
`tests/src/core/HardwareIoTest.cpp`, already in the base baseline (`gate-baselines/2da2ed356….json`:
`new file over 500 lines: tests/src/core/HardwareIoTest.cpp`). It was 708 lines at base and is 754 now, and the
gate's key comparison ignores the count. Only the base-baseline key comparison decided that row.

## Behaviour change to review

Literal text that does not convert to an integer used to read as 0 (false) and now reads as the default (true).
Examples: `prefader="false"`, `listen="false"`, `prefader="0.5"`. This is the fix the finding asks for ("retain
the default for malformed values"). The writers never emit these forms, so saved projects are not affected. A
hand-edited `"false"` now gives the default, not false. If the owner wants `"true"`/`"false"` parsed as booleans,
that is a separate change.

## Self-check

Items from the completion-check list for a non-feature fix:

1. **Each new test FAILS with the product change reverted, then PASSES restored.** Yes. Step 4b above: reverted
   with `git apply -R`, `test-6-proof-unfixed.log` EXIT 8 (both tests fail); reapplied, `test-7-proof-refixed.log`
   EXIT 0 (26/26).
2. **Regression test is deterministic.** Yes. It uses only in-memory `QDomDocument` XML and calls the load
   functions directly. No timing, sleeps, threads or races.
3. **The fix is at the cause and complete.** Yes. The cause is the unchecked `toInt()`, and both flags that have it
   in `HardwareIo.cpp` are fixed through one helper. `HardwareInsert.cpp:220` and `Mixer.cpp:2695` were checked and
   do not override a default-on value.
4. **No collateral damage.** Missing attributes still give `true`. Well-formed `0`/`1` read as before. The writers
   emit only `0`/`1`, so save and load round-trip (`everythingSurvivesASaveAndLoad` passes). The one behaviour
   change for non-integer text is listed above.
5. **Audio thread.** Nothing on the audio thread changed. `savedFlag` runs only on project load
   (`loadCueSends` and `loadMixer`). It does no allocation, lock, log or I/O beyond the existing Qt attribute read.
6. **Undo and commands.** No command was added or changed, and this is load-time code, so there is no undo step.
7. **Portability.** `QDomElement::attribute` and `QString::toInt(bool*)` are available in Qt 5.15 and Qt 6, and
   no platform-specific code is involved.
8. **Registration.** No new files. Changed existing files are already registered.
9. **Gates.** See the Gates section: `feature_check.py` itself cannot write its log folder from this jail (EXIT 1).
   The same gate list run through a driver with logs in the LOG folder returned EXIT 0, PASS.

Answers to the completion-check judgement items, by their wording:

- **The regression test FAILS with your product change reverted and PASSES with it (exit codes).** Yes: RED
  `test-2-red.log` EXIT 8 (before the fix); unfixed proof `test-6-proof-unfixed.log` EXIT 8; fixed
  `test-3-green.log` and `test-7-proof-refixed.log` EXIT 0.
- **The test is deterministic: no wall-clock timing, sleeps or races (name the seam or injected values it uses).**
  Yes. Both tests build XML in memory from fixed attribute strings (`"abc"`, `"0"`) and call
  `ChannelHardwareIo::load` and `hardwareio::loadMixer` directly. No seam or injected value is needed.
- **The fix is at the cause and complete: every caller and sibling of what you changed was checked and stays consistent.**
  Yes. See "Sibling readers checked" above: both `toInt() != 0` default-on flags in `HardwareIo.cpp` now go through
  `savedFlag()`. `HardwareInsert.cpp:220` and `Mixer.cpp:2695` default to false and were left as they are.
- **No collateral damage: old projects and saved data still load as before, and any limit or check you added does not hit other callers that legitimately exceed it.**
  Yes for old projects. The helper adds no limit. The one behaviour change, non-integer text such as `"false"`, is
  listed under "Behaviour change to review".
- **If your verdict is ALREADY-FIXED or FALSE-POSITIVE: the code (file:line) or the intended contract that shows it.**
  Not applicable. The verdict is FIXED (REAL); the defect is at `src/core/HardwareIo.cpp:96` and `:292` on the base.

Completion check (`submit_check.py`, `submit-1.log`, EXIT 1): commits on the branch PASS, working tree clean PASS,
report PASS, tests added or changed PASS (`tests/src/core/HardwareIoTest.cpp`). The **gates row is FAIL only
because `feature_check.py` cannot create `zene-qwen-new-p16-81-feature-check` outside the jail** (EXIT 1,
`PermissionError`). The gate list itself passes when run through the driver (EXIT 0, see Gates). I did not
edit the check or the gate to get around this.
