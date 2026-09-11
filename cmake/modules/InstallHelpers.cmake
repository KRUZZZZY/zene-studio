#
# install all files matching certain wildcards below ${LMMS_DATA_DIR}/<subdir>
#
# example:
#
#   INSTALL_DATA_SUBDIRS("samples" "*.ogg;*.wav;*.flac")
#
# Copyright (c) 2008 Tobias Doerffel
#


# helper-macro
MACRO(LIST_CONTAINS var value)
	SET(${var})
		FOREACH (value2 ${ARGN})
			IF (${value} STREQUAL ${value2})
				SET(${var} TRUE)
			ENDIF (${value} STREQUAL ${value2})
	ENDFOREACH (value2)
ENDMACRO(LIST_CONTAINS)


MACRO(INSTALL_DATA_SUBDIRS _subdir _wildcards)
	FOREACH(_wildcard ${_wildcards})
		FILE(GLOB_RECURSE files ${_wildcard})
		LIST(SORT files)
		SET(SUBDIRS)

		FOREACH(_item ${files})
			GET_FILENAME_COMPONENT(_dir "${_item}" PATH)
			LIST_CONTAINS(contains _dir ${SUBDIRS})
			IF(NOT contains)
				LIST(APPEND SUBDIRS "${_dir}")
			ENDIF(NOT contains)
		ENDFOREACH(_item ${files})

		FOREACH(_dir ${SUBDIRS})
			FILE(GLOB files "${_dir}/${_wildcard}")
			LIST(SORT files)
			# Path arithmetic, not string surgery. The previous code stripped
			# "${CMAKE_CURRENT_SOURCE_DIR}/" — with a trailing slash — from a directory
			# path that GET_FILENAME_COMPONENT returns WITHOUT one. For any file sitting
			# directly in this CMakeLists' directory the strip therefore never matched,
			# _dir stayed ABSOLUTE, and the destination became
			#   <data-dir>/<subdir>/home/user/…/data/<subdir>
			# On Linux that quietly installs a junk tree under the prefix; CPack cannot
			# create "C:/…" beneath the package root and fails outright (CI windows-arm64,
			# NSIS: "file cannot create directory … Maybe need administrative privileges").
			FILE(RELATIVE_PATH _rel "${CMAKE_CURRENT_SOURCE_DIR}" "${_dir}")
			IF(_rel STREQUAL ".")
				SET(_rel "")
			ENDIF()
			FOREACH(_file ${files})
				INSTALL(FILES "${_file}" DESTINATION "${LMMS_DATA_DIR}/${_subdir}/${_rel}/")
			ENDFOREACH(_file ${files})
		ENDFOREACH(_dir ${SUBDIRS})
	ENDFOREACH(_wildcard ${_wildcards})
ENDMACRO(INSTALL_DATA_SUBDIRS)

