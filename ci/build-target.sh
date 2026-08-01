#!/usr/bin/env bash
set -euo pipefail

target="${1:?usage: build-target.sh TARGET}"
build_dir="build/ci-${target}"
# Package managers require an absolute path for locally built packages.
asset_dir="$(pwd)/release-assets/${target}"

# Container jobs run as root while actions/checkout creates the worktree as the
# runner user. Trust this exact checkout so versioning and source archives can
# inspect Git history in subsequent container steps.
git config --global --add safe.directory "$(pwd)"

case "$target" in
  fedora-rawhide|debian-sid)
    package_target=0
    ;;
  *)
    package_target=1
    ;;
esac

package_targets=OFF
if (( package_target )); then
  package_targets=ON
fi
if [[ "$target" == "archlinux" ]]; then
  package_targets=OFF
fi

cmake -S . -B "$build_dir" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX=/usr \
  -DCMAKE_INSTALL_UDEVRULESDIR=/usr/lib/udev/rules.d \
  -DPACKAGE_TARGETS="$package_targets"
cmake --build "$build_dir" --parallel 2
"$build_dir/projecteur" --version
"$build_dir/projecteur" --help >/dev/null

if (( ! package_target )); then
  exit 0
fi

mkdir -p "$asset_dir"

if [[ "$target" == "archlinux" ]]; then
  package_output_dir=build/packages
  if (( EUID == 0 )); then
    # GitHub container jobs run as root, while makepkg deliberately refuses to.
    build_user=projecteur-ci
    if ! id "$build_user" >/dev/null 2>&1; then
      useradd --system --create-home --user-group "$build_user"
    fi
    build_group="$(id -gn "$build_user")"
    rootless_stage="$(pwd)/build/ci-arch-package"
    package_output_dir="$(pwd)/build/ci-arch-packages"
    install -d -o "$build_user" -g "$build_group" "$rootless_stage" "$package_output_dir"
    runuser -u "$build_user" -- env \
      HOME="$(getent passwd "$build_user" | cut -d: -f6)" \
      GIT_CONFIG_COUNT=1 \
      GIT_CONFIG_KEY_0=safe.directory \
      GIT_CONFIG_VALUE_0="$(pwd)" \
      just arch_dir="$rootless_stage" package_dir="$package_output_dir" package
  else
    just package
  fi
  cp "$package_output_dir"/projecteur-[0-9]*.pkg.tar.zst "$asset_dir/"
else
  cmake --build "$build_dir" --target dist-package
  cp "$build_dir"/dist-pkg/* "$asset_dir/"
  if [[ "$target" == "fedora-44" ]]; then
    cmake --build "$build_dir" --target source-archive
    cp "$build_dir"/dist-pkg/*_source.tar.gz "$asset_dir/"
  fi
fi

case "$target" in
  archlinux)
    pacman -U --noconfirm "$asset_dir"/projecteur-[0-9]*.pkg.tar.zst
    ;;
  fedora-44)
    dnf -y -q install "$asset_dir"/*.rpm
    ;;
  tumbleweed)
    zypper -qn --no-gpg-checks install -y "$asset_dir"/*.rpm
    ;;
  debian-testing|ubuntu-26.10)
    DEBIAN_FRONTEND=noninteractive apt-get install -y -qq "$asset_dir"/*.deb
    ;;
esac

/usr/bin/projecteur --version
find "$asset_dir" -maxdepth 1 -type f -printf '%f\n' | sort
