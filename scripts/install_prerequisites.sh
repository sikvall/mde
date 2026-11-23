#!/usr/bin/env bash
set -euo pipefail

# install_prerequisites.sh - Install build prerequisites for mde and md2epub.

AUTO_YES=false
DRY_RUN=false
WITH_OPTIONAL=false

usage() {
    cat <<'EOF'
Usage: install_prerequisites.sh [OPTIONS]

Install all compiler, build, and library prerequisites required to compile
mde and md2epub (C11 compiler, make, ncursesw headers/libs, libzip headers/libs).

Options:
  -y, --yes            Run package manager non-interactively
      --with-optional  Also install optional tools (unzip, python3, pandoc)
      --dry-run        Print commands without executing them
  -h, --help           Show this help message and exit

Supported platforms:
  - Debian / Ubuntu / Mint / Pop!_OS (apt-get)
  - Fedora / RHEL / Rocky / Alma / CentOS (dnf / yum)
  - Arch Linux / Manjaro / EndeavourOS (pacman)
  - openSUSE / SLES (zypper)
  - Alpine Linux (apk)
  - macOS (Homebrew)
EOF
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        -y|--yes)
            AUTO_YES=true
            ;;
        --with-optional)
            WITH_OPTIONAL=true
            ;;
        --dry-run)
            DRY_RUN=true
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
    shift
done

run_cmd() {
    if [ "$DRY_RUN" = true ]; then
        printf '[dry-run]'
        for arg in "$@"; do
            printf ' %q' "$arg"
        done
        printf '\n'
    else
        "$@"
    fi
}

SUDO=""
setup_sudo() {
    if [ "${EUID:-$(id -u)}" -eq 0 ]; then
        SUDO=""
    elif command -v sudo >/dev/null 2>&1; then
        SUDO="sudo"
    elif [ "$DRY_RUN" = true ]; then
        SUDO="sudo"
    else
        echo "Error: Root privileges or 'sudo' are required to install system packages." >&2
        exit 1
    fi
}

run_priv() {
    if [ -n "$SUDO" ]; then
        run_cmd "$SUDO" "$@"
    else
        run_cmd "$@"
    fi
}

install_debian() {
    setup_sudo
    local ncurses_pkg="libncursesw5-dev"
    if command -v apt-cache >/dev/null 2>&1 && apt-cache show libncurses-dev >/dev/null 2>&1; then
        ncurses_pkg="libncurses-dev"
    fi

    local pkgs=(build-essential "$ncurses_pkg" libzip-dev)
    if [ "$WITH_OPTIONAL" = true ]; then
        pkgs+=(unzip python3 pandoc)
    fi

    local apt_flags=()
    if [ "$AUTO_YES" = true ]; then
        apt_flags+=(-y)
    fi

    echo "==> Detected Debian/Ubuntu (apt-get)"
    run_priv apt-get update
    run_priv apt-get install "${apt_flags[@]}" "${pkgs[@]}"
}

install_redhat() {
    setup_sudo
    local pm="dnf"
    if ! command -v dnf >/dev/null 2>&1; then
        pm="yum"
    fi

    local pkgs=(gcc make ncurses-devel libzip-devel)
    if [ "$WITH_OPTIONAL" = true ]; then
        pkgs+=(unzip python3 pandoc)
    fi

    local flags=()
    if [ "$AUTO_YES" = true ]; then
        flags+=(-y)
    fi

    echo "==> Detected Fedora/RHEL ($pm)"
    run_priv "$pm" install "${flags[@]}" "${pkgs[@]}"
}

install_arch() {
    setup_sudo
    local pkgs=(gcc make ncurses libzip)
    if [ "$WITH_OPTIONAL" = true ]; then
        pkgs+=(unzip python pandoc)
    fi

    local flags=(--needed)
    if [ "$AUTO_YES" = true ]; then
        flags+=(--noconfirm)
    fi

    echo "==> Detected Arch Linux (pacman)"
    run_priv pacman -S "${flags[@]}" "${pkgs[@]}"
}

install_suse() {
    setup_sudo
    local pkgs=(gcc make ncurses-devel libzip-devel)
    if [ "$WITH_OPTIONAL" = true ]; then
        pkgs+=(unzip python3 pandoc)
    fi

    local flags=()
    if [ "$AUTO_YES" = true ]; then
        flags+=(--non-interactive)
    fi

    echo "==> Detected openSUSE/SLES (zypper)"
    run_priv zypper "${flags[@]}" install "${pkgs[@]}"
}

install_alpine() {
    setup_sudo
    local pkgs=(build-base ncurses-dev libzip-dev)
    if [ "$WITH_OPTIONAL" = true ]; then
        pkgs+=(unzip python3 pandoc)
    fi

    echo "==> Detected Alpine Linux (apk)"
    run_priv apk add --no-cache "${pkgs[@]}"
}

install_macos() {
    echo "==> Detected macOS (Homebrew)"
    if ! xcode-select -p >/dev/null 2>&1; then
        echo "Xcode Command Line Tools not found. Triggering installation..."
        run_cmd xcode-select --install || true
    fi
    if ! command -v brew >/dev/null 2>&1; then
        echo "Error: Homebrew is required on macOS (https://brew.sh)." >&2
        exit 1
    fi

    local pkgs=(ncurses libzip)
    if [ "$WITH_OPTIONAL" = true ]; then
        pkgs+=(unzip python3 pandoc)
    fi

    run_cmd brew install "${pkgs[@]}"

    if command -v brew >/dev/null 2>&1; then
        local ncurses_prefix
        ncurses_prefix="$(brew --prefix ncurses 2>/dev/null || true)"
        if [ -n "$ncurses_prefix" ]; then
            echo ""
            echo "Note: Homebrew 'ncurses' is keg-only. If compilation cannot find"
            echo "<ncursesw/curses.h>, build with:"
            echo "  CFLAGS=\"-I${ncurses_prefix}/include\" LDFLAGS=\"-L${ncurses_prefix}/lib -lncursesw -lzip\" make"
        fi
    fi
}

verify_toolchain() {
    if [ "$DRY_RUN" = true ]; then
        echo "==> Dry-run complete. No packages were modified."
        return 0
    fi

    echo ""
    echo "==> Verifying compiler and library prerequisites..."

    local compiler=""
    if command -v cc >/dev/null 2>&1; then
        compiler="cc"
    elif command -v gcc >/dev/null 2>&1; then
        compiler="gcc"
    elif command -v clang >/dev/null 2>&1; then
        compiler="clang"
    else
        echo "Error: No C11 compiler (cc, gcc, or clang) found on PATH." >&2
        exit 1
    fi

    if ! command -v make >/dev/null 2>&1; then
        echo "Error: 'make' not found on PATH." >&2
        exit 1
    fi

    local tmp_bin
    tmp_bin="$(mktemp /tmp/mde_prereq_check.XXXXXX)"
    trap 'rm -f "$tmp_bin"' RETURN

    if printf '#include <ncursesw/curses.h>\n#include <zip.h>\nint main(void) { return 0; }\n' \
        | "$compiler" -std=c11 -x c - -lncursesw -lzip -o "$tmp_bin" >/dev/null 2>&1; then
        echo "[OK] C11 compiler ($compiler), make, ncursesw, and libzip verified."
    else
        echo "Warning: Compile check for <ncursesw/curses.h> and <zip.h> did not succeed with default flags." >&2
        echo "         Verify include/library paths before running 'make'." >&2
        exit 1
    fi

    echo ""
    echo "Ready to build mde:"
    echo "  make"
    echo "  make test"
}

main() {
    local os_name
    os_name="$(uname -s)"

    case "$os_name" in
        Linux)
            if command -v apt-get >/dev/null 2>&1; then
                install_debian
            elif command -v dnf >/dev/null 2>&1 || command -v yum >/dev/null 2>&1; then
                install_redhat
            elif command -v pacman >/dev/null 2>&1; then
                install_arch
            elif command -v zypper >/dev/null 2>&1; then
                install_suse
            elif command -v apk >/dev/null 2>&1; then
                install_alpine
            else
                echo "Error: Unsupported Linux distribution. Please install gcc/clang, make, ncursesw-dev, and libzip-dev manually." >&2
                exit 1
            fi
            ;;
        Darwin)
            install_macos
            ;;
        *)
            echo "Error: Unsupported OS '$os_name'." >&2
            exit 1
            ;;
    esac

    verify_toolchain
}

main
