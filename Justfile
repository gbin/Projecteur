set shell := ["bash", "-euo", "pipefail", "-c"]

project_root := justfile_directory()
build_dir := project_root / "build"
arch_dir := build_dir / "arch-package"
package_dir := build_dir / "packages"

# Show the available developer commands.
default:
    @just --list

# Compile Projecteur.
build: _require-arch deps
    cmake -S "{{ project_root }}" -B "{{ build_dir }}" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX=/usr \
        -DPACKAGE_TARGETS=OFF
    cmake --build "{{ build_dir }}" --parallel

# Build an Arch Linux package from the current working tree.
package: _require-arch deps
    #!/usr/bin/env bash
    set -euo pipefail

    if (( EUID == 0 )); then
        echo "error: makepkg must be run as a regular user, not root." >&2
        exit 1
    fi

    root="{{ project_root }}"
    stage="{{ arch_dir }}"
    packages="{{ package_dir }}"

    mkdir -p "$stage" "$packages"
    install -m 0644 "$root/packaging/arch/PKGBUILD" "$stage/PKGBUILD"

    cmake -S "$root" -B "$stage/version-build" -DPACKAGE_TARGETS=OFF
    cp "$stage/version-build/version-string.archlinux" "$stage/projecteur-pkgver"

    git -C "$root" ls-files --cached --others --exclude-standard -z \
        | while IFS= read -r -d '' path; do
            if [[ -e "$root/$path" || -L "$root/$path" ]]; then
                printf '%s\0' "$path"
            fi
        done \
        | tar -C "$root" --null --no-recursion --files-from=- \
            --transform='s,^,projecteur-local/,' \
            -czf "$stage/projecteur-local.tar.gz"

    (
        cd "$stage"
        updpkgsums
        BUILDDIR="$stage/work" \
        PKGDEST="$packages" \
        SRCDEST="$stage/sources" \
            makepkg --cleanbuild --clean --force --noconfirm --syncdeps
    )

    echo
    echo "Package created:"
    (
        cd "$stage"
        PKGDEST="$packages" makepkg --packagelist
    )

# Build, package, and install Projecteur through pacman.
install: _stop-projecteur build package
    #!/usr/bin/env bash
    set -euo pipefail

    stage="{{ arch_dir }}"
    packages="{{ package_dir }}"

    package_file="$(
        cd "$stage"
        PKGDEST="$packages" makepkg --packagelist | head -n 1
    )"

    if [[ ! -f "$package_file" ]]; then
        echo "error: expected package was not created: $package_file" >&2
        exit 1
    fi

    if (( EUID != 0 )); then
        if ! command -v sudo >/dev/null 2>&1; then
            echo "error: installing the package requires root or sudo." >&2
            exit 1
        fi
        # Authenticate before installing the package.
        sudo -v
    fi

    if (( EUID == 0 )); then
        pacman -U --noconfirm "$package_file"
    else
        sudo pacman -U --noconfirm "$package_file"
    fi

    systemctl --user restart plasma-plasmashell.service

_stop-projecteur:
    #!/usr/bin/env bash
    if [[ -x /usr/bin/projecteur ]]; then
        /usr/bin/projecteur --command quit >/dev/null 2>&1 || true
    fi

# Install the compiler and Projecteur build dependencies.
deps: _require-arch
    #!/usr/bin/env bash
    set -euo pipefail

    dependencies=(
        base-devel
        cmake
        extra-cmake-modules
        git
        kcoreaddons
        layer-shell-qt
        libplasma
        libglvnd
        pacman-contrib
        qt6-base
        qt6-declarative
        qt6-tools
    )

    mapfile -t missing < <(pacman -T "${dependencies[@]}" || true)
    if (( ${#missing[@]} == 0 )); then
        echo "Arch build dependencies are already installed."
        exit 0
    fi

    echo "Installing missing Arch build dependencies: ${missing[*]}"
    if (( EUID == 0 )); then
        pacman -S --needed --noconfirm "${missing[@]}"
    elif command -v sudo >/dev/null 2>&1; then
        sudo pacman -S --needed --noconfirm "${missing[@]}"
    else
        echo "error: installing build dependencies requires root or sudo." >&2
        exit 1
    fi

_require-arch:
    #!/usr/bin/env bash
    set -euo pipefail

    if [[ ! -r /etc/os-release ]]; then
        echo "error: /etc/os-release is unavailable; this workflow requires Arch Linux." >&2
        exit 1
    fi

    # shellcheck disable=SC1091
    source /etc/os-release
    distro_ids=" ${ID:-} ${ID_LIKE:-} "
    if [[ "$distro_ids" != *" arch "* ]]; then
        echo "error: this workflow requires Arch Linux or an Arch-based distribution." >&2
        echo "       detected: ${PRETTY_NAME:-unknown Linux distribution}" >&2
        exit 1
    fi

    if ! command -v pacman >/dev/null 2>&1; then
        echo "error: pacman was not found; this does not look like a usable Arch system." >&2
        exit 1
    fi
