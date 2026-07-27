list(APPEND _PkgDeps_Projecteur_archlinux
  "qt6-base>=6.11"
  "qt6-declarative>=6.11"
  "layer-shell-qt>=6.7"
  "kconfig>=6.7"
  "kconfigwidgets>=6.7"
  "kcoreaddons>=6.7"
  "kdbusaddons>=6.7"
  "knotifications>=6.7"
  "kwindowsystem>=6.7"
  "kxmlgui>=6.7"
  "libplasma>=6.7"
  "udev"
)

list(APPEND _PkgDepsMake_Projecteur_archlinux
  "fakeroot" "awk" "cmake>=3.20" "extra-cmake-modules>=6.7" "make" "lsb-release" "tar"
  "pkg-config" "qt6-tools>=6.11"
)

list(APPEND PkgDependencies_MAP_Projecteur
  "archlinux::_PkgDeps_Projecteur_archlinux"
  "arch::_PkgDeps_Projecteur_archlinux"
)

list(APPEND PkgDependenciesMake_MAP_Projecteur
  "archlinux::_PkgDepsMake_Projecteur_archlinux"
  "arch::_PkgDepsMake_Projecteur_archlinux"
)
