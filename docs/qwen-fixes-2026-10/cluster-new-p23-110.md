FALSE-POSITIVE

# cluster new-p23-110 - "Zene Gate reports doubled output if dry/wet defaults are both 100%"

## Verdict

FALSE-POSITIVE. The premise of the finding - a default of dry 100 % / wet 100 % -
does not exist in this tree. Dry and wet are one complementary control
(`dryLevel() == 1 - wetLevel()`), and its default is wet 100 % / dry 0 %. At that
default the Gate's mix line is `input * gain`, not `input * (1 + gain)`.

## Evidence (file:line, read this session)

- `plugins/Gate/Gate.cpp:112-113` - the mix factors come from the effect base:
  `const float d = dryLevel(); const float w = wetLevel();`.
- `plugins/Gate/Gate.cpp:138` - the reported line:
  `frame = frame * d + frame * (m_gain * w);`
- `include/Effect.h:236-244` - the two accessors are complementary:
  - `wetLevel()` returns `m_wetDryModel.value()` (line 238);
  - `dryLevel()` returns `1.0f - m_wetDryModel.value()` (line 243).

  Therefore `d = 1 - w`; there is no state in which both are 1.0.
- `src/core/Effect.cpp:65` - the model's construction:
  `m_wetDryModel(1.0f, -1.0f, 1.0f, 0.01f, this, tr("Wet/Dry mix"))`.
  The first argument is the default: wet = 1.0, hence dry = 1.0 - 1.0 = 0.0.
- `src/core/Effect.cpp:114` and `src/core/Effect.cpp:186` - the same single
  `m_wetDryModel` is saved and loaded as the one attribute `"wet"`. There is no
  separate saved dry level that an old project could restore as 100 % alongside
  wet; a project without the attribute keeps the 1.0 default.
- `src/gui/EffectView.cpp:69,250` - the visible W/D knob is bound to that one
  model, so the user cannot set a second, independent dry control.
- `plugins/Gate/Gate.cpp:78-90` - `targetGain()` returns 1.0 when open and at
  most 1.0 when closed (gate `dbfsToAmp(rangeDb)`, expander/ducker likewise), so
  `m_gain` never amplifies.

Algebra: `frame*d + frame*(gain*w) = frame*((1-w) + gain*w)`. At the default
`w = 1`, `d = 0`, the expression is `frame*gain`. With `w = 0.5` and `gain = 1`
it is exactly `frame`. It is a standard linear crossfade, never
`frame*(1+gain)`.

## Sibling check

The same crossfade is used by other effects, e.g.
- `plugins/Console/Console.cpp:183` - `frame = frame * d + shaped * output * w;`
- `plugins/DeEsser/DeEsser.cpp:130` -
  `frame * d + shaped(frame, mode, false) * w`.

Gate matches the family; changing it to wet-only or to `d*(1-w)` would diverge
from every sibling and would silently discard the dry signal for users who set
wet below 100 %. No product code was changed for this finding.

## Procedure steps that do not apply

Steps 3, 4 and 4b (test matrix, RED, GREEN) apply to a real fix. This finding is
not real, so there is no product change to revert and no regression test to
write; the verdict rests on the code contract above. Nothing was skipped for a
real change.

## Commands run

Feature gates (unpiped exit code):

```
$ python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-new-p23-110 > /home/kruzzzzy/.config/kads/zene/state/runs/new-p23-110.r1/logs/1-feature-check.log 2>&1; echo EXIT=$?
EXIT=0
```

Table (`logs/1-feature-check.log`):

```
feature-check kads-new-p23-110 (base fa4a5f2e7, 429 changed files; logs in .../build/feature-check)
  PASS  fork-sources            exit 0
  PASS  all-sources-reproduce   exit 0
  PASS  no-upstream-regression  exit 0
  PASS  check-namespace         exit 0
  PASS  no-tautology            exit 0
  PASS  doc-figures             exit 0
  PASS  complexity              no new item in your files
  PASS  file-length             no new item in your files
RESULT: PASS
```

Working tree before the report commit:

```
$ git status --porcelain
(no output)
EXIT=0
```

## Self-check

- **The regression test FAILS with your product change reverted and PASSES with
  it (exit codes).** Not applicable: this is a FALSE-POSITIVE, so there is no
  product change and no test. The submit check's claim-only path covers this
  verdict; the evidence is the code contract above.
- **The test is deterministic: no wall-clock timing, sleeps or races.** No test
  was added, so there is no timing or race surface. The verdict is static code
  evidence.
- **The fix is at the cause and complete: every caller and sibling of what you
  changed was checked and stays consistent.** No code was changed. For
  completeness I checked every user of `dryLevel()`/`wetLevel()`
  (`rg -n "dryLevel\\(\\)|wetLevel\\(\\)" plugins src include`): Gate is
  consistent with the effect family, including Console and DeEsser. Gate has no
  second mix path; `GateControlDialog.cpp:45-51` exposes only the six Gate
  knobs, and the W/D knob is the base class's single model.
- **No collateral damage: old projects and saved data still load as before, and
  any limit or check you added does not hit other callers that legitimately
  exceed it.** No product, save, load or project code changed; only this report
  is added.
- **If your verdict is ALREADY-FIXED or FALSE-POSITIVE: the code (file:line) or
  the intended contract that shows it.** See Evidence: `include/Effect.h:236-244`
  (dry is `1 - wet`), `src/core/Effect.cpp:65` (default wet = 1.0),
  `src/core/Effect.cpp:114,186` (one persisted `"wet"` value), and
  `plugins/Gate/Gate.cpp:138` with the algebra above.
- **Sibling commands in the command files you changed** (`app.*`,
  `arrangement.*`): this lane changed no command file. The submit check lists
  them because `base..HEAD` (base `fa4a5f2e7`) contains the train's earlier
  commits; this cluster's only commit adds one report file:

  ```
  $ git show --stat --oneline HEAD
  a771f35bff docs: cluster new-p23-110 - Gate dry/wet mix is a complementary crossfade (FALSE-POSITIVE)
   docs/qwen-fixes-2026-10/cluster-new-p23-110.md | 119 +++++++++++++++++++++++++
   1 file changed, 119 insertions(+)
  ```

  So none of those commands is touched, and none needs a consistency change.
  The same applies to the `tests/control-*.py` files named by the "tests added
  or changed" row: they come from earlier commits on the branch, not from this
  cluster.

## Submit check

```
$ python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/submit_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-new-p23-110 new-p23-110 > /home/kruzzzzy/.config/kads/zene/state/runs/new-p23-110.r1/logs/3-submit-check.log 2>&1; echo EXIT=$?
EXIT=0
```

Table (`logs/3-submit-check.log`):

```
PRE-SUBMIT CHECK for new-p23-110
  PASS  commits on the branch
  PASS  working tree clean
  PASS  report docs/qwen-fixes-2026-10/cluster-new-p23-110.md
  PASS  tests added or changed - tests/control-audio-test-tone.py, tests/control-no-audio-device.py, tests/control-session-lifecycle-transcript.py
  PASS  new test files registered in tests/CMakeLists.txt
  PASS  gates (feature_check.py)
  ?     ... (all asks answered in Self-check above)
RESULT: PASS - submit
```

## Other findings

None reported here; anything unrelated is out of scope for this lane.
