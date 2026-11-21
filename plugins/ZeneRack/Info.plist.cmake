<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<!--
  The Info.plist of the ZeneRack.vst3 CFBundle.

  Named Info.plist.cmake on purpose: it is a build-time template consumed by
  configure_file(), and the .cmake extension tells the upstream-divergence gate
  that this fork-new file is build configuration, not a changed inherited source.

  The SDK's macOS loader is CoreFoundation's bundle loader
  (public.sdk/source/vst/hosting/module_mac.mm): CFBundleCreate() followed by
  CFBundleLoadExecutableAndReturnError(). CFBundle finds the module through
  CFBundleExecutable, which must name the file under Contents/MacOS. The layout
  and the entry symbol are both the loader's contract; ZeneRack/CMakeLists.txt
  selects macmain.cpp on APPLE and lays the module out under Contents/MacOS.

  The keys are the ones the SDK's own SMTG_Bundle.cmake generates for its
  samples. @LMMS_ZENERACK_NAME@ is substituted by configure_file() from the same
  variable the bundle directory is named from, so the two cannot drift apart.
-->
<plist version="1.0">
<dict>
	<key>CFBundleDevelopmentRegion</key>
	<string>en</string>
	<key>CFBundleExecutable</key>
	<string>@LMMS_ZENERACK_NAME@</string>
	<key>CFBundleIdentifier</key>
	<string>io.zene.studio.@LMMS_ZENERACK_NAME@</string>
	<key>CFBundleInfoDictionaryVersion</key>
	<string>6.0</string>
	<key>CFBundleName</key>
	<string>Zene Rack</string>
	<key>CFBundlePackageType</key>
	<string>BNDL</string>
	<key>CFBundleShortVersionString</key>
	<string>1.0</string>
	<key>CFBundleSignature</key>
	<string>????</string>
	<key>CFBundleSupportedPlatforms</key>
	<array>
		<string>MacOSX</string>
	</array>
	<key>CFBundleVersion</key>
	<string>1</string>
</dict>
</plist>
