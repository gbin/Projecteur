if(NOT CMAKE_LIBRARY_OUTPUT_DIRECTORY)
  set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
endif()

qt_add_dbus_interface(PROJECTEUR_DBUS_INTERFACE_SOURCES
  "${CMAKE_CURRENT_SOURCE_DIR}/org.projecteur.Projecteur.xml"
  projecteurcontrolinterface
)

plasma_add_applet(org.projecteur.Projecteur
  QML_SOURCES
    qml/CompactRepresentation.qml
    qml/main.qml
    qml/FullRepresentation.qml
  CPP_SOURCES
    projecteurapplet.cc
    projecteurapplet.h
    ${PROJECTEUR_DBUS_INTERFACE_SOURCES}
)

target_include_directories(org.projecteur.Projecteur
  PRIVATE "${CMAKE_CURRENT_BINARY_DIR}"
)

target_link_libraries(org.projecteur.Projecteur
  PRIVATE
    Qt6::DBus
    KF6::CoreAddons
    Plasma::Plasma
)
