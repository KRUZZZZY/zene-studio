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
	SET(CPACK_INSTALL_FIX "$PROGRAMFILES64\\\\${CPACK_PACKAGE_INSTALL_DIRECTORY}\\\\")
	SET(CPACK_NSIS_DEFINES "
		${CPACK_NSIS_DEFINES} 
		InstallDir ${CPACK_INSTALL_FIX}
		")
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
