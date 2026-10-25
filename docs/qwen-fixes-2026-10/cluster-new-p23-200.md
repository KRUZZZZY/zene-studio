FALSE-POSITIVE
new-p23-200: FALSE-POSITIVE

Evidence: `src/core/ParameterAliases.cpp:141` passes the original string to `DeviceOptions::setAlias`, but `src/core/DeviceOptions.cpp:64-68` trims the value before storing it and removes it when the trimmed value equals the key. The reported input `" Foo "` therefore stores as `"Foo"`, not with surrounding whitespace. `DeviceOptions::load` also routes saved aliases through this setter at `src/core/DeviceOptions.cpp:101`.

No regression test or RED/GREEN run was added for this finding: the described behavior is already prevented by the existing storage contract, so a test for surrounding whitespace would pass before any change. Procedure step 2 directs that a non-real finding skip to the report with code evidence.

Commands and results:

- `nl -ba src/core/DeviceOptions.cpp | sed -n '60,72p'` — EXIT=0.
- `nl -ba src/core/ParameterAliases.cpp | sed -n '135,166p'` — EXIT=0.
- `rg -n 'void DeviceOptions::load|setAlias\(it.key' src/core/DeviceOptions.cpp` — EXIT=0.
- `rg -n '^src/core/ParameterAliases.cpp$|^tests/src/core/DeviceToolsTest.cpp$' tests/fork-sources.txt tests/all-sources.txt` — EXIT=0.
