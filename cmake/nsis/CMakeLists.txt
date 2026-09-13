if(LMMS_MSVC_YEAR)
	set(WIN_PLATFORM "msvc${LMMS_MSVC_YEAR}")
elseif(DEFINED ENV{MSYSTEM})
    string(TOLOWER "$ENV{MSYSTEM}" WIN_PLATFORM)
else()
	set(WIN_PLATFORM mingw)
endif()

# the final slash needs to be flipped for CPACK_PACKAGE_ICON to work:
#   https://cmake.org/pipermail/cmake/2008-June/022085.html
SET(CPACK_PACKAGE_ICON                  "${CMAKE_SOURCE_DIR}/cmake/nsis\\\\nsis_branding.bmp")
SET(CPACK_NSIS_MUI_ICON                 "${CMAKE_SOURCE_DIR}/cmake/nsis/icon.ico")
SET(CPACK_NSIS_INSTALLED_ICON_NAME      "${CMAKE_PROJECT_NAME}.exe" PARENT_SCOPE)
SET(CPACK_NSIS_DISPLAY_NAME             "Zene Studio ${VERSION}" PARENT_SCOPE)
SET(CPACK_NSIS_HELP_LINK                "${PROJECT_URL}" PARENT_SCOPE)
SET(CPACK_NSIS_URL_INFO_ABOUT           "${PROJECT_URL}" PARENT_SCOPE)
SET(CPACK_NSIS_CONTACT                  "${PROJECT_EMAIL}" PARENT_SCOPE)
SET(CPACK_PACKAGE_EXECUTABLES           "${CMAKE_PROJECT_NAME}.exe;Zene Studio" PARENT_SCOPE)
SET(CPACK_NSIS_MENU_LINKS               "${CMAKE_PROJECT_NAME}.exe;Zene Studio" PARENT_SCOPE)
SET(CPACK_NSIS_DEFINES                  "
  !addincludedir ${CMAKE_SOURCE_DIR}/cmake/nsis
  !include FileAssociation.nsh
  !include LogicLib.nsh
  !include WinVer.nsh")
SET(CPACK_PACKAGE_FILE_NAME             "${CMAKE_PROJECT_NAME}-${VERSION}-${WIN_PLATFORM}-win32")
SET(CPACK_NSIS_EXTRA_INSTALL_COMMANDS   "
  \\\${registerExtension} \\\"$INSTDIR\\\\${CMAKE_PROJECT_NAME}.exe\\\" \\\".mmp\\\" \\\"Zene Studio Project\\\"
  \\\${registerExtension} \\\"$INSTDIR\\\\${CMAKE_PROJECT_NAME}.exe\\\" \\\".mmpz\\\" \\\"Zene Studio Project (compressed)\\\"
  \\\${IfNot} \\\${AtMostWin7}
    WriteRegDWORD HKLM \\\"Software\\\\Microsoft\\\\Windows\\\\CurrentVersion\\\\SideBySide\\\" \\\"PreferExternalManifest\\\" \\\"1\\\"
  \\\${EndIf}
  " PARENT_SCOPE)
SET(CPACK_NSIS_EXTRA_UNINSTALL_COMMANDS "
  \\\${unregisterExtension} \\\".mmp\\\" \\\"Zene Studio Project\\\"
  \\\${unregisterExtension} \\\".mmpz\\\" \\\"Zene Studio Project (compressed)\\\"
  DeleteRegKey HKCR \\\"Zene Studio Project\\\"
  DeleteRegKey HKCR \\\"Zene Studio Project (compressed)\\\"
  " PARENT_SCOPE)

IF(WIN64)
	if(IS_ARM64)
		set(CPACK_PACKAGE_FILE_NAME      "${CMAKE_PROJECT_NAME}-${VERSION}-${WIN_PLATFORM}-arm64")
	else()
		set(CPACK_PACKAGE_FILE_NAME      "${CMAKE_PROJECT_NAME}-${VERSION}-${WIN_PLATFORM}-win64")
	endif()
	# 64-bit builds install into the 64-bit Program Files. That belongs in CPACK_NSIS_INSTALL_ROOT:
	# CPack's NSIS template renders its own InstallDir line from it
	# (`InstallDir "@CPACK_NSIS_INSTALL_ROOT@\@CPACK_PACKAGE_INSTALL_DIRECTORY@"`, quoted), and the
	# template's repair/uninstall comparison uses the same variable, so the installer and the
	# uninstaller agree on $INSTDIR.
	#
	# The inherited code injected the path through CPACK_NSIS_DEFINES instead:
	#   SET(CPACK_INSTALL_FIX "$PROGRAMFILES64\\${CPACK_PACKAGE_INSTALL_DIRECTORY}\\")
	#   SET(CPACK_NSIS_DEFINES "${CPACK_NSIS_DEFINES} \n InstallDir ${CPACK_INSTALL_FIX}")
	# which reaches makensis as an UNQUOTED, space-containing path:
	#   InstallDir $PROGRAMFILES64\\Zene Studio\\
	# makensis tokenises that as several parameters and aborts the whole package immediately:
	#   InstallDir expects 1 parameters, got 2.
	#   Usage: InstallDir default_install_directory
	#   Error in script ".../project.nsi" on line 49 -- aborting creation process
	# CPack reports only its generic pair ("Problem running NSIS command" and "Problem compressing
	# the directory") because makensis's own output goes to _CPack_Packages/<system>/NSIS/
	# NSISOutput.log, which no workflow step printed or uploaded -- which is why this failed on every
	# Windows job that reached packaging (the arm64 job's Package step has never once succeeded) and
	# why the cause was invisible. Reproduced and fixed end to end on 2026-09-13 against Ubuntu's
	# nsis 3.09, the same makensis the mingw64 job packages with (job 103647387841 / run 34728692245).
	SET(CPACK_NSIS_INSTALL_ROOT "$PROGRAMFILES64" PARENT_SCOPE)
	# InstallDir is set by CPack itself: its NSIS template emits
	#   InstallDir "@CPACK_NSIS_INSTALL_ROOT@\@CPACK_PACKAGE_INSTALL_DIRECTORY@"
	# which is QUOTED, and CPACK_NSIS_INSTALL_ROOT defaults to $PROGRAMFILES64 for
	# a win64 target. An unquoted "InstallDir <path>" is a single NSIS parameter
	# only while <path> contains no space; CPACK_PACKAGE_INSTALL_DIRECTORY became
	# "Zene Studio" in the rename, so the override that used to be appended below
	# had two, and makensis aborted before compressing anything ("InstallDir
	# expects 1 parameters, got 2"). Removing it also keeps the default install
	# directory identical to the one CPack's own install/repair checks compare
	# $INSTDIR against.
ENDIF()

# Setup missing parent scopes
SET(CPACK_PACKAGE_FILE_NAME             "${CPACK_PACKAGE_FILE_NAME}" PARENT_SCOPE)
SET(CPACK_NSIS_DEFINES                  "${CPACK_NSIS_DEFINES}" PARENT_SCOPE)
SET(CPACK_PACKAGE_ICON                  "${CPACK_PACKAGE_ICON}" PARENT_SCOPE)
SET(CPACK_NSIS_MUI_ICON                 "${CPACK_NSIS_MUI_ICON}" PARENT_SCOPE)

# Disable cpack's strip for historic reasons
set(CPACK_STRIP_FILES_ORIG "${CPACK_STRIP_FILES}" PARENT_SCOPE)
set(CPACK_STRIP_FILES FALSE PARENT_SCOPE)

if(CPACK_DEBUG)
	# CMake 3.19+
	set(CPACK_NSIS_EXECUTABLE_PRE_ARGUMENTS "-V4")
endif()

# Windows resource compilers
CONFIGURE_FILE("zene.rc.in"             "${CMAKE_BINARY_DIR}/zene.rc")
CONFIGURE_FILE("zynaddsubfx.rc.in"      "${CMAKE_BINARY_DIR}/plugins/ZynAddSubFx/zynaddsubfx.rc")

INSTALL(FILES "zene.exe.manifest" DESTINATION .)
INSTALL(FILES "zene.VisualElementsManifest.xml" DESTINATION .)
INSTALL(DIRECTORY "assets" DESTINATION .)
