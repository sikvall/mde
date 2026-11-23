#!/usr/bin/env bash
set -euo pipefail

# build_packages.sh - Build native Linux packages and portable archives for mde.

SRCDIR="${SRCDIR:-.}"
BUILDDIR="${BUILDDIR:-.}"
BIN_DIR="${BIN_DIR:-$BUILDDIR/bin}"
STRIP_BIN="${MDE_STRIP:-strip}"
GZIP_BIN="${MDE_GZIP:-gzip}"

TARGET="all"
SOURCE_ONLY=false

usage() {
    cat <<'EOF'
Usage: build_packages.sh [OPTIONS]

Build distribution packages and portable archives for mde.

Options:
  -t, --target TARGET   Target to build: deb, deb-src, rpm, arch, alpine, tarball, all
                        (default: all)
      --source-only     For deb-src, pass -S to dpkg-buildpackage
  -h, --help            Show this help message and exit
EOF
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        -t|--target)
            [ "$#" -ge 2 ] || { echo "Error: --target requires an argument" >&2; exit 1; }
            TARGET="$2"
            shift 2
            ;;
        --target=*)
            TARGET="${1#*=}"
            shift
            ;;
        --source-only)
            SOURCE_ONLY=true
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "Error: Unknown option '$1'" >&2
            usage >&2
            exit 1
            ;;
    esac
done

abs_path() {
    local p="$1"
    if [ -d "$p" ]; then
        (cd "$p" && pwd)
    else
        local dir base
        dir=$(dirname "$p")
        base=$(basename "$p")
        printf '%s/%s\n' "$(cd "$dir" && pwd)" "$base"
    fi
}

SRCDIR_ABS=$(abs_path "$SRCDIR")
BUILDDIR_ABS=$(abs_path "$BUILDDIR")
mkdir -p "$BIN_DIR"
BIN_DIR_ABS=$(abs_path "$BIN_DIR")

VERSION="${VERSION:-$(awk -F'"' '/#define[[:space:]]+MDE_VERSION_STRING/{print $2; exit}' "$SRCDIR_ABS/src/version.h")}"
: "${VERSION:=0.1.3}"

resolve_deb_arch() {
    local arch_raw
    arch_raw=$(dpkg --print-architecture 2>/dev/null || uname -m)
    case "$arch_raw" in
        x86_64)  printf 'amd64\n' ;;
        aarch64) printf 'arm64\n' ;;
        armv7l)  printf 'armhf\n' ;;
        *)       printf '%s\n' "$arch_raw" ;;
    esac
}

resolve_deb_depends() {
    local mde_bin="$1"
    local md2epub_bin="$2"
    local work_dir
    work_dir=$(mktemp -d "${TMPDIR:-/tmp}/mde_shlib.XXXXXX")

    if command -v dpkg-shlibdeps >/dev/null 2>&1; then
        mkdir -p "$work_dir/debian"
        cp "$SRCDIR_ABS/packaging/debian/control" "$work_dir/debian/control"
        local shlib_out
        if shlib_out=$(cd "$work_dir" && LC_ALL=C dpkg-shlibdeps -O "$mde_bin" "$md2epub_bin" 2>/dev/null); then
            local deps
            deps=$(printf '%s\n' "$shlib_out" | sed -n 's/^shlibs:Depends=//p' | head -n 1)
            if [ -n "$deps" ]; then
                rm -rf "$work_dir"
                printf '%s\n' "$deps"
                return 0
            fi
        fi
    fi

    rm -rf "$work_dir"
    # Fallback when dpkg-shlibdeps is unavailable: match host packages if possible
    if command -v ldd >/dev/null 2>&1 && command -v dpkg >/dev/null 2>&1; then
        local pkg_list="" soname pkg
        for bin in "$mde_bin" "$md2epub_bin"; do
            while read -r soname; do
                [ -n "$soname" ] || continue
                pkg=$(dpkg -S "*${soname}" 2>/dev/null | awk -F: '{print $1; exit}' || true)
                if [ -n "$pkg" ]; then
                    case ",$pkg_list," in
                        *",$pkg,"*) ;;
                        *) pkg_list="${pkg_list:+$pkg_list, }$pkg" ;;
                    esac
                fi
            done < <(ldd "$bin" 2>/dev/null | awk '/=>/ {print $1}' | xargs -r -n1 basename)
        done
        if [ -n "$pkg_list" ]; then
            printf '%s\n' "$pkg_list"
            return 0
        fi
    fi

    printf 'libc6, libncursesw6 (>= 6) | libncursesw5, libzip5 | libzip4t64 | libzip4\n'
}

strip_if_available() {
    local bin_path="$1"
    if [ -n "$STRIP_BIN" ] && [ "$STRIP_BIN" != "true" ] && command -v "$STRIP_BIN" >/dev/null 2>&1; then
        "$STRIP_BIN" "$bin_path"
    fi
}

ensure_binaries_built() {
    if [ ! -x "$BIN_DIR_ABS/mde" ] || [ ! -x "$BIN_DIR_ABS/md2epub" ]; then
        make -C "$BUILDDIR_ABS" all
    fi
}

ensure_dist_tarball() {
    if [ ! -f "$BUILDDIR_ABS/mde-${VERSION}.tar.gz" ]; then
        make -C "$BUILDDIR_ABS" dist
    fi
}

write_sha256() {
    local file_path="$1"
    if command -v sha256sum >/dev/null 2>&1; then
        (cd "$(dirname "$file_path")" && sha256sum "$(basename "$file_path")" > "$(basename "$file_path").sha256")
    fi
}

build_deb() {
    if ! command -v dpkg-deb >/dev/null 2>&1; then
        echo "Error: dpkg-deb not found (install dpkg)" >&2
        return 1
    fi

    ensure_binaries_built

    local deb_arch deb_pkg_name staging out_deb deps installed_kb
    deb_arch=$(resolve_deb_arch)
    deb_pkg_name="mde_${VERSION}_${deb_arch}"
    staging="$BIN_DIR_ABS/${deb_pkg_name}.staging"
    out_deb="$BIN_DIR_ABS/${deb_pkg_name}.deb"

    echo "==> Packaging Debian/Ubuntu binary package ${deb_pkg_name}.deb..."
    rm -rf "$staging" "$out_deb"
    mkdir -p "$staging/DEBIAN" \
             "$staging/usr/bin" \
             "$staging/usr/share/man/man1" \
             "$staging/usr/share/doc/mde"

    install -m 755 "$BIN_DIR_ABS/mde" "$staging/usr/bin/mde"
    install -m 755 "$BIN_DIR_ABS/md2epub" "$staging/usr/bin/md2epub"
    strip_if_available "$staging/usr/bin/mde"
    strip_if_available "$staging/usr/bin/md2epub"

    "$GZIP_BIN" -9 -n -c "$SRCDIR_ABS/man/mde.1" > "$staging/usr/share/man/man1/mde.1.gz"
    "$GZIP_BIN" -9 -n -c "$SRCDIR_ABS/man/md2epub.1" > "$staging/usr/share/man/man1/md2epub.1.gz"
    chmod 644 "$staging/usr/share/man/man1/mde.1.gz" "$staging/usr/share/man/man1/md2epub.1.gz"

    install -m 644 "$SRCDIR_ABS/README.md" "$staging/usr/share/doc/mde/README.md"
    install -m 644 "$SRCDIR_ABS/CHANGELOG.md" "$staging/usr/share/doc/mde/changelog.md"
    install -m 644 "$SRCDIR_ABS/LICENSE.md" "$staging/usr/share/doc/mde/LICENSE.md"
    cp "$SRCDIR_ABS/packaging/debian/copyright" "$staging/usr/share/doc/mde/copyright"
    chmod 644 "$staging/usr/share/doc/mde/copyright"

    deps=$(resolve_deb_depends "$staging/usr/bin/mde" "$staging/usr/bin/md2epub")
    installed_kb=$(du -sk "$staging/usr" | awk '{print $1}')

    cat > "$staging/DEBIAN/control" <<EOF
Package: mde
Version: ${VERSION}
Section: editors
Priority: optional
Architecture: ${deb_arch}
Installed-Size: ${installed_kb}
Maintainer: Täpp Anders Sikvall <anders@sikvall.se>
Homepage: https://github.com/sikvall/mde
Depends: ${deps}
Suggests: pandoc
Description: Full-screen text-mode Markdown editor with native EPUB exporter
 mde is a specialized terminal text editor written in pure C11 and ncursesw
 for editing and viewing Markdown (.md) documents with dual-pane live preview,
 in-place rich syntax styling, fullscreen rendered editing, column ruler,
 task checkboxes, UTF-8 support, hypertext file linking, and native EPUB 2/3
 e-book export alongside the standalone md2epub command-line tool.
EOF

    (cd "$staging" && find usr -type f | LC_ALL=C sort | xargs md5sum > DEBIAN/md5sums)

    find "$staging" -type d -exec chmod 755 {} +
    find "$staging" -type f -exec chmod 644 {} +
    chmod 755 "$staging/usr/bin/mde" "$staging/usr/bin/md2epub"

    dpkg-deb --build --root-owner-group "$staging" "$out_deb"
    rm -rf "$staging"
    write_sha256 "$out_deb"
    echo "[OK] Built $out_deb"
}

build_deb_src() {
    if ! command -v dpkg-buildpackage >/dev/null 2>&1; then
        echo "Error: dpkg-buildpackage not found (install dpkg-dev)" >&2
        return 1
    fi
    if ! command -v dh >/dev/null 2>&1; then
        echo "Error: debhelper ('dh') not found (install debhelper to build full Debian/Ubuntu source/binary packages)" >&2
        return 1
    fi

    ensure_dist_tarball

    local work_dir="$BIN_DIR_ABS/deb-build"
    local src_unpack="$work_dir/mde-${VERSION}"
    echo "==> Building Debian/Ubuntu package via dpkg-buildpackage..."
    rm -rf "$work_dir"
    mkdir -p "$work_dir"
    cp "$BUILDDIR_ABS/mde-${VERSION}.tar.gz" "$work_dir/mde_${VERSION}.orig.tar.gz"
    tar -xzf "$work_dir/mde_${VERSION}.orig.tar.gz" -C "$work_dir"
    cp -Rp "$SRCDIR_ABS/packaging/debian" "$src_unpack/debian"
    chmod 755 "$src_unpack/debian/rules"

    local dpkg_flags=(-us -uc)
    if [ "$SOURCE_ONLY" = true ]; then
        dpkg_flags=(-S -us -uc)
    fi

    (cd "$src_unpack" && dpkg-buildpackage "${dpkg_flags[@]}")
    cp -f "$work_dir"/mde_*.deb "$work_dir"/mde_*.dsc "$work_dir"/mde_*.changes "$BIN_DIR_ABS/" 2>/dev/null || true
    echo "[OK] Debian/Ubuntu package artifacts placed in $BIN_DIR_ABS (build tree: $work_dir)"
}

build_rpm() {
    if ! command -v rpmbuild >/dev/null 2>&1; then
        echo "Error: rpmbuild not found (install rpm-build on RHEL/Fedora/openSUSE, or see packaging/README.md for container builds)" >&2
        return 1
    fi

    ensure_dist_tarball

    local rpm_top="$BIN_DIR_ABS/rpmbuild"
    echo "==> Building RPM packages using packaging/rpm/mde.spec..."
    rm -rf "$rpm_top"
    mkdir -p "$rpm_top"/{BUILD,BUILDROOT,RPMS,SOURCES,SPECS,SRPMS}
    cp "$BUILDDIR_ABS/mde-${VERSION}.tar.gz" "$rpm_top/SOURCES/"
    cp "$SRCDIR_ABS/packaging/rpm/mde.spec" "$rpm_top/SPECS/"

    rpmbuild --define "_topdir $rpm_top" -ba "$rpm_top/SPECS/mde.spec"
    find "$rpm_top/RPMS" "$rpm_top/SRPMS" -type f -name '*.rpm' -exec cp -f {} "$BIN_DIR_ABS/" \;
    echo "[OK] Built RPM packages in $BIN_DIR_ABS"
}

build_arch() {
    if ! command -v makepkg >/dev/null 2>&1; then
        echo "Error: makepkg not found (run on Arch Linux / Manjaro or see packaging/README.md for container builds)" >&2
        return 1
    fi
    if [ "${EUID:-$(id -u)}" -eq 0 ]; then
        echo "Error: Arch makepkg cannot run as root. Run 'make arch' as an unprivileged user." >&2
        return 1
    fi

    ensure_dist_tarball

    local arch_dir="$BIN_DIR_ABS/archbuild"
    echo "==> Building Arch Linux package using packaging/arch/PKGBUILD..."
    rm -rf "$arch_dir"
    mkdir -p "$arch_dir"
    cp "$SRCDIR_ABS/packaging/arch/PKGBUILD" "$arch_dir/PKGBUILD"
    cp "$BUILDDIR_ABS/mde-${VERSION}.tar.gz" "$arch_dir/mde-${VERSION}.tar.gz"

    (cd "$arch_dir" && makepkg -fc --noconfirm)
    cp -f "$arch_dir"/*.pkg.tar.* "$BIN_DIR_ABS/"
    echo "[OK] Built Arch package in $BIN_DIR_ABS"
}

build_alpine() {
    if ! command -v abuild >/dev/null 2>&1; then
        echo "Error: abuild not found (run on Alpine Linux with alpine-sdk or see packaging/README.md)" >&2
        return 1
    fi

    ensure_dist_tarball

    local alpine_dir="$BIN_DIR_ABS/alpinebuild"
    echo "==> Building Alpine Linux APK package using packaging/alpine/APKBUILD..."
    rm -rf "$alpine_dir"
    mkdir -p "$alpine_dir"
    cp "$SRCDIR_ABS/packaging/alpine/APKBUILD" "$alpine_dir/APKBUILD"
    cp "$BUILDDIR_ABS/mde-${VERSION}.tar.gz" "$alpine_dir/mde-${VERSION}.tar.gz"

    (cd "$alpine_dir" && abuild checksum && abuild -r)
    find "${REPODEST:-$HOME/packages}" -type f -name "mde-*${VERSION}*.apk" -exec cp -f {} "$BIN_DIR_ABS/" \; 2>/dev/null || true
    echo "[OK] Built Alpine package"
}

build_tarball() {
    ensure_binaries_built

    local machine archive_stem staging out_tar
    machine=$(uname -m)
    archive_stem="mde-${VERSION}-linux-${machine}"
    staging="$BIN_DIR_ABS/${archive_stem}.staging/$archive_stem"
    out_tar="$BIN_DIR_ABS/${archive_stem}.tar.gz"

    echo "==> Packaging portable Linux binary archive ${archive_stem}.tar.gz..."
    rm -rf "$BIN_DIR_ABS/${archive_stem}.staging" "$out_tar"
    mkdir -p "$staging/bin" "$staging/share/man/man1" "$staging/share/doc/mde"

    install -m 755 "$BIN_DIR_ABS/mde" "$staging/bin/mde"
    install -m 755 "$BIN_DIR_ABS/md2epub" "$staging/bin/md2epub"
    strip_if_available "$staging/bin/mde"
    strip_if_available "$staging/bin/md2epub"

    install -m 644 "$SRCDIR_ABS/man/mde.1" "$staging/share/man/man1/mde.1"
    install -m 644 "$SRCDIR_ABS/man/md2epub.1" "$staging/share/man/man1/md2epub.1"
    install -m 644 "$SRCDIR_ABS/README.md" "$staging/share/doc/mde/README.md"
    install -m 644 "$SRCDIR_ABS/CHANGELOG.md" "$staging/share/doc/mde/CHANGELOG.md"
    install -m 644 "$SRCDIR_ABS/LICENSE.md" "$staging/share/doc/mde/LICENSE.md"

    cat > "$staging/install.sh" <<'EOF'
#!/bin/sh
set -eu
SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
PREFIX="${PREFIX:-/usr/local}"
DESTDIR="${DESTDIR:-}"

for arg in "$@"; do
    case "$arg" in
        --prefix=*) PREFIX="${arg#*=}" ;;
        --destdir=*) DESTDIR="${arg#*=}" ;;
        -h|--help)
            echo "Usage: ./install.sh [--prefix=/usr/local] [--destdir=]"
            exit 0
            ;;
        *)
            echo "Unknown option: $arg" >&2
            exit 1
            ;;
    esac
done

mkdir -p "${DESTDIR}${PREFIX}/bin" "${DESTDIR}${PREFIX}/share/man/man1" "${DESTDIR}${PREFIX}/share/doc/mde"
install -m 755 "$SCRIPT_DIR/bin/mde" "${DESTDIR}${PREFIX}/bin/mde"
install -m 755 "$SCRIPT_DIR/bin/md2epub" "${DESTDIR}${PREFIX}/bin/md2epub"
install -m 644 "$SCRIPT_DIR/share/man/man1/mde.1" "${DESTDIR}${PREFIX}/share/man/man1/mde.1"
install -m 644 "$SCRIPT_DIR/share/man/man1/md2epub.1" "${DESTDIR}${PREFIX}/share/man/man1/md2epub.1"
install -m 644 "$SCRIPT_DIR/share/doc/mde/README.md" "${DESTDIR}${PREFIX}/share/doc/mde/README.md"
install -m 644 "$SCRIPT_DIR/share/doc/mde/CHANGELOG.md" "${DESTDIR}${PREFIX}/share/doc/mde/CHANGELOG.md"
install -m 644 "$SCRIPT_DIR/share/doc/mde/LICENSE.md" "${DESTDIR}${PREFIX}/share/doc/mde/LICENSE.md"
echo "Installed mde and md2epub into ${DESTDIR}${PREFIX}"
EOF
    cat > "$staging/uninstall.sh" <<'EOF'
#!/bin/sh
set -eu
PREFIX="${PREFIX:-/usr/local}"
DESTDIR="${DESTDIR:-}"

for arg in "$@"; do
    case "$arg" in
        --prefix=*) PREFIX="${arg#*=}" ;;
        --destdir=*) DESTDIR="${arg#*=}" ;;
        -h|--help)
            echo "Usage: ./uninstall.sh [--prefix=/usr/local] [--destdir=]"
            exit 0
            ;;
        *)
            echo "Unknown option: $arg" >&2
            exit 1
            ;;
    esac
done

rm -f "${DESTDIR}${PREFIX}/bin/mde" "${DESTDIR}${PREFIX}/bin/md2epub" \
      "${DESTDIR}${PREFIX}/share/man/man1/mde.1" "${DESTDIR}${PREFIX}/share/man/man1/md2epub.1" \
      "${DESTDIR}${PREFIX}/share/doc/mde/README.md" "${DESTDIR}${PREFIX}/share/doc/mde/CHANGELOG.md" \
      "${DESTDIR}${PREFIX}/share/doc/mde/LICENSE.md"
rmdir "${DESTDIR}${PREFIX}/share/doc/mde" 2>/dev/null || true
echo "Uninstalled mde and md2epub from ${DESTDIR}${PREFIX}"
EOF
    chmod 755 "$staging/install.sh" "$staging/uninstall.sh"

    tar -C "$BIN_DIR_ABS/${archive_stem}.staging" -czf "$out_tar" "$archive_stem"
    rm -rf "$BIN_DIR_ABS/${archive_stem}.staging"
    write_sha256 "$out_tar"
    echo "[OK] Built $out_tar"
}

build_all() {
    build_tarball

    if command -v dpkg-deb >/dev/null 2>&1; then
        build_deb
    else
        echo "[SKIP] .deb packaging: dpkg-deb not installed"
    fi

    if command -v dpkg-buildpackage >/dev/null 2>&1 && command -v dh >/dev/null 2>&1; then
        build_deb_src
    else
        echo "[SKIP] deb-src packaging: debhelper ('dh') or dpkg-buildpackage not installed"
    fi

    if command -v rpmbuild >/dev/null 2>&1; then
        build_rpm
    else
        echo "[SKIP] .rpm packaging: rpmbuild not installed (use packaging/rpm/mde.spec on RHEL/Fedora/openSUSE)"
    fi

    if command -v makepkg >/dev/null 2>&1 && [ "${EUID:-$(id -u)}" -ne 0 ]; then
        build_arch
    else
        echo "[SKIP] Arch packaging: makepkg not available (use packaging/arch/PKGBUILD on Arch/Manjaro)"
    fi

    if command -v abuild >/dev/null 2>&1; then
        build_alpine
    else
        echo "[SKIP] Alpine packaging: abuild not installed (use packaging/alpine/APKBUILD on Alpine)"
    fi
}

case "$TARGET" in
    deb)     build_deb ;;
    deb-src) build_deb_src ;;
    rpm)     build_rpm ;;
    arch)    build_arch ;;
    alpine)  build_alpine ;;
    tarball) build_tarball ;;
    all)     build_all ;;
    *)
        echo "Error: Unknown packaging target '$TARGET'" >&2
        usage >&2
        exit 1
        ;;
esac
