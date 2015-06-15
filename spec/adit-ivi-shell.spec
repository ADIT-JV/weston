Name:           adit-ivi-shell
Version:        %{adit_git_tag}
Release:        1
Summary:        IVI-Shell library for weston compositor
Group:          System Environment/Libraries
Vendor:         ADIT
License:        MIT
Source0:        %{name}-%{version}.tar.bz2

%description
This package contains ivi-shell library

%prep
if [ ! -e %{_topdir}/BUILD/%{name}-%{version} ]; then
	ln -sf %{_topdir}/../weston %{_topdir}/BUILD/%{name}-%{version}
fi

%build
cd %{name}-%{version}
make

%install
cd %{name}-%{version}
make install DEST_DIR=%{_topdir}/../sysroot
make install DEST_DIR=%{buildroot}

%post
if [ -e /usr/lib/weston/ivi-shell.so ]; then
    mv /usr/lib/weston/ivi-shell.so /usr/lib/weston/ivi-shell.so.org
fi
ln -sf /usr/lib/weston/ivi-shell-adit.so /usr/lib/weston/ivi-shell.so

%files
%{_libdir}/weston/ivi-shell-adit.so
/home/pulse/.config/ias.conf.ivi
/home/pulse/.config/weston.ini.ivi
/root/.config/ias.conf.ivi
/root/.config/weston.ini.ivi

%changelog
* Fri Feb 06 2015 ADIT
- Initial version
