list(APPEND _PkgDeps_Projecteur_archlinux
  "qt6-base>=6.10"
  "qt6-declarative>=6.10"
  "qt6-wayland>=6.10"
  "layer-shell-qt>=6.7"
  "kconfig>=6.7"
  "kconfigwidgets>=6.7"
  "kcoreaddons>=6.7"
  "kdbusaddons>=6.7"
  "kglobalaccel>=6.7"
  "ki18n>=6.7"
  "kpipewire>=6.7"
  "knotifications>=6.7"
  "kwidgetsaddons>=6.7"
  "kwindowsystem>=6.7"
  "kxmlgui>=6.7"
  "libplasma>=6.7"
  "udev"
)

list(APPEND _PkgDepsMake_Projecteur_archlinux
  "fakeroot" "awk" "rust>=1.85" "cmake>=3.20" "extra-cmake-modules>=6.7" "gettext" "make" "lsb-release"
  "tar" "pkg-config" "qt6-shadertools>=6.10"
)

set(_PkgDeps_Projecteur_debian
  "udev" "qml6-module-org-kde-layershell" "qml6-module-org-kde-pipewire")
set(_PkgDeps_Projecteur_ubuntu
  "udev" "qml6-module-org-kde-layershell" "qml6-module-org-kde-pipewire")
set(_PkgDeps_Projecteur_fedora "systemd-udev" "kpipewire" "layer-shell-qt")
set(_PkgDeps_Projecteur_opensuse "udev" "kpipewire6" "layer-shell-qt6")

list(APPEND PkgDependencies_MAP_Projecteur
  "archlinux::_PkgDeps_Projecteur_archlinux"
  "arch::_PkgDeps_Projecteur_archlinux"
  "debian::_PkgDeps_Projecteur_debian"
  "ubuntu::_PkgDeps_Projecteur_ubuntu"
  "fedora::_PkgDeps_Projecteur_fedora"
  "opensuse::_PkgDeps_Projecteur_opensuse"
  "opensuse-tumbleweed::_PkgDeps_Projecteur_opensuse"
)

list(APPEND PkgDependenciesMake_MAP_Projecteur
  "archlinux::_PkgDepsMake_Projecteur_archlinux"
  "arch::_PkgDepsMake_Projecteur_archlinux"
)
