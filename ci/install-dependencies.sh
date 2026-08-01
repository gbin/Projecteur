#!/usr/bin/env bash
set -euo pipefail

target="${1:?usage: install-dependencies.sh TARGET}"

case "$target" in
  archlinux)
    pacman -Syu --noconfirm --needed \
      base-devel cmake extra-cmake-modules gettext git just pacman-contrib \
      kconfig kconfigwidgets kcoreaddons kdbusaddons kglobalaccel ki18n \
      knotifications kpipewire kwidgetsaddons kwindowsystem kxmlgui \
      layer-shell-qt libplasma libglvnd qt6-base qt6-declarative \
      qt6-shadertools qt6-wayland
    ;;
  fedora-44|fedora-rawhide)
    dnf -y -q install \
      gcc-c++ cmake extra-cmake-modules gettext git pkgconf-pkg-config \
      systemd-devel rpm-build qt6-qtbase-devel qt6-qtdeclarative-devel \
      qt6-qtshadertools-devel qt6-qtwayland-devel kf6-kconfig-devel \
      kf6-kconfigwidgets-devel kf6-kcoreaddons-devel kf6-kdbusaddons-devel \
      kf6-kglobalaccel-devel kf6-ki18n-devel kf6-knotifications-devel \
      kf6-kpackage-devel kf6-kirigami-devel kf6-kwidgetsaddons-devel \
      kf6-kwindowsystem-devel kf6-kxmlgui-devel kpipewire-devel \
      libplasma-devel layer-shell-qt-devel
    ;;
  tumbleweed)
    zypper -qn refresh
    zypper -qn install -y \
      gcc-c++ cmake extra-cmake-modules gettext-tools git-core \
      pkgconf-pkg-config libudev-devel rpm-build qt6-base-devel \
      qt6-declarative-devel qt6-shadertools-devel qt6-wayland-devel \
      kf6-kconfig-devel kf6-kconfigwidgets-devel kf6-kcoreaddons-devel \
      kf6-kdbusaddons-devel kf6-kglobalaccel-devel kf6-ki18n-devel \
      kf6-knotifications-devel kf6-kpackage-devel kf6-kirigami-devel \
      kf6-kwidgetsaddons-devel kf6-kwindowsystem-devel kf6-kxmlgui-devel \
      kpipewire6-devel libplasma6-devel layer-shell-qt6-devel
    ;;
  debian-testing|debian-sid|ubuntu-26.10)
    export DEBIAN_FRONTEND=noninteractive
    apt-get update -qq
    apt-get install -y -qq \
      build-essential cmake dpkg-dev extra-cmake-modules file gettext git \
      pkg-config udev libudev-dev qt6-base-dev qt6-declarative-dev \
      qt6-shadertools-dev qt6-wayland-dev libkf6config-dev \
      libkf6configwidgets-dev libkf6coreaddons-dev libkf6dbusaddons-dev \
      libkf6globalaccel-dev libkf6i18n-dev libkf6notifications-dev \
      libkf6package-dev libkirigami-dev libkf6widgetsaddons-dev \
      libkf6windowsystem-dev libkf6xmlgui-dev libkpipewire-dev \
      libplasma-dev liblayershellqtinterface-dev
    ;;
  *)
    echo "error: unsupported CI target: $target" >&2
    exit 2
    ;;
esac

case "$target" in
  debian-testing)
    . /etc/os-release
    if [[ "${VERSION_CODENAME:-}" != "forky" ]]; then
      echo "error: debian-testing no longer identifies as forky" >&2
      exit 1
    fi
    ;;
  ubuntu-26.10)
    . /etc/os-release
    if [[ "${VERSION_ID:-}" != "26.10" ]]; then
      echo "error: ubuntu:devel is no longer Ubuntu 26.10" >&2
      exit 1
    fi
    ;;
esac
