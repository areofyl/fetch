# Commit for v2.3.0; keep in sync with _service when updating the release.
%global commit 9c8bb96bf47460b7c73c9e3423f8d073a67222d3

Name:           fetch
Version:        2.3.0
Release:        1.%(date +%%Y%%m%%d)%{?dist}
Summary:        Animated 3D fetch tool for your terminal

License:        ISC
URL:            https://github.com/areofyl/fetch
Source0:        %{url}/archive/%{commit}.tar.gz#/fetch-%{version}.tar.gz

BuildRequires:  gcc
BuildRequires:  make

%description
A donut.c-inspired fetch tool that spins your distro logo in 3D with live-updating
system info. Takes any ASCII/Unicode distro logo, turns each character into a point
cloud based on its visual density, and renders it as a rotating 3D relief with
Blinn-Phong shading. System info is gathered natively from /proc, /sys, and GTK
config — no external dependencies required.

%prep
%autosetup -n fetch-%{commit}

%build
%make_build

%install
%make_install DESTDIR=%{buildroot} PREFIX=%{_prefix}

%files
%doc README.md
%license LICENSE
%{_bindir}/fetch

%changelog
* Fri Aug 21 2026 Youssef Tarek <amazingritro66@gmail.com> - 2.3.0-1
- Update to 2.3.0

* Fri Jul 24 2026 Youssef Tarek <amazingritro66@gmail.com> - 2.2.1-1
- Update to 2.2.1

* Wed Jul 22 2026 Youssef Tarek <amazingritro66@gmail.com> - 2.2.0-1
- Update to 2.2.0

* Mon Jun 09 2026 Youssef Tarek <amazingritro66@gmail.com> - 2.1.0-1
- Initial RPM package for Fedora
