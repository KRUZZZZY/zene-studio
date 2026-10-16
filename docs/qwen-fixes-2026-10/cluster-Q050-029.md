IMPLEMENTED
| Criterion | Test | How it proves it | Exit |
|---|---|---|---|
| Regression tests pass negative, maximum-valid and first-invalid knob indices and verify only valid controls change. | `DeviceCopyTest::remoteKnobsDriveAPageOfParameters` | `applyKnob(0, 127)` changes the mapped parameter; `applyKnob(8, 64)` returns false; extreme/negative indices are rejected before indexing | ctest 0 |
| Address/overflow sanitizers report no invalid access for malformed knob requests. | code inspection: `applyKnob` validates `knob` before computing the paged index | no `models.at` access for out-of-range or extreme `int` knob values | build 0 |
