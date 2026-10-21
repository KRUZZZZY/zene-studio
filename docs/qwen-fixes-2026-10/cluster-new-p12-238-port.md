# Cluster new-p12-238-port

**Verdict: FIXED**

Port of the accepted fix `bugs/qwen-new-p12-238` (commits `4eeb1bec5` +
`86ae4a9e0`) onto the newest train line. The accepted branch could not ride the
train because it conflicted in `src/core/SessionModel.cpp` and
`tests/CMakeLists.txt`; the fix was therefore re-applied BY HAND on this fresh
branch (no merge, no cherry-pick, no rebase), adapted to the newest line's
shape of the two legacy readers.

## The finding under this cluster

`src/core/SessionClip.cpp:72` (before the fix, on this branch):

```cpp
action.jumpTo = actionElement.attribute( QStringLiteral( "jump" ), QStringLiteral( "0" ) ).toInt();
```

`QString::toInt()` answers `0` for a value it cannot convert - a nonnumeric
text and an overflowing integer alike - and `0` is a VALID scene row. A
Follow Action restored with `jump="oops"` therefore armed a silent jump to the
FIRST scene instead of failing.

**Verdict: REAL** - the code on this branch was demonstrably wrong (the RED
run below shows the malformed attribute silently becoming scene 0), and the
newest line does not fix it.

## The fix (one strict reader, three readers)

One strict reader, `sessionSerialization::intAttribute()`
([src/core/SessionModelPrivate.h:75](../../src/core/SessionModelPrivate.h#L75)),
plus its 0/1 sibling `boolAttribute()`
([src/core/SessionModelPrivate.h:88](../../src/core/SessionModelPrivate.h#L88)),
is the single spelling of the rule:

- a Follow Action's jump target that is not a number becomes `-1`, this
  codebase's existing "no addressable target" - the action keeps its place in
  the chain and its fire is REFUSED
  ([src/core/SessionClip.cpp:85](../../src/core/SessionClip.cpp#L85)),
  never a silent jump to scene 0;
- a legacy `<scene index="...">` row
  ([src/core/SessionModel.cpp:93](../../src/core/SessionModel.cpp#L93)) and a
  legacy `<clip track="..." scene="...">` cell
  ([src/core/SessionModel.cpp:147](../../src/core/SessionModel.cpp#L147),
  [src/core/SessionModel.cpp:149](../../src/core/SessionModel.cpp#L149)) with an
  unreadable coordinate are DECLINED, exactly as an out-of-grid one already
  was - and `SessionModel::restoreState` then clears the model and answers
  false, so Song's claim-or-preserve walk keeps the malformed `<session>`
  block verbatim instead of half-applying it;
- the two `<z:scenes>` claim helpers
  ([src/core/SessionModelPrivate.h:204](../../src/core/SessionModelPrivate.h#L204),
  [src/core/SessionModelPrivate.h:223](../../src/core/SessionModelPrivate.h#L223),
  [src/core/SessionModelPrivate.h:226](../../src/core/SessionModelPrivate.h#L226))
  now use the same helper, so there is ONE spelling of the rule in the whole
  session reader;
- every other attribute falls back to its own documented default (the action
  `type` and `linked` flag in the chain, the cell's `type` / `pattern` /
  `quantisation`, the scene's time-signature halves).

An attribute that is not there at all keeps its legacy default (`jump` absent
-> `0`), so an old project loads exactly as before - pinned by the test case
`absentJumpTargetKeepsTheLegacyDefault`.

## What changed against the newest line (the port)

The accepted fix was written against an older shape of `SessionModel.cpp`.
The newest line moved the two legacy readers into named functions
(`restoreScenes`, `restoreClips`), which is exactly where the merge conflict
came from. The port keeps the newest line's shape and re-applies the same
rule inside it. The product diff is line-for-line the accepted fix's:

```
 src/core/SessionClip.cpp       | 68 +++++++++++++++++++++++++++++-------------
 src/core/SessionModel.cpp      | 22 +++++++++-----
 src/core/SessionModelPrivate.h | 52 ++++++++++++++++++++++----------
```

(the same three numbers the accepted commit `4eeb1bec5` reports for the same
three files).

## The regression test

[tests/src/core/SessionMalformedAttributeTest.cpp](../../tests/src/core/SessionMalformedAttributeTest.cpp)
(a fork-NEW source, registered in `tests/CMakeLists.txt`,
`tests/all-sources.txt` and `tests/fork-sources.txt`). Seven cases:

| case | what it pins |
|---|---|
| `malformedJumpTargetIsNotSceneZero` | the finding: `jump="oops"` and an overflowing `jump` must not become scene 0, and `followTargetScene` must refuse them |
| `wellFormedJumpTargetStillNamesItsRow` | control: `jump="2"` still names row 2 |
| `absentJumpTargetKeepsTheLegacyDefault` | control: no `jump` attribute at all still means 0 |
| `malformedLegacySceneIndexIsRejected` | `<scene index="bad">` must not land on scene 0 |
| `wellFormedLegacySceneIndexStillLandsOnItsRow` | control: `<scene index="1">` still lands on row 1 |
| `malformedLegacyClipCoordinatesAreRejected` | `<clip track="bad" ...>` must not overwrite cell 0/0 |
| `wellFormedLegacyClipCoordinatesStillLandInTheirCell` | control: `<clip track="1" scene="1">` still lands in 1/1 |

It is deterministic: the fixtures are XML strings parsed into a `QDomDocument`
and the model is read back; there is no engine, no clock, no thread, no sleep
and no randomness.

## Evidence

All logs are in
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-238-port.r1/logs`.

| Evidence | Exact command | Exit | Result |
|---|---|---:|---|
| RED (product code unfixed) | `cd build/tests && ctest -R SessionMalformedAttributeTest --output-on-failure > logs/test-1-red.log 2>&1` | 8 | 3 of the 7 cases FAIL: `malformedJumpTargetIsNotSceneZero` ('action.jumpTo != 0' returned FALSE), `malformedLegacySceneIndexIsRejected`, `malformedLegacyClipCoordinatesAreRejected`; the 4 control cases PASS |
| GREEN (product code fixed) | `cd build/tests && ctest -R SessionMalformedAttributeTest --output-on-failure > logs/test-2-green.log 2>&1` | 0 | `100% tests passed out of 1` (all 7 cases PASS) |
| Reversal proof (fix reverted) | `git diff -- src/core/SessionModelPrivate.h src/core/SessionClip.cpp src/core/SessionModel.cpp > logs/fix.patch && git apply -R logs/fix.patch && cmake --build build --target SessionMalformedAttributeTest AbletonLinkSessionTest zene -j6 && cd build/tests && ctest -R "SessionMalformedAttributeTest\|AbletonLinkSessionTest" --output-on-failure > logs/test-5-reversal-red.log 2>&1` | 8 | the same 3 cases FAIL again (`Totals: 6 passed, 3 failed`) - the test catches the bug, it does not merely describe it |
| Reversal proof (fix restored) | `git apply logs/fix.patch && cmake --build build --target SessionMalformedAttributeTest zene -j6 && cd build/tests && ctest -R SessionMalformedAttributeTest --output-on-failure > logs/test-6-restored-green.log 2>&1` | 0 | `100% tests passed out of 1` again |
| Affected family | `cd build/tests && ctest -R "Session" --output-on-failure > logs/test-4-session-family-full.log 2>&1` | 8 | **31 tests run, 30 pass**; the one failure is `AbletonLinkSessionTest` (SIGSEGV inside `lmms::AbletonLinkSession::numPeers()`, after the multicast case SKIPped because this host delivers no multicast datagram). It fails IDENTICALLY on the reverted tree (see `logs/test-5-reversal-red.log`), so it is pre-existing and unrelated to the session serialisation |
| Build (product + tests) | `cmake --build build --target zene SessionMalformedAttributeTest -j6 > logs/build-2.log 2>&1` | 0 | `zene` and the new test target build |
| Build (session family) | `cmake --build build --target SessionModelTest SessionFollowTest SessionSceneFollowTest ... -j6 > logs/build-3-session-tests.log 2>&1` | 0 | the 24 session test targets build |
| Gates | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > logs/gate-feature-check-2.log 2>&1` | 1 | see "The gates" below |
| Pre-submit check | `python3 ~/.local/share/zene-bunny-fix/qwen/submit_check.py <worktree> new-p12-238-port > logs/gate-submit-check-1.log 2>&1` | 1 | the ASK items are answered in "Self-check" below |

### The gates

`feature_check.py` on this branch:

```
  FAIL  fork-sources            exit 1
  FAIL  all-sources-reproduce   exit 1
  PASS  no-upstream-regression  exit 0
  PASS  check-namespace         exit 0
  PASS  no-tautology           exit 0
  PASS  doc-figures            exit 0
  PASS  complexity              no new item in your files
  PASS  file-length             no new item in your files
RESULT: FAIL
```

**The two FAILs are PRE-EXISTING and unrelated to this cluster.** Both are the
same single disagreement: `tests/all-sources.txt` lists the 1148 vendored
sfizz paths (`plugins/SfzPlayer/sfizz/...`) while the manifest's own header
recipe excludes them, so the manifest does not reproduce from its own recipe.

Proof that it is not this change: the BASE commit's manifest fails the very
same check -

```
$ git show HEAD:tests/all-sources.txt > /tmp/pristine.txt   # HEAD = 0a8842d39, the base
$ cp /tmp/pristine.txt tests/all-sources.txt
$ bash tests/all-sources-reproduce.sh
FAIL: all-sources.txt does not match the recipe in its own header.
  ... -plugins/SfzPlayer/sfizz/external/abseil-cpp/... (1148 lines)
```

and the mismatch was introduced by the train's own automatic re-derivation
(`5ef150729` "chore(train-10): ... all-sources re-derived (automatic merge
train)"), which put the sfizz paths INTO the manifest while the recipe
(excluding them since `39f297929`) still excluded them. The train regenerates
this manifest itself on every merge; a bug-fix lane must not re-derive it
here. The one path this cluster adds
(`tests/src/core/SessionMalformedAttributeTest.cpp`) IS registered in BOTH
manifests and IS derived by the recipe once the file is tracked - the diff
above shows the new path on the "manifest has it, recipe cannot derive it"
side only while the file was still untracked.

The other six gates pass, including the two ratchets: no new complexity item
and no new file-length item in any file this change touched.

## Files in this commit

- `src/core/SessionModelPrivate.h` - the strict readers `intAttribute` /
  `boolAttribute`, and the two `<z:scenes>` claim helpers using them.
- `src/core/SessionClip.cpp` - the Follow Action chain reader (type, linked,
  jump), the cell reader (type, pattern, quantisation) and the scene reader
  (time-signature halves).
- `src/core/SessionModel.cpp` - the two legacy readers `restoreScenes` and
  `restoreClips`.
- `tests/src/core/SessionMalformedAttributeTest.cpp` - NEW, the regression
  source (fork-NEW, registered in `tests/fork-sources.txt`).
- `tests/CMakeLists.txt` - the new test target.
- `tests/all-sources.txt`, `tests/fork-sources.txt` - the registration of the
  new source.
- `docs/qwen-fixes-2026-10/cluster-new-p12-238-port.md` - this report.

## Self-check

### The regression test FAILS with your product change reverted and PASSES with it (exit codes).

Reverted: `git apply -R logs/fix.patch` + rebuild + `ctest -R
"SessionMalformedAttributeTest|AbletonLinkSessionTest"` -> **exit 8**, and the
log names the three failing cases
(`logs/test-5-reversal-red.log`: `Totals: 6 passed, 3 failed`).
Restored: `git apply logs/fix.patch` + rebuild + `ctest -R
SessionMalformedAttributeTest` -> **exit 0**, `100% tests passed out of 1`
(`logs/test-6-restored-green.log`). The first RED run, before any product
edit, is `logs/test-1-red.log` (exit 8, same three failures).

### The test is deterministic: no wall-clock timing, sleeps or races (name the seam or injected values it uses).

The only inputs are XML literals parsed by `QDomDocument::setContent()` and
read back through `ClipSlot::restoreState()` / `SessionModel::restoreState()`;
the only "random" value in the whole path, `followTargetScene`'s `rngUnit`, is
injected as the fixed literal `0.5`. There is no timer, no sleep, no thread,
no engine and no file in the test. Repeated runs give the same result (the
RED and GREEN runs above are separate invocations of the same binary).

### The fix is at the cause and complete: every caller and sibling of what you changed was checked and stays consistent.

- The cause is the unchecked `QString::toInt()` on an index/target; the fix
  puts ONE strict reader (`intAttribute`) in the shared
  `sessionSerialization` namespace and routes every index/target read of the
  session readers through it: the jump target, the legacy scene index, the
  legacy clip coordinates, the `<z:scenes>` claim helpers' index/track/scene.
- Siblings checked and left consistent: the chain's `type` and `linked`
  attributes, the cell's `type` / `pattern` / `quantisation`, the scene's
  time-signature halves - all read through the same helpers now, each falling
  back to its own documented default.
- The remaining `toInt()` calls in the session readers were checked and left
  alone on purpose: `launchmode`, `legato`, `loopstart`, `looplength`,
  `transpose`, `detune`, `ram`, `warp` all have `0` as their documented
  default, so a malformed value lands on that default and cannot address a
  valid target; the `<session version>` read has `1` as its documented
  default; `SubScene::restoreState` already checks the conversion by hand.
- The only OTHER reader of a Follow Action chain is `Scene::restoreState`
  (R5.2's scene chains) - it calls the same `restoreFollowChain()`, so the
  scene chain is fixed by the same edit (checked, not assumed).

### No collateral damage: old projects and saved data still load as before, and any limit or check you added does not hit other callers that legitimately exceed it.

- The WRITER was not touched: `saveFollowChain` still writes the same
  attributes, and the save bytes of a well-formed project are unchanged.
- A well-formed document takes the same path as before: the four control
  cases in the regression test pin the well-formed jump target, the well-formed
  legacy scene index, the well-formed legacy clip coordinates and the ABSENT
  `jump` attribute (still 0), and the whole session family (31 tests, 30
  pass, the one failure pre-existing) plus `SessionModelTest`,
  `SessionFollowTest`, `SessionSceneFollowTest` and
  `SessionCommandArgsTest` are green.
- No limit was introduced: the strict reader refuses only a value that is not
  a number at all. A legitimate large index (e.g. scene 511) still parses and
  still lands on its own row; only a nonnumeric text or an int overflow is
  refused, and that is the behaviour the readers already had for an
  out-of-grid coordinate.
- A malformed `<session>` block is still PRESERVED verbatim by Song's
  claim-or-preserve walk (the reader answers false, exactly as it already did
  for an out-of-grid row), so no project data is dropped.
- No new control command was added (the ControlCommandsSnapshot canary does
  not move), no undo step was added, and nothing on the audio thread changed -
  the readers are file-parsing code that runs on the load path only.

### If your verdict is ALREADY-FIXED or FALSE-POSITIVE: the code (file:line) or the intended contract that shows it.

Not applicable: the verdict is REAL/FIXED. The unfixed code on this branch
was `src/core/SessionClip.cpp:72`
(`actionElement.attribute( "jump", "0" ).toInt()`), and the RED run shows it
turning `jump="oops"` into scene 0.
