cmake_minimum_required(VERSION 3.20)

# Set the default build type to release
if( NOT CMAKE_BUILD_TYPE )
  message(STATUS "Setting build type to 'Release' as none was specified.")
  set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Choose the type of build." FORCE)
endif()

set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release" "MinSizeRel" "RelWithDebInfo")

project(Projecteur LANGUAGES C CXX)

add_compile_options(-Wall -Wextra -Werror)
#set(CMAKE_CXX_CLANG_TIDY clang-tidy-12)

find_package(ECM 6.7 REQUIRED NO_MODULE)
list(APPEND CMAKE_MODULE_PATH ${ECM_MODULE_PATH})
include(KDEInstallDirs6)
include(ECMQtDeclareLoggingCategory)
find_package(QtWaylandScanner REQUIRED)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake/modules")
include(GitVersion)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.11 REQUIRED COMPONENTS Core DBus Gui Quick ShaderTools WaylandClient Widgets)
find_package(KF6 6.7 REQUIRED COMPONENTS Config ConfigWidgets CoreAddons DBusAddons GlobalAccel I18n
                                        Notifications Package KirigamiPlatform WidgetsAddons WindowSystem XmlGui)
find_package(KPipeWire 6.7 REQUIRED)
find_package(Plasma 6.7 REQUIRED)
find_package(LayerShellQt 6.7 REQUIRED)
message(STATUS "Using Qt version: ${Qt6_VERSION}")

qt6_add_resources(RESOURCES qml/qml.qrc resources.qrc)

add_executable(projecteur
  src/main.cc                  src/enum-helper.h
  src/actiondelegate.cc        src/actiondelegate.h
  src/device.cc                src/device.h
  src/device-command-helper.cc src/device-command-helper.h
  src/device-hidpp.cc          src/device-hidpp.h
  src/device-key-lookup.cc     src/device-key-lookup.h
  src/deviceinput.cc           src/deviceinput.h
  src/devicescan.cc            src/devicescan.h
  src/deviceswidget.cc         src/deviceswidget.h
  src/hidpp.cc                 src/hidpp.h
  src/linuxdesktop.cc          src/linuxdesktop.h
  src/kwinscreencast.cc        src/kwinscreencast.h
  src/iconwidgets.cc           src/iconwidgets.h
  src/imageitem.cc             src/imageitem.h
  src/inputmapconfig.cc        src/inputmapconfig.h
  src/inputseqedit.cc          src/inputseqedit.h
  src/nativekeyseqedit.cc      src/nativekeyseqedit.h
  src/preferencesdlg.cc        src/preferencesdlg.h
  src/presentationtimer.cc     src/presentationtimer.h
  src/projecteurapp.cc         src/projecteurapp.h
  src/projecteurcontrol.cc     src/projecteurcontrol.h
  src/settings.cc              src/settings.h
  src/spotlight.cc             src/spotlight.h
  src/spotshapes.cc            src/spotshapes.h
  src/virtualdevice.cc         src/virtualdevice.h
  ${RESOURCES})

qt6_add_shaders(projecteur projecteur_shaders
  PREFIX "/shaders"
  BASE "${CMAKE_CURRENT_SOURCE_DIR}/qml/shaders"
  FILES qml/shaders/textzoom.frag
)

ecm_add_qtwayland_client_protocol(projecteur
  PROTOCOL "${CMAKE_CURRENT_SOURCE_DIR}/protocols/zkde-screencast-unstable-v1.xml"
  BASENAME zkde-screencast-unstable-v1
)

ecm_qt_declare_logging_category(projecteur
  HEADER projecteur_main_debug.h
  IDENTIFIER PROJECTEUR_MAIN_LOG
  CATEGORY_NAME projecteur.mainapp
  DEFAULT_SEVERITY Info
  DESCRIPTION "Projecteur application"
  EXPORT Projecteur
)
ecm_qt_declare_logging_category(projecteur
  HEADER projecteur_command_debug.h
  IDENTIFIER PROJECTEUR_COMMAND_LOG
  CATEGORY_NAME projecteur.cmdserver
  DEFAULT_SEVERITY Info
  DESCRIPTION "Projecteur command service"
  EXPORT Projecteur
)
ecm_qt_declare_logging_category(projecteur
  HEADER projecteur_desktop_debug.h
  IDENTIFIER PROJECTEUR_DESKTOP_LOG
  CATEGORY_NAME projecteur.desktop
  DEFAULT_SEVERITY Info
  DESCRIPTION "Projecteur desktop integration"
  EXPORT Projecteur
)
ecm_qt_declare_logging_category(projecteur
  HEADER projecteur_device_debug.h
  IDENTIFIER PROJECTEUR_DEVICE_LOG
  CATEGORY_NAME projecteur.device
  DEFAULT_SEVERITY Info
  DESCRIPTION "Projecteur device handling"
  EXPORT Projecteur
)
ecm_qt_declare_logging_category(projecteur
  HEADER projecteur_hid_debug.h
  IDENTIFIER PROJECTEUR_HID_LOG
  CATEGORY_NAME projecteur.hid
  OLD_CATEGORY_NAMES projecteur.HID
  DEFAULT_SEVERITY Info
  DESCRIPTION "Projecteur HID++ protocol"
  EXPORT Projecteur
)
ecm_qt_declare_logging_category(projecteur
  HEADER projecteur_input_debug.h
  IDENTIFIER PROJECTEUR_INPUT_LOG
  CATEGORY_NAME projecteur.input
  DEFAULT_SEVERITY Info
  DESCRIPTION "Projecteur input mapping"
  EXPORT Projecteur
)
ecm_qt_declare_logging_category(projecteur
  HEADER projecteur_settings_debug.h
  IDENTIFIER PROJECTEUR_SETTINGS_LOG
  CATEGORY_NAME projecteur.settings
  DEFAULT_SEVERITY Info
  DESCRIPTION "Projecteur settings"
  EXPORT Projecteur
)
ecm_qt_declare_logging_category(projecteur
  HEADER projecteur_virtual_device_debug.h
  IDENTIFIER PROJECTEUR_VIRTUAL_DEVICE_LOG
  CATEGORY_NAME projecteur.virtualdevice
  DEFAULT_SEVERITY Info
  DESCRIPTION "Projecteur virtual input devices"
  EXPORT Projecteur
)
ecm_qt_install_logging_categories(
  EXPORT Projecteur
  FILE projecteur.categories
  DESTINATION "${KDE_INSTALL_LOGGINGCATEGORIESDIR}"
  SORT
)

set(PROJECTEUR_CONTROL_XML
  "${CMAKE_CURRENT_SOURCE_DIR}/src/org.projecteur.Projecteur.xml")
qt_add_dbus_adaptor(PROJECTEUR_DBUS_ADAPTOR_SOURCES
  "${PROJECTEUR_CONTROL_XML}"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/projecteurcontrol.h"
  ProjecteurControl
  projecteurcontroladaptor
  ProjecteurControlAdaptor
)
target_sources(projecteur PRIVATE ${PROJECTEUR_DBUS_ADAPTOR_SOURCES})

kconfig_target_kcfg_file(projecteur
  FILE "${CMAKE_CURRENT_SOURCE_DIR}/src/projecteurconfig.kcfg"
  CLASS_NAME ProjecteurConfig
  MUTATORS
  DEFAULT_VALUE_GETTERS
  KCONFIG_CONSTRUCTOR
)

target_include_directories(projecteur PRIVATE src "${CMAKE_CURRENT_BINARY_DIR}")

target_link_libraries(projecteur
  PRIVATE Qt6::Core Qt6::DBus Qt6::Quick Qt6::WaylandClient Qt6::Widgets
          K::KPipeWire
          KF6::ConfigCore KF6::ConfigGui KF6::ConfigWidgets KF6::CoreAddons KF6::DBusAddons
          KF6::GlobalAccel KF6::I18n KF6::Notifications KF6::WidgetsAddons
          KF6::WindowSystem KF6::XmlGui
          LayerShellQt::Interface
)

target_compile_options(projecteur
  PRIVATE
    $<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>>:-Wall -Wextra>
)

target_compile_definitions(projecteur PRIVATE
  CXX_COMPILER_ID=${CMAKE_CXX_COMPILER_ID} CXX_COMPILER_VERSION=${CMAKE_CXX_COMPILER_VERSION})

add_subdirectory(plasma)

# Set version project properties for builds not from a git repository (e.g. created with git archive)
# If creating the version number via git information fails, the following target properties
# will be used. IMPORTANT - when creating a release tag with git flow:
#  Update this information - the version numbers and the version type.
#  VERSION_TYPE must be either 'release' or 'develop'
set_target_properties(projecteur PROPERTIES
  VERSION_MAJOR 1
  VERSION_MINOR 0
  VERSION_PATCH 0
  VERSION_TYPE develop
  VERSION_DISTANCE_OFFSET 200
)
add_version_info(projecteur "${CMAKE_CURRENT_SOURCE_DIR}")

# Create files containing generated version strings, helping package maintainers
get_target_property(PROJECTEUR_VERSION_STRING projecteur VERSION_STRING)
# Arch Linux = PKGBUILD/makepkg: '-' is not allowed in version number
string(REPLACE "-" "" PROJECTEUR_VERSION_STRING_ARCHLINUX "${PROJECTEUR_VERSION_STRING}")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/version-string" "${PROJECTEUR_VERSION_STRING}")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/version-string.archlinux" "${PROJECTEUR_VERSION_STRING_ARCHLINUX}")

# Install gettext catalogs supplied by KDE's translation infrastructure.
ki18n_install(po)

# Add target with non-source files for convenience when using IDEs like QtCreator and others
add_custom_target(non-sources SOURCES README.md CONTRIBUTING.md LICENSE.md
                                      doc/CHANGELOG.md doc/USER-GUIDE.md
                                      doc/TROUBLESHOOTING.md devices.conf
                                      src/extra-devices.cc.in src/projecteurconfig.kcfg
                                      src/org.projecteur.Projecteur.xml
                                      qml/shaders/textzoom.frag
                                      55-projecteur.rules.in projecteur.notifyrc
                                      cmake/templates/projecteur.desktop.in)

# Install
#---------------------------------------------------------------------------------------------------
# Set default directory permissions with CMake >= 3.11, avoids some
# lintian 'non-standard-dir-perm' errors for deb packages.
set(CMAKE_INSTALL_DEFAULT_DIRECTORY_PERMISSIONS
  OWNER_READ OWNER_WRITE OWNER_EXECUTE
  GROUP_READ GROUP_EXECUTE
  WORLD_READ WORLD_EXECUTE
)

install(TARGETS projecteur DESTINATION bin)
set(PROJECTEUR_INSTALL_PATH "${CMAKE_INSTALL_PREFIX}/bin/projecteur") #used in desktop file template

# Use udev.pc pkg-config file to set the dir path
if (NOT CMAKE_INSTALL_UDEVRULESDIR)
  set (UDEVDIR /lib/udev)
  find_package(PkgConfig)
  if(PKG_CONFIG_FOUND)
    pkg_check_modules(PKGCONFIG_UDEV udev)
    if(PKGCONFIG_UDEV_FOUND)
      execute_process(
        COMMAND ${PKG_CONFIG_EXECUTABLE} --variable=udevdir udev
        OUTPUT_VARIABLE PKGCONFIG_UDEVDIR
        OUTPUT_STRIP_TRAILING_WHITESPACE
      )
      if(PKGCONFIG_UDEVDIR)
        file(TO_CMAKE_PATH "${PKGCONFIG_UDEVDIR}" UDEVDIR)
      endif(PKGCONFIG_UDEVDIR)
    endif(PKGCONFIG_UDEV_FOUND)
  endif(PKG_CONFIG_FOUND)
endif(NOT CMAKE_INSTALL_UDEVRULESDIR)
set (CMAKE_INSTALL_UDEVDIR ${UDEVDIR} CACHE PATH "Udev base dir.")
mark_as_advanced(CMAKE_INSTALL_UDEVDIR)
set (CMAKE_INSTALL_UDEVRULESDIR ${UDEVDIR}/rules.d CACHE PATH "Where to install udev rules")
mark_as_advanced(CMAKE_INSTALL_UDEVRULESDIR)

# Configure and install files
set(OUTDIR "${CMAKE_CURRENT_BINARY_DIR}")
set(TMPLDIR "${CMAKE_CURRENT_SOURCE_DIR}/cmake/templates")

# Read devices.conf file
set(idRegex "0x([0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F])")
set(lineRegex "^[ \t]*${idRegex}[ \t]*,[ \t]*${idRegex}[ \t]*,[ \t]*(usb|bt)[ \t]*,[ \t]*(.*)[ \t]*")
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/devices.conf" CONFLINES REGEX "${lineRegex}")
foreach(line ${CONFLINES})
  #message(STATUS "## ${line}")
  if(line MATCHES "${lineRegex}")
    # message(STATUS "vendorId: ${CMAKE_MATCH_1}, productId: ${CMAKE_MATCH_2}, ${CMAKE_MATCH_3}, '${CMAKE_MATCH_4}'")
    set(vendorId "${CMAKE_MATCH_1}")
    set(productId "${CMAKE_MATCH_2}")

    if("${CMAKE_MATCH_3}" STREQUAL "usb")
      string(APPEND EXTRA_USB_UDEV_RULES "\n## Extra-Device: ${CMAKE_MATCH_4}")
      string(APPEND EXTRA_USB_UDEV_RULES "\nSUBSYSTEMS==\"usb\", ATTRS{idVendor}==\"${vendorId}\"")
      string(APPEND EXTRA_USB_UDEV_RULES ", ATTRS{idProduct}==\"${productId}\", MODE=\"0660\", TAG+=\"uaccess\"")
      string(APPEND SUPPORTED_EXTRA_DEVICES "\n    {0x${vendorId}, 0x${productId}, false, \"${CMAKE_MATCH_4}\"}, // ${CMAKE_MATCH_4}")
    elseif("${CMAKE_MATCH_3}" STREQUAL "bt")
      string(APPEND SUPPORTED_EXTRA_DEVICES "\n    {0x${vendorId}, 0x${productId}, true, \"${CMAKE_MATCH_4}\"}, // ${CMAKE_MATCH_4}")
      if("${vendorId}" MATCHES "0*([0-9a-fA-F]+)")
        set(vendorId "${CMAKE_MATCH_1}")
      endif()
      if("${productId}" MATCHES "0*([0-9a-fA-F]+)")
        set(productId "${CMAKE_MATCH_1}")
      endif()
      string(APPEND EXTRA_BLUETOOTH_UDEV_RULES "\n## Extra-Device: ${CMAKE_MATCH_4}")
      string(APPEND EXTRA_BLUETOOTH_UDEV_RULES "\nSUBSYSTEMS==\"input\", ")
      string(APPEND EXTRA_BLUETOOTH_UDEV_RULES "ENV{LIBINPUT_DEVICE_GROUP}=\"5/${vendorId}/${productId}*\", ")
      string(APPEND EXTRA_BLUETOOTH_UDEV_RULES "MODE=\"0660\", TAG+=\"uaccess\"")
    endif()
  endif()
endforeach()

configure_file("src/extra-devices.cc.in" "src/extra-devices.cc" @ONLY)
set_property(TARGET projecteur APPEND PROPERTY SOURCES "${CMAKE_CURRENT_BINARY_DIR}/src/extra-devices.cc")

configure_file("55-projecteur.rules.in" "55-projecteur.rules" @ONLY)
install(FILES "${OUTDIR}/55-projecteur.rules" DESTINATION ${CMAKE_INSTALL_UDEVRULESDIR}/)

install(FILES icons/projecteur-tray.svg DESTINATION share/icons/hicolor/48x48/apps/ RENAME projecteur.svg)
install(FILES icons/projecteur-tray.svg DESTINATION share/icons/hicolor/64x64/apps/ RENAME projecteur.svg)
install(FILES icons/projecteur-tray.svg DESTINATION share/icons/hicolor/128x128/apps/ RENAME projecteur.svg)
install(FILES icons/projecteur-tray.svg DESTINATION share/icons/hicolor/256x256/apps/ RENAME projecteur.svg)
install(FILES projecteur.notifyrc DESTINATION ${KDE_INSTALL_KNOTIFYRCDIR})
install(FILES src/org.projecteur.Projecteur.xml DESTINATION ${KDE_INSTALL_DBUSINTERFACEDIR})

# Set variables for file configurations
get_target_property(VERSION_STRING projecteur VERSION_STRING)
get_target_property(VERSION_DATE_MONTH_YEAR projecteur VERSION_DATE_MONTH_YEAR)
set(HOMEPAGE "https://github.com/gbin/Projecteur-kde")

set(PROJECTEUR_APPLICATION_ID "org.projecteur.Projecteur")
configure_file("${TMPLDIR}/projecteur.desktop.in"
               "${PROJECTEUR_APPLICATION_ID}.desktop" @ONLY)
install(FILES "${OUTDIR}/${PROJECTEUR_APPLICATION_ID}.desktop"
        DESTINATION share/applications/)
kdbusaddons_generate_dbus_service_file(
  projecteur
  "${PROJECTEUR_APPLICATION_ID}"
  "${KDE_INSTALL_FULL_BINDIR}"
)

# Configure man page and gzip it.
option(COMPRESS_MAN_PAGE "Compress the man page" ON)
configure_file("${TMPLDIR}/projecteur.1" "${OUTDIR}/projecteur.1" @ONLY)

if(COMPRESS_MAN_PAGE)
  find_program(GZIP_EXECUTABLE gzip)
  add_custom_command(
    OUTPUT ${OUTDIR}/projecteur.1.gz
    COMMAND ${GZIP_EXECUTABLE} -9f -n "${OUTDIR}/projecteur.1"
    WORKING_DIRECTORY ${OUTDIR}
  )
  add_custom_target(gzip-manpage ALL DEPENDS "${OUTDIR}/projecteur.1.gz")
  install(FILES "${OUTDIR}/projecteur.1.gz" DESTINATION share/man/man1/)
else()
  install(FILES "${OUTDIR}/projecteur.1" DESTINATION share/man/man1/)
endif()

configure_file("${TMPLDIR}/projecteur.metainfo.xml"
               "${PROJECTEUR_APPLICATION_ID}.metainfo.xml" @ONLY)
install(FILES "${OUTDIR}/${PROJECTEUR_APPLICATION_ID}.metainfo.xml"
        DESTINATION share/metainfo/)

configure_file("${TMPLDIR}/projecteur.bash-completion" "projecteur.bash-completion" @ONLY)
install(FILES "${OUTDIR}/projecteur.bash-completion"
        DESTINATION share/bash-completion/completions/
        RENAME projecteur)

configure_file("${TMPLDIR}/preinst.in" "pkg/scripts/preinst" @ONLY)
configure_file("${TMPLDIR}/postinst.in" "pkg/scripts/postinst" @ONLY)

# --- Linux packaging ---
include(LinuxPackaging)

option(PACKAGE_TARGETS "Create packaging build targets" ON)

if(PACKAGE_TARGETS)
  # Add 'source-archive' target
  add_source_archive_target(projecteur)

  # Add 'dist-package' target: Creates a deb/rpm/tgz package depending on the current Linux distribution
  add_dist_package_target(
    PROJECT "${CMAKE_PROJECT_NAME}"
    TARGET projecteur
    DESCRIPTION_BRIEF "Wayland application for the Logitech Spotlight device."
    DESCRIPTION_FULL
"Projecteur is a virtual laser pointer for use with inertial pointers such as
 the Logitech Spotlight. Projecteur can show a colored dot, a highlighted
 circle or a zoom effect to act as a pointer. The location of the pointer
 moves in response to moving the handheld pointer device. The effect is much
 like that of a traditional laser pointer, except that it is captured by
 recording software and works across multiple screens."
    CONTACT "Jahn Fuchs <projecteur@jahn.textmo.de>"
    HOMEPAGE "${HOMEPAGE}"
    DEBIAN_SECTION "utils"
    # PREINST_SCRIPT "${OUTDIR}/pkg/scripts/preinst"
    POSTINST_SCRIPT "${OUTDIR}/pkg/scripts/postinst"
  )
  add_dependencies(dist-package projecteur)
  if(TARGET gzip-manpage)
    add_dependencies(dist-package gzip-manpage)
  endif()

  # Additional files for debian packages, adhering to some debian rules,
  # see https://manpages.debian.org/buster/lintian/lintian.1.en.html
  if ("${PKG_TYPE}" STREQUAL "DEB")
    # TODO Lintian expects the the copyright file in /usr/share/doc/projecteur
    #      This clashes with the default CMAKE_INSTALL_PREFIX /usr/local
    #      Need to check if this would clash with packages from the debian/ubuntu repos.
    #configure_file("${TMPLDIR}/copyright.in" "pkg/copyright" @ONLY)
    #install(FILES "${OUTDIR}/pkg/copyright" DESTINATION /usr/share/doc/projecteur/)

    ## -- prevent additional lintian warnings 'non-standard-dir-perm' for the cmake install prefix
    set(DIR_TO_INSTALL "${CMAKE_INSTALL_PREFIX}")
    while(NOT "${DIR_TO_INSTALL}" STREQUAL "/" AND NOT "${DIR_TO_INSTALL}" STREQUAL "")
      install(DIRECTORY DESTINATION "${DIR_TO_INSTALL}")
      get_filename_component(DIR_TO_INSTALL "${DIR_TO_INSTALL}" PATH)
    endwhile()
  endif()
endif()

option(ENABLE_IWYU "Enable Include-What-You-Use" OFF)
find_program(iwyu_path NAMES include-what-you-use iwyu)
if(ENABLE_IWYU AND iwyu_path)
  set_property(TARGET projecteur PROPERTY CXX_INCLUDE_WHAT_YOU_USE ${iwyu_path})
endif()
