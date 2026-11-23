# Linux Packaging for mde

`mde` supports native source/binary packaging recipes for major Linux distributions as well as a portable release tarball for any Linux system.

## Packaging Recipes Overview

| Distribution Family | Package Format | Recipe Location | Build Command / Make Target |
|---|---|---|---|
| **Debian / Ubuntu / Mint / Pop!_OS** | `.deb` (direct) | `scripts/build_packages.sh` | `make deb` |
| **Debian / Ubuntu (source pkg)** | `.dsc` / `.deb` | `packaging/debian/` | `make deb-src` (or `dpkg-buildpackage -us -uc`) |
| **RHEL / Fedora / Rocky / Alma / openSUSE / SLES** | `.rpm` | `packaging/rpm/mde.spec` | `make rpm` (or `rpmbuild -ta mde-<ver>.tar.gz`) |
| **Arch Linux / Manjaro / EndeavourOS** | `.pkg.tar.zst` | `packaging/arch/PKGBUILD` | `make arch` (or `cd packaging/arch && makepkg -si`) |
| **Alpine Linux / postmarketOS** | `.apk` | `packaging/alpine/APKBUILD` | `make alpine` (or `abuild -r`) |
| **Void Linux** | `.xbps` | `packaging/void/template` | `./xbps-src pkg mde` |
| **Gentoo Linux** | Portage ebuild | `packaging/gentoo/mde-<ver>.ebuild` | `ebuild mde-<ver>.ebuild merge` |
| **NixOS / Nix** | Nix derivation | `packaging/nix/default.nix` | `nix-build -E 'with import <nixpkgs> {}; callPackage ./packaging/nix/default.nix {}'` |
| **Snap (Ubuntu / cross-distro)** | `.snap` | `packaging/snap/snapcraft.yaml` | `snapcraft` (from `packaging/snap/` or repo root) |
| **Generic Linux (any distro)** | `.tar.gz` | `scripts/build_packages.sh` | `make pkg-tarball` |

---

## Per-Distribution Instructions

### 1. Debian & Ubuntu (`.deb`)

Quick binary package on any Debian or Ubuntu system (requires `dpkg-deb`; uses `dpkg-shlibdeps` when installed to resolve exact `libzip4`/`libzip4t64`/`libzip5` and `libncursesw6` runtime dependencies):

```bash
./configure
make
make deb
sudo apt install ./bin/mde_*.deb
```

Full Debian/Ubuntu source + binary build (requires `debhelper` and `dpkg-dev`):

```bash
./configure
make deb-src
```

Or using clean containerized Debian/Ubuntu environments:

```bash
# Ubuntu 24.04
docker run --rm -v "$PWD:/src" -w /src ubuntu:24.04 sh -c \
  'apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y gcc make libncurses-dev libzip-dev pkg-config dpkg-dev && ./configure --disable-werror && make deb'

# Debian 12
docker run --rm -v "$PWD:/src" -w /src debian:bookworm sh -c \
  'apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y gcc make libncurses-dev libzip-dev pkg-config dpkg-dev && ./configure --disable-werror && make deb'
```

### 2. RHEL / Fedora / Rocky / AlmaLinux / CentOS Stream / openSUSE (`.rpm`)

Directly from a distribution tarball (works on RHEL 8/9/10, Fedora, Rocky, Alma, and openSUSE):

```bash
./configure
make dist
rpmbuild -ta mde-*.tar.gz
```

Or via Make:

```bash
./configure
make rpm
```

Or via container (e.g. Fedora / Rocky Linux 9 / openSUSE Tumbleweed):

```bash
# Fedora
docker run --rm -v "$PWD:/src" -w /src fedora:latest sh -c \
  'dnf install -y gcc make ncurses-devel libzip-devel pkgconf-pkg-config rpm-build && ./configure --disable-werror && make rpm'

# Rocky Linux 9 / RHEL 9 (enable CRB/EPEL if needed for libzip-devel)
docker run --rm -v "$PWD:/src" -w /src rockylinux:9 sh -c \
  'dnf install -y epel-release && dnf config-manager --set-enabled crb && dnf install -y gcc make ncurses-devel libzip-devel pkgconf-pkg-config rpm-build && ./configure --disable-werror && make rpm'
```

### 3. Arch Linux / Manjaro / EndeavourOS (`.pkg.tar.zst`)

```bash
sudo pacman -S --needed base-devel ncurses libzip
./configure
make arch
# Or directly with makepkg:
make dist && cp mde-*.tar.gz packaging/arch/ && (cd packaging/arch && makepkg -si)
```

### 4. Alpine Linux (`.apk`)

```bash
apk add build-base ncurses-dev libzip-dev alpine-sdk
./configure
make alpine
```

### 5. Void Linux, Gentoo, NixOS, Snap & Portable Tarball

- **Void Linux**: Copy `packaging/void/template` to `void-packages/srcpkgs/mde/template` and run `./xbps-src pkg mde`.
- **Gentoo**: Copy `packaging/gentoo/mde-<version>.ebuild` to your local overlay under `app-editors/mde/` and run `ebuild ... manifest clean merge`.
- **Nix / NixOS**: `nix-build -E 'with import <nixpkgs> {}; callPackage ./packaging/nix/default.nix {}'`.
- **Generic Linux Binary Archive**: `make pkg-tarball` creates `bin/mde-<version>-linux-<arch>.tar.gz` (with `.sha256` checksum and bundled `install.sh` / `uninstall.sh` for `--prefix` installation on any Linux system).

## Verification

Run offline recipe & package checks (verifies `.deb`, portable tarball, version synchronization across all packaging manifests, shell syntax, and simulated Debian/RPM/Arch/Alpine `./configure` workflows):

```bash
make check-packaging
```
