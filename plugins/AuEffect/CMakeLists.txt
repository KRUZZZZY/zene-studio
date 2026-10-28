# Audio Unit (AUv2) effect hosting, Q050-491. Audio Units exist on macOS only: every other build
# compiles the format out (no module, no "au" in plugin.list - src/core/ControlDeviceAu.cpp). Nothing
# is downloaded or vendored: the host uses the system's AudioToolbox, AudioUnit, CoreAudioKit and AppKit
# frameworks. WANT_AU=OFF disables it on macOS too.
IF(NOT LMMS_BUILD_APPLE)
	RETURN()
ENDIF()
SET(WANT_AU ON CACHE BOOL "Build Audio Unit (AUv2) hosting on macOS")
IF(NOT WANT_AU)
	MESSAGE(STATUS "Audio Unit hosting disabled (-DWANT_AU=OFF)")
	RETURN()
ENDIF()

# AuHost.cpp talks to AppKit and CoreAudioKit, so it is Objective-C++ (manual retain/release, no ARC).
# It keeps the .cpp name so the source manifests and gates see it like any other source.
SET_SOURCE_FILES_PROPERTIES(AuHost.cpp AuCocoaView.cpp PROPERTIES COMPILE_OPTIONS "-xobjective-c++")

BUILD_PLUGIN(aueffect
	AuCocoaView.cpp
	AuEffect.cpp
	AuEffectControlDialog.cpp
	AuEffectControls.cpp
	AuHost.cpp
	AuParameter.cpp
	AuState.cpp
	AuSubPluginFeatures.cpp
	MOCFILES
		AuEffectControlDialog.h
		AuEffectControls.h
		AuParameter.h
	EMBEDDED_RESOURCES
		"${CMAKE_CURRENT_SOURCE_DIR}/logo.png"
)

# The bounded lock-free MIDI queue is the VST3 host's header-only one (no VST3 SDK type in it).
TARGET_INCLUDE_DIRECTORIES(aueffect PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../Vst3Effect")
TARGET_LINK_LIBRARIES(aueffect
	"-framework AudioToolbox" "-framework AudioUnit" "-framework CoreAudio" "-framework CoreAudioKit"
	"-framework CoreFoundation" "-framework AppKit")
