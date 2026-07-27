if(NOT CMAKE_LIBRARY_OUTPUT_DIRECTORY)
  set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
endif()

plasma_add_applet(org.projecteur.Projecteur
  QML_SOURCES
    qml/main.qml
    qml/FullRepresentation.qml
  CPP_SOURCES
    projecteurapplet.cc
    projecteurapplet.h
)

target_link_libraries(org.projecteur.Projecteur
  PRIVATE
    Qt6::DBus
    KF6::CoreAddons
    Plasma::Plasma
)
