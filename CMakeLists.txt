cmake_minimum_required(VERSION 3.20)

file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" PROJECTEUR_RELEASE_VERSION
     LIMIT_COUNT 1 REGEX "^[0-9]+\\.[0-9]+\\.[0-9]+$")
if(NOT PROJECTEUR_RELEASE_VERSION)
  message(FATAL_ERROR "VERSION must contain one semantic version such as 1.0.0")
endif()
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/Cargo.toml" PROJECTEUR_CARGO_VERSION_LINE
     REGEX "^version = \"[0-9]+\\.[0-9]+\\.[0-9]+\"$")
string(REGEX REPLACE "^version = \"([^\"]+)\"$" "\\1"
       PROJECTEUR_CARGO_VERSION "${PROJECTEUR_CARGO_VERSION_LINE}")
if(NOT "${PROJECTEUR_CARGO_VERSION}" STREQUAL "${PROJECTEUR_RELEASE_VERSION}")
  message(FATAL_ERROR "Cargo.toml and VERSION must contain the same release version")
endif()

# Cargo owns the application build. CMake remains only for the native Plasma
# applet, installation layout, and distro package generation.
project(Projecteur VERSION "${PROJECTEUR_RELEASE_VERSION}" LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(ECM 6.7 REQUIRED NO_MODULE)
list(APPEND CMAKE_MODULE_PATH ${ECM_MODULE_PATH} "${CMAKE_CURRENT_SOURCE_DIR}/cmake/modules")
include(KDEInstallDirs6)

find_package(Qt6 6.10 REQUIRED COMPONENTS Core DBus)
find_package(KF6 6.7 REQUIRED COMPONENTS Config CoreAddons Package)
find_package(Plasma 6.7 REQUIRED)

find_program(CARGO_EXECUTABLE cargo REQUIRED)
set(PROJECTEUR_CARGO_TARGET_DIR "${CMAKE_CURRENT_SOURCE_DIR}/target" CACHE PATH
    "Cargo target directory containing the Projecteur binary")
set(PROJECTEUR_CARGO_BINARY "${PROJECTEUR_CARGO_TARGET_DIR}/release/projecteur")

add_custom_target(projecteur ALL
  COMMAND "${CMAKE_COMMAND}" -E env
          "CARGO_TARGET_DIR=${PROJECTEUR_CARGO_TARGET_DIR}"
          "${CARGO_EXECUTABLE}" build --locked --release --bin projecteur
  BYPRODUCTS "${PROJECTEUR_CARGO_BINARY}"
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
  COMMENT "Building the Projecteur Rust application"
  VERBATIM)
set_target_properties(projecteur PROPERTIES
  VERSION_MAJOR "${PROJECT_VERSION_MAJOR}"
  VERSION_MINOR "${PROJECT_VERSION_MINOR}"
  VERSION_PATCH "${PROJECT_VERSION_PATCH}"
  VERSION_STRING "${PROJECT_VERSION}"
  VERSION_STRING_FULL "${PROJECT_VERSION}"
  VERSION_FLAG ""
  VERSION_DISTANCE 0)

add_subdirectory(plasma)

set(CMAKE_INSTALL_DEFAULT_DIRECTORY_PERMISSIONS
  OWNER_READ OWNER_WRITE OWNER_EXECUTE
  GROUP_READ GROUP_EXECUTE
  WORLD_READ WORLD_EXECUTE)
install(PROGRAMS "${PROJECTEUR_CARGO_BINARY}" DESTINATION bin)

if(NOT CMAKE_INSTALL_UDEVRULESDIR)
  set(UDEVDIR /lib/udev)
  find_package(PkgConfig)
  if(PKG_CONFIG_FOUND)
    pkg_check_modules(PKGCONFIG_UDEV udev)
    if(PKGCONFIG_UDEV_FOUND)
      execute_process(
        COMMAND "${PKG_CONFIG_EXECUTABLE}" --variable=udevdir udev
        OUTPUT_VARIABLE PKGCONFIG_UDEVDIR
        OUTPUT_STRIP_TRAILING_WHITESPACE)
      if(PKGCONFIG_UDEVDIR)
        file(TO_CMAKE_PATH "${PKGCONFIG_UDEVDIR}" UDEVDIR)
      endif()
    endif()
  endif()
endif()
set(CMAKE_INSTALL_UDEVDIR "${UDEVDIR}" CACHE PATH "Udev base directory")
set(CMAKE_INSTALL_UDEVRULESDIR "${UDEVDIR}/rules.d" CACHE PATH "Udev rules directory")
mark_as_advanced(CMAKE_INSTALL_UDEVDIR CMAKE_INSTALL_UDEVRULESDIR)

set(idRegex "0x([0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F])")
set(lineRegex "^[ \t]*${idRegex}[ \t]*,[ \t]*${idRegex}[ \t]*,[ \t]*(usb|bt)[ \t]*,[ \t]*(.*)[ \t]*")
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/devices.conf" CONFLINES REGEX "${lineRegex}")
foreach(line ${CONFLINES})
  if(line MATCHES "${lineRegex}")
    set(vendorId "${CMAKE_MATCH_1}")
    set(productId "${CMAKE_MATCH_2}")
    if("${CMAKE_MATCH_3}" STREQUAL "usb")
      string(APPEND EXTRA_USB_UDEV_RULES "\n## Extra-Device: ${CMAKE_MATCH_4}")
      string(APPEND EXTRA_USB_UDEV_RULES "\nSUBSYSTEMS==\"usb\", ATTRS{idVendor}==\"${vendorId}\"")
      string(APPEND EXTRA_USB_UDEV_RULES ", ATTRS{idProduct}==\"${productId}\", MODE=\"0660\", TAG+=\"uaccess\"")
    elseif("${CMAKE_MATCH_3}" STREQUAL "bt")
      string(REGEX REPLACE "^0+" "" vendorId "${vendorId}")
      string(REGEX REPLACE "^0+" "" productId "${productId}")
      string(APPEND EXTRA_BLUETOOTH_UDEV_RULES "\n## Extra-Device: ${CMAKE_MATCH_4}")
      string(APPEND EXTRA_BLUETOOTH_UDEV_RULES "\nSUBSYSTEMS==\"input\", ENV{LIBINPUT_DEVICE_GROUP}=\"5/${vendorId}/${productId}*\", MODE=\"0660\", TAG+=\"uaccess\"")
    endif()
  endif()
endforeach()

set(OUTDIR "${CMAKE_CURRENT_BINARY_DIR}")
set(TMPLDIR "${CMAKE_CURRENT_SOURCE_DIR}/cmake/templates")
configure_file("55-projecteur.rules.in" "55-projecteur.rules" @ONLY)
install(FILES "${OUTDIR}/55-projecteur.rules" DESTINATION "${CMAKE_INSTALL_UDEVRULESDIR}")

install(FILES icons/projecteur-tray.svg DESTINATION share/icons/hicolor/scalable/apps RENAME projecteur.svg)
install(FILES projecteur.notifyrc DESTINATION "${KDE_INSTALL_KNOTIFYRCDIR}")
install(FILES plasma/org.projecteur.Projecteur.xml DESTINATION "${KDE_INSTALL_DBUSINTERFACEDIR}")

set(HOMEPAGE "https://github.com/gbin/Projecteur")
set(PROJECTEUR_APPLICATION_ID "org.projecteur.Projecteur")
set(PROJECTEUR_INSTALL_PATH "${CMAKE_INSTALL_PREFIX}/bin/projecteur")
configure_file("${TMPLDIR}/projecteur.desktop.in" "${PROJECTEUR_APPLICATION_ID}.desktop" @ONLY)
install(FILES "${OUTDIR}/${PROJECTEUR_APPLICATION_ID}.desktop" DESTINATION share/applications)

set(_dbus_service "${OUTDIR}/${PROJECTEUR_APPLICATION_ID}.service")
file(WRITE "${_dbus_service}"
  "[D-BUS Service]\nName=${PROJECTEUR_APPLICATION_ID}\nExec=${PROJECTEUR_INSTALL_PATH}\n")
install(FILES "${_dbus_service}" DESTINATION "${KDE_INSTALL_DBUSSERVICEDIR}")

set(VERSION_STRING "${PROJECT_VERSION}")
string(TIMESTAMP VERSION_DATE_MONTH_YEAR "%B %Y")
configure_file("${TMPLDIR}/projecteur.1" "${OUTDIR}/projecteur.1" @ONLY)
install(FILES "${OUTDIR}/projecteur.1" DESTINATION share/man/man1)

configure_file("${TMPLDIR}/projecteur.metainfo.xml"
               "${PROJECTEUR_APPLICATION_ID}.metainfo.xml" @ONLY)
install(FILES "${OUTDIR}/${PROJECTEUR_APPLICATION_ID}.metainfo.xml" DESTINATION share/metainfo)
install(FILES "${TMPLDIR}/projecteur.bash-completion"
        DESTINATION share/bash-completion/completions RENAME projecteur)

configure_file("${TMPLDIR}/postinst.in" "pkg/scripts/postinst" @ONLY)

option(PACKAGE_TARGETS "Create distro packaging targets" ON)
if(PACKAGE_TARGETS)
  set(_source_name "projecteur-${PROJECT_VERSION}_source.tar.gz")
  set(_source_path "${CMAKE_CURRENT_BINARY_DIR}/dist-pkg/${_source_name}")
  add_custom_target(source-archive
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/dist-pkg"
    COMMAND sh -c "git archive --format=tar --prefix=projecteur-${PROJECT_VERSION}/ HEAD | gzip -9n > '${_source_path}'"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    VERBATIM)
  set_target_properties(source-archive PROPERTIES OUTPUT_ARCHIVE "${_source_path}")

  include(LinuxPackaging)
  add_dist_package_target(
    PROJECT "${CMAKE_PROJECT_NAME}"
    TARGET projecteur
    DESCRIPTION_BRIEF "Wayland application for Logitech presenter devices."
    DESCRIPTION_FULL "Projecteur is a virtual laser pointer and live magnifier for KDE Plasma presentations."
    CONTACT "Guillaume Binet and Projecteur contributors"
    HOMEPAGE "${HOMEPAGE}"
    DEBIAN_SECTION "utils"
    POSTINST_SCRIPT "${OUTDIR}/pkg/scripts/postinst")
  add_dependencies(dist-package projecteur)
  add_dependencies(dist-package org.projecteur.Projecteur)
endif()
