# Install script for directory: /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/lmms-fl-research/lmms-stems/src

# Set the install prefix
if(NOT DEFINED CMAKE_INSTALL_PREFIX)
  set(CMAKE_INSTALL_PREFIX "/usr/local")
endif()
string(REGEX REPLACE "/$" "" CMAKE_INSTALL_PREFIX "${CMAKE_INSTALL_PREFIX}")

# Set the install configuration name.
if(NOT DEFINED CMAKE_INSTALL_CONFIG_NAME)
  if(BUILD_TYPE)
    string(REGEX REPLACE "^[^A-Za-z0-9_]+" ""
           CMAKE_INSTALL_CONFIG_NAME "${BUILD_TYPE}")
  else()
    set(CMAKE_INSTALL_CONFIG_NAME "Release")
  endif()
  message(STATUS "Install configuration: \"${CMAKE_INSTALL_CONFIG_NAME}\"")
endif()

# Set the component getting installed.
if(NOT CMAKE_INSTALL_COMPONENT)
  if(COMPONENT)
    message(STATUS "Install component: \"${COMPONENT}\"")
    set(CMAKE_INSTALL_COMPONENT "${COMPONENT}")
  else()
    set(CMAKE_INSTALL_COMPONENT)
  endif()
endif()

# Install shared libraries without execute permission?
if(NOT DEFINED CMAKE_INSTALL_SO_NO_EXE)
  set(CMAKE_INSTALL_SO_NO_EXE "1")
endif()

# Is this installation the result of a crosscompile?
if(NOT DEFINED CMAKE_CROSSCOMPILING)
  set(CMAKE_CROSSCOMPILING "FALSE")
endif()

# Set default install directory permissions.
if(NOT DEFINED CMAKE_OBJDUMP)
  set(CMAKE_OBJDUMP "/usr/bin/objdump")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/share/man/man1" TYPE FILE PERMISSIONS OWNER_READ GROUP_READ WORLD_READ FILES "/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/lmms-fl-research/lmms-stems/doc/lmms.1")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/bin" TYPE EXECUTABLE FILES "/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/lmms-fl-research/lmms-stems/build-ort-present/lmms")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/bin/lmms" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/bin/lmms")
    if(CMAKE_INSTALL_DO_STRIP)
      execute_process(COMMAND "/usr/bin/strip" "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/bin/lmms")
    endif()
  endif()
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for each subdirectory.
  include("/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/lmms-fl-research/lmms-stems/build-ort-present/src/3rdparty/cmake_install.cmake")
  include("/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/lmms-fl-research/lmms-stems/build-ort-present/src/common/cmake_install.cmake")
  include("/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/lmms-fl-research/lmms-stems/build-ort-present/src/core/cmake_install.cmake")
  include("/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/lmms-fl-research/lmms-stems/build-ort-present/src/gui/cmake_install.cmake")
  include("/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/lmms-fl-research/lmms-stems/build-ort-present/src/tracks/cmake_install.cmake")

endif()

