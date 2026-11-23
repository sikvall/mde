# Copyright 2026 Gentoo Authors
# Distributed under the terms of the MIT License

EAPI=8

DESCRIPTION="Full-screen text-mode Markdown editor with native EPUB 2/3 exporter"
HOMEPAGE="https://github.com/sikvall/mde"
SRC_URI="https://github.com/sikvall/mde/archive/refs/tags/v${PV}.tar.gz -> ${P}.tar.gz"

LICENSE="MIT"
SLOT="0"
KEYWORDS="~amd64 ~arm64 ~x86"

RDEPEND="
	sys-libs/ncurses:0=
	dev-libs/libzip:=
"
DEPEND="${RDEPEND}"
BDEPEND="virtual/pkgconfig"

src_configure() {
	econf --disable-werror
}
