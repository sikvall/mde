#!/usr/bin/env bash
set -euo pipefail

# check_packaging.sh - Offline validation for all Linux packaging manifests,
# build workflows, and generated artifacts.

SRCDIR="${SRCDIR:-.}"
BUILDDIR="${BUILDDIR:-.}"
BIN_DIR="${BIN_DIR:-$BUILDDIR/bin}"

SRCDIR_ABS=$(cd "$SRCDIR" && pwd)
BUILDDIR_ABS=$(cd "$BUILDDIR" && pwd)
BIN_DIR_ABS=$(mkdir -p "$BIN_DIR" && cd "$BIN_DIR" && pwd)

VERSION=$(awk -F'"' '/#define[[:space:]]+MDE_VERSION_STRING/{print $2; exit}' "$SRCDIR_ABS/src/version.h")
: "${VERSION:=0.1.3}"

PASS=0
FAIL=0

ok() {
    PASS=$((PASS + 1))
    echo "  [PASS] $1"
}

fail() {
    FAIL=$((FAIL + 1))
    echo "  [FAIL] $1" >&2
}

assert_grep() {
    local pattern="$1"
    local file="$2"
    local label="$3"
    if grep -Eq "$pattern" "$file"; then
        ok "$label"
    else
        fail "$label (expected /$pattern/ in $file)"
    fi
}

echo "=========================================================="
echo " Verifying Linux Packaging Manifests & Artifacts (v${VERSION})"
echo "=========================================================="

echo ""
echo "--- 1. Checking Manifest Presence & Version Sync ---"
required_files=(
    "$SRCDIR_ABS/packaging/debian/control"
    "$SRCDIR_ABS/packaging/debian/rules"
    "$SRCDIR_ABS/packaging/debian/changelog"
    "$SRCDIR_ABS/packaging/debian/copyright"
    "$SRCDIR_ABS/packaging/debian/source/format"
    "$SRCDIR_ABS/packaging/debian/watch"
    "$SRCDIR_ABS/packaging/rpm/mde.spec"
    "$SRCDIR_ABS/packaging/arch/PKGBUILD"
    "$SRCDIR_ABS/packaging/arch/.SRCINFO"
    "$SRCDIR_ABS/packaging/alpine/APKBUILD"
    "$SRCDIR_ABS/packaging/void/template"
    "$SRCDIR_ABS/packaging/gentoo/mde-${VERSION}.ebuild"
    "$SRCDIR_ABS/packaging/nix/default.nix"
    "$SRCDIR_ABS/packaging/snap/snapcraft.yaml"
    "$SRCDIR_ABS/packaging/README.md"
    "$SRCDIR_ABS/scripts/build_packages.sh"
)
for f in "${required_files[@]}"; do
    if [ -f "$f" ]; then
        ok "Exists: ${f#$SRCDIR_ABS/}"
    else
        fail "Missing: ${f#$SRCDIR_ABS/}"
    fi
done

assert_grep "^mde \\(${VERSION}-" "$SRCDIR_ABS/packaging/debian/changelog" "Debian changelog matches version ${VERSION}"
assert_grep "^Version:[[:space:]]*${VERSION}$" "$SRCDIR_ABS/packaging/rpm/mde.spec" "RPM spec matches version ${VERSION}"
assert_grep "^pkgver=${VERSION}$" "$SRCDIR_ABS/packaging/arch/PKGBUILD" "Arch PKGBUILD matches version ${VERSION}"
assert_grep "^[[:space:]]*pkgver = ${VERSION}$" "$SRCDIR_ABS/packaging/arch/.SRCINFO" "Arch .SRCINFO matches version ${VERSION}"
assert_grep "^pkgver=${VERSION}$" "$SRCDIR_ABS/packaging/alpine/APKBUILD" "Alpine APKBUILD matches version ${VERSION}"
assert_grep "^version=${VERSION}$" "$SRCDIR_ABS/packaging/void/template" "Void template matches version ${VERSION}"
assert_grep "version = \"${VERSION}\";" "$SRCDIR_ABS/packaging/nix/default.nix" "Nix derivation matches version ${VERSION}"
assert_grep "^version: '${VERSION}'$" "$SRCDIR_ABS/packaging/snap/snapcraft.yaml" "Snapcraft manifest matches version ${VERSION}"

echo ""
echo "--- 2. Checking Shell Script & Recipe Syntax ---"
for sh_script in \
    "$SRCDIR_ABS/configure" \
    "$SRCDIR_ABS/scripts/verify_no_pandoc.sh" \
    "$SRCDIR_ABS/packaging/alpine/APKBUILD" \
    "$SRCDIR_ABS/packaging/void/template"; do
    if sh -n "$sh_script"; then
        ok "POSIX sh -n syntax valid: ${sh_script#$SRCDIR_ABS/}"
    else
        fail "POSIX sh -n syntax invalid: ${sh_script#$SRCDIR_ABS/}"
    fi
done

for bash_script in \
    "$SRCDIR_ABS/scripts/install_prerequisites.sh" \
    "$SRCDIR_ABS/scripts/build_packages.sh" \
    "$SRCDIR_ABS/scripts/check_packaging.sh" \
    "$SRCDIR_ABS/packaging/arch/PKGBUILD"; do
    if bash -n "$bash_script"; then
        ok "Bash syntax valid: ${bash_script#$SRCDIR_ABS/}"
    else
        fail "Bash syntax invalid: ${bash_script#$SRCDIR_ABS/}"
    fi
done

if [ -x "$SRCDIR_ABS/packaging/debian/rules" ] || [ -f "$SRCDIR_ABS/packaging/debian/rules" ]; then
    if grep -q $'\t' "$SRCDIR_ABS/packaging/debian/rules"; then
        ok "Debian rules uses valid recipe tabs"
    else
        fail "Debian rules missing recipe tabs"
    fi
fi

echo ""
echo "--- 3. Testing .deb Package Creation & Archive Contents ---"
TMP_TEST=$(mktemp -d "${TMPDIR:-/tmp}/mde_pkg_check.XXXXXX")
trap 'rm -rf "$TMP_TEST"' EXIT

if command -v dpkg-deb >/dev/null 2>&1; then
    make -C "$BUILDDIR_ABS" deb
    deb_file=$(ls -1 "$BIN_DIR_ABS"/mde_"${VERSION}"_*.deb | head -n 1 || true)
    if [ -n "$deb_file" ] && [ -f "$deb_file" ]; then
        ok "Built .deb archive: $(basename "$deb_file")"
        deb_info=$(dpkg-deb -I "$deb_file")
        if printf '%s\n' "$deb_info" | grep -q "Package: mde" && \
           printf '%s\n' "$deb_info" | grep -q "Version: ${VERSION}" && \
           printf '%s\n' "$deb_info" | grep -Eq "Depends:.*libzip"; then
            ok ".deb control metadata and resolved Depends valid"
        else
            fail ".deb control metadata incomplete"
        fi
        deb_contents=$(dpkg-deb -c "$deb_file")
        if printf '%s\n' "$deb_contents" | grep -q "./usr/bin/mde" && \
           printf '%s\n' "$deb_contents" | grep -q "./usr/bin/md2epub" && \
           printf '%s\n' "$deb_contents" | grep -q "./usr/share/man/man1/mde.1.gz" && \
           printf '%s\n' "$deb_contents" | grep -q "./usr/share/man/man1/md2epub.1.gz" && \
           printf '%s\n' "$deb_contents" | grep -q "./usr/share/doc/mde/copyright"; then
            ok ".deb payload contains binaries, man pages, and docs"
        else
            fail ".deb payload missing expected files"
        fi
        mkdir -p "$TMP_TEST/deb_root"
        dpkg-deb -x "$deb_file" "$TMP_TEST/deb_root"
        if "$TMP_TEST/deb_root/usr/bin/md2epub" --version | grep -q "${VERSION}"; then
            ok "Extracted .deb md2epub executes and reports v${VERSION}"
        else
            fail "Extracted .deb md2epub failed --version check"
        fi
    else
        fail "Expected .deb archive not produced"
    fi
else
    echo "  [SKIP] dpkg-deb not installed"
fi

echo ""
echo "--- 4. Testing Portable Linux Tarball & Installer ---"
make -C "$BUILDDIR_ABS" pkg-tarball
tar_file=$(ls -1 "$BIN_DIR_ABS"/mde-"${VERSION}"-linux-*.tar.gz | head -n 1 || true)
if [ -n "$tar_file" ] && [ -f "$tar_file" ]; then
    ok "Built portable tarball: $(basename "$tar_file")"
    if [ -f "${tar_file}.sha256" ] && (cd "$BIN_DIR_ABS" && sha256sum -c "$(basename "$tar_file").sha256" >/dev/null); then
        ok "Portable tarball SHA-256 checksum verified"
    else
        fail "Portable tarball SHA-256 checksum failed"
    fi
    mkdir -p "$TMP_TEST/tar_unpack" "$TMP_TEST/tar_prefix"
    tar -xzf "$tar_file" -C "$TMP_TEST/tar_unpack"
    tar_root=$(find "$TMP_TEST/tar_unpack" -mindepth 1 -maxdepth 1 -type d | head -n 1)
    "$tar_root/install.sh" --prefix="$TMP_TEST/tar_prefix" >/dev/null
    if "$TMP_TEST/tar_prefix/bin/md2epub" --version | grep -q "${VERSION}" && \
       "$TMP_TEST/tar_prefix/bin/mde" --version | grep -q "${VERSION}"; then
        ok "Portable tarball install.sh installs working mde and md2epub"
    else
        fail "Portable tarball installed binaries failed --version check"
    fi
    "$tar_root/uninstall.sh" --prefix="$TMP_TEST/tar_prefix" >/dev/null
    if [ ! -e "$TMP_TEST/tar_prefix/bin/mde" ] && [ ! -e "$TMP_TEST/tar_prefix/bin/md2epub" ]; then
        ok "Portable tarball uninstall.sh cleanly removes installed files"
    else
        fail "Portable tarball uninstall.sh left leftover binaries"
    fi
else
    fail "Expected portable tarball not produced"
fi

echo ""
echo "--- 5. Simulating Distro Configure & Destdir Build Profiles ---"
simulate_profile() {
    local name="$1"
    shift
    local pdir="$TMP_TEST/sim_${name}"
    mkdir -p "$pdir"
    if (cd "$pdir" && "$SRCDIR_ABS/configure" --quiet "$@"); then
        ok "${name} configure flags accepted cleanly"
    else
        fail "${name} configure flags failed"
        return 1
    fi
    if [ "$name" = "rpm" ]; then
        if make -C "$pdir" -s all check && \
           make -C "$pdir" -s DESTDIR="$TMP_TEST/rpm_stage" install && \
           [ -x "$TMP_TEST/rpm_stage/usr/bin/mde" ] && \
           [ -x "$TMP_TEST/rpm_stage/usr/bin/md2epub" ] && \
           [ -f "$TMP_TEST/rpm_stage/usr/share/man/man1/mde.1" ]; then
            ok "Simulated RPM out-of-tree build, test, and DESTDIR install succeeded"
        else
            fail "Simulated RPM out-of-tree build/install failed"
        fi
    fi
}

simulate_profile "debian" \
    --build=x86_64-linux-gnu --prefix=/usr --includedir=\${prefix}/include \
    --mandir=\${prefix}/share/man --infodir=\${prefix}/share/info \
    --sysconfdir=/etc --localstatedir=/var --disable-option-checking \
    --disable-silent-rules --libdir=\${prefix}/lib/x86_64-linux-gnu \
    --runstatedir=/run --disable-maintainer-mode --disable-dependency-tracking \
    --disable-werror

simulate_profile "rpm" \
    --build=x86_64-redhat-linux-gnu --host=x86_64-redhat-linux-gnu \
    --program-prefix= --disable-dependency-tracking --prefix=/usr \
    --exec-prefix=/usr --bindir=/usr/bin --sbindir=/usr/sbin \
    --sysconfdir=/etc --datadir=/usr/share --includedir=/usr/include \
    --libdir=/usr/lib64 --libexecdir=/usr/libexec --localstatedir=/var \
    --sharedstatedir=/var/lib --mandir=/usr/share/man --infodir=/usr/share/info \
    --disable-werror

simulate_profile "arch" \
    --prefix=/usr --disable-werror

simulate_profile "alpine" \
    --build=x86_64-alpine-linux-musl --host=x86_64-alpine-linux-musl \
    --prefix=/usr --sysconfdir=/etc --mandir=/usr/share/man \
    --docdir=/usr/share/doc/mde --disable-werror

echo ""
echo "=========================================================="
echo " Packaging Checks: ${PASS} passed, ${FAIL} failed"
echo "=========================================================="
[ "$FAIL" -eq 0 ]
