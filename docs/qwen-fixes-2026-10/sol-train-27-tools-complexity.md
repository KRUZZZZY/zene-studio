# Train 27: tools merge complexity

Hosted run 43226162933 at d96c1125f4562ceb76a054976531622082d349f5
rejects `merge_elem` at CCN 59. This is developer-tool source complexity, not a
hosted infrastructure outage. The existing issue ci-6d61162ce0abfd39 still points
to the older product-scope diagnostic; this repair addresses the current tools
failure directly without altering its budget, queue or review records.

## Repair

`_ElementMerge` separates missing-element decisions, attribute changes, child order,
child dispatch and conflict recording. `merge_elem` retains its public call signature
and signature snapshots. Delete/modify decisions still use deep pre-mutation
signatures. Safe comment hosts are passed before detaching children, and add/add
comments retain their original placement after the surviving child is appended.
All extracted methods measure CCN at most 7; merge_elem measures 5.

No baseline, exemption, gate script or existing assertion changes. The full
MmpzGitDepthTest is unchanged. Two additional ctest registrations run its merge-related
classes and the unchanged tools gate in check mode. CMake and tools-source changes
are registered with reasons, and declarations are inserted away from pending CI
repair append hunks so those already-verified repairs remain separate.

## Verification

A private `build/ci-python` install of Lizard 1.25.1 reproduces the hosted analyzer
rather than relying on local 1.24's multiline-signature parsing bug.

- `python3 -m pip install --target build/ci-python 'lizard==1.25.1'`: exit 0.
- `PYTHONPATH=build/ci-python bash tests/complexity-gate.sh --scope tools --check`:
  original HEAD source exit 1, merge_elem CCN 59; repaired source exit 0.
- From build/tests with absolute PYTHONPATH to that install,
  `ctest -R '^MmpzGitToolsComplexity$' --output-on-failure`:
  original HEAD source exit 8; repaired source exit 0. Source restored in finally.
- `PYTHONPATH=build/ci-python bash tests/file-length-gate.sh --scope tools --check`:
  exit 0 without a baseline change.

The exploratory full Python suite ran 51 tests and reported 39 failures, all in
compression identity checks (38 fixture recompressions plus a v1 refusal control).
Restoring HEAD and running `python3 tools/mmpz-git/tests/test_mmpz_git.py Container V2ContainerRefusal.test_controls_still_succeed_on_v1_and_on_plain_xml -v`
reproduces the same 39 failures. Those tests and compression implementation remain
unchanged; this repair does not claim the full suite passes. Seven optional binary/
Qt helper cases were skipped in the exploratory run before a local Zene was built.

Final build and ctest results are recorded below.

## Additional merge verification

`python3 tools/mmpz-git/tests/test_mmpz_git.py -v Merge MergeDepth MusicalPresentation LargeAssets GitDriverFlow`:
exit 0, 20 tests passed, no skips. The cases cover disjoint edits, conflicts,
nested edits versus deletion/rename, safe marker hosts, large embedded samples
and actual git merge-driver use.

An in-memory comparison loaded HEAD and the refactor as separate modules and
merged every pair of nine XML variants (unchanged, two tempo edits, two note edits,
rename, deletion, and two different additions). All 81 pairs returned identical
serialized XML and conflict records, or identical exception outcomes. No temporary
source checkout or alternative implementation is committed.

`bash tests/no-upstream-regression-gate.sh`, `bash tests/fork-sources-gate.sh`, and
`bash tests/evidence-gate.sh` all exit 0. Gates that enumerate tracked paths must
also run after the runner commits the explicit repair paths. No source outside this
private checkout was edited; no commits or queue writes were made.

## Build configuration

Exact configure command (WANT_WASM resolves to ON):

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE:STRING=RelWithDebInfo -DCMAKE_EXE_LINKER_FLAGS:STRING=-Wl,--strip-debug -DUSE_WERROR:BOOL=ON -DWANT_ABLETON_LINK:BOOL=ON -DWANT_ALSA:BOOL=ON -DWANT_CALF:BOOL=ON -DWANT_CAPS:BOOL=ON -DWANT_CARLA:BOOL=ON -DWANT_CMT:BOOL=ON -DWANT_COLLAB:BOOL=ON -DWANT_DEBUG_CPACK:BOOL=ON -DWANT_GIG:BOOL=ON -DWANT_JACK:BOOL=ON -DWANT_LV2:BOOL=ON -DWANT_MP3LAME:BOOL=ON -DWANT_OGGVORBIS:BOOL=ON -DWANT_OSS:BOOL=ON -DWANT_PORTAUDIO:BOOL=ON -DWANT_PULSEAUDIO:BOOL=ON -DWANT_QT6:BOOL=ON -DWANT_RUBBERBAND:BOOL=ON -DWANT_SDL:BOOL=ON -DWANT_SF2:BOOL=ON -DWANT_SID:BOOL=ON -DWANT_SNDIO:BOOL=ON -DWANT_SOUNDIO:BOOL=ON -DWANT_STEM_SPLIT:BOOL=ON -DWANT_STK:BOOL=ON -DWANT_SUIL:BOOL=ON -DWANT_SWH:BOOL=ON -DWANT_TAP:BOOL=ON -DWANT_VST:BOOL=ON -DWANT_VST3_TEST_INSTRUMENT:BOOL=ON -DWANT_VST_32:BOOL=ON -DWANT_VST_64:BOOL=ON -DWANT_WASM:BOOL=ON -DWANT_WEAKJACK:BOOL=ON -DZENE_TELEMETRY:BOOL=ON -DZENE_WANT_FFMPEG:BOOL=ON -DZENE_WANT_LIBAAF:BOOL=ON -DWANT_COVERAGE:BOOL=OFF -DWANT_CPACK_TARBALL:BOOL=OFF -DWANT_DEBUG_ASAN:BOOL=OFF -DWANT_DEBUG_FPE:BOOL=OFF -DWANT_DEBUG_GPROF:BOOL=OFF -DWANT_DEBUG_MSAN:BOOL=OFF -DWANT_DEBUG_RTSAN:BOOL=OFF -DWANT_DEBUG_TSAN:BOOL=OFF -DWANT_DEBUG_UBSAN:BOOL=OFF -DWANT_WINMM:BOOL=OFF -DWANT_CLAP:STRING=ON -DWANT_VST3:STRING=ON -DLMMS_CLAP_FETCH=ON -DONNXRUNTIME_ROOT=/home/kruzzzzy/.local/zene-deps/onnxruntime-linux-x64-1.28.0 -DCMAKE_CXX_COMPILER_LAUNCHER=ccache -DCMAKE_C_COMPILER_LAUNCHER=ccache '-DCMAKE_CXX_FLAGS=-isystem /usr/include/cairo -Wno-error=deprecated-declarations -Wno-error=unused-result -Wno-error=sfinae-incomplete' '-DCMAKE_C_FLAGS=-isystem /usr/include/cairo -Wno-error=deprecated-declarations -Wno-error=unused-result -Wno-error=sfinae-incomplete'
```

`cmake --build build --target zene -j12`: exit 0, 1,222 steps completed.

The pending combined CI repair ee9b376701b8 remains applicable: its binary-inclusive
diff passes `git apply --check` against this worktree. It is not superseded.

## Final ctest verification

After `cmake -S . -B build` (exit 0), repeat
`cmake --build build --target zene -j12`: exit 0 (resource regeneration and relink).
From build/tests, with PYTHONPATH set to this checkout's absolute
`build/ci-python` path:

```sh
ctest -R '^(MmpzGitToolsComplexity|MmpzGitMergeTest)$' --output-on-failure --no-tests=error
```

Exit 0: **2/2 matched and passed**, 4.47 seconds; the focused merge entry runs
20 unittest cases, none skipped. Lizard 1.25.1 supplies the tools gate.
The same two entries also pass under system Lizard 1.24.0. The original-source
red control used 1.25.1 and failed with CCN 59; no parser bug or baseline update
is used to obtain the green result. `git diff --check`: exit 0.
The unchanged full suite still retains the independently reproduced compression
failures; a hosted CI rerun and normal review remain required before promotion.
