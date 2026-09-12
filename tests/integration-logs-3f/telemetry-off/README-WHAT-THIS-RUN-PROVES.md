# F3c, first attempt — READ THIS BEFORE QUOTING ANYTHING IN THIS DIRECTORY

**The OFF build FAILED, and one file here looks like a pass but is not.**

- `configure-off.log` — EXIT=0: the option was accepted.
- `build-off.log` — **EXIT=2, the build FAILED**: `src/core/ControlCommandsTelemetry.cpp:87`
  (`QJsonObject consentState(const TelemetryConsent& consent)`) cannot compile once the client is removed,
  and `-DUSE_WERROR=ON` makes the warning fatal.
- `ctest-off.log` — **EXIT=0, 86/86, and this is NOT evidence about the OFF configuration.** Because the
  build failed, `build/zene` was never replaced, so ctest ran against the *pre-existing* binaries. A green
  suite over a stale binary is the same class of result as the fail-open autosave and the blind coverage
  capture this programme has already caught twice. **Quote it as void.**
- `off.txt` — the differential reads **99 symbols / 305 strings, identical to ON**, for the same reason:
  the binary never changed. That is the honest reading of the measurement, and the measurement's
  interpretation here is precisely what led to finding the defect.
- `on.txt` — the control, taken before any reconfigure: the ON binary carries 99 telemetry symbols and
  305 `telemetry` strings.

**The defect this found**: `CMakeLists.txt:136-140` documents `-DZENE_TELEMETRY=OFF` as a packager kill
switch that *"removes the client, its 'what we send' screen and its networking code from the build
entirely, so a distribution can ship a binary in which no send path exists."* It does not build at all.
The release's honesty gate cannot see this: it reads the build options a binary **reports**, not whether a
configuration **builds**. A fix lane is on branch `fix/telemetry-kill-switch`; if it is not a guard-sized
fix, the honest alternative is retracting the claim rather than shipping it.

`configure-restore.log` / `build-restore.log` — EXIT=0, and `build/CMakeCache.txt` reads
`ZENE_TELEMETRY:BOOL=ON`: the release configuration this directory started in is restored.
