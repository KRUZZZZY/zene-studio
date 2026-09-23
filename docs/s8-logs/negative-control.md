# S8 negative control — the phantom-clip class (SPEC-ARCH-4 5.2, risk 1)

**Claim under test.** New scene-cell state must never be written as a child of an existing
element (`<track>` above all): an older build's `loadTrack` materialises an unrecognised
`<track>` child as a real Clip, so any leak under `<track>` is silent data invention. The
companion claim is that `ScenesUpconvertTest::newModelStateIsNeverWrittenUnderATrack` can
actually SEE that leak — a control that has only ever been green is not a control.

**Method (S7's pattern, re-run for S8).** Three runs of the one test against the same tree,
differing only in `src/core/Song.cpp`:

1. **Baseline — GREEN.** The committed writer puts everything in the top-level `<z:scenes>`
   section; nothing lands under `<track>`.

   ```
   BASELINE_GREEN=0
   100% tests passed, 0 tests failed out of 1
   ```

2. **Planted writer — RED (this is the control).** A temporary plant appended a `<z:cell>`
   element under every `<track>` whenever the scenes writer ran — the exact defect class
   risk 1 describes. Rebuild + `ctest -R ScenesUpconvertTest --output-on-failure`:

   ```
   PLANT_BUILD=0
   PLANT_CTEST=8
   1/1 Test #118: ScenesUpconvertTest ..............***Failed    1.42 sec
   FAIL!  : ScenesUpconvertTest::newModelStateIsNeverWrittenUnderATrack()
            PHANTOM-CLIP RISK: <z:cell> sits directly under <track> (element 0) in a file
            this build wrote. A reader without this build's claim branch materialises an
            unrecognised <track> child as a real Clip (SPEC-ARCH-4 5.2 risk 1): new
            scene-cell state belongs in the top-level <z:scenes> section, never under
            <track>.
   The following tests FAILED:
       118 - ScenesUpconvertTest (Failed)
   ```

   The test named the planted element, the element it sat under, and the risk clause — not
   a generic failure.

3. **Plant reverted — GREEN again.** The plant block was deleted from `Song.cpp` in the
   same session (`git diff src/core/Song.cpp` shows only the shipped writer swap and the
   claim branch), the file rebuilt, and the test re-run:

   ```
   REVERT_BUILD=0
   REVERT_GREEN=0
   100% tests passed, 0 tests failed out of 1
   ```

**Conclusion.** The negative control is real: seen RED with state planted under `<track>`,
reverted to GREEN without touching the test. The shipped writer places nothing under any
existing element — all new state rides the one top-level `<z:scenes>` section, which a
reader without this build's claim branch preserves verbatim (1.6.1) instead of
materialising as Clips.

Command forms, unpiped (workspace rule 6):

```
cmake --build build -j2 --target ScenesUpconvertTest   > build.log 2>&1; echo $?
cd build/tests && ctest -R ScenesUpconvertTest --output-on-failure > red.log 2>&1; echo $?
```
