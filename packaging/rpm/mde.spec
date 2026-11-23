Name:           mde
Version:        0.2.0
Release:        1%{?dist}
Summary:        Full-screen text-mode Markdown editor with native EPUB exporter

License:        MIT
URL:            https://github.com/sikvall/mde
Source0:        %{name}-%{version}.tar.gz

BuildRequires:  gcc
BuildRequires:  make
BuildRequires:  pkgconfig(ncursesw)
BuildRequires:  pkgconfig(libzip)
Suggests:       pandoc

%description
mde is a specialized terminal text editor written in pure C11 and ncursesw
for editing and viewing Markdown (.md) documents with dual-pane live preview,
in-place rich syntax styling, fullscreen rendered editing, column ruler,
task checkboxes, UTF-8 support, hypertext file linking, and native EPUB 2/3
e-book export alongside the standalone md2epub command-line tool.

%prep
%autosetup -n %{name}-%{version}

%build
%configure --disable-werror
make %{?_smp_mflags}

%check
make check

%install
make DESTDIR=%{buildroot} install
rm -rf %{buildroot}%{_datadir}/doc/%{name} %{buildroot}%{_datadir}/doc/packages/%{name}

%files
%license LICENSE.md
%doc README.md CHANGELOG.md
%{_bindir}/mde
%{_bindir}/md2epub
%{_mandir}/man1/mde.1*
%{_mandir}/man1/md2epub.1*

%changelog
* Tue Nov 24 2026 Täpp Anders Sikvall <anders@sikvall.se> - 0.2.0-1
- Release 0.2.0 minor version bump

* Tue Nov 24 2026 Täpp Anders Sikvall <anders@sikvall.se> - 0.1.3-1
- Release 0.1.3 with GNU configure/make build system, multi-distro packaging, and buffer safety hardening

* Mon Nov 23 2026 Täpp Anders Sikvall <anders@sikvall.se> - 0.1.2-1
- Release 0.1.2 with GNU configure build system, native EPUB 2/3 publisher, and md2epub CLI
