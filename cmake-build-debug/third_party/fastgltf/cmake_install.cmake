# Install script for directory: /mnt/c/Users/ellipsoid/CLionProjects/vulkan-guide/third_party/fastgltf

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
    set(CMAKE_INSTALL_CONFIG_NAME "Debug")
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
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include" TYPE FILE FILES "/mnt/c/Users/ellipsoid/CLionProjects/vulkan-guide/third_party/fastgltf/deps/simdjson/simdjson.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "/mnt/c/Users/ellipsoid/CLionProjects/vulkan-guide/cmake-build-debug/third_party/fastgltf/libfastgltf_simdjson.a")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/cmake/fastgltf/fastgltf_simdjsonTargets.cmake")
    file(DIFFERENT _cmake_export_file_changed FILES
         "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/cmake/fastgltf/fastgltf_simdjsonTargets.cmake"
         "/mnt/c/Users/ellipsoid/CLionProjects/vulkan-guide/cmake-build-debug/third_party/fastgltf/CMakeFiles/Export/14b504216a8c2b5661a6a7c88f3023ae/fastgltf_simdjsonTargets.cmake")
    if(_cmake_export_file_changed)
      file(GLOB _cmake_old_config_files "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/cmake/fastgltf/fastgltf_simdjsonTargets-*.cmake")
      if(_cmake_old_config_files)
        string(REPLACE ";" ", " _cmake_old_config_files_text "${_cmake_old_config_files}")
        message(STATUS "Old export file \"$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/cmake/fastgltf/fastgltf_simdjsonTargets.cmake\" will be replaced.  Removing files [${_cmake_old_config_files_text}].")
        unset(_cmake_old_config_files_text)
        file(REMOVE ${_cmake_old_config_files})
      endif()
      unset(_cmake_old_config_files)
    endif()
    unset(_cmake_export_file_changed)
  endif()
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/cmake/fastgltf" TYPE FILE FILES "/mnt/c/Users/ellipsoid/CLionProjects/vulkan-guide/cmake-build-debug/third_party/fastgltf/CMakeFiles/Export/14b504216a8c2b5661a6a7c88f3023ae/fastgltf_simdjsonTargets.cmake")
  if(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Dd][Ee][Bb][Uu][Gg])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/cmake/fastgltf" TYPE FILE FILES "/mnt/c/Users/ellipsoid/CLionProjects/vulkan-guide/cmake-build-debug/third_party/fastgltf/CMakeFiles/Export/14b504216a8c2b5661a6a7c88f3023ae/fastgltf_simdjsonTargets-debug.cmake")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/fastgltf" TYPE FILE FILES
    "/mnt/c/Users/ellipsoid/CLionProjects/vulkan-guide/third_party/fastgltf/include/fastgltf/base64.hpp"
    "/mnt/c/Users/ellipsoid/CLionProjects/vulkan-guide/third_party/fastgltf/include/fastgltf/glm_element_traits.hpp"
    "/mnt/c/Users/ellipsoid/CLionProjects/vulkan-guide/third_party/fastgltf/include/fastgltf/parser.hpp"
    "/mnt/c/Users/ellipsoid/CLionProjects/vulkan-guide/third_party/fastgltf/include/fastgltf/tools.hpp"
    "/mnt/c/Users/ellipsoid/CLionProjects/vulkan-guide/third_party/fastgltf/include/fastgltf/types.hpp"
    "/mnt/c/Users/ellipsoid/CLionProjects/vulkan-guide/third_party/fastgltf/include/fastgltf/util.hpp"
    )
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "/mnt/c/Users/ellipsoid/CLionProjects/vulkan-guide/cmake-build-debug/third_party/fastgltf/libfastgltf.a")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/cmake/fastgltf/fastgltfConfig.cmake")
    file(DIFFERENT _cmake_export_file_changed FILES
         "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/cmake/fastgltf/fastgltfConfig.cmake"
         "/mnt/c/Users/ellipsoid/CLionProjects/vulkan-guide/cmake-build-debug/third_party/fastgltf/CMakeFiles/Export/14b504216a8c2b5661a6a7c88f3023ae/fastgltfConfig.cmake")
    if(_cmake_export_file_changed)
      file(GLOB _cmake_old_config_files "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/cmake/fastgltf/fastgltfConfig-*.cmake")
      if(_cmake_old_config_files)
        string(REPLACE ";" ", " _cmake_old_config_files_text "${_cmake_old_config_files}")
        message(STATUS "Old export file \"$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/cmake/fastgltf/fastgltfConfig.cmake\" will be replaced.  Removing files [${_cmake_old_config_files_text}].")
        unset(_cmake_old_config_files_text)
        file(REMOVE ${_cmake_old_config_files})
      endif()
      unset(_cmake_old_config_files)
    endif()
    unset(_cmake_export_file_changed)
  endif()
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/cmake/fastgltf" TYPE FILE FILES "/mnt/c/Users/ellipsoid/CLionProjects/vulkan-guide/cmake-build-debug/third_party/fastgltf/CMakeFiles/Export/14b504216a8c2b5661a6a7c88f3023ae/fastgltfConfig.cmake")
  if(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Dd][Ee][Bb][Uu][Gg])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/cmake/fastgltf" TYPE FILE FILES "/mnt/c/Users/ellipsoid/CLionProjects/vulkan-guide/cmake-build-debug/third_party/fastgltf/CMakeFiles/Export/14b504216a8c2b5661a6a7c88f3023ae/fastgltfConfig-debug.cmake")
  endif()
endif()

