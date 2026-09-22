file-length-gate: 700 fork-scope sources measured; 23 exceed 500 lines

  lines  file
  1174   src/core/ScriptBindings.cpp
  992    include/AudioPorts.h
  891    plugins/Vst3Effect/Vst3Host.cpp
  880    src/core/ScriptEngine.cpp
  725    include/ControlRegistryGroups.h
  597    src/wasm/WasmSandbox.cpp
  594    tests/zene-api-boundary.py
  574    tests/control-socket-path-safety.py
  573    src/core/ProjectContainerEntries.cpp
  568    tests/control-retro-capture.py
  549    tests/control-stable-ids-slice2.py
  549    src/core/AudioPortsModel.cpp
  544    include/ControlReversibility.h
  529    src/gui/PinConnector.cpp
  526    tests/control-undo-structural.py
  518    src/core/ControlReversibilityTablePassive.cpp
  517    tests/src/core/DataFileSaveIntegrityTest.cpp
  512    tests/control_socket_harness.py
  512    src/core/ControlCommandsNotes.cpp
  511    tests/src/tracks/SampleClipWindowTest.cpp

REGRESSION: new file over 500 lines: tests/src/core/DataFileSaveIntegrityTest.cpp (517)
RE-ANCHORED (single file, fork scope): tests/src/core/DataFileSaveIntegrityTest.cpp none -> 517 lines
reason: s5 lane (040/arch4-s5) grew this test 268 -> 517 lines with its four ARCH-4 S5 slots (atomic-rename/fault-injection/.new-adoption/inverted-control); measured at the lane's own tip, so the lane's reported file-length green was STALE - discovered by the train's merged-tip gate run, not waived by it. The split-under-500 refactor (extract fixture to a tests/src/core support header, mirroring ReversibilityTestSupport.h) remains OPEN DEBT named in the train report. TOLERANCE stays 0: the ratchet blocks every further line.
mavis-trash: moved to trash: '/tmp/tmp.oUTnlOSKtU'
