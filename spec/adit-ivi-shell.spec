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

%files
%{_libdir}/weston/ivi-shell.so

%changelog
* Fri Feb 06 2015 ADIT
- Initial version
