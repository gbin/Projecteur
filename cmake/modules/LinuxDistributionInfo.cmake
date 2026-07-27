# This file is part of Projecteur - https://github.com/jahnf/projecteur - See LICENSE.md and README.md
cmake_minimum_required(VERSION 3.20)

# Try to get the Linux distribution and version as a string (host system)
# When cross compiling this function won't work to get the target distribution.
function(get_linux_distribution VAR_DIST_NAME VAR_DIST_VERSION)
  # Set fallback defaults
  set(DIST_NAME "linux")
  set(DIST_NAME_SET 0)
  set(DIST_VERSION "unknown")
  set(DIST_VERSION_SET 0)

  # Read os- lsb-... release files
  file(GLOB rel_info_files "/etc/*-release")
  foreach(info_file IN LISTS rel_info_files)
    file(STRINGS "${info_file}" file_lines LIMIT_COUNT 128)
    list(APPEND rel_info_all "${file_lines}")
  endforeach()

  # Get distribution id/name - try different keys
  foreach(var ID DISTRIB_ID NAME)
    foreach(line IN LISTS rel_info_all)
      if( "${line}" MATCHES "^${var}=[\"]?([^ \"]*)")
        string(STRIP "${CMAKE_MATCH_1}" DIST_NAME)
        string(TOLOWER "${DIST_NAME}" DIST_NAME)
        string(REPLACE "\\" "_" DIST_NAME "${DIST_NAME}")
        string(REPLACE "/" "_" DIST_NAME "${DIST_NAME}")
        set(DIST_NAME_SET 1)
        break()
      endif()
    endforeach()
    if(DIST_NAME_SET)
      break()
    endif()
  endforeach()

  # Get distribution version/release - try different keys
  foreach(var VERSION_ID DISTRIB_RELEASE VERSION)
    foreach(line IN LISTS rel_info_all)
      if( "${line}" MATCHES "^${var}=[\"]?([^ \"]*)")
        string(STRIP "${CMAKE_MATCH_1}" DIST_VERSION)
        string(TOLOWER "${DIST_VERSION}" DIST_VERSION)
        string(REPLACE "\\" "_" DIST_VERSION "${DIST_VERSION}")
        string(REPLACE "/" "_" DIST_VERSION "${DIST_VERSION}")
        set(DIST_VERSION_SET 1)
        break()
      endif()
    endforeach()
    if(DIST_VERSION_SET)
      break()
    endif()
  endforeach()

  if(NOT DIST_NAME_SET)
    message(STATUS "Could not get linux distribution id, defaulting to 'linux'")
  endif()

  if(NOT DIST_VERSION_SET)
    message(STATUS "Could not get linux version, defaulting to 'unknown'")
  endif()

  set(${VAR_DIST_NAME} "${DIST_NAME}" PARENT_SCOPE)
  set(${VAR_DIST_VERSION} "${DIST_VERSION}" PARENT_SCOPE)
endfunction()
