# Projecteur packages

Official Projecteur release files are published exclusively through
[GitHub Releases](https://github.com/gbin/Projecteur/releases). Each release
contains checksums and GitHub-hosted provenance information.

## Upstream release assets

Projecteur currently builds x86-64 packages for:

- Arch Linux and Arch-based distributions (`.pkg.tar.zst`)
- Fedora 44 (`.rpm`)
- openSUSE Tumbleweed (`.rpm`)
- Debian testing (`.deb`)
- Ubuntu 26.10 (`.deb`)

These files are standalone release downloads, not package repositories. Your
package manager can install a downloaded file and resolve its dependencies from
the distribution's normal repositories.

Fedora Rawhide and Debian sid are continuous compatibility checks. They do not
produce additional release downloads because their packages would duplicate a
supported target while becoming stale quickly.

## Distribution repositories

Some distributions independently package Projecteur. Their versions and
support schedules are controlled by the respective maintainers:

- [Debian](https://packages.debian.org/search?keywords=projecteur&searchon=names&suite=all&section=all)
- [Ubuntu](https://packages.ubuntu.com/search?keywords=projecteur&searchon=names)
- [Gentoo](https://packages.gentoo.org/packages/x11-misc/projecteur)
- [Arch User Repository](https://aur.archlinux.org/packages?K=projecteur)
- [openSUSE Software](https://software.opensuse.org/search?baseproject=ALL&q=projecteur)

Check that a downstream package is the Plasma 6 / Wayland edition before
installing it. The older Qt 5 edition remains available from the
[`legacy/qt5`](https://github.com/gbin/Projecteur/tree/legacy/qt5) branch.
